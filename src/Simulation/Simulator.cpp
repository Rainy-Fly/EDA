#include"Simulator.hpp"

#include<utility>

#include"SimulationValidator.hpp"

namespace Simulation{

Simulator::Simulator(SimulationCircuit circuit)
    : circuit_(std::move(circuit)){
    //构造时把结构问题一次算清楚：结构有 error 就不进入求值
    diagnostics_ = validate_circuit(circuit_);
}

bool Simulator::valid() const{
    return !has_error(diagnostics_);
}

bool Simulator::set_input(const std::string& componentId, LogicValue value){
    for(auto& component : circuit_.components){
        if(component.id != componentId) continue;
        if(component.kind != GateKind::Input) return false;   //不是输入开关
        component.inputValue = value;
        return true;
    }
    return false;   //没有这个元件
}

bool Simulator::set_input(const std::string& componentId, bool value){
    return set_input(componentId, from_bool(value));
}

LogicValue Simulator::compute_gate(GateKind kind, const std::vector<LogicValue>& inputs){
    switch(kind){
        case GateKind::Not:
            return inputs.empty() ? LogicValue::Unknown : logical_not(inputs.front());
        case GateKind::And:  return logical_and(inputs);
        case GateKind::Or:   return logical_or(inputs);
        case GateKind::Xor:  return logical_xor(inputs);
        case GateKind::Xnor: return logical_not(logical_xor(inputs));
        case GateKind::Nand: return logical_not(logical_and(inputs));
        case GateKind::Nor:  return logical_not(logical_or(inputs));
        //导通/缓冲：输出 = 输入（单刀开关闭合时用它把一侧电平送到另一侧）
        case GateKind::Buffer:
            return inputs.empty() ? LogicValue::Unknown : inputs.front();
        case GateKind::Input:
        case GateKind::Dff:
        case GateKind::Clock:
        case GateKind::Unsupported:
            break;   //不是组合门：不参与迭代求值（Dff/Clock 由状态表定值）
    }
    return LogicValue::Unknown;
}

//把“当前状态”写进网络值表：输入开关、时钟源输出、触发器 Q
void Simulator::apply_state(std::map<std::string, LogicValue>& values) const{
    for(const auto& component : circuit_.components){
        if(component.kind == GateKind::Input && !component.outputNet.empty()){
            values[component.outputNet] = component.inputValue;
        }else if(component.kind == GateKind::Clock && !component.outputNet.empty()){
            const auto it = clockValues_.find(component.outputNet);
            values[component.outputNet] =
                (it == clockValues_.end()) ? LogicValue::Zero : it->second;
        }else if(component.kind == GateKind::Dff && !component.outputNet.empty()){
            const auto it = dffQ_.find(component.outputNet);
            values[component.outputNet] =
                (it == dffQ_.end()) ? LogicValue::Zero : it->second;
        }
    }
}

//对所有 D触发器做 CLK 上升沿检测（上次 0 → 本次 1），沿上把 D 采入 Q。
//inputNets 顺序固定为 [D, CLK]。返回是否有任一触发器更新。
bool Simulator::sample_dffs(const std::map<std::string, LogicValue>& values){
    bool any = false;
    for(const auto& component : circuit_.components){
        if(component.kind != GateKind::Dff || component.inputNets.size() < 2) continue;
        const std::string& dNet   = component.inputNets[0];
        const std::string& clkNet = component.inputNets[1];
        const std::string& qNet   = component.outputNet;
        if(qNet.empty()) continue;

        const auto clkNowIt = values.find(clkNet);
        const LogicValue clkNow = (clkNowIt == values.end()) ? LogicValue::Unknown
                                                             : clkNowIt->second;
        const LogicValue clkPrev = lastClk_.count(clkNet) ? lastClk_[clkNet]
                                                          : LogicValue::Unknown;

        if(clkPrev == LogicValue::Zero && clkNow == LogicValue::One){
            const auto dIt = values.find(dNet);
            dffQ_[qNet] = (dIt == values.end()) ? LogicValue::Unknown : dIt->second;
            any = true;
        }
        lastClk_[clkNet] = clkNow;
    }
    return any;
}

//组合迭代求值到不动点（Dff/Clock 输出由状态表定值，不参与迭代）。
//返回是否在限次内收敛。
bool Simulator::iterate_gates(std::map<std::string, LogicValue>& values){
    const std::size_t maxIterations = circuit_.components.size() * 2 + 16;
    std::size_t iteration = 0;
    bool converged = false;
    while(iteration < maxIterations){
        ++iteration;
        bool changed = false;
        for(const auto& component : circuit_.components){
            if(!is_gate(component.kind)) continue;

            std::vector<LogicValue> inputs;
            inputs.reserve(component.inputNets.size());
            for(const auto& net : component.inputNets){
                const auto it = values.find(net);
                inputs.push_back((it == values.end()) ? LogicValue::Unknown : it->second);
            }

            const LogicValue output = compute_gate(component.kind, inputs);
            LogicValue& slot = values[component.outputNet];
            if(slot != output){
                slot = output;
                changed = true;
            }
        }
        if(!changed){
            converged = true;
            break;
        }
    }
    return converged;
}

SimulationResult Simulator::evaluate(){
    SimulationResult result;
    result.diagnostics = diagnostics_;   //结构问题先带上

    if(!valid()){
        //结构本身有问题：不求值，直接返回（ok 保持 false，诊断里有 error）
        return result;
    }

    std::map<std::string, LogicValue> values = initial_net_values(circuit_);
    apply_state(values);
    if(!iterate_gates(values)){
        std::vector<std::string> gateIds;
        for(const auto& component : circuit_.components){
            if(is_gate(component.kind)) gateIds.push_back(component.id);
        }
        result.diagnostics.push_back(make_error(
            "SIMULATION_COMBINATIONAL_LOOP",
            "组合逻辑在限次内没有稳定，可能存在反馈环。",
            gateIds));
    }

    //时序：CLK 上升沿采样 D，之后把新 Q 传播一轮
    const bool sampled = sample_dffs(values);
    if(sampled){
        std::map<std::string, LogicValue> values2 = initial_net_values(circuit_);
        apply_state(values2);
        if(!iterate_gates(values2)){
            result.diagnostics.push_back(make_error(
                "SIMULATION_COMBINATIONAL_LOOP",
                "组合逻辑在限次内没有稳定，可能存在反馈环。",
                {}));
        }
        values = std::move(values2);
    }

    result.iterations = 1;

    result.netValues = values;
    lastNetValues_ = values;

    lastProbeValues_.clear();
    for(const auto& probe : circuit_.probes){
        const auto it = values.find(probe.net);
        const LogicValue value = (it == values.end()) ? LogicValue::Unknown : it->second;
        result.probeValues[probe.id] = value;
        lastProbeValues_[probe.id] = value;
    }

    result.ok = !result.has_error();
    return result;
}

SimulationResult Simulator::clock_tick(){
    //全部时钟源输出翻转一次（0↔1），产生一个上升沿
    for(const auto& component : circuit_.components){
        if(component.kind != GateKind::Clock || component.outputNet.empty()) continue;
        const auto it = clockValues_.find(component.outputNet);
        const LogicValue current = (it == clockValues_.end()) ? LogicValue::Zero
                                                              : it->second;
        clockValues_[component.outputNet] =
            (current == LogicValue::One) ? LogicValue::Zero : LogicValue::One;
    }
    return evaluate();
}

LogicValue Simulator::net_value(const std::string& net) const{
    const auto it = lastNetValues_.find(net);
    return (it == lastNetValues_.end()) ? LogicValue::Unknown : it->second;
}

LogicValue Simulator::probe_value(const std::string& probeId) const{
    const auto it = lastProbeValues_.find(probeId);
    return (it == lastProbeValues_.end()) ? LogicValue::Unknown : it->second;
}

} // namespace Simulation

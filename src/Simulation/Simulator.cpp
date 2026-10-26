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
        case GateKind::Unsupported:
            break;   //不是门，不参与求值
    }
    return LogicValue::Unknown;
}

SimulationResult Simulator::evaluate(){
    SimulationResult result;
    result.diagnostics = diagnostics_;   //结构问题先带上

    if(!valid()){
        //结构本身有问题：不求值，直接返回（ok 保持 false，诊断里有 error）
        return result;
    }

    std::map<std::string, LogicValue> values = initial_net_values(circuit_);

    //输入开关是常量源：先用调用方设置的值覆盖一遍
    for(const auto& component : circuit_.components){
        if(component.kind == GateKind::Input && !component.outputNet.empty()){
            values[component.outputNet] = component.inputValue;
        }
    }

    //迭代求值到不动点（就地更新 = Gauss-Seidel，比整轮快照收敛更快）。
    //初值全为 X 时也能收敛：例如 0 AND X 直接得 0。
    const std::size_t maxIterations = circuit_.components.size() * 2 + 16;

    std::vector<std::string> gateIds;
    for(const auto& component : circuit_.components){
        if(is_gate(component.kind)) gateIds.push_back(component.id);
    }

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

    result.iterations = static_cast<int>(iteration);

    //不收敛：明确诊断，而不是继续转（组合反馈/环）
    if(!converged){
        result.diagnostics.push_back(make_error(
            "SIMULATION_COMBINATIONAL_LOOP",
            "组合逻辑在 " + std::to_string(maxIterations) +
                " 轮内没有稳定，可能存在反馈环。",
            gateIds));
    }

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

LogicValue Simulator::net_value(const std::string& net) const{
    const auto it = lastNetValues_.find(net);
    return (it == lastNetValues_.end()) ? LogicValue::Unknown : it->second;
}

LogicValue Simulator::probe_value(const std::string& probeId) const{
    const auto it = lastProbeValues_.find(probeId);
    return (it == lastProbeValues_.end()) ? LogicValue::Unknown : it->second;
}

} // namespace Simulation

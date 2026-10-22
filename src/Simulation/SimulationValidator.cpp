#include"SimulationValidator.hpp"
#include<map>
#include<set>

namespace Simulation{

bool has_error(const std::vector<SimulationDiagnostic>& diagnostics){
    for(const auto& diagnostic : diagnostics)
        if(diagnostic.severity == "error") return true;
    return false;
}

std::vector<SimulationDiagnostic> validate_circuit(const SimulationCircuit& circuit){
    std::vector<SimulationDiagnostic> diagnostics;

    const std::set<std::string> nets(circuit.nets.begin(), circuit.nets.end());
    const auto net_known = [&nets](const std::string& net){
        return !net.empty() && nets.count(net) > 0;
    };

    std::set<std::string> componentIds;
    std::set<std::string> probeIds;
    std::map<std::string, std::string> driverOf;   //网络 -> 驱动它的元件ID

    for(const auto& component : circuit.components){
        if(component.id.empty()){
            diagnostics.push_back(make_error("SIMULATION_COMPONENT_ID_EMPTY",
                                             "元件缺少实例ID。"));
        }else if(!componentIds.insert(component.id).second){
            diagnostics.push_back(make_error("SIMULATION_COMPONENT_ID_DUPLICATE",
                                             "元件ID重复：" + component.id, {component.id}));
        }

        std::size_t minInputs = 0;
        std::size_t maxInputs = 0;
        if(component.kind == GateKind::Unsupported ||
           !gate_input_count_range(component.kind, minInputs, maxInputs)){
            const std::string typeName = component.typeName.empty()
                                             ? std::string(gate_kind_name(component.kind))
                                             : component.typeName;
            diagnostics.push_back(make_error("SIMULATION_COMPONENT_TYPE_UNSUPPORTED",
                                             "不支持的元件类型：" + typeName, {component.id}));
            continue;   //类型都不支持，后面的引脚检查没有意义
        }

        if(component.inputNets.size() < minInputs || component.inputNets.size() > maxInputs){
            diagnostics.push_back(make_error(
                "SIMULATION_GATE_PIN_COUNT_INVALID",
                std::string(gate_kind_name(component.kind)) + " 的输入引脚数不符：" +
                    std::to_string(component.inputNets.size()) + " 个",
                {component.id}));
        }

        for(const auto& net : component.inputNets){
            if(!net_known(net)){
                diagnostics.push_back(make_error("SIMULATION_NET_UNKNOWN",
                                                 "元件引用了未声明的网络：" + net,
                                                 {component.id, net}));
            }
        }

        if(component.outputNet.empty()){
            diagnostics.push_back(make_error("SIMULATION_OUTPUT_NET_EMPTY",
                                             "元件缺少输出网络。", {component.id}));
        }else{
            if(!net_known(component.outputNet)){
                diagnostics.push_back(make_error("SIMULATION_NET_UNKNOWN",
                                                 "元件的输出网络未声明：" + component.outputNet,
                                                 {component.id, component.outputNet}));
            }
            const auto inserted = driverOf.insert({component.outputNet, component.id});
            if(!inserted.second){
                diagnostics.push_back(make_error(
                    "SIMULATION_NET_MULTIPLE_DRIVERS",
                    "网络有多个驱动源：" + component.outputNet + "（" +
                        inserted.first->second + "、" + component.id + "）",
                    {component.outputNet, inserted.first->second, component.id}));
            }
        }
    }

    for(const auto& probe : circuit.probes){
        if(probe.id.empty()){
            diagnostics.push_back(make_error("SIMULATION_PROBE_ID_EMPTY", "探针缺少ID。"));
        }else if(!probeIds.insert(probe.id).second){
            diagnostics.push_back(make_error("SIMULATION_PROBE_ID_DUPLICATE",
                                             "探针ID重复：" + probe.id, {probe.id}));
        }
        if(!net_known(probe.net)){
            diagnostics.push_back(make_error("SIMULATION_NET_UNKNOWN",
                                             "探针观察的网络未声明：" + probe.net,
                                             {probe.id, probe.net}));
        }
    }

    for(const auto& entry : circuit.initialNetValues){
        if(!net_known(entry.first)){
            diagnostics.push_back(make_error("SIMULATION_NET_UNKNOWN",
                                             "网络初值引用了未声明的网络：" + entry.first,
                                             {entry.first}));
        }
    }

    return diagnostics;
}

} // namespace Simulation

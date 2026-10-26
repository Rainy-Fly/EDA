#include "./ProjectToModule.hpp"

#include "../Netlist/PcbProjectAdapter.hpp"
#include "../Netlist/PcbProjectSnapshot.hpp"
#include "../verilator/VerilatorApi.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace VerilogBridge{
namespace{

using Verilator::Diagnostic;
using Verilator::DiagnosticKind;
using Verilator::DiagnosticSeverity;
using Verilator::LogicValue;
using Verilator::PortDirection;
using Verilator::SignalKind;

// 极简并查集：节点名由引脚/几何点/导线端点/分支/标签生成，
// 与 Simulation/Pcb 的连通性规则保持一致（只有端点与显式引用算连接）。
class UnionFind{
public:
    void ensure(const std::string& key){
        if(parent_.find(key) == parent_.end()) parent_[key] = key;
    }

    std::string find(const std::string& key){
        auto it = parent_.find(key);
        if(it == parent_.end()){
            parent_[key] = key;
            return key;
        }
        if(it->second == key) return key;
        const std::string root = find(it->second);
        it->second = root;
        return root;
    }

    void unite(const std::string& left, const std::string& right){
        ensure(left);
        ensure(right);
        const std::string left_root = find(left);
        const std::string right_root = find(right);
        if(left_root != right_root) parent_[left_root] = right_root;
    }

private:
    std::map<std::string, std::string> parent_;
};

struct PinRef{
    int component_id = 0;
    std::string pin_id;
};

struct ComponentInfo{
    const NetlistData::PcbSnapshotComponent* component = nullptr;
    bool is_gate = false;
    bool is_input_terminal = false;
    bool is_output_terminal = false;
    bool is_constant = false;
};

bool gate_kind_for_type(const std::string& type, Verilator::GateKind* kind){
    if(type == "AND"){  *kind = Verilator::GateKind::And;  return true; }
    if(type == "OR"){   *kind = Verilator::GateKind::Or;   return true; }
    if(type == "NOT"){  *kind = Verilator::GateKind::Not;  return true; }
    if(type == "NAND"){ *kind = Verilator::GateKind::Nand; return true; }
    if(type == "NOR"){  *kind = Verilator::GateKind::Nor;  return true; }
    if(type == "XOR"){  *kind = Verilator::GateKind::Xor;  return true; }
    if(type == "XNOR"){ *kind = Verilator::GateKind::Xnor; return true; }
    return false;
}

std::string sanitize_identifier(const std::string& raw){
    std::string out;
    out.reserve(raw.size() + 1);
    for(const unsigned char ch : raw){
        const bool valid = (ch == '_' || ch == '$') ||
                           (ch >= 'a' && ch <= 'z') ||
                           (ch >= 'A' && ch <= 'Z') ||
                           (ch >= '0' && ch <= '9');
        out += valid ? static_cast<char>(ch) : '_';
    }
    if(out.empty()) return out;
    const unsigned char first = static_cast<unsigned char>(out.front());
    if(!(first == '_' || (first >= 'a' && first <= 'z') ||
         (first >= 'A' && first <= 'Z'))){
        out.insert(out.begin(), '_');
    }
    return out;
}

std::string unique_identifier(const std::string& raw, std::set<std::string>* used){
    std::string candidate = sanitize_identifier(raw);
    if(candidate.empty()) candidate = "sig";
    if(Verilator::is_reserved_verilog_identifier(candidate)) candidate += "_";
    while(used->find(candidate) != used->end()){
        candidate += "_";
    }
    used->insert(candidate);
    return candidate;
}

std::string geo_key(int x, int y){
    return "g" + std::to_string(x) + "," + std::to_string(y);
}

std::string wire_key(const std::string& wire_id, char side){
    return "w" + wire_id + side;
}

std::string label_key(const std::string& scope, const std::string& text){
    return "l" + scope + "|" + text;
}

std::string pin_key(int component_id, const std::string& pin_id){
    return "p" + std::to_string(component_id) + ":" + pin_id;
}

bool is_ground_category(const std::string& category){
    return category == "Ground";
}

bool is_power_type(const std::string& type){
    return type == "POWER";
}

bool is_ground_type(const std::string& type){
    return type == "GND";
}

// 电源/地引脚 -> 常量值。无法判定的引脚返回 false。
bool constant_value_for_pin(const NetlistData::PcbSnapshotComponent& component,
                            const NetlistData::PcbSnapshotPin& pin,
                            LogicValue* value){
    if(is_ground_category(component.category) || is_ground_type(component.type)){
        *value = LogicValue::Zero;
        return true;
    }
    if(is_power_type(component.type)){
        if(pin.id == "+"){ *value = LogicValue::One;  return true; }
        if(pin.id == "-"){ *value = LogicValue::Zero; return true; }
        return false;
    }
    return false;
}

} // namespace

ProjectToModuleResult BuildModuleFromProject(const DealProjectXML::ProjectData& project,
                                             const std::string& topName){
    ProjectToModuleResult result;

    const auto add_error = [&result](std::string code, std::string message){
        result.diagnostics.push_back(
            Verilator::make_error(DiagnosticKind::Model, std::move(code),
                                  std::move(message), Verilator::SourceLocation{}));
    };
    const auto add_warning = [&result](std::string code, std::string message){
        result.diagnostics.push_back(
            Verilator::make_diagnostic(DiagnosticKind::Model,
                                       DiagnosticSeverity::Warning,
                                       std::move(code), std::move(message),
                                       Verilator::SourceLocation{}));
    };

    // ---- 模块名 ----
    {
        std::string name = sanitize_identifier(topName);
        if(name.empty()) name = "top";
        if(Verilator::is_reserved_verilog_identifier(name)) name += "_";
        if(name != topName){
            add_warning("PROJECT_VERILOG_MODULE_NAME_ADJUSTED",
                        "顶层模块名 '" + topName + "' 不是合法的 Verilog 标识符，"
                        "已改用 '" + name + "'。");
        }
        result.module.name = name;
    }

    // ---- 项目快照：复用已有的引脚/导线解析 ----
    const NetlistData::PcbProjectSnapshot snapshot =
        NetlistData::BuildPcbSnapshotFromProject(project);
    for(const NetlistData::Diagnostic& diagnostic : snapshot.diagnostics){
        const DiagnosticSeverity severity =
            diagnostic.severity == "error" ? DiagnosticSeverity::Error
                                           : DiagnosticSeverity::Warning;
        result.diagnostics.push_back(
            Verilator::make_diagnostic(DiagnosticKind::Model, severity,
                                       diagnostic.code, diagnostic.message,
                                       Verilator::SourceLocation{}));
    }

    // ---- 元件分类 ----
    std::map<int, ComponentInfo> components;
    for(const NetlistData::PcbSnapshotComponent& component : snapshot.components){
        int id = 0;
        try{
            id = std::stoi(component.instanceId);
        }catch(...){
            add_error("PROJECT_VERILOG_COMPONENT_ID_INVALID",
                      "元件实例 ID '" + component.instanceId + "' 不是整数。");
            continue;
        }

        ComponentInfo info;
        info.component = &component;
        info.is_input_terminal  = component.type == "IN_TERMINAL";
        info.is_output_terminal = component.type == "OUT_TERMINAL";
        info.is_constant        = is_power_type(component.type) ||
                                  is_ground_type(component.type) ||
                                  is_ground_category(component.category);
        Verilator::GateKind ignored_kind = Verilator::GateKind::And;
        info.is_gate = gate_kind_for_type(component.type, &ignored_kind);

        if(component.pins.empty()){
            add_error("PROJECT_VERILOG_COMPONENT_NO_PINS",
                      "元件 '" + component.type + "'（实例 " +
                          component.instanceId + "）没有引脚，无法导出。");
        }

        if(!info.is_gate && !info.is_input_terminal && !info.is_output_terminal &&
           !info.is_constant){
            if(component.category == "Hierarchy"){
                add_error("PROJECT_VERILOG_HIERARCHY_UNSUPPORTED",
                          "模块实例 '" + component.instanceId +
                              "' 属于层次化元件，当前版本不支持导出 Verilog。");
            }else{
                add_error("PROJECT_VERILOG_UNSUPPORTED_COMPONENT",
                          "元件类型 '" + component.type + "'（实例 " +
                              component.instanceId + "）不支持导出 Verilog。");
            }
        }

        components.emplace(id, info);
    }

    // ---- 端口名（先于网络确定，网络名要与端口名一致） ----
    std::set<std::string> used_names;
    std::map<int, std::string> port_name_by_component;
    {
        int input_index = 0;
        int output_index = 0;
        for(const auto& entry : components){
            const ComponentInfo& info = entry.second;
            if(!info.is_input_terminal && !info.is_output_terminal) continue;
            const NetlistData::PcbSnapshotComponent& component = *info.component;

            std::string base = component.reference;
            if(base.empty()){
                base = info.is_input_terminal
                           ? "in" + std::to_string(++input_index)
                           : "out" + std::to_string(++output_index);
            }
            const std::string name = unique_identifier(base, &used_names);
            if(name != base){
                add_warning("PROJECT_VERILOG_PORT_NAME_ADJUSTED",
                            "端子位号 '" + base + "' 不是合法的 Verilog 标识符，"
                            "端口名已改用 '" + name + "'。");
            }
            port_name_by_component.emplace(entry.first, name);
        }
    }

    // ---- 连通性：引脚、导线端点、junction、label ----
    UnionFind uf;
    std::map<std::pair<int, std::string>, std::string> pin_key_by_component_pin;

    for(const auto& entry : components){
        const NetlistData::PcbSnapshotComponent& component = *entry.second.component;
        for(const NetlistData::PcbSnapshotPin& pin : component.pins){
            const std::string key = pin_key(entry.first, pin.id);
            uf.ensure(key);
            uf.unite(key, geo_key(pin.position.x, pin.position.y));
            pin_key_by_component_pin[{entry.first, pin.id}] = key;
        }
    }

    for(const NetlistData::PcbSnapshotWire& wire : snapshot.wires){
        if(wire.bus){
            add_warning("PROJECT_VERILOG_BUS_UNSUPPORTED",
                        "总线 '" + wire.id + "' 不参与 Verilog 导出。");
            continue;
        }
        if(wire.points.size() < 2) continue;

        const std::string start_key = wire_key(wire.id, 's');
        const std::string end_key   = wire_key(wire.id, 'e');
        uf.ensure(start_key);
        uf.ensure(end_key);
        uf.unite(start_key, end_key);

        const auto connect_endpoint = [&](const NetlistData::PcbSnapshotEndpoint& endpoint,
                                          const std::string& key){
            uf.unite(key, geo_key(endpoint.point.x, endpoint.point.y));
            if(endpoint.kind == "pin"){
                int component_id = 0;
                try{
                    component_id = std::stoi(endpoint.componentId);
                }catch(...){
                    add_warning("PROJECT_VERILOG_ENDPOINT_INVALID",
                                "导线 '" + wire.id + "' 的引脚端点引用了非法实例 ID。");
                    return;
                }
                const std::string target = pin_key(component_id, endpoint.pinId);
                uf.ensure(target);
                uf.unite(key, target);
            }else if(endpoint.kind == "junction"){
                uf.unite(key, "j" + endpoint.junctionId);
            }else if(endpoint.kind == "label"){
                uf.unite(key, "label-id:" + endpoint.labelId);
            }
        };
        connect_endpoint(wire.start, start_key);
        connect_endpoint(wire.end, end_key);
    }

    for(const NetlistData::PcbSnapshotJunction& junction : snapshot.junctions){
        uf.ensure("j" + junction.id);
        uf.unite("j" + junction.id, geo_key(junction.position.x, junction.position.y));
    }

    for(const NetlistData::PcbSnapshotLabel& label : snapshot.labels){
        const std::string key = label_key(label.scope, label.text);
        uf.ensure(key);
        uf.unite(key, geo_key(label.position.x, label.position.y));
        // 导线端点通过 label-id 引用标签，这里把两种引法合并到同一点。
        uf.unite("label-id:" + label.id, key);
    }

    // 合并后重新归组（导线可能把多个引脚组连在一起）。
    std::map<std::string, std::vector<PinRef>> merged_pins;
    std::map<std::string, std::vector<std::string>> merged_labels;
    for(const auto& entry : pin_key_by_component_pin){
        const std::string root = uf.find(entry.second);
        merged_pins[root].push_back(PinRef{entry.first.first, entry.first.second});
    }
    for(const NetlistData::PcbSnapshotLabel& label : snapshot.labels){
        merged_labels[uf.find(label_key(label.scope, label.text))].push_back(label.text);
    }

    // ---- 每组网络的常量属性与端子 ----
    std::map<std::string, LogicValue> constant_by_group;
    std::map<std::string, int> terminal_by_group;

    for(const auto& entry : merged_pins){
        for(const PinRef& pin_ref : entry.second){
            const auto info_it = components.find(pin_ref.component_id);
            if(info_it == components.end()) continue;
            const ComponentInfo& info = info_it->second;
            const NetlistData::PcbSnapshotComponent& component = *info.component;

            if(info.is_constant){
                const auto pin_it = std::find_if(
                    component.pins.begin(), component.pins.end(),
                    [&](const NetlistData::PcbSnapshotPin& pin){
                        return pin.id == pin_ref.pin_id;
                    });
                if(pin_it == component.pins.end()) continue;

                LogicValue value = LogicValue::Zero;
                if(!constant_value_for_pin(component, *pin_it, &value)){
                    add_error("PROJECT_VERILOG_CONSTANT_PIN_UNKNOWN",
                              "电源/地元件 '" + component.type + "'（实例 " +
                                  component.instanceId + "）的引脚 '" +
                                  pin_ref.pin_id + "' 无法映射为常量。");
                    continue;
                }
                const auto known = constant_by_group.find(entry.first);
                if(known != constant_by_group.end() && known->second != value){
                    add_error("PROJECT_VERILOG_CONFLICTING_CONSTANT",
                              "同一网络同时连接了 1'b0 和 1'b1 两个常量源。");
                }else{
                    constant_by_group[entry.first] = value;
                }
            }else if(info.is_input_terminal || info.is_output_terminal){
                const auto known = terminal_by_group.find(entry.first);
                if(known != terminal_by_group.end()){
                    add_error("PROJECT_VERILOG_TERMINAL_SHARED_NET",
                              "端子 '" + component.reference + "'（实例 " +
                                  component.instanceId + "）与另一个端子连接到了同一网络，"
                                  "无法为两个端口生成同一个 Verilog 信号。");
                }else{
                    terminal_by_group[entry.first] = pin_ref.component_id;
                }
            }
        }
    }

    // 端子网络同时有常量源 -> 冲突
    for(const auto& entry : terminal_by_group){
        if(constant_by_group.find(entry.first) != constant_by_group.end()){
            add_error("PROJECT_VERILOG_PORT_ON_CONSTANT",
                      "输入/输出端子连接到了常量网络，无法导出为端口。");
        }
    }

    // ---- 网络与信号 ----
    std::map<std::string, std::string> net_id_by_group;
    std::map<std::string, std::string> pin_net_id;

    int net_index = 0;
    for(const auto& entry : merged_pins){
        const std::string& root = entry.first;
        const bool is_constant = constant_by_group.find(root) != constant_by_group.end();

        std::string net_id;
        if(is_constant){
            net_id = "k" + std::to_string(net_index++);
            result.module.nets.push_back(Verilator::Net{
                net_id,
                constant_by_group[root] == LogicValue::One ? "1'b1" : "1'b0",
                true,
                constant_by_group[root],
                Verilator::SourceLocation{},
                {},
                {}
            });
            result.module.nets.back().drivers.push_back(Verilator::Driver{
                Verilator::DriverKind::Constant, "", "",
                constant_by_group[root], Verilator::SourceLocation{}
            });
        }else{
            net_id = "n" + std::to_string(net_index++);

            std::string net_name;
            const auto terminal_it = terminal_by_group.find(root);
            if(terminal_it != terminal_by_group.end()){
                const auto name_it = port_name_by_component.find(terminal_it->second);
                net_name = name_it != port_name_by_component.end()
                               ? name_it->second
                               : "port_" + std::to_string(terminal_it->second);
            }else{
                const auto labels = merged_labels.find(root);
                if(labels != merged_labels.end() && !labels->second.empty()){
                    std::set<std::string> sorted(labels->second.begin(), labels->second.end());
                    const std::string candidate = *sorted.begin();
                    if(Verilator::is_valid_verilog_identifier(candidate) &&
                       !Verilator::is_reserved_verilog_identifier(candidate) &&
                       used_names.find(candidate) == used_names.end()){
                        net_name = candidate;
                    }
                }
                if(net_name.empty()){
                    net_name = unique_identifier("net" + std::to_string(net_index), &used_names);
                }else{
                    used_names.insert(net_name);
                }
            }

            result.module.nets.push_back(Verilator::Net{
                net_id, net_name, false, LogicValue::Zero,
                Verilator::SourceLocation{}, {}, {}
            });
            result.module.signals.push_back(Verilator::Signal{
                net_id, net_name, SignalKind::Wire, Verilator::SourceLocation{}
            });
        }

        net_id_by_group[root] = net_id;
        for(const PinRef& pin_ref : entry.second){
            pin_net_id[pin_key(pin_ref.component_id, pin_ref.pin_id)] = net_id;
        }
    }

    // 常量组的网络 id 也已建立，这里补齐引脚 -> 网络映射（上面循环已覆盖）。

    // ---- 端口与端子连接 ----
    for(const auto& entry : components){
        const ComponentInfo& info = entry.second;
        if(!info.is_input_terminal && !info.is_output_terminal) continue;

        const NetlistData::PcbSnapshotComponent& component = *info.component;
        const auto name_it = port_name_by_component.find(entry.first);
        if(name_it == port_name_by_component.end()) continue;
        const std::string& port_name = name_it->second;

        if(component.pins.empty()){
            add_error("PROJECT_VERILOG_TERMINAL_NO_PIN",
                      "端子 '" + port_name + "' 没有引脚。");
            continue;
        }
        const std::string pin_id = component.pins.front().id;
        const std::string group_key = uf.find(pin_key(entry.first, pin_id));
        const auto net_it = net_id_by_group.find(group_key);
        if(net_it == net_id_by_group.end()) continue;

        // 同一网络上的第二个端子已在前面报错，这里只保留第一个。
        if(terminal_by_group[group_key] != entry.first) continue;
        if(constant_by_group.find(group_key) != constant_by_group.end()) continue;

        const PortDirection direction = info.is_input_terminal
                                            ? PortDirection::Input
                                            : PortDirection::Output;
        result.module.ports.push_back(Verilator::Port{
            port_name, direction, Verilator::SourceLocation{}
        });

        Verilator::Net* net = Verilator::find_net(result.module, net_it->second);
        if(!net) continue;

        if(direction == PortDirection::Input){
            net->drivers.push_back(Verilator::Driver{
                Verilator::DriverKind::Port, port_name, "",
                LogicValue::Zero, Verilator::SourceLocation{}
            });
            // 端口信号类型由 Wire 改成 Input（id/name 不变）。
            for(Verilator::Signal& signal : result.module.signals){
                if(signal.id == net->id){
                    signal.kind = SignalKind::Input;
                    break;
                }
            }
        }else{
            net->sinks.push_back(Verilator::Terminal{
                Verilator::TerminalKind::Port, port_name, "",
                Verilator::SourceLocation{}
            });
            for(Verilator::Signal& signal : result.module.signals){
                if(signal.id == net->id){
                    signal.kind = SignalKind::Output;
                    break;
                }
            }
        }
    }

    // 端口按输入在前、输出在后排列，生成代码更符合常见的 Verilog 书写顺序。
    std::stable_sort(result.module.ports.begin(), result.module.ports.end(),
                     [](const Verilator::Port& left, const Verilator::Port& right){
                         return left.direction == PortDirection::Input &&
                                right.direction == PortDirection::Output;
                     });

    // ---- 门 ----
    for(const auto& entry : components){
        const ComponentInfo& info = entry.second;
        if(!info.is_gate) continue;

        Verilator::GateKind kind = Verilator::GateKind::And;
        gate_kind_for_type(info.component->type, &kind);

        Verilator::LogicGate gate;
        gate.id = "g" + std::to_string(entry.first);
        gate.kind = kind;

        const NetlistData::PcbSnapshotComponent& component = *info.component;
        int input_index = 0;
        std::vector<std::string> input_pin_ids;
        std::vector<const NetlistData::PcbSnapshotPin*> output_pins;
        for(const NetlistData::PcbSnapshotPin& pin : component.pins){
            const bool input = pin.electricalType == "input" ||
                               (pin.electricalType.empty() && pin.id.rfind("IN", 0) == 0);
            const bool output = pin.electricalType == "output" ||
                                (pin.electricalType.empty() && pin.id.rfind("OUT", 0) == 0);
            if(input){
                gate.inputs.push_back(Verilator::Pin{
                    "in" + std::to_string(input_index++), pin.name,
                    Verilator::PinDirection::Input
                });
                input_pin_ids.push_back(pin.id);
            }else if(output){
                output_pins.push_back(&pin);
            }
        }

        if(output_pins.empty()){
            add_error("PROJECT_VERILOG_GATE_NO_OUTPUT",
                      "门 '" + gate.id + "' 没有输出引脚。");
            continue;
        }
        if(output_pins.size() > 1){
            add_error("PROJECT_VERILOG_GATE_MULTIPLE_OUTPUT",
                      "门 '" + gate.id + "' 有多个输出引脚，当前版本不支持。");
        }
        gate.output = Verilator::Pin{"out", output_pins.front()->name,
                                     Verilator::PinDirection::Output};

        if(gate.inputs.size() != Verilator::gate_input_arity(kind)){
            add_error("PROJECT_VERILOG_GATE_ARITY",
                      "门 '" + gate.id + "'（" +
                          std::string(Verilator::gate_kind_name(kind)) +
                          "）的输入引脚数量不符合当前 Verilog 子集要求。");
        }

        // 输出网络：登记驱动
        const std::string output_pin_key = pin_key(entry.first, output_pins.front()->id);
        const auto output_net_it = pin_net_id.find(output_pin_key);
        if(output_net_it != pin_net_id.end()){
            Verilator::Net* net = Verilator::find_net(result.module, output_net_it->second);
            if(net){
                if(net->isConstant){
                    add_error("PROJECT_VERILOG_CONSTANT_DRIVEN",
                              "门 '" + gate.id + "' 的输出连接到了常量网络。");
                }else{
                    net->drivers.push_back(Verilator::Driver{
                        Verilator::DriverKind::GateOutput, gate.id, "out",
                        LogicValue::Zero, Verilator::SourceLocation{}
                    });
                }
            }
        }

        // 输入网络：登记接收端
        for(std::size_t i = 0; i < gate.inputs.size(); ++i){
            const auto pin_it = pin_net_id.find(pin_key(entry.first, input_pin_ids[i]));
            if(pin_it == pin_net_id.end()) continue;
            Verilator::Net* net = Verilator::find_net(result.module, pin_it->second);
            if(!net) continue;
            net->sinks.push_back(Verilator::Terminal{
                Verilator::TerminalKind::GateInput, gate.id, gate.inputs[i].id,
                Verilator::SourceLocation{}
            });
        }

        result.module.gates.push_back(std::move(gate));
    }

    // ---- 多重驱动：给出比验证器更贴近项目语义的诊断 ----
    for(const Verilator::Net& net : result.module.nets){
        if(net.drivers.size() > 1 && !net.isConstant){
            add_error("PROJECT_VERILOG_MULTIPLE_DRIVERS",
                      "网络 '" + net.name + "' 存在多个驱动源。");
        }
    }

    if(!Verilator::has_error(result.diagnostics)){
        const Verilator::ValidationResult validation =
            Verilator::validate_circuit(result.module);
        result.diagnostics.insert(result.diagnostics.end(),
                                  validation.diagnostics.begin(),
                                  validation.diagnostics.end());
    }

    result.ok = !Verilator::has_error(result.diagnostics);
    return result;
}

} // namespace VerilogBridge

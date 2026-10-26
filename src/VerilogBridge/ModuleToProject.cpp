#include "./ModuleToProject.hpp"

#include "../Canva/CanvasItem.hpp"
#include "../metadata/MetaDataJson.hpp"
#include "../verilator/VerilatorApi.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace VerilogBridge{
namespace{

using Verilator::DiagnosticKind;
using Verilator::DiagnosticSeverity;
using Verilator::DriverKind;
using Verilator::LogicValue;
using Verilator::PortDirection;
using Verilator::TerminalKind;

constexpr int kLayerStepX = 220;   //层间距
constexpr int kRowStepY = 90;      //同层行距
constexpr int kFirstRowY = 40;     //首行纵向偏移
constexpr int kGrid = 10;          //布局所用网格

//按类型缓存元数据解析出的引脚与显示名（避免重复读 JSON）。
struct TypeInfo{
    std::vector<Pin> pins;
    std::string name;
    std::string category;
};

TypeInfo& type_info_for(std::map<std::string, TypeInfo>* cache, const std::string& type){
    auto it = cache->find(type);
    if(it != cache->end()) return it->second;

    TypeInfo info;
    MetaData* metadata = load_metadata(type);
    if(metadata){
        info.name = metadata->get_name();
        info.category = metadata->get_category();
    }
    info.pins = item_pins_for(metadata, type);
    delete metadata;
    return cache->emplace(type, std::move(info)).first->second;
}

const Pin* find_type_pin(const TypeInfo& info, const std::string& pin_id){
    for(const Pin& pin : info.pins){
        if(pin.id == pin_id) return &pin;
    }
    return nullptr;
}

wxPoint pin_position(const Pin& pin, const wxPoint& component){
    return wxPoint(component.x + pin.offset.x, component.y + pin.offset.y);
}

bool pin_position_for(const TypeInfo& info, const wxPoint& component,
                      const std::string& pin_id, wxPoint* out){
    const Pin* pin = find_type_pin(info, pin_id);
    if(!pin) return false;
    *out = pin_position(*pin, component);
    return true;
}

int snap_value(int value){
    return static_cast<int>(std::lround(static_cast<double>(value) / kGrid)) * kGrid;
}

//正交折线：先水平走到中间 x，再竖直，再水平到终点。
//布局保证终点在起点右侧；若方向相反则改用中间 y 折线，仍保证没有斜线。
std::vector<wxPoint> orthogonal_path(const wxPoint& from, const wxPoint& to){
    if(from.x == to.x || from.y == to.y) return {from, to};
    if(to.x > from.x){
        int mid_x = snap_value((from.x + to.x) / 2);
        if(mid_x <= from.x) mid_x = from.x + kGrid;
        if(mid_x >= to.x) mid_x = to.x - kGrid;
        return {from, wxPoint(mid_x, from.y), wxPoint(mid_x, to.y), to};
    }
    int mid_y = snap_value((from.y + to.y) / 2);
    if(mid_y == from.y) mid_y += kGrid;
    return {from, wxPoint(from.x, mid_y), wxPoint(to.x, mid_y), to};
}

//驱动源分类：门输出 / 输入端口 / 常量。
struct SourceRef{
    enum class Kind{ None, Gate, InputPort, Constant };
    Kind kind = Kind::None;
    std::string owner;                 //门 id / 端口名 / 常量网络 id
    LogicValue value = LogicValue::Zero;
};

//沿 DriverKind::Net 别名链回溯到真正的驱动网络；遇到环返回 nullptr。
const Verilator::Net* resolve_driver_net(const Verilator::Module& module,
                                         const Verilator::Net& net){
    const Verilator::Net* current = &net;
    std::set<std::string> visited;
    while(current){
        if(!visited.insert(current->id).second) return nullptr;
        if(current->isConstant) return current;
        if(current->drivers.empty()) return nullptr;
        const Verilator::Driver& driver = current->drivers.front();
        if(driver.kind != DriverKind::Net) return current;
        current = Verilator::find_net(module, driver.ownerId);
    }
    return nullptr;
}

SourceRef resolve_source(const Verilator::Module& module, const Verilator::Net& net){
    SourceRef source;
    const Verilator::Net* resolved = resolve_driver_net(module, net);
    if(!resolved || resolved->drivers.empty()) return source;

    const Verilator::Driver& driver = resolved->drivers.front();
    if(resolved->isConstant || driver.kind == DriverKind::Constant){
        source.kind = SourceRef::Kind::Constant;
        source.owner = resolved->id;
        source.value = resolved->isConstant ? resolved->constantValue
                                            : driver.constantValue;
        return source;
    }
    if(driver.kind == DriverKind::GateOutput){
        source.kind = SourceRef::Kind::Gate;
        source.owner = driver.ownerId;
        return source;
    }
    if(driver.kind == DriverKind::Port){
        source.kind = SourceRef::Kind::InputPort;
        source.owner = driver.ownerId;
        return source;
    }
    return source;
}

} // namespace

ModuleToProjectResult BuildProjectFromModule(const Verilator::Module& module,
                                             const wxPoint& origin){
    ModuleToProjectResult result;

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

    if(module.ports.empty() && module.gates.empty()){
        add_error("PROJECT_VERILOG_EMPTY_MODULE",
                  "模块 '" + module.name + "' 既没有端口也没有逻辑门，无法生成画布内容。");
        result.ok = false;
        return result;
    }

    //先完整校验：模型不合法时不做任何布局/映射。
    const Verilator::ValidationResult validation = Verilator::validate_circuit(module);
    if(!validation.ok){
        result.diagnostics.insert(result.diagnostics.end(),
                                  validation.diagnostics.begin(),
                                  validation.diagnostics.end());
        result.ok = false;
        return result;
    }

    std::map<std::string, TypeInfo> type_cache;

    result.project.schema_version = "2.0";
    result.project.info.name = module.name;
    result.project.offset = origin;
    result.project.scale = 1.0f;
    result.project.grid_step = 10;
    result.project.libraries.push_back(DealProjectXML::LibraryData{"builtin", "1.0"});

    // ---- 索引：网络 -> 接收端，端口 -> 网络 ----
    std::map<std::pair<std::string, std::string>, const Verilator::Net*> sink_net_by_gate_pin;
    std::map<std::string, const Verilator::Net*> input_port_net;
    std::map<std::string, const Verilator::Net*> output_port_net;
    for(const Verilator::Net& net : module.nets){
        if(net.isConstant) continue;
        for(const Verilator::Terminal& sink : net.sinks){
            if(sink.kind == TerminalKind::GateInput){
                sink_net_by_gate_pin[{sink.ownerId, sink.pinId}] = &net;
            }else if(sink.kind == TerminalKind::Port){
                output_port_net[sink.ownerId] = &net;
            }
        }
        for(const Verilator::Driver& driver : net.drivers){
            if(driver.kind == DriverKind::Port){
                input_port_net[driver.ownerId] = &net;
            }
        }
    }

    // ---- 拓扑分层：门所在层 = 前驱最大层 + 1（至少第 1 层） ----
    std::map<std::string, int> gate_layers;
    std::set<std::string> visiting;
    std::function<int(const std::string&)> layer_of = [&](const std::string& gate_id) -> int{
        const auto known = gate_layers.find(gate_id);
        if(known != gate_layers.end()) return known->second;
        if(!visiting.insert(gate_id).second) return 0;   //校验已排除环，这里仅防御

        int best = 0;
        const Verilator::LogicGate* gate = Verilator::find_gate(module, gate_id);
        if(gate){
            for(const Verilator::Pin& pin : gate->inputs){
                const auto net_it = sink_net_by_gate_pin.find({gate_id, pin.id});
                if(net_it == sink_net_by_gate_pin.end() || !net_it->second) continue;
                const SourceRef source = resolve_source(module, *net_it->second);
                if(source.kind == SourceRef::Kind::Gate){
                    best = std::max(best, layer_of(source.owner) + 1);
                }
            }
        }
        visiting.erase(gate_id);
        const int layer = std::max(1, best);
        gate_layers[gate_id] = layer;
        return layer;
    };
    for(const Verilator::LogicGate& gate : module.gates) layer_of(gate.id);

    int max_gate_layer = 0;
    for(const auto& entry : gate_layers){
        max_gate_layer = std::max(max_gate_layer, entry.second);
    }
    const int output_layer = module.gates.empty() ? 1 : max_gate_layer + 1;

    // ---- 元件与位置 ----
    int next_component_id = 1;
    int reference_index = 1;
    std::map<int, wxPoint> component_positions;
    std::map<int, std::string> component_types;
    std::map<std::string, int> input_port_component;
    std::map<std::string, int> output_port_component;

    const auto add_component = [&](const std::string& type, const wxPoint& position,
                                   const std::string& reference) {
        TypeInfo& info = type_info_for(&type_cache, type);
        DealProjectXML::ComponentData data;
        data.id = next_component_id++;
        data.type = type;
        data.name = info.name.empty() ? type : info.name;
        data.category = info.category;
        data.x = position.x;
        data.y = position.y;
        data.reference = reference;
        component_positions[data.id] = position;
        component_types[data.id] = type;
        result.project.components.push_back(std::move(data));
        return next_component_id - 1;
    };

    int input_row = 0;
    for(const Verilator::Port& port : module.ports){
        if(port.direction != PortDirection::Input) continue;
        const wxPoint position(origin.x,
                               origin.y + kFirstRowY + input_row * kRowStepY);
        input_port_component[port.name] =
            add_component("IN_TERMINAL", position, port.name);
        ++input_row;
    }

    int output_row = 0;
    for(const Verilator::Port& port : module.ports){
        if(port.direction != PortDirection::Output) continue;
        const wxPoint position(origin.x + kLayerStepX * output_layer,
                               origin.y + kFirstRowY + output_row * kRowStepY);
        output_port_component[port.name] =
            add_component("OUT_TERMINAL", position, port.name);
        ++output_row;
    }

    // 门：同层按前驱平均 y 排序，减少连线交叉。
    struct GatePlacement{
        int component_id = 0;
        wxPoint position;
        std::vector<std::string> input_pin_ids;   //与 gate.inputs 一一对应
        std::string output_pin_id = "OUT";
    };
    std::map<std::string, GatePlacement> gate_placements;

    std::map<int, std::vector<const Verilator::LogicGate*>> gates_by_layer;
    for(const Verilator::LogicGate& gate : module.gates){
        gates_by_layer[gate_layers[gate.id]].push_back(&gate);
    }

    for(auto& layer_entry : gates_by_layer){
        std::vector<const Verilator::LogicGate*>& gates = layer_entry.second;
        const auto barycenter = [&](const Verilator::LogicGate* gate) {
            double sum = 0.0;
            int count = 0;
            for(const Verilator::Pin& pin : gate->inputs){
                const auto net_it = sink_net_by_gate_pin.find({gate->id, pin.id});
                if(net_it == sink_net_by_gate_pin.end() || !net_it->second) continue;
                const SourceRef source = resolve_source(module, *net_it->second);
                int component_id = 0;
                if(source.kind == SourceRef::Kind::Gate){
                    const auto placement = gate_placements.find(source.owner);
                    if(placement == gate_placements.end()) continue;
                    component_id = placement->second.component_id;
                }else if(source.kind == SourceRef::Kind::InputPort){
                    const auto component = input_port_component.find(source.owner);
                    if(component == input_port_component.end()) continue;
                    component_id = component->second;
                }else if(source.kind == SourceRef::Kind::Constant){
                    continue;   //常量稍后单独布局
                }
                const auto position = component_positions.find(component_id);
                if(position == component_positions.end()) continue;
                sum += position->second.y;
                ++count;
            }
            return count == 0 ? 0.0 : sum / count;
        };
        std::stable_sort(gates.begin(), gates.end(),
                         [&](const Verilator::LogicGate* left,
                             const Verilator::LogicGate* right){
                             return barycenter(left) < barycenter(right);
                         });

        int row = 0;
        for(const Verilator::LogicGate* gate : gates){
            const std::string type = Verilator::gate_kind_name(gate->kind);
            const wxPoint position(origin.x + kLayerStepX * layer_entry.first,
                                   origin.y + kFirstRowY + row * kRowStepY);
            const int component_id =
                add_component(type, position, "U" + std::to_string(reference_index++));

            GatePlacement placement;
            placement.component_id = component_id;
            placement.position = position;

            TypeInfo& info = type_info_for(&type_cache, type);
            for(std::size_t i = 0; i < gate->inputs.size(); ++i){
                const std::string pin_id = "IN" + std::to_string(i + 1);
                if(!find_type_pin(info, pin_id)){
                    add_error("PROJECT_VERILOG_GATE_PIN_MISSING",
                              "门元件 '" + type + "' 缺少输入引脚 '" + pin_id + "'。");
                }
                placement.input_pin_ids.push_back(pin_id);
            }
            if(!find_type_pin(info, placement.output_pin_id)){
                add_error("PROJECT_VERILOG_GATE_PIN_MISSING",
                          "门元件 '" + type + "' 缺少输出引脚 '" +
                              placement.output_pin_id + "'。");
            }
            gate_placements[gate->id] = placement;
            ++row;
        }
    }

    // 接收端 -> 画布元件引脚。
    const auto sink_target = [&](const Verilator::Terminal& sink, int* component_id,
                                 std::string* pin_id) -> bool {
        if(sink.kind == TerminalKind::GateInput){
            const auto placement = gate_placements.find(sink.ownerId);
            if(placement == gate_placements.end()) return false;
            const Verilator::LogicGate* gate = Verilator::find_gate(module, sink.ownerId);
            if(!gate) return false;
            for(std::size_t i = 0; i < gate->inputs.size(); ++i){
                if(gate->inputs[i].id != sink.pinId) continue;
                if(i >= placement->second.input_pin_ids.size()) return false;
                *component_id = placement->second.component_id;
                *pin_id = placement->second.input_pin_ids[i];
                return true;
            }
            return false;
        }
        if(sink.kind == TerminalKind::Port){
            const auto component = output_port_component.find(sink.ownerId);
            if(component == output_port_component.end()) return false;
            *component_id = component->second;
            *pin_id = "A";
            return true;
        }
        return false;
    };

    // ---- 常量：每个常量网络一个电源元件，放在其接收元件左侧 ----
    std::map<std::string, int> constant_component;   //常量网络 id -> 元件 id
    std::map<int, std::string> constant_pin;         //元件 id -> "+" / "-"
    std::map<int, int> constant_slot;                //锚点元件 -> 已放置常量数

    for(const Verilator::Net& net : module.nets){
        const SourceRef source = resolve_source(module, net);
        if(source.kind != SourceRef::Kind::Constant) continue;
        if(constant_component.find(source.owner) != constant_component.end()) continue;

        int anchor_component = 0;
        for(const Verilator::Terminal& sink : net.sinks){
            int component_id = 0;
            std::string pin_id;
            if(sink_target(sink, &component_id, &pin_id)){
                anchor_component = component_id;
                break;
            }
        }
        if(anchor_component == 0) continue;   //常量网络未被使用：不放置符号

        const auto anchor_position = component_positions.find(anchor_component);
        if(anchor_position == component_positions.end()) continue;
        const int slot = constant_slot[anchor_component]++;
        const wxPoint position(anchor_position->second.x - 160,
                               anchor_position->second.y + slot * 60);

        const int power_id = add_component(
            "POWER", position, "V" + std::to_string(anchor_component));
        constant_component[source.owner] = power_id;
        constant_pin[power_id] = source.value == LogicValue::One ? "+" : "-";
    }

    // ---- 导线：驱动引脚 -> 每个接收引脚 ----
    int next_wire_id = 1;
    for(const Verilator::Net& net : module.nets){
        if(net.sinks.empty()) continue;

        const SourceRef source = resolve_source(module, net);
        int source_component = 0;
        std::string source_pin;
        if(source.kind == SourceRef::Kind::Gate){
            const auto placement = gate_placements.find(source.owner);
            if(placement == gate_placements.end()) continue;
            source_component = placement->second.component_id;
            source_pin = placement->second.output_pin_id;
        }else if(source.kind == SourceRef::Kind::InputPort){
            const auto component = input_port_component.find(source.owner);
            if(component == input_port_component.end()) continue;
            source_component = component->second;
            source_pin = "A";
        }else if(source.kind == SourceRef::Kind::Constant){
            const auto component = constant_component.find(source.owner);
            if(component == constant_component.end()) continue;
            source_component = component->second;
            source_pin = constant_pin[source_component];
        }else{
            add_error("PROJECT_VERILOG_DRIVER_UNRESOLVED",
                      "网络 '" + net.name + "' 没有可用的驱动源，无法生成导线。");
            continue;
        }

        const auto source_type = component_types.find(source_component);
        const auto source_position = component_positions.find(source_component);
        if(source_type == component_types.end() || source_position == component_positions.end()){
            continue;
        }
        wxPoint source_point;
        const TypeInfo& source_info = type_info_for(&type_cache, source_type->second);
        if(!pin_position_for(source_info, source_position->second, source_pin,
                             &source_point)){
            add_error("PROJECT_VERILOG_PIN_UNRESOLVED",
                      "元件引脚 '" + source_pin + "' 在元数据中不存在。");
            continue;
        }

        for(const Verilator::Terminal& sink : net.sinks){
            int target_component = 0;
            std::string target_pin;
            if(!sink_target(sink, &target_component, &target_pin)){
                add_error("PROJECT_VERILOG_SINK_UNRESOLVED",
                          "网络 '" + net.name + "' 的接收端无法映射到画布元件。");
                continue;
            }

            const auto target_type = component_types.find(target_component);
            const auto target_position = component_positions.find(target_component);
            if(target_type == component_types.end() ||
               target_position == component_positions.end()){
                continue;
            }
            wxPoint target_point;
            const TypeInfo& target_info = type_info_for(&type_cache, target_type->second);
            if(!pin_position_for(target_info, target_position->second, target_pin,
                                 &target_point)){
                add_error("PROJECT_VERILOG_PIN_UNRESOLVED",
                          "元件引脚 '" + target_pin + "' 在元数据中不存在。");
                continue;
            }

            DealProjectXML::WireData wire;
            wire.id = next_wire_id++;
            wire.type = "WIRE";
            wire.name = "导线";
            wire.points = orthogonal_path(source_point, target_point);
            wire.start.kind = "pin";
            wire.start.component_id = source_component;
            wire.start.pin_id = source_pin;
            wire.end.kind = "pin";
            wire.end.component_id = target_component;
            wire.end.pin_id = target_pin;
            result.project.wires.push_back(std::move(wire));
        }
    }

    if(module.name.empty()){
        add_warning("PROJECT_VERILOG_MODULE_NAME_EMPTY",
                    "模块名为空，画布内容已生成但缺少名称。");
    }

    result.ok = !Verilator::has_error(result.diagnostics);
    return result;
}

} // namespace VerilogBridge

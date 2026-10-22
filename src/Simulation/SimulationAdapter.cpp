#include"SimulationAdapter.hpp"

#include<algorithm>
#include<map>
#include<set>
#include<string>

// 元件 ↔ 仿真角色 的映射表（以后新增可仿真元件，只需要改这里）：
//
//   LOGIC GATE   type = NOT / AND / OR / NAND / NOR / XOR（category=LogicGate）
//                引脚 direction=input 顺序即输入顺序，direction=output 为输出
//   SWITCH       category=Switch 或 type 以 "Switch-COM-" 开头（SPST/按钮/SPDT…）
//                两脚只取"已接线"的那一脚作为被驱动网络；状态存实例参数 State=0/1
//   POWER        引脚 "+"=1、"-"=0（常量源）
//   GND          引脚 "GND"=0（常量源）
//   PROBE        Miscellaneous-COM-Probe_Point（1 脚）：观察该脚网络
//   LAMP         Miscellaneous-COM-Lamp-Incandescent / Lamp-Indicator / Diode-COM-LED
//                两脚取已接线的那一脚作为观察网络，值=1 时画亮
//   其它         不参与仿真（给 warning + 画布上打"不支持"标记）
namespace Simulation{
namespace{

// ---- 并查集：键是字符串节点名；根固定取字典序最小的节点，结果与合并顺序无关 ----
class UnionFind{
public:
    void ensure(const std::string& key){
        if(parent_.find(key) == parent_.end()) parent_[key] = key;
    }
    std::string find(const std::string& key){
        ensure(key);
        std::string root = key;
        while(parent_[root] != root) root = parent_[root];
        std::string node = key;
        while(parent_[node] != root){
            const std::string next = parent_[node];
            parent_[node] = root;
            node = next;
        }
        return root;
    }
    void unite(const std::string& a, const std::string& b){
        const std::string ra = find(a);
        const std::string rb = find(b);
        if(ra == rb) return;
        if(ra < rb) parent_[rb] = ra;
        else        parent_[ra] = rb;
    }
    const std::map<std::string,std::string>& entries() const{ return parent_; }
private:
    std::map<std::string,std::string> parent_;
};

std::string pin_node(int componentId, const std::string& pinId){
    return "p" + std::to_string(componentId) + ":" + pinId;
}
std::string geo_node(const SimPoint& point){
    return "g" + std::to_string(point.x) + "," + std::to_string(point.y);
}
std::string junction_node(const std::string& id){ return "j" + id; }
std::string label_node(const std::string& scope, const std::string& text){
    return "l" + scope + "|" + text;
}
std::string wire_end_node(int wireId, bool start){
    return "w" + std::to_string(wireId) + (start ? "s" : "e");
}
std::string id_str(int id){ return std::to_string(id); }

//开关状态：实例参数 State（0/1/X），缺省 0（断开）
LogicValue parse_state(const std::map<std::string,std::string>& params){
    const auto it = params.find("State");
    if(it == params.end()) return LogicValue::Zero;
    const std::string& text = it->second;
    if(text == "1" || text == "true" || text == "on" || text == "high") return LogicValue::One;
    if(text == "X" || text == "x" || text == "?" || text == "unknown") return LogicValue::Unknown;
    return LogicValue::Zero;
}

bool gate_kind_for_type(const std::string& type, GateKind& kind){
    if(type == "NOT"){  kind = GateKind::Not;  return true; }
    if(type == "AND"){  kind = GateKind::And;  return true; }
    if(type == "OR"){   kind = GateKind::Or;   return true; }
    if(type == "XOR"){  kind = GateKind::Xor;  return true; }
    if(type == "NAND"){ kind = GateKind::Nand; return true; }
    if(type == "NOR"){  kind = GateKind::Nor;  return true; }
    return false;
}

bool is_lamp_type(const std::string& type){
    return type == "Miscellaneous-COM-Lamp-Incandescent" ||
           type == "Miscellaneous-COM-Lamp-Indicator"    ||
           type == "Diode-COM-LED";
}
bool is_probe_type(const std::string& type){
    return type == "Miscellaneous-COM-Probe_Point" || type == "OUT_TERMINAL";
}
bool is_input_terminal(const std::string& type){ return type == "IN_TERMINAL"; }
bool is_output_terminal(const std::string& type){ return type == "OUT_TERMINAL"; }
bool is_switch_component(const SimComponent& component){
    if(is_input_terminal(component.type)) return true;   //输入端子也是"单端可点击驱动源"
    return component.category == "Switch" || component.type.rfind("Switch-COM-", 0) == 0;
}

const SimLabel* find_label(const SimProject& project, const std::string& id){
    for(const SimLabel& label : project.labels)
        if(label.id == id) return &label;
    return nullptr;
}

// 网络命名表：按组内最小节点名排序后依次命名 net0..netN（确定性）
struct NetTable{
    std::map<std::string, std::string> netOfNode;   //节点 → 网络ID
    std::map<std::string, int> terminals;           //网络ID → 终端数（引脚数+导线数）
    std::vector<std::string> ids;
};

NetTable build_nets(UnionFind& uf){
    std::map<std::string, std::vector<std::string>> groups;
    for(const auto& entry : uf.entries()) groups[uf.find(entry.first)].push_back(entry.first);

    std::vector<std::string> roots;
    roots.reserve(groups.size());
    for(const auto& group : groups) roots.push_back(group.first);
    std::sort(roots.begin(), roots.end(), [&groups](const std::string& a, const std::string& b){
        const std::string& ma = *std::min_element(groups[a].begin(), groups[a].end());
        const std::string& mb = *std::min_element(groups[b].begin(), groups[b].end());
        return ma < mb;
    });

    NetTable table;
    for(size_t i = 0; i < roots.size(); ++i){
        const std::string id = "net" + std::to_string(i);
        table.ids.push_back(id);
        for(const std::string& node : groups[roots[i]]) table.netOfNode[node] = id;
    }
    return table;
}

} // namespace

const char* sim_role_name(SimRole role){
    switch(role){
        case SimRole::Gate:        return "GATE";
        case SimRole::InputSwitch: return "SWITCH";
        case SimRole::Constant:    return "CONST";
        case SimRole::Probe:       return "PROBE";
        case SimRole::Lamp:        return "LAMP";
        case SimRole::Unsupported: break;
    }
    return "UNSUPPORTED";
}

SimulationModel build_simulation_model(const SimProject& project){
    SimulationModel model;
    UnionFind uf;

    //---- 1) 引脚节点：与自身坐标的几何节点合并（旧 1.0 项目按坐标也能连上）----
    for(const SimComponent& component : project.components){
        if(component.pins.empty()){
            model.diagnostics.push_back(make_warning(
                "SIMULATION_COMPONENT_NO_PIN",
                "元件没有引脚定义，无法参与仿真：" + component.name, {id_str(component.id)}));
            continue;
        }
        for(const SimPinRef& pin : component.pins)
            uf.unite(pin_node(component.id, pin.id), geo_node(pin.position));
    }

    //---- 2) 导线：两端同网；端点按 pin/junction/label 连接，并始终合并端点坐标 ----
    std::map<std::pair<int,std::string>, bool> knownPins;
    for(const SimComponent& component : project.components)
        for(const SimPinRef& pin : component.pins)
            knownPins[{component.id, pin.id}] = true;

    for(const SimWire& wire : project.wires){
        const std::string startNode = wire_end_node(wire.id, true);
        const std::string endNode   = wire_end_node(wire.id, false);
        uf.unite(startNode, endNode);   //一根导线是一个导体

        const auto handle_endpoint = [&](const SimEndpoint& endpoint, const std::string& node){
            uf.unite(node, geo_node(endpoint.position));
            if(endpoint.kind == "pin"){
                if(knownPins.find({endpoint.componentId, endpoint.pinId}) == knownPins.end()){
                    model.diagnostics.push_back(make_warning(
                        "SIMULATION_ENDPOINT_PIN_UNKNOWN",
                        "导线端点引用了不存在的引脚，仅按坐标连接。",
                        {id_str(endpoint.componentId), endpoint.pinId}));
                }
                uf.unite(node, pin_node(endpoint.componentId, endpoint.pinId));
            }else if(endpoint.kind == "junction"){
                uf.unite(node, junction_node(endpoint.junctionId));
            }else if(endpoint.kind == "label"){
                if(const SimLabel* label = find_label(project, endpoint.labelId)){
                    uf.unite(node, label_node(label->scope, label->text));
                }else{
                    model.diagnostics.push_back(make_warning(
                        "SIMULATION_ENDPOINT_LABEL_UNKNOWN",
                        "导线端点引用了不存在的网络标签，仅按坐标连接。", {endpoint.labelId}));
                }
            }
            //kind 为空（旧 1.0）或 free：只靠坐标连接
        };
        handle_endpoint(wire.start, startNode);
        handle_endpoint(wire.end, endNode);
    }

    //---- 3) junction / label 自身也并入坐标节点 ----
    for(const SimPoint& position : project.junctionPositions) uf.ensure(geo_node(position));
    for(const SimLabel& label : project.labels)
        uf.unite(label_node(label.scope, label.text), geo_node(label.position));

    //---- 4) 分网络 ----
    NetTable nets = build_nets(uf);

    //---- 5) 引脚/导线 → 网络；统计每个网络的终端数 ----
    for(const SimComponent& component : project.components){
        for(const SimPinRef& pin : component.pins){
            const std::string& net = nets.netOfNode[pin_node(component.id, pin.id)];
            if(net.empty()) continue;
            model.binding.pinNet[{component.id, pin.id}] = net;
            nets.terminals[net] += 1;   //引脚算一个终端
        }
    }
    for(const SimWire& wire : project.wires){
        const std::string& net = nets.netOfNode[wire_end_node(wire.id, true)];
        if(net.empty()) continue;
        model.binding.wireNet[wire.id] = net;
        nets.terminals[net] += 1;       //一根导线算一个终端
    }

    //---- 6) 角色与驱动源（按元件ID升序，保证"取第一个驱动源"是确定的）----
    //标记为不连接(no_connect)但实际接了线：给 warning，仍按已连接处理
    for(const auto& noConnect : project.noConnects){
        const auto it = model.binding.pinNet.find(noConnect);
        if(it == model.binding.pinNet.end()) continue;
        if(nets.terminals[it->second] > 1){
            model.diagnostics.push_back(make_warning(
                "SIMULATION_NOCONNECT_WIRED",
                "标记为不连接的引脚仍然接了线，仿真按已连接处理。",
                {id_str(noConnect.first), noConnect.second}));
        }
    }

    std::vector<const SimComponent*> ordered;
    ordered.reserve(project.components.size());
    for(const SimComponent& component : project.components) ordered.push_back(&component);
    std::sort(ordered.begin(), ordered.end(),
              [](const SimComponent* a, const SimComponent* b){ return a->id < b->id; });

    std::map<std::string, int> driverOf;   //网络 → 驱动它的元件ID
    std::set<std::string> netsToDeclare;

    const auto declare = [&netsToDeclare](const std::string& net){
        if(!net.empty()) netsToDeclare.insert(net);
    };

    //登记驱动源；已被别的元件驱动时给 warning 并返回 false（调用方跳过这个驱动）
    const auto claim_driver = [&](const std::string& net, int componentId,
                                  const std::string& what) -> bool{
        if(net.empty()) return false;
        const auto it = driverOf.find(net);
        if(it != driverOf.end()){
            model.diagnostics.push_back(make_warning(
                "SIMULATION_NET_MULTIPLE_DRIVERS",
                "网络有多个驱动源（" + what + "），只采用元件ID最小的那个。",
                {std::to_string(it->second), id_str(componentId), net}));
            return false;
        }
        driverOf[net] = componentId;
        model.binding.netDriver[net] = componentId;   //诊断：这个网络有驱动源
        declare(net);
        return true;
    };

    //挑"已经接了东西"的引脚（终端数 > 1）；多个/没有时取第一个并标记 ambiguous
    const auto pick_connected_pin = [&](const SimComponent& component,
                                        bool& ambiguous) -> const SimPinRef*{
        ambiguous = false;
        const SimPinRef* best = nullptr;
        int bestScore = -1;
        int connected = 0;
        for(const SimPinRef& pin : component.pins){
            const auto it = model.binding.pinNet.find({component.id, pin.id});
            const int score = (it == model.binding.pinNet.end())
                                  ? 0 : nets.terminals[it->second];
            if(score > 1) ++connected;
            if(score > bestScore){ bestScore = score; best = &pin; }
        }
        ambiguous = connected > 1;
        return best;
    };

    for(const SimComponent* component : ordered){
        const int id = component->id;
        const std::string simId = id_str(id);
        GateKind kind = GateKind::Unsupported;

        //---- 逻辑门 ----
        if(gate_kind_for_type(component->type, kind)){
            std::vector<std::string> inputNets;
            std::string outputNet;
            for(const SimPinRef& pin : component->pins){
                const auto it = model.binding.pinNet.find({id, pin.id});
                if(it == model.binding.pinNet.end()) continue;
                if(pin.direction == "input") inputNets.push_back(it->second);
                else if(pin.direction == "output" && outputNet.empty()) outputNet = it->second;
            }
            if(inputNets.empty() || outputNet.empty()){
                model.diagnostics.push_back(make_error(
                    "SIMULATION_GATE_PIN_INVALID",
                    "逻辑门缺少输入或输出引脚：" + component->name, {simId}));
                model.binding.role[id] = SimRole::Unsupported;
                continue;
            }
            model.binding.role[id] = SimRole::Gate;
            model.binding.deviceNet[id] = outputNet;
            for(const std::string& net : inputNets) declare(net);
            if(claim_driver(outputNet, id, component->name)){
                model.circuit.components.push_back(SimulationComponent(
                    simId, kind, inputNets, outputNet, LogicValue::Unknown, gate_kind_name(kind)));
            }
            continue;
        }

        //---- 输入开关 ----
        if(is_switch_component(*component) && !component->pins.empty()){
            bool ambiguous = false;
            const SimPinRef* pin = pick_connected_pin(*component, ambiguous);
            const auto it = (pin == nullptr) ? model.binding.pinNet.end()
                                             : model.binding.pinNet.find({id, pin->id});
            if(pin == nullptr || it == model.binding.pinNet.end()){
                model.diagnostics.push_back(make_warning(
                    "SIMULATION_COMPONENT_UNCONNECTED",
                    "开关没有接入任何网络：" + component->name, {simId}));
                model.binding.role[id] = SimRole::Unsupported;
                continue;
            }
            if(ambiguous){
                model.diagnostics.push_back(make_warning(
                    "SIMULATION_SWITCH_AMBIGUOUS_PIN",
                    "开关两个引脚都接了线，只把它当单端输入使用（取第一个引脚）。", {simId}));
            }
            const std::string net = it->second;
            //只连到它自己 = 没接线：仍然可点击，但诊断里明确说明
            if(nets.terminals[net] <= 1){
                model.diagnostics.push_back(make_warning(
                    is_input_terminal(component->type)
                        ? "SIMULATION_TERMINAL_UNCONNECTED"
                        : "SIMULATION_COMPONENT_UNCONNECTED",
                    (is_input_terminal(component->type) ? "输入端子" : "开关") +
                        std::string("没有接线（只连到自己），点击不会影响任何网络：") +
                        component->name, {simId}));
            }
            const LogicValue value = parse_state(component->params);
            model.binding.role[id] = SimRole::InputSwitch;
            model.binding.deviceNet[id] = net;
            model.binding.switchValue[id] = value;
            if(claim_driver(net, id, component->name)){
                model.circuit.components.push_back(SimulationComponent(
                    simId, GateKind::Input, {}, net, value, "SWITCH"));
            }
            continue;
        }

        //---- 常量源：POWER(+=1, -=0) / GND(GND=0) ----
        if(component->type == "POWER" || component->type == "GND"){
            bool connected = false;
            for(const SimPinRef& pin : component->pins){
                const auto it = model.binding.pinNet.find({id, pin.id});
                if(it == model.binding.pinNet.end()) continue;
                if(!connected){
                    model.binding.deviceNet[id] = it->second;   //状态显示看第一个引脚
                    connected = true;
                }
                const LogicValue value = (component->type == "POWER" && pin.id == "+")
                                             ? LogicValue::One : LogicValue::Zero;
                if(claim_driver(it->second, id, component->name)){
                    model.circuit.components.push_back(SimulationComponent(
                        simId + ":" + pin.id, GateKind::Input, {}, it->second, value, "CONST"));
                }
            }
            model.binding.role[id] = connected ? SimRole::Constant : SimRole::Unsupported;
            if(!connected){
                model.diagnostics.push_back(make_warning(
                    "SIMULATION_COMPONENT_UNCONNECTED",
                    "常量源没有接入任何网络：" + component->name, {simId}));
            }
            continue;
        }

        //---- 输出探针 / 输出灯 ----
        if(is_probe_type(component->type) || is_lamp_type(component->type)){
            bool ambiguous = false;
            const SimPinRef* pin = pick_connected_pin(*component, ambiguous);
            const auto it = (pin == nullptr) ? model.binding.pinNet.end()
                                             : model.binding.pinNet.find({id, pin->id});
            if(pin == nullptr || it == model.binding.pinNet.end()){
                model.diagnostics.push_back(make_warning(
                    "SIMULATION_COMPONENT_UNCONNECTED",
                    "输出元件没有接入任何网络：" + component->name, {simId}));
                model.binding.role[id] = SimRole::Unsupported;
                continue;
            }
            if(ambiguous){
                model.diagnostics.push_back(make_warning(
                    "SIMULATION_OUTPUT_AMBIGUOUS_PIN",
                    "输出元件两个引脚都接了线，只观察第一个引脚。", {simId}));
            }
            //输出端子只连到自己：显示 X，并在诊断里说明"没有接线"
            if(nets.terminals[it->second] <= 1){
                model.diagnostics.push_back(make_warning(
                    is_output_terminal(component->type)
                        ? "SIMULATION_TERMINAL_UNCONNECTED"
                        : "SIMULATION_COMPONENT_UNCONNECTED",
                    (is_output_terminal(component->type) ? "输出端子" : "输出元件") +
                        std::string("没有接线（只连到自己）：") + component->name, {simId}));
            }
            model.circuit.probes.push_back(SimulationProbe(simId, it->second));
            declare(it->second);
            model.binding.role[id] = is_lamp_type(component->type)
                                         ? SimRole::Lamp : SimRole::Probe;
            model.binding.deviceNet[id] = it->second;
            continue;
        }

        //---- 其它元件 ----
        model.binding.role[id] = SimRole::Unsupported;
        model.diagnostics.push_back(make_warning(
            "SIMULATION_COMPONENT_UNSUPPORTED",
            "该元件不参与仿真：" + component->name + "（" + component->type + "）", {simId}));
    }

    //---- 7) 声明网络（确定性顺序）----
    model.circuit.nets.assign(netsToDeclare.begin(), netsToDeclare.end());
    std::sort(model.circuit.nets.begin(), model.circuit.nets.end());
    return model;
}

} // namespace Simulation

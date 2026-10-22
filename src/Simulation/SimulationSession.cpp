#include"SimulationSession.hpp"

#include<memory>

#include"Simulator.hpp"

namespace Simulation{

SimulationSession::SimulationSession() = default;
SimulationSession::~SimulationSession() = default;

void SimulationSession::clear(){
    simulator_.reset();
    model_ = SimulationModel{};
    last_ = SimulationResult{};
    diagnostics_.clear();
    names_.clear();
    types_.clear();
    hasModel_ = false;
}

bool SimulationSession::rebuild(const SimProject& project){
    clear();
    //报告要用到的名字/类型（适配层不关心显示名）
    for(const SimComponent& component : project.components){
        names_[component.id] = component.name.empty() ? component.type : component.name;
        types_[component.id] = component.type;
    }
    model_ = build_simulation_model(project);
    diagnostics_ = model_.diagnostics;

    hasModel_ = model_.has_simulatable_content();
    if(!hasModel_) return false;

    //结构校验 + 求值器
    simulator_ = std::make_unique<Simulator>(model_.circuit);
    for(const SimulationDiagnostic& diagnostic : simulator_->diagnostics())
        diagnostics_.push_back(diagnostic);

    if(enabled_) evaluate();
    return true;
}

bool SimulationSession::evaluate(){
    if(!simulator_) return false;
    last_ = simulator_->evaluate();
    return last_.ok;
}

int SimulationSession::errorCount() const{
    int count = 0;
    for(const SimulationDiagnostic& diagnostic : diagnostics_)
        if(diagnostic.severity == "error") ++count;
    return count;
}

int SimulationSession::warningCount() const{
    int count = 0;
    for(const SimulationDiagnostic& diagnostic : diagnostics_)
        if(diagnostic.severity == "warning") ++count;
    return count;
}

int SimulationSession::netCount() const{
    return static_cast<int>(model_.circuit.nets.size());
}

int SimulationSession::componentCount() const{
    return static_cast<int>(model_.circuit.components.size());
}

bool SimulationSession::isSwitch(int componentId) const{
    const auto it = model_.binding.role.find(componentId);
    return it != model_.binding.role.end() && it->second == SimRole::InputSwitch;
}

bool SimulationSession::isLamp(int componentId) const{
    const auto it = model_.binding.role.find(componentId);
    return it != model_.binding.role.end() && it->second == SimRole::Lamp;
}

bool SimulationSession::isSupported(int componentId) const{
    const auto it = model_.binding.role.find(componentId);
    //角色表里没有 = 还没参与重建（例如刚放置的元件）：按支持处理，避免误画"不支持"标记
    if(it == model_.binding.role.end()) return true;
    return it->second != SimRole::Unsupported;
}

LogicValue SimulationSession::value_of_net(const std::string& net) const{
    return last_.net_value(net);
}

bool SimulationSession::net_has_value(const std::string& net) const{
    return last_.netValues.find(net) != last_.netValues.end();
}

bool SimulationSession::wireHasValue(int wireId) const{
    const auto it = model_.binding.wireNet.find(wireId);
    return it != model_.binding.wireNet.end() && net_has_value(it->second);
}

LogicValue SimulationSession::wireValue(int wireId) const{
    const auto it = model_.binding.wireNet.find(wireId);
    return (it == model_.binding.wireNet.end()) ? LogicValue::Unknown
                                                : value_of_net(it->second);
}

bool SimulationSession::pinHasValue(int componentId, const std::string& pinId) const{
    const auto it = model_.binding.pinNet.find({componentId, pinId});
    return it != model_.binding.pinNet.end() && net_has_value(it->second);
}

LogicValue SimulationSession::pinValue(int componentId, const std::string& pinId) const{
    const auto it = model_.binding.pinNet.find({componentId, pinId});
    return (it == model_.binding.pinNet.end()) ? LogicValue::Unknown
                                               : value_of_net(it->second);
}

bool SimulationSession::componentHasValue(int componentId) const{
    const auto it = model_.binding.deviceNet.find(componentId);
    return it != model_.binding.deviceNet.end() && net_has_value(it->second);
}

LogicValue SimulationSession::componentValue(int componentId) const{
    //输入端子/开关：徽标显示"当前开关状态 0/1"（真正传到网络上的电平看导线颜色）
    const auto role = model_.binding.role.find(componentId);
    if(role != model_.binding.role.end() && role->second == SimRole::InputSwitch){
        const auto state = model_.binding.switchValue.find(componentId);
        if(state != model_.binding.switchValue.end()) return state->second;
    }
    const auto it = model_.binding.deviceNet.find(componentId);
    return (it == model_.binding.deviceNet.end()) ? LogicValue::Unknown
                                                  : value_of_net(it->second);
}

//诊断报告：把"每个元件看到什么、每个网络是谁驱动的"摊开写清楚。
//用户报"门显示 0/线没变色"这类问题时，先看这里就能定位（悬空 vs 真 0 vs 冲突）。
std::string SimulationSession::report_text() const{
    std::string text;
    const auto append = [&text](const std::string& line){ text += line; text += "\n"; };
    const auto value_text = [](LogicValue value){ return std::string(to_string(value)); };
    const auto driver_note = [this](const std::string& net) -> std::string{
        const auto it = model_.binding.netDriver.find(net);
        if(it == model_.binding.netDriver.end()) return "　（无驱动：悬空/未接信号源）";
        return "　（驱动 #" + std::to_string(it->second) + "）";
    };

    append("TinyEDA 仿真诊断");
    append("------------------------------------------------------------");
    append(std::string("状态：") + (enabled_ ? "运行" : "已暂停") +
           "　网络 " + std::to_string(netCount()) +
           "　仿真元件 " + std::to_string(componentCount()) +
           "　错误 " + std::to_string(errorCount()) +
           "　警告 " + std::to_string(warningCount()));
    append("图例：1 = 亮绿　0 = 中灰　X = 蓝　深灰细线 = 未参与仿真");
    if(!hasModel_) append("（当前画布没有可仿真的元件）");
    append("");

    append("【元件】");
    if(model_.binding.role.empty()) append("  （无）");
    for(const auto& entry : model_.binding.role){
        const int id = entry.first;
        std::string head = "  #" + std::to_string(id);
        const auto nameIt = names_.find(id);
        if(nameIt != names_.end() && !nameIt->second.empty()) head += " " + nameIt->second;
        const auto typeIt = types_.find(id);
        if(typeIt != types_.end() && !typeIt->second.empty()) head += "（" + typeIt->second + "）";
        head += "　角色=";
        head += sim_role_name(entry.second);
        append(head);

        for(const auto& pin : model_.binding.pinNet){
            if(pin.first.first != id) continue;
            const std::string& net = pin.second;
            append("      引脚 " + pin.first.second + " → " + net + " = " +
                   value_text(value_of_net(net)) + driver_note(net));
        }
        const auto device = model_.binding.deviceNet.find(id);
        if(device != model_.binding.deviceNet.end()){
            append("      状态网络 " + device->second + " = " +
                   value_text(value_of_net(device->second)) + driver_note(device->second));
        }
    }

    append("");
    append("【网络】");
    if(model_.circuit.nets.empty()) append("  （无）");
    for(const std::string& net : model_.circuit.nets)
        append("  " + net + " = " + value_text(value_of_net(net)) + driver_note(net));

    append("");
    append("【诊断】");
    if(diagnostics_.empty()) append("  （无）");
    for(const SimulationDiagnostic& diagnostic : diagnostics_)
        append("  [" + diagnostic.severity + "] " + diagnostic.code + "  " +
               diagnostic.message);

    return text;
}

} // namespace Simulation

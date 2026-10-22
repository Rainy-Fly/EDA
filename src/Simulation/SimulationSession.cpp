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
    hasModel_ = false;
}

bool SimulationSession::rebuild(const SimProject& project){
    clear();
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
    const auto it = model_.binding.deviceNet.find(componentId);
    return (it == model_.binding.deviceNet.end()) ? LogicValue::Unknown
                                                  : value_of_net(it->second);
}

} // namespace Simulation

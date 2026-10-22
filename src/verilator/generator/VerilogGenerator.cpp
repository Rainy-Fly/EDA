#include "VerilogGenerator.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace Verilator {
namespace {

const Net* input_net_for_pin(const Module& module, const std::string& gateId,
                             const std::string& pinId) {
    for (const auto& net : module.nets) {
        for (const auto& sink : net.sinks) {
            if (sink.kind == TerminalKind::GateInput &&
                sink.ownerId == gateId && sink.pinId == pinId) {
                return &net;
            }
        }
    }
    return nullptr;
}

std::string net_reference(const Module& module, const std::string& netId) {
    const Net* net = find_net(module, netId);
    if (!net) return "/* 无效网络 */";
    if (net->isConstant) return logic_value_literal(net->constantValue);
    if (net->name.empty()) return "/* 未命名网络 */";
    return net->name;
}

std::string gate_expression(const Module& module, const LogicGate& gate) {
    std::vector<std::string> inputs;
    inputs.reserve(gate.inputs.size());
    for (const auto& pin : gate.inputs) {
        const Net* net = input_net_for_pin(module, gate.id, pin.id);
        inputs.push_back(net ? net_reference(module, net->id)
                             : "/* 未连接 */");
    }

    switch (gate.kind) {
        case GateKind::Not:
            return "~" + (inputs.empty() ? std::string("/* 未连接 */")
                                         : inputs.front());
        case GateKind::And:
            if (inputs.size() < 2) return "/* 输入不足 */";
            return "(" + inputs[0] + " & " + inputs[1] + ")";
        case GateKind::Or:
            if (inputs.size() < 2) return "/* 输入不足 */";
            return "(" + inputs[0] + " | " + inputs[1] + ")";
        case GateKind::Xor:
            if (inputs.size() < 2) return "/* 输入不足 */";
            return "(" + inputs[0] + " ^ " + inputs[1] + ")";
        case GateKind::Xnor:
            if (inputs.size() < 2) return "/* 输入不足 */";
            return "(" + inputs[0] + " ~^ " + inputs[1] + ")";
        case GateKind::Nand:
            if (inputs.size() < 2) return "/* 输入不足 */";
            return "~(" + inputs[0] + " & " + inputs[1] + ")";
        case GateKind::Nor:
            if (inputs.size() < 2) return "/* 输入不足 */";
            return "~(" + inputs[0] + " | " + inputs[1] + ")";
    }
    return "/* 无效门类型 */";
}

std::string driver_expression(const Module& module, const Driver& driver) {
    switch (driver.kind) {
        case DriverKind::Constant:
            return logic_value_literal(driver.constantValue);
        case DriverKind::Net:
            return net_reference(module, driver.ownerId);
        case DriverKind::GateOutput: {
            const LogicGate* gate = find_gate(module, driver.ownerId);
            return gate ? gate_expression(module, *gate)
                        : "/* 无效门 */";
        }
        case DriverKind::Port:
            return "/* 无效端口驱动 */";
    }
    return "/* 无效驱动 */";
}

} // namespace

std::string generate_verilog_source(const Module& module) {
    std::ostringstream out;

    out << "module " << module.name << " (\n";
    for (std::size_t i = 0; i < module.ports.size(); ++i) {
        const Port& port = module.ports[i];
        out << "    "
            << (port.direction == PortDirection::Input ? "input " : "output ")
            << port.name;
        if (i + 1 != module.ports.size()) out << ",";
        out << "\n";
    }
    out << ");\n";

    std::vector<const Signal*> wires;
    for (const auto& signal : module.signals) {
        if (signal.kind == SignalKind::Wire) wires.push_back(&signal);
    }

    if (!wires.empty()) {
        out << "\n";
        for (const Signal* wire : wires) {
            out << "    wire " << wire->name << ";\n";
        }
    }

    std::vector<std::string> assignments;
    for (const auto& signal : module.signals) {
        if (signal.kind == SignalKind::Input) continue;

        const Net* net = find_net(module, signal.id);
        if (!net || net->drivers.empty()) continue;

        const Driver& driver = net->drivers.front();
        if (driver.kind == DriverKind::Port) continue;

        std::string line = "assign " + net->name + " = " +
                           driver_expression(module, driver) + ";";
        assignments.push_back(std::move(line));
    }

    if (!assignments.empty()) {
        out << "\n";
        for (const auto& line : assignments) {
            out << "    " << line << "\n";
        }
    }

    out << "\nendmodule\n";
    return out.str();
}

} // namespace Verilator

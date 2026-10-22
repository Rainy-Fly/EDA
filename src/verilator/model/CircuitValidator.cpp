#include "CircuitValidator.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Verilator {
namespace {

struct NetRef {
    std::string netId;
    SourceLocation location;
};

enum class DriveState {
    Driven,
    Undriven,
    Cycle,
    Visiting
};

const Pin* find_pin(const LogicGate& gate, const std::string& pinId,
                    PinDirection direction) {
    if (gate.output.id == pinId && gate.output.direction == direction) {
        return &gate.output;
    }
    for (const auto& pin : gate.inputs) {
        if (pin.id == pinId && pin.direction == direction) return &pin;
    }
    return nullptr;
}

} // namespace

std::vector<Diagnostic> validate_module(const Module& module) {
    std::vector<Diagnostic> diagnostics;
    const auto add = [&diagnostics](const std::string& code,
                                    const std::string& message,
                                    SourceLocation location) {
        diagnostics.push_back(
            make_error(DiagnosticKind::Model, code, message, location));
    };

    if (!is_valid_verilog_identifier(module.name)) {
        add("CIRCUIT_MODULE_NAME_INVALID",
            "模块名不是合法的 Verilog 标识符。", module.location);
    } else if (is_reserved_verilog_identifier(module.name)) {
        add("CIRCUIT_MODULE_NAME_RESERVED",
            "模块名 '" + module.name +
                "' 是 Verilog 保留字，不能作为普通标识符。",
            module.location);
    }

    // ---- 信号 ----
    std::unordered_map<std::string, std::size_t> signalByName;
    std::unordered_map<std::string, std::size_t> signalById;
    std::unordered_set<std::string> signalIds;
    for (std::size_t i = 0; i < module.signals.size(); ++i) {
        const Signal& signal = module.signals[i];
        if (signal.id.empty()) {
            add("CIRCUIT_SIGNAL_ID_INVALID", "信号 id 不能为空。",
                signal.location);
        } else if (!signalIds.insert(signal.id).second) {
            add("CIRCUIT_SIGNAL_ID_DUPLICATE",
                "信号 id '" + signal.id + "' 重复。", signal.location);
        }
        signalById.emplace(signal.id, i);
        if (!signalByName.emplace(signal.name, i).second) {
            add("CIRCUIT_SIGNAL_NAME_DUPLICATE",
                "信号名 '" + signal.name + "' 重复。", signal.location);
        }
        if (!is_valid_verilog_identifier(signal.name)) {
            add("CIRCUIT_SIGNAL_NAME_INVALID",
                "信号名 '" + signal.name + "' 不是合法的 Verilog 标识符。",
                signal.location);
        } else if (is_reserved_verilog_identifier(signal.name)) {
            add("CIRCUIT_SIGNAL_NAME_RESERVED",
                "信号名 '" + signal.name +
                    "' 是 Verilog 保留字，不能作为普通标识符。",
                signal.location);
        }
    }

    // ---- 端口 ----
    std::unordered_map<std::string, std::size_t> portByName;
    std::unordered_set<std::string> portNames;
    for (std::size_t i = 0; i < module.ports.size(); ++i) {
        const Port& port = module.ports[i];
        if (port.name.empty()) {
            add("CIRCUIT_PORT_NAME_INVALID", "端口名不能为空。", port.location);
        } else if (!portNames.insert(port.name).second) {
            add("CIRCUIT_PORT_NAME_DUPLICATE",
                "端口名 '" + port.name + "' 重复。", port.location);
        } else if (!is_valid_verilog_identifier(port.name)) {
            add("CIRCUIT_PORT_NAME_INVALID",
                "端口名 '" + port.name + "' 不是合法的 Verilog 标识符。",
                port.location);
        } else if (is_reserved_verilog_identifier(port.name)) {
            add("CIRCUIT_PORT_NAME_RESERVED",
                "端口名 '" + port.name +
                    "' 是 Verilog 保留字，不能作为普通标识符。",
                port.location);
        }
        portByName.emplace(port.name, i);

        const auto signal = signalByName.find(port.name);
        if (signal == signalByName.end()) {
            add("CIRCUIT_PORT_SIGNAL_MISSING",
                "端口 '" + port.name + "' 没有对应的信号。", port.location);
            continue;
        }
        const SignalKind expected = port.direction == PortDirection::Input
                                        ? SignalKind::Input
                                        : SignalKind::Output;
        if (module.signals[signal->second].kind != expected) {
            add("CIRCUIT_PORT_SIGNAL_KIND",
                "端口 '" + port.name + "' 的方向与信号类型不一致。",
                port.location);
        }
    }

    for (const auto& signal : module.signals) {
        if ((signal.kind == SignalKind::Input ||
             signal.kind == SignalKind::Output) &&
            portByName.find(signal.name) == portByName.end()) {
            add("CIRCUIT_SIGNAL_PORT_MISSING",
                "信号 '" + signal.name +
                    "' 被声明为输入/输出，但没有对应的端口。",
                signal.location);
        }
    }

    // ---- 网络 ----
    std::unordered_map<std::string, std::size_t> netById;
    std::unordered_set<std::string> netIds;
    for (std::size_t i = 0; i < module.nets.size(); ++i) {
        const Net& net = module.nets[i];
        if (net.id.empty()) {
            add("CIRCUIT_NET_ID_INVALID", "网络 id 不能为空。", net.location);
        } else if (!netIds.insert(net.id).second) {
            add("CIRCUIT_NET_ID_DUPLICATE",
                "网络 id '" + net.id + "' 重复。", net.location);
        }
        netById.emplace(net.id, i);

        if (net.isConstant) {
            const bool nameIsZero = net.name == "1'b0";
            const bool nameIsOne = net.name == "1'b1";
            if (!nameIsZero && !nameIsOne) {
                add("CIRCUIT_CONSTANT_NET_NAME_INVALID",
                    "常量网络 '" + net.id + "' 的名称必须是 1'b0 或 1'b1。",
                    net.location);
            } else {
                const LogicValue expected =
                    nameIsOne ? LogicValue::One : LogicValue::Zero;
                if (net.constantValue != expected) {
                    add("CIRCUIT_CONSTANT_NET_VALUE_MISMATCH",
                        "常量网络 '" + net.id + "' 的名称为 '" + net.name +
                            "'，但常量值与名称不一致。",
                        net.location);
                }
            }

            if (net.drivers.size() != 1 ||
                net.drivers.front().kind != DriverKind::Constant) {
                add("CIRCUIT_CONSTANT_NET_INVALID",
                    "常量网络 '" + net.id +
                        "' 必须有且只有一个常量驱动源。",
                    net.location);
            } else if (net.drivers.front().constantValue !=
                       net.constantValue) {
                add("CIRCUIT_CONSTANT_NET_DRIVER_MISMATCH",
                    "常量网络 '" + net.id +
                        "' 的唯一常量驱动值与网络常量值不一致。",
                    net.drivers.front().location);
            }

            if (signalById.find(net.id) != signalById.end()) {
                add("CIRCUIT_CONSTANT_NET_SIGNAL_CONFLICT",
                    "常量网络 id '" + net.id +
                        "' 与普通信号 id 冲突。",
                    net.location);
            }
        } else if (!net.name.empty() &&
                   !is_valid_verilog_identifier(net.name)) {
            add("CIRCUIT_NET_NAME_INVALID",
                "网络名 '" + net.name + "' 不是合法的 Verilog 标识符。",
                net.location);
        }
    }

    for (const auto& signal : module.signals) {
        if (netById.find(signal.id) == netById.end()) {
            add("CIRCUIT_SIGNAL_NET_MISSING",
                "信号 '" + signal.name + "' 没有对应的网络。",
                signal.location);
        }
    }

    // 第一阶段中，除常量网络外，每个网络都必须对应一个命名信号；
    // 这样生成器声明的 wire 与 assign 左侧才能保持一致。
    for (const auto& net : module.nets) {
        if (net.isConstant) continue;
        const auto signal = signalById.find(net.id);
        if (signal == signalById.end()) {
            add("CIRCUIT_NET_SIGNAL_MISSING",
                "网络 '" + net.name + "' 没有对应的命名信号。", net.location);
        } else if (module.signals[signal->second].name != net.name) {
            add("CIRCUIT_NET_SIGNAL_NAME_MISMATCH",
                "网络名 '" + net.name + "' 与信号名 '" +
                    module.signals[signal->second].name + "' 不一致。",
                net.location);
        }
    }

    // ---- 门与引脚 ----
    std::unordered_map<std::string, std::size_t> gateById;
    std::unordered_set<std::string> gateIds;
    for (std::size_t i = 0; i < module.gates.size(); ++i) {
        const LogicGate& gate = module.gates[i];
        if (gate.id.empty()) {
            add("CIRCUIT_GATE_ID_INVALID", "门实例 id 不能为空。",
                SourceLocation{});
        } else if (!gateIds.insert(gate.id).second) {
            add("CIRCUIT_GATE_ID_DUPLICATE",
                "门实例 id '" + gate.id + "' 重复。", SourceLocation{});
        }
        gateById.emplace(gate.id, i);

        if (gate.inputs.size() != gate_input_arity(gate.kind)) {
            add("CIRCUIT_GATE_ARITY",
                "门 '" + gate.id + "'（" + gate_kind_name(gate.kind) +
                    "）的输入引脚数量不符合要求。",
                SourceLocation{});
        }

        std::unordered_set<std::string> pinIds;
        for (const auto& pin : gate.inputs) {
            if (pin.id.empty() || !pinIds.insert(pin.id).second) {
                add("CIRCUIT_PIN_INVALID",
                    "门 '" + gate.id + "' 存在空或重复的输入引脚 id。",
                    SourceLocation{});
            }
            if (pin.direction != PinDirection::Input) {
                add("CIRCUIT_PIN_DIRECTION_INVALID",
                    "门 '" + gate.id + "' 的输入引脚方向不是 input。",
                    SourceLocation{});
            }
        }
        if (gate.output.id.empty() ||
            !pinIds.insert(gate.output.id).second) {
            add("CIRCUIT_PIN_INVALID",
                "门 '" + gate.id + "' 存在空或重复的输出引脚 id。",
                SourceLocation{});
        }
        if (gate.output.direction != PinDirection::Output) {
            add("CIRCUIT_PIN_DIRECTION_INVALID",
                "门 '" + gate.id + "' 的输出引脚方向不是 output。",
                SourceLocation{});
        }
    }

    // ---- 别名链的有效驱动 ----
    // 直连驱动（DriverKind::Net）可能形成多级链。这里沿链回溯，只有最终落到
    // 输入端口、常量或合法门输出才算有效驱动；遇到环则交给后续循环检测报告。
    std::unordered_map<std::string, DriveState> driveState;
    std::function<DriveState(const std::string&)> resolve_drive =
        [&](const std::string& netId) -> DriveState {
        const auto known = driveState.find(netId);
        if (known != driveState.end()) {
            return known->second == DriveState::Visiting
                       ? DriveState::Cycle
                       : known->second;
        }

        const auto index = netById.find(netId);
        if (index == netById.end()) return DriveState::Undriven;

        driveState[netId] = DriveState::Visiting;
        bool hasValidDriver = false;
        bool hasCycle = false;
        const Net& net = module.nets[index->second];
        for (const auto& driver : net.drivers) {
            switch (driver.kind) {
                case DriverKind::Port: {
                    const auto port = portByName.find(driver.ownerId);
                    if (port != portByName.end() &&
                        module.ports[port->second].direction ==
                            PortDirection::Input) {
                        hasValidDriver = true;
                    }
                    break;
                }
                case DriverKind::Constant:
                    hasValidDriver = true;
                    break;
                case DriverKind::GateOutput: {
                    const auto gate = gateById.find(driver.ownerId);
                    if (gate != gateById.end() &&
                        find_pin(module.gates[gate->second], driver.pinId,
                                 PinDirection::Output)) {
                        hasValidDriver = true;
                    }
                    break;
                }
                case DriverKind::Net: {
                    const DriveState upstream = resolve_drive(driver.ownerId);
                    if (upstream == DriveState::Driven) {
                        hasValidDriver = true;
                    } else if (upstream == DriveState::Cycle) {
                        hasCycle = true;
                    }
                    break;
                }
            }
        }

        const DriveState result =
            hasValidDriver
                ? DriveState::Driven
                : (hasCycle ? DriveState::Cycle : DriveState::Undriven);
        driveState[netId] = result;
        return result;
    };

    // ---- 显式连接 ----
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::vector<NetRef>>>
        gateInputSinks;
    std::unordered_map<std::string, std::vector<std::string>> gateOutputNets;
    std::unordered_map<std::string, std::vector<std::string>> portSinkNets;
    std::unordered_map<std::string, std::vector<std::string>> portDriverNets;

    for (const auto& net : module.nets) {
        if (net.drivers.size() > 1) {
            add("CIRCUIT_MULTIPLE_DRIVER",
                "网络 '" + net.name + "' 存在多个驱动源。", net.location);
        }

        for (const auto& driver : net.drivers) {
            switch (driver.kind) {
                case DriverKind::GateOutput: {
                    const auto gate = gateById.find(driver.ownerId);
                    if (gate == gateById.end()) {
                        add("CIRCUIT_DRIVER_GATE_UNKNOWN",
                            "网络 '" + net.name + "' 的门输出驱动引用了不存在的门 '" +
                                driver.ownerId + "'。",
                            driver.location);
                        break;
                    }
                    const LogicGate& sourceGate =
                        module.gates[gate->second];
                    if (!find_pin(sourceGate, driver.pinId,
                                  PinDirection::Output)) {
                        add("CIRCUIT_DRIVER_PIN_INVALID",
                            "网络 '" + net.name +
                                "' 的门输出驱动引用了无效引脚 '" +
                                driver.pinId + "'。",
                            driver.location);
                    }
                    gateOutputNets[driver.ownerId].push_back(net.id);
                    break;
                }
                case DriverKind::Net: {
                    const auto source = netById.find(driver.ownerId);
                    if (source == netById.end()) {
                        add("CIRCUIT_DRIVER_NET_UNKNOWN",
                            "网络 '" + net.name + "' 的直连驱动引用了不存在的网络 '" +
                                driver.ownerId + "'。",
                            driver.location);
                        break;
                    }

                    if (resolve_drive(driver.ownerId) ==
                        DriveState::Undriven) {
                        add("CIRCUIT_DRIVER_NET_UNRIVEN",
                            "网络 '" + net.name + "' 的直连驱动源 '" +
                                module.nets[source->second].name +
                                "' 没有有效驱动源。",
                            driver.location);
                    }
                    break;
                }
                case DriverKind::Port: {
                    const auto port = portByName.find(driver.ownerId);
                    if (port == portByName.end()) {
                        add("CIRCUIT_DRIVER_PORT_UNKNOWN",
                            "网络 '" + net.name +
                                "' 的驱动引用了不存在的端口 '" +
                                driver.ownerId + "'。",
                            driver.location);
                    } else if (module.ports[port->second].direction !=
                               PortDirection::Input) {
                        add("CIRCUIT_DRIVER_PORT_INVALID",
                            "网络 '" + net.name +
                                "' 只能由输入端口驱动，不能由输出端口 '" +
                                driver.ownerId + "' 驱动。",
                            driver.location);
                    } else {
                        portDriverNets[driver.ownerId].push_back(net.id);
                    }
                    break;
                }
                case DriverKind::Constant:
                    break;
            }
        }

        for (const auto& sink : net.sinks) {
            switch (sink.kind) {
                case TerminalKind::GateInput: {
                    const auto gate = gateById.find(sink.ownerId);
                    if (gate == gateById.end()) {
                        add("CIRCUIT_SINK_GATE_UNKNOWN",
                            "网络 '" + net.name +
                                "' 的接收端引用了不存在的门 '" +
                                sink.ownerId + "'。",
                            sink.location);
                        break;
                    }
                    const LogicGate& targetGate =
                        module.gates[gate->second];
                    if (!find_pin(targetGate, sink.pinId,
                                  PinDirection::Input)) {
                        add("CIRCUIT_SINK_PIN_INVALID",
                            "网络 '" + net.name +
                                "' 的接收端引用了无效输入引脚 '" +
                                sink.pinId + "'。",
                            sink.location);
                        break;
                    }
                    gateInputSinks[sink.ownerId][sink.pinId].push_back(
                        NetRef{net.id, sink.location});
                    break;
                }
                case TerminalKind::Port: {
                    const auto port = portByName.find(sink.ownerId);
                    if (port == portByName.end()) {
                        add("CIRCUIT_SINK_PORT_UNKNOWN",
                            "网络 '" + net.name +
                                "' 的接收端引用了不存在的端口 '" +
                                sink.ownerId + "'。",
                            sink.location);
                    } else if (module.ports[port->second].direction !=
                               PortDirection::Output) {
                        add("CIRCUIT_SINK_PORT_INVALID",
                            "网络 '" + net.name +
                                "' 不能作为输入端口 '" + sink.ownerId +
                                "' 的接收端。",
                            sink.location);
                    } else {
                        portSinkNets[sink.ownerId].push_back(net.id);
                    }
                    break;
                }
            }
        }
    }

    // ---- 门引脚连接 ----
    for (const auto& gate : module.gates) {
        for (const auto& pin : gate.inputs) {
            const auto byPin = gateInputSinks.find(gate.id);
            const std::size_t count =
                byPin == gateInputSinks.end()
                    ? 0
                    : byPin->second.count(pin.id) != 0
                          ? byPin->second.at(pin.id).size()
                          : 0;
            if (count == 0) {
                add("CIRCUIT_GATE_INPUT_UNCONNECTED",
                    "门 '" + gate.id + "' 的输入引脚 '" + pin.id +
                        "' 没有连接到任何网络。",
                    SourceLocation{});
            } else if (count > 1) {
                add("CIRCUIT_GATE_INPUT_MULTIPLE",
                    "门 '" + gate.id + "' 的输入引脚 '" + pin.id +
                        "' 连接了多个网络。",
                    SourceLocation{});
            }
        }

        const auto outputs = gateOutputNets.find(gate.id);
        const std::size_t outputCount =
            outputs == gateOutputNets.end() ? 0 : outputs->second.size();
        if (outputCount == 0) {
            add("CIRCUIT_GATE_OUTPUT_UNCONNECTED",
                "门 '" + gate.id + "' 的输出引脚没有连接到任何网络。",
                SourceLocation{});
        } else if (outputCount > 1) {
            add("CIRCUIT_GATE_OUTPUT_MULTIPLE",
                "门 '" + gate.id + "' 的输出引脚连接了多个网络。",
                SourceLocation{});
        }
    }

    // ---- 端口连接 ----
    for (const auto& port : module.ports) {
        const auto signal = signalByName.find(port.name);
        if (signal == signalByName.end()) continue;
        const Signal& portSignal = module.signals[signal->second];
        const auto net = netById.find(portSignal.id);
        if (net == netById.end()) continue;

        if (port.direction == PortDirection::Input) {
            const auto drivers = portDriverNets.find(port.name);
            if (drivers == portDriverNets.end() || drivers->second.empty()) {
                add("CIRCUIT_INPUT_UNRIVEN",
                    "输入端口 '" + port.name + "' 没有外部驱动源。",
                    port.location);
            } else {
                for (const auto& netId : drivers->second) {
                    if (netId != portSignal.id) {
                        add("CIRCUIT_PORT_CONNECTION_MISMATCH",
                            "输入端口 '" + port.name +
                                "' 的驱动连接到了错误的网络。",
                            port.location);
                    }
                }
            }
        } else {
            const auto sinks = portSinkNets.find(port.name);
            if (sinks == portSinkNets.end() || sinks->second.empty()) {
                add("CIRCUIT_OUTPUT_PORT_UNCONNECTED",
                    "输出端口 '" + port.name + "' 没有连接到网络。",
                    port.location);
            } else {
                if (sinks->second.size() > 1) {
                    add("CIRCUIT_PORT_MULTIPLE_CONNECTIONS",
                        "输出端口 '" + port.name + "' 连接了多个网络。",
                        port.location);
                }
                for (const auto& netId : sinks->second) {
                    if (netId != portSignal.id) {
                        add("CIRCUIT_PORT_CONNECTION_MISMATCH",
                            "输出端口 '" + port.name +
                                "' 的连接指向了错误的网络。",
                            port.location);
                    }
                }
            }

            const Net& outputNet = module.nets[net->second];
            if (outputNet.drivers.empty()) {
                add("CIRCUIT_OUTPUT_UNRIVEN",
                    "输出端口 '" + port.name + "' 没有合法驱动源。",
                    port.location);
            } else if (outputNet.drivers.size() == 1 &&
                       outputNet.drivers.front().kind == DriverKind::Port) {
                add("CIRCUIT_OUTPUT_INVALID_DRIVER",
                    "输出端口 '" + port.name + "' 不能由输入端口驱动。",
                    port.location);
            }
        }
    }

    // ---- 门输入必须有驱动源 ----
    std::unordered_map<std::string, SignalKind> netSignalKind;
    for (const auto& signal : module.signals) {
        netSignalKind[signal.id] = signal.kind;
    }

    std::unordered_set<std::string> reportedUnriven;
    for (const auto& net : module.nets) {
        bool usedAsGateInput = false;
        for (const auto& sink : net.sinks) {
            if (sink.kind == TerminalKind::GateInput) {
                usedAsGateInput = true;
                break;
            }
        }
        if (!usedAsGateInput || net.isConstant || !net.drivers.empty()) continue;

        const auto kind = netSignalKind.find(net.id);
        const bool isPortNet = kind != netSignalKind.end() &&
                               (kind->second == SignalKind::Input ||
                                kind->second == SignalKind::Output);
        if (isPortNet) continue;

        if (reportedUnriven.insert(net.id).second) {
            add("CIRCUIT_SOURCE_UNRIVEN",
                "内部网络 '" + net.name + "' 被门引用但没有驱动源。",
                net.location);
        }
    }

    // ---- 组合逻辑循环依赖 ----
    std::unordered_map<std::string, std::vector<std::string>> dependencies;
    for (const auto& net : module.nets) {
        for (const auto& driver : net.drivers) {
            if (driver.kind == DriverKind::Net) {
                if (netById.find(driver.ownerId) != netById.end()) {
                    dependencies[driver.ownerId].push_back(net.id);
                }
            } else if (driver.kind == DriverKind::GateOutput) {
                const auto gate = gateById.find(driver.ownerId);
                if (gate == gateById.end()) continue;
                const LogicGate& sourceGate = module.gates[gate->second];
                for (const auto& pin : sourceGate.inputs) {
                    const auto byPin = gateInputSinks.find(sourceGate.id);
                    if (byPin == gateInputSinks.end()) continue;
                    const auto refs = byPin->second.find(pin.id);
                    if (refs == byPin->second.end()) continue;
                    for (const auto& ref : refs->second) {
                        dependencies[ref.netId].push_back(net.id);
                    }
                }
            }
        }
    }

    std::unordered_map<std::string, int> color;
    bool cycleReported = false;
    std::function<void(const std::string&)> visit = [&](const std::string& id) {
        if (cycleReported) return;
        int& state = color[id];
        if (state == 1) {
            const auto net = netById.find(id);
            const Net* target = net == netById.end()
                                    ? nullptr
                                    : &module.nets[net->second];
            add("CIRCUIT_CYCLIC_DEPENDENCY",
                "信号 '" + (target ? target->name : id) +
                    "' 参与了组合逻辑循环依赖。",
                target ? target->location : SourceLocation{});
            cycleReported = true;
            return;
        }
        if (state == 2) return;

        state = 1;
        const auto children = dependencies.find(id);
        if (children != dependencies.end()) {
            for (const auto& child : children->second) {
                if (cycleReported) break;
                visit(child);
            }
        }
        state = 2;
    };

    for (const auto& entry : netById) {
        if (cycleReported) break;
        visit(entry.first);
    }

    return diagnostics;
}

} // namespace Verilator

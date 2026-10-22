#include "CircuitBuilder.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Verilator {
namespace {

class Builder {
public:
    explicit Builder(const Ast::Module& ast)
        : ast_(ast) {}

    BuildResult build() {
        result_.module.name = ast_.name;
        result_.module.location = ast_.location;

        declare_ports();
        declare_body_signals();
        finalize_undirected_ports();
        create_signal_nets();
        process_assignments();

        return std::move(result_);
    }

private:
    struct SourceRef {
        std::string netId;
        bool valid = false;
    };

    void declare_ports() {
        for (const auto& declaration : ast_.ports) {
            if (portIndex_.count(declaration.name) != 0) {
                add_error("VERILOG_DUPLICATE_PORT",
                          "端口 '" + declaration.name + "' 重复声明。",
                          declaration.location);
                continue;
            }

            Port port;
            port.name = declaration.name;
            port.location = declaration.location;
            port.direction = declaration.hasDirection ? declaration.direction
                                                      : PortDirection::Input;
            portIndex_[declaration.name] = result_.module.ports.size();
            result_.module.ports.push_back(std::move(port));

            if (declaration.hasDirection) {
                directionKnown_[declaration.name] = true;
                const SignalKind kind =
                    declaration.direction == PortDirection::Input
                        ? SignalKind::Input
                        : SignalKind::Output;
                declare_signal(declaration.name, kind, declaration.location);
            } else {
                directionKnown_[declaration.name] = false;
            }
        }
    }

    void declare_body_signals() {
        for (const auto& declaration : ast_.declarations) {
            if (declaration.kind == SignalKind::Wire) {
                declare_signal(declaration.name, SignalKind::Wire,
                               declaration.location);
                continue;
            }

            const auto port = portIndex_.find(declaration.name);
            if (port == portIndex_.end()) {
                add_error("VERILOG_PORT_NOT_IN_LIST",
                          "信号 '" + declaration.name +
                              "' 被声明为端口，但没有出现在模块端口列表中。",
                          declaration.location);
                continue;
            }

            if (directionKnown_[declaration.name]) {
                add_error("VERILOG_DUPLICATE_DECLARATION",
                          "信号 '" + declaration.name + "' 重复声明。",
                          declaration.location);
                continue;
            }

            directionKnown_[declaration.name] = true;
            result_.module.ports[port->second].direction =
                declaration.kind == SignalKind::Input ? PortDirection::Input
                                                      : PortDirection::Output;
            const SignalKind kind = declaration.kind == SignalKind::Input
                                        ? SignalKind::Input
                                        : SignalKind::Output;
            declare_signal(declaration.name, kind, declaration.location);
        }
    }

    void finalize_undirected_ports() {
        for (const auto& port : result_.module.ports) {
            const auto known = directionKnown_.find(port.name);
            if (known == directionKnown_.end() || !known->second) {
                add_error("VERILOG_PORT_DIRECTION_MISSING",
                          "端口 '" + port.name +
                              "' 没有 input/output 方向声明。",
                          port.location);
            }
        }
    }

    bool declare_signal(const std::string& name, SignalKind kind,
                        SourceLocation location) {
        if (signalIndex_.count(name) != 0) {
            add_error("VERILOG_DUPLICATE_DECLARATION",
                      "信号 '" + name + "' 重复声明。", location);
            return false;
        }

        Signal signal;
        signal.id = name;
        signal.name = name;
        signal.kind = kind;
        signal.location = location;
        signalIndex_[name] = result_.module.signals.size();
        result_.module.signals.push_back(std::move(signal));
        return true;
    }

    void create_signal_nets() {
        for (const auto& signal : result_.module.signals) {
            Net net;
            net.id = signal.id;
            net.name = signal.name;
            net.location = signal.location;

            if (signal.kind == SignalKind::Input) {
                // 输入端口由外部驱动。
                Driver driver;
                driver.kind = DriverKind::Port;
                driver.ownerId = signal.name;
                driver.pinId = "in";
                driver.location = signal.location;
                net.drivers.push_back(std::move(driver));
            } else if (signal.kind == SignalKind::Output) {
                // 输出端口是网络的一个接收端。
                Terminal sink;
                sink.kind = TerminalKind::Port;
                sink.ownerId = signal.name;
                sink.pinId = "out";
                sink.location = signal.location;
                net.sinks.push_back(std::move(sink));
            }

            netIndex_[net.id] = result_.module.nets.size();
            result_.module.nets.push_back(std::move(net));
        }
    }

    void process_assignments() {
        for (const auto& assignment : ast_.assignments) {
            const auto signal = signalIndex_.find(assignment.target);
            if (signal == signalIndex_.end()) {
                add_error("VERILOG_UNDECLARED_SIGNAL",
                          "assign 目标信号 '" + assignment.target +
                              "' 未声明。",
                          assignment.targetLocation);
                continue;
            }

            const Signal& targetSignal =
                result_.module.signals[signal->second];
            if (targetSignal.kind == SignalKind::Input) {
                add_error("VERILOG_ASSIGN_TO_INPUT",
                          "输入端口 '" + assignment.target +
                              "' 不能被 assign 驱动。",
                          assignment.targetLocation);
                continue;
            }

            const auto target = netIndex_.find(targetSignal.id);
            if (target == netIndex_.end()) continue;
            if (!result_.module.nets[target->second].drivers.empty()) {
                add_error("VERILOG_MULTIPLE_DRIVER",
                          "信号 '" + assignment.target +
                              "' 被多条 assign 或端口重复驱动。",
                          assignment.targetLocation);
                continue;
            }

            if (!assignment.expression) continue;
            // 复制目标网络 id：构建子表达式时可能向 signals/nets 追加中间网络，
            // 不能让 targetNetId 继续引用可能失效的 vector 元素。
            const std::string targetNetId = targetSignal.id;
            SourceRef source;
            build_expression(*assignment.expression, targetNetId, true, source);
        }
    }

    bool build_expression(const Ast::Expression& expression,
                          const std::string& targetNetId, bool topLevel,
                          SourceRef& source) {
        switch (expression.kind) {
            case Ast::ExpressionKind::Identifier:
                return build_identifier(expression, targetNetId, topLevel,
                                        source);
            case Ast::ExpressionKind::Constant:
                return build_constant(expression, targetNetId, topLevel,
                                      source);
            case Ast::ExpressionKind::Not:
                return build_not(expression, targetNetId, topLevel, source);
            case Ast::ExpressionKind::Binary:
                return build_binary(expression, targetNetId, topLevel, source);
        }
        return false;
    }

    bool build_identifier(const Ast::Expression& expression,
                          const std::string& targetNetId, bool topLevel,
                          SourceRef& source) {
        const auto signal = signalIndex_.find(expression.text);
        if (signal == signalIndex_.end()) {
            add_error("VERILOG_UNDECLARED_SIGNAL",
                      "表达式引用了未声明的信号 '" + expression.text + "'。",
                      expression.location);
            return false;
        }

        source.netId = result_.module.signals[signal->second].id;
        source.valid = true;
        if (topLevel) {
            Driver driver;
            driver.kind = DriverKind::Net;
            driver.ownerId = source.netId;
            driver.location = expression.location;
            add_driver(targetNetId, std::move(driver));
        }
        return true;
    }

    bool build_constant(const Ast::Expression& expression,
                        const std::string& targetNetId, bool topLevel,
                        SourceRef& source) {
        const LogicValue value =
            expression.text == "1" ? LogicValue::One : LogicValue::Zero;

        source.valid = true;
        if (topLevel) {
            Driver driver;
            driver.kind = DriverKind::Constant;
            driver.constantValue = value;
            driver.location = expression.location;
            add_driver(targetNetId, std::move(driver));
        } else {
            source.netId = constant_net(value, expression.location);
        }
        return true;
    }

    bool build_not(const Ast::Expression& expression,
                   const std::string& targetNetId, bool topLevel,
                   SourceRef& source) {
        if (!expression.left) return false;
        SourceRef operand;
        if (!build_expression(*expression.left, std::string(), false, operand)) {
            return false;
        }

        const std::string outputNet =
            topLevel ? targetNetId : create_intermediate_net(expression.location);
        create_gate(GateKind::Not, {operand.netId}, outputNet,
                    expression.location);
        source.netId = outputNet;
        source.valid = true;
        return true;
    }

    bool build_binary(const Ast::Expression& expression,
                      const std::string& targetNetId, bool topLevel,
                      SourceRef& source) {
        if (!expression.left || !expression.right) return false;

        SourceRef left;
        SourceRef right;
        if (!build_expression(*expression.left, std::string(), false, left)) {
            return false;
        }
        if (!build_expression(*expression.right, std::string(), false, right)) {
            return false;
        }

        const std::string outputNet =
            topLevel ? targetNetId : create_intermediate_net(expression.location);
        create_gate(binary_gate_kind(expression.binaryOperator),
                    {left.netId, right.netId}, outputNet, expression.location);
        source.netId = outputNet;
        source.valid = true;
        return true;
    }

    GateKind binary_gate_kind(Ast::BinaryOperator op) const {
        switch (op) {
            case Ast::BinaryOperator::And:  return GateKind::And;
            case Ast::BinaryOperator::Or:   return GateKind::Or;
            case Ast::BinaryOperator::Xor:  return GateKind::Xor;
            case Ast::BinaryOperator::Xnor: return GateKind::Xnor;
        }
        return GateKind::And;
    }

    std::string constant_net(LogicValue value, SourceLocation location) {
        const bool isOne = value == LogicValue::One;
        if (isOne && !constantOneNet_.empty()) return constantOneNet_;
        if (!isOne && !constantZeroNet_.empty()) return constantZeroNet_;

        const std::string id = isOne ? "$const1" : "$const0";
        Net net;
        net.id = id;
        net.name = isOne ? "1'b1" : "1'b0";
        net.isConstant = true;
        net.constantValue = value;
        net.location = location;

        Driver driver;
        driver.kind = DriverKind::Constant;
        driver.constantValue = value;
        driver.location = location;
        net.drivers.push_back(std::move(driver));

        netIndex_[id] = result_.module.nets.size();
        result_.module.nets.push_back(std::move(net));
        if (isOne) {
            constantOneNet_ = id;
        } else {
            constantZeroNet_ = id;
        }
        return id;
    }

    std::string create_intermediate_net(SourceLocation location) {
        std::string name;
        do {
            name = "__verilator_net_" + std::to_string(intermediateCounter_++);
        } while (signalIndex_.count(name) != 0 || netIndex_.count(name) != 0);

        Signal signal;
        signal.id = name;
        signal.name = name;
        signal.kind = SignalKind::Wire;
        signal.location = location;
        signalIndex_[name] = result_.module.signals.size();
        result_.module.signals.push_back(std::move(signal));

        Net net;
        net.id = name;
        net.name = name;
        net.location = location;
        netIndex_[name] = result_.module.nets.size();
        result_.module.nets.push_back(std::move(net));
        return name;
    }

    void create_gate(GateKind kind, const std::vector<std::string>& inputNets,
                     const std::string& outputNetId, SourceLocation location) {
        LogicGate gate;
        gate.id = next_gate_id();
        gate.kind = kind;

        const std::size_t arity = gate_input_arity(kind);
        for (std::size_t i = 0; i < arity; ++i) {
            Pin pin;
            pin.id = "in" + std::to_string(i);
            pin.name = pin.id;
            pin.direction = PinDirection::Input;
            gate.inputs.push_back(std::move(pin));
        }

        gate.output.id = "out";
        gate.output.name = "out";
        gate.output.direction = PinDirection::Output;
        result_.module.gates.push_back(std::move(gate));

        const LogicGate& stored = result_.module.gates.back();
        for (std::size_t i = 0; i < arity && i < inputNets.size(); ++i) {
            Net* source = find_net(result_.module, inputNets[i]);
            if (!source) continue;
            Terminal sink;
            sink.kind = TerminalKind::GateInput;
            sink.ownerId = stored.id;
            sink.pinId = stored.inputs[i].id;
            sink.location = location;
            source->sinks.push_back(std::move(sink));
        }

        Net* target = find_net(result_.module, outputNetId);
        if (target) {
            Driver driver;
            driver.kind = DriverKind::GateOutput;
            driver.ownerId = stored.id;
            driver.pinId = stored.output.id;
            driver.location = location;
            target->drivers.push_back(std::move(driver));
        }
    }

    void add_driver(const std::string& netId, Driver driver) {
        Net* net = find_net(result_.module, netId);
        if (net) net->drivers.push_back(std::move(driver));
    }

    std::string next_gate_id() {
        std::string id;
        do {
            id = "g" + std::to_string(gateCounter_++);
        } while (gate_id_exists(id));
        return id;
    }

    bool gate_id_exists(const std::string& id) const {
        for (const auto& gate : result_.module.gates) {
            if (gate.id == id) return true;
        }
        return false;
    }

    void add_error(const std::string& code, const std::string& message,
                   SourceLocation location) {
        result_.diagnostics.push_back(
            make_error(DiagnosticKind::Semantic, code, message, location));
    }

    const Ast::Module& ast_;
    BuildResult result_;
    std::unordered_map<std::string, std::size_t> portIndex_;
    std::unordered_map<std::string, std::size_t> signalIndex_;
    std::unordered_map<std::string, std::size_t> netIndex_;
    std::unordered_map<std::string, bool> directionKnown_;
    std::string constantZeroNet_;
    std::string constantOneNet_;
    std::size_t gateCounter_ = 0;
    std::size_t intermediateCounter_ = 0;
};

} // namespace

BuildResult build_circuit(const Ast::Module& astModule) {
    Builder builder(astModule);
    return builder.build();
}

} // namespace Verilator

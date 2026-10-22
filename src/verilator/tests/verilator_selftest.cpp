// Verilog 双向转换核心自检。
// 覆盖《Verilator规划.md》第 8 节要求的正常案例和错误案例：
// 单门、多级组合逻辑、多输入输出、多条 assign、优先级、常量、共享信号、
// 非法输入诊断、双向往返转换以及转换前后真值表一致性。
#include "../VerilatorApi.hpp"

#include <iostream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const std::string& label) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << label << '\n';
        ++failures;
    }
}

using namespace Verilator;

const Diagnostic* find_diagnostic(const std::vector<Diagnostic>& diagnostics,
                                  const std::string& code) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.code == code) return &diagnostic;
    }
    return nullptr;
}

bool has_code(const std::vector<Diagnostic>& diagnostics,
              const std::string& code) {
    return find_diagnostic(diagnostics, code) != nullptr;
}

std::vector<std::string> input_port_names(const Module& module) {
    std::vector<std::string> names;
    for (const auto& port : module.ports) {
        if (port.direction == PortDirection::Input) names.push_back(port.name);
    }
    return names;
}

std::vector<std::string> output_port_names(const Module& module) {
    std::vector<std::string> names;
    for (const auto& port : module.ports) {
        if (port.direction == PortDirection::Output) names.push_back(port.name);
    }
    return names;
}

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

Net* first_constant_net(Module& module) {
    for (auto& net : module.nets) {
        if (net.isConstant) return &net;
    }
    return nullptr;
}

bool eval_gate_kind(GateKind kind, bool a, bool b) {
    switch (kind) {
        case GateKind::And:  return a && b;
        case GateKind::Or:   return a || b;
        case GateKind::Not:  return !a;
        case GateKind::Nand: return !(a && b);
        case GateKind::Nor:  return !(a || b);
        case GateKind::Xor:  return a != b;
        case GateKind::Xnor: return a == b;
    }
    return false;
}

class Evaluator {
public:
    explicit Evaluator(const Module& module)
        : module_(module) {}

    void set_input(const std::string& name, bool value) {
        inputs_[name] = value;
    }

    void reset() { memo_.clear(); }

    LogicValue eval_net(const std::string& netId) {
        const auto memo = memo_.find(netId);
        if (memo != memo_.end()) return memo->second;
        if (!visiting_.insert(netId).second) return LogicValue::Zero;

        const Net* net = find_net(module_, netId);
        LogicValue value = LogicValue::Zero;
        if (net) {
            if (net->isConstant) {
                value = net->constantValue;
            } else if (!net->drivers.empty()) {
                const Driver& driver = net->drivers.front();
                switch (driver.kind) {
                    case DriverKind::Constant:
                        value = driver.constantValue;
                        break;
                    case DriverKind::Net:
                        value = eval_net(driver.ownerId);
                        break;
                    case DriverKind::Port: {
                        const auto input = inputs_.find(driver.ownerId);
                        value = input != inputs_.end() && input->second
                                    ? LogicValue::One
                                    : LogicValue::Zero;
                        break;
                    }
                    case DriverKind::GateOutput: {
                        const LogicGate* gate =
                            find_gate(module_, driver.ownerId);
                        if (gate) value = eval_gate(*gate);
                        break;
                    }
                }
            }
        }

        visiting_.erase(netId);
        memo_[netId] = value;
        return value;
    }

private:
    LogicValue eval_gate(const LogicGate& gate) {
        std::vector<bool> values;
        values.reserve(gate.inputs.size());
        for (const auto& pin : gate.inputs) {
            const Net* net = input_net_for_pin(module_, gate.id, pin.id);
            values.push_back(net && eval_net(net->id) == LogicValue::One);
        }

        const bool a = values.empty() ? false : values[0];
        const bool b = values.size() < 2 ? false : values[1];
        return eval_gate_kind(gate.kind, a, b) ? LogicValue::One
                                               : LogicValue::Zero;
    }

    const Module& module_;
    std::unordered_map<std::string, bool> inputs_;
    std::unordered_map<std::string, LogicValue> memo_;
    std::unordered_set<std::string> visiting_;
};

using InputAssignment = std::map<std::string, bool>;
using TruthTable = std::map<std::string, std::string>;

std::string eval_outputs(const Module& module,
                         const InputAssignment& assignment) {
    Evaluator evaluator(module);
    for (const auto& [name, value] : assignment) {
        evaluator.set_input(name, value);
    }
    evaluator.reset();

    std::string result;
    for (const auto& port : module.ports) {
        if (port.direction != PortDirection::Output) continue;
        result.push_back(evaluator.eval_net(port.name) == LogicValue::One
                             ? '1'
                             : '0');
    }
    return result;
}

TruthTable build_truth_table(const Module& module) {
    TruthTable table;
    const std::vector<std::string> inputNames = input_port_names(module);
    if (inputNames.size() > 10) return table;

    const std::size_t combinations = std::size_t{1} << inputNames.size();
    for (std::size_t mask = 0; mask < combinations; ++mask) {
        InputAssignment assignment;
        std::string key;
        for (std::size_t i = 0; i < inputNames.size(); ++i) {
            const bool value = ((mask >> i) & 1u) != 0;
            assignment[inputNames[i]] = value;
            key.push_back(value ? '1' : '0');
        }
        table[key] = eval_outputs(module, assignment);
    }
    return table;
}

void check_same_truth_table(const Module& left, const Module& right,
                            const std::string& label) {
    const TruthTable leftTable = build_truth_table(left);
    const TruthTable rightTable = build_truth_table(right);
    check(!leftTable.empty(), label + "：左侧真值表非空");
    check(leftTable == rightTable, label + "：转换前后真值表一致");
}

// 手工构造一个门电路模型：a/b 输入，y 输出。
Module make_gate_circuit(GateKind kind) {
    Module module;
    module.name = "gate_model";

    Port portA;
    portA.name = "a";
    portA.direction = PortDirection::Input;
    Port portB;
    portB.name = "b";
    portB.direction = PortDirection::Input;
    Port portY;
    portY.name = "y";
    portY.direction = PortDirection::Output;
    module.ports = {portA, portB, portY};

    Signal signalA;
    signalA.id = "a";
    signalA.name = "a";
    signalA.kind = SignalKind::Input;
    Signal signalB;
    signalB.id = "b";
    signalB.name = "b";
    signalB.kind = SignalKind::Input;
    Signal signalY;
    signalY.id = "y";
    signalY.name = "y";
    signalY.kind = SignalKind::Output;
    module.signals = {signalA, signalB, signalY};

    auto make_net = [](const std::string& name, SignalKind kind) {
        Net net;
        net.id = name;
        net.name = name;
        if (kind == SignalKind::Input) {
            Driver driver;
            driver.kind = DriverKind::Port;
            driver.ownerId = name;
            driver.pinId = "in";
            net.drivers.push_back(driver);
        } else if (kind == SignalKind::Output) {
            Terminal sink;
            sink.kind = TerminalKind::Port;
            sink.ownerId = name;
            sink.pinId = "out";
            net.sinks.push_back(sink);
        }
        return net;
    };
    module.nets = {make_net("a", SignalKind::Input),
                   make_net("b", SignalKind::Input),
                   make_net("y", SignalKind::Output)};

    LogicGate gate;
    gate.id = "g0";
    gate.kind = kind;
    const std::size_t arity = gate_input_arity(kind);
    for (std::size_t i = 0; i < arity; ++i) {
        Pin pin;
        pin.id = "in" + std::to_string(i);
        pin.name = pin.id;
        pin.direction = PinDirection::Input;
        gate.inputs.push_back(pin);
    }
    gate.output.id = "out";
    gate.output.name = "out";
    gate.output.direction = PinDirection::Output;
    module.gates.push_back(gate);

    for (std::size_t i = 0; i < arity; ++i) {
        Net* net = find_net(module, i == 0 ? "a" : "b");
        Terminal sink;
        sink.kind = TerminalKind::GateInput;
        sink.ownerId = "g0";
        sink.pinId = "in" + std::to_string(i);
        net->sinks.push_back(sink);
    }

    Net* output = find_net(module, "y");
    Driver driver;
    driver.kind = DriverKind::GateOutput;
    driver.ownerId = "g0";
    driver.pinId = "out";
    output->drivers.push_back(driver);

    return module;
}

// ---- 1) 单个门 ----
void test_individual_gates() {
    const std::pair<const char*, GateKind> binaryGates[] = {
        {"&", GateKind::And},
        {"|", GateKind::Or},
        {"^", GateKind::Xor},
        {"~^", GateKind::Xnor},
        {"^~", GateKind::Xnor},
    };

    for (const auto& [op, kind] : binaryGates) {
        const std::string source =
            std::string("module g(input a, input b, output y);\n"
                        "    assign y = a ") +
            op + " b;\nendmodule\n";
        const ParseResult parsed = parse_verilog(source);
        check(parsed.ok, std::string("解析二元门 ") + op);
        check(parsed.module.gates.size() == 1,
              std::string("二元门 ") + op + " 生成一个门");
        if (parsed.module.gates.size() == 1) {
            check(parsed.module.gates.front().kind == kind,
                  std::string("二元门类型 ") + op);
        }

        const TruthTable table = build_truth_table(parsed.module);
        for (const auto& [key, actual] : table) {
            const bool a = key[0] == '1';
            const bool b = key[1] == '1';
            check(actual == (eval_gate_kind(kind, a, b) ? "1" : "0"),
                  std::string("真值表 ") + op + " " + key);
        }
    }

    // NOT
    const ParseResult notParsed = parse_verilog(
        "module g(input a, output y); assign y = ~a; endmodule");
    check(notParsed.ok, "解析 NOT 门");
    check(notParsed.module.gates.size() == 1, "NOT 门生成一个门");
    if (notParsed.module.gates.size() == 1) {
        check(notParsed.module.gates.front().kind == GateKind::Not,
              "NOT 门类型");
    }
    check(build_truth_table(notParsed.module)["0"] == "1", "NOT 0 = 1");
    check(build_truth_table(notParsed.module)["1"] == "0", "NOT 1 = 0");

    // NAND / NOR：Verilog 子集没有对应运算符，但可以用等价表达式验证语义。
    const struct {
        const char* expression;
        GateKind kind;
    } expandedGates[] = {
        {"~(a & b)", GateKind::Nand},
        {"~(a | b)", GateKind::Nor},
    };
    for (const auto& expanded : expandedGates) {
        const std::string source =
            std::string("module g(input a, input b, output y); assign y = ") +
            expanded.expression + "; endmodule";
        const ParseResult parsed = parse_verilog(source);
        check(parsed.ok, std::string("解析等价 ") +
                             gate_kind_name(expanded.kind));
        const TruthTable table = build_truth_table(parsed.module);
        for (const auto& [key, actual] : table) {
            const bool a = key[0] == '1';
            const bool b = key[1] == '1';
            check(actual == (eval_gate_kind(expanded.kind, a, b) ? "1" : "0"),
                  std::string("真值表 ") + gate_kind_name(expanded.kind) + " " +
                      key);
        }
    }

    // 手工模型生成 Verilog 后再解析，覆盖 NAND/NOR/XNOR 的生成路径。
    for (const GateKind kind : {GateKind::And, GateKind::Or, GateKind::Not,
                                GateKind::Nand, GateKind::Nor, GateKind::Xor,
                                GateKind::Xnor}) {
        const Module circuit = make_gate_circuit(kind);
        const ValidationResult validation = validate_circuit(circuit);
        check(validation.ok, std::string("手工 ") + gate_kind_name(kind) +
                                 " 电路模型合法");

        const GenerateResult generated = generate_verilog(circuit);
        check(generated.ok, std::string("生成 ") + gate_kind_name(kind));
        check(!generated.verilog.empty(),
              std::string("生成代码非空 ") + gate_kind_name(kind));

        if (!generated.ok) continue;
        const ParseResult reparsed = parse_verilog(generated.verilog);
        check(reparsed.ok, std::string("重新解析 ") + gate_kind_name(kind));
        if (reparsed.ok) {
            check_same_truth_table(circuit, reparsed.module,
                                   std::string("电路往返 ") +
                                       gate_kind_name(kind));
        }
    }
}

// ---- 2) 运算符优先级与括号 ----
void test_precedence_and_parentheses() {
    struct Case {
        const char* expression;
        bool (*expected)(bool, bool, bool);
        const char* label;
    };

    const Case cases[] = {
        {"a | b & c",
         [](bool a, bool b, bool c) { return a || (b && c); },
         "a | b & c 等价于 a | (b & c)"},
        {"(a | b) & c",
         [](bool a, bool b, bool c) { return (a || b) && c; },
         "(a | b) & c"},
        {"a ^ b & c",
         [](bool a, bool b, bool c) { return a != (b && c); },
         "a ^ b & c 等价于 a ^ (b & c)"},
        {"~a & b",
         [](bool a, bool b, bool c) { return !a && b; },
         "~a & b"},
        {"a & ~(b | c)",
         [](bool a, bool b, bool c) { return a && !(b || c); },
         "a & ~(b | c)"},
    };

    for (const auto& item : cases) {
        const std::string source =
            std::string("module p(input a, input b, input c, output y);\n"
                        "    assign y = ") +
            item.expression + ";\nendmodule\n";
        const ParseResult parsed = parse_verilog(source);
        check(parsed.ok, std::string("解析 ") + item.label);
        if (!parsed.ok) continue;

        const TruthTable table = build_truth_table(parsed.module);
        for (const auto& [key, actual] : table) {
            const bool a = key[0] == '1';
            const bool b = key[1] == '1';
            const bool c = key[2] == '1';
            check(actual == (item.expected(a, b, c) ? "1" : "0"),
                  std::string(item.label) + " 真值表 " + key);
        }
    }
}

// ---- 3) 多级、多输入输出、多条 assign ----
const char* kComplexSource = R"(module combo(
    input a,
    input b,
    input c,
    input d,
    output y,
    output z
);
    wire w;
    assign w = (a & b) | (~c & d);
    assign y = w ^ c;
    assign z = ~(a | d);
endmodule
)";

void test_multi_level_multi_io() {
    const ParseResult parsed = parse_verilog(kComplexSource);
    check(parsed.ok, "解析多级多输出模块");
    if (!parsed.ok) return;

    check(parsed.module.ports.size() == 6, "多输出模块端口数量");
    check(input_port_names(parsed.module).size() == 4, "四个输入端口");
    check(output_port_names(parsed.module).size() == 2, "两个输出端口");
    check(parsed.module.gates.size() >= 6, "多级表达式生成多个门");

    const TruthTable table = build_truth_table(parsed.module);
    for (const auto& [key, actual] : table) {
        const bool a = key[0] == '1';
        const bool b = key[1] == '1';
        const bool c = key[2] == '1';
        const bool d = key[3] == '1';
        const bool w = (a && b) || (!c && d);
        const bool expectedY = w != c;
        const bool expectedZ = !(a || d);
        std::string expected;
        expected.push_back(expectedY ? '1' : '0');
        expected.push_back(expectedZ ? '1' : '0');
        check(actual == expected, "多级组合逻辑真值表 " + key);
    }
}

void test_multiple_assignments_and_shared_signal() {
    const ParseResult parsed = parse_verilog(R"(module shared(
    input a,
    input b,
    input c,
    output y,
    output z
);
    wire w;
    assign w = a & b;
    assign y = w | c;
    assign z = ~w;
endmodule
)");
    check(parsed.ok, "解析共享信号模块");
    if (!parsed.ok) return;

    const Net* shared = find_net(parsed.module, "w");
    check(shared != nullptr, "内部信号 w 存在对应网络");
    if (shared) {
        std::size_t sinkCount = 0;
        for (const auto& sink : shared->sinks) {
            if (sink.kind == TerminalKind::GateInput) ++sinkCount;
        }
        check(sinkCount == 2, "同一个信号被两个门引用");
    }

    const TruthTable table = build_truth_table(parsed.module);
    for (const auto& [key, actual] : table) {
        const bool a = key[0] == '1';
        const bool b = key[1] == '1';
        const bool c = key[2] == '1';
        const bool w = a && b;
        std::string expected;
        expected.push_back((w || c) ? '1' : '0');
        expected.push_back((!w) ? '1' : '0');
        check(actual == expected, "多条 assign 真值表 " + key);
    }
}

// ---- 4) 常量 ----
void test_constants() {
    const ParseResult parsed = parse_verilog(R"(module constants(
    input a,
    output y,
    output z,
    output one,
    output zero
);
    assign y = a & 1'b1;
    assign z = a | 1'b0;
    assign one = 1'b1;
    assign zero = 1'b0;
endmodule
)");
    check(parsed.ok, "解析常量模块");
    if (!parsed.ok) return;

    const Net* constZero = find_net(parsed.module, "$const0");
    const Net* constOne = find_net(parsed.module, "$const1");
    check(constZero != nullptr && constZero->isConstant,
          "1'b0 建立显式常量网络");
    check(constOne != nullptr && constOne->isConstant,
          "1'b1 建立显式常量网络");
    check(constZero && constZero->constantValue == LogicValue::Zero,
          "常量网络 0 的值");
    check(constOne && constOne->constantValue == LogicValue::One,
          "常量网络 1 的值");

    const TruthTable table = build_truth_table(parsed.module);
    for (const auto& [key, actual] : table) {
        const bool a = key[0] == '1';
        std::string expected;
        expected.push_back(a ? '1' : '0');
        expected.push_back(a ? '1' : '0');
        expected.push_back('1');
        expected.push_back('0');
        check(actual == expected, "常量真值表 " + key);
    }

    const GenerateResult generated = generate_verilog(parsed.module);
    check(generated.ok, "常量电路生成 Verilog");
    check(generated.verilog.find("1'b0") != std::string::npos,
          "生成代码包含 1'b0");
    check(generated.verilog.find("1'b1") != std::string::npos,
          "生成代码包含 1'b1");

    const ParseResult reparsed = parse_verilog(generated.verilog);
    check(reparsed.ok, "常量生成代码可重新解析");
    if (reparsed.ok) {
        check_same_truth_table(parsed.module, reparsed.module,
                               "常量电路往返");
    }
}

// ---- 4b) 常量网络模型不变量 ----
void test_constant_model_invariants() {
    const ParseResult parsed = parse_verilog(
        "module m(input a, output y); assign y = a & 1'b0; endmodule");
    check(parsed.ok, "常量不变量测试源码解析");
    if (!parsed.ok) return;

    check(validate_circuit(parsed.module).ok, "合法常量模型通过校验");

    const auto check_generator_rejects =
        [](const Module& circuit, const std::string& label) {
            const GenerateResult generated = generate_verilog(circuit);
            check(!generated.ok, label + "：生成应失败");
            check(generated.verilog.empty(), label + "：不输出代码");
        };

    {
        Module circuit = parsed.module;
        Net* constant = first_constant_net(circuit);
        check(constant != nullptr, "测试模型包含常量网络");
        if (constant) {
            constant->name = "zero";
            const ValidationResult validation = validate_circuit(circuit);
            check(!validation.ok, "非法常量名称：校验应失败");
            check(has_code(validation.diagnostics,
                           "CIRCUIT_CONSTANT_NET_NAME_INVALID"),
                  "非法常量名称：返回名称诊断");
            check_generator_rejects(circuit, "非法常量名称");
        }
    }

    {
        Module circuit = parsed.module;
        Net* constant = first_constant_net(circuit);
        check(constant != nullptr, "值不一致测试包含常量网络");
        if (constant) {
            constant->name = "1'b1"; // 常量值仍是 Zero
            const ValidationResult validation = validate_circuit(circuit);
            check(!validation.ok, "常量值与名称不一致：校验应失败");
            check(has_code(validation.diagnostics,
                           "CIRCUIT_CONSTANT_NET_VALUE_MISMATCH"),
                  "常量值与名称不一致：返回值诊断");
            check_generator_rejects(circuit, "常量值与名称不一致");
        }
    }

    {
        Module circuit = parsed.module;
        Net* constant = first_constant_net(circuit);
        check(constant != nullptr, "驱动不一致测试包含常量网络");
        if (constant) {
            constant->drivers.front().constantValue = LogicValue::One;
            const ValidationResult validation = validate_circuit(circuit);
            check(!validation.ok, "常量驱动值与网络值不一致：校验应失败");
            check(has_code(validation.diagnostics,
                           "CIRCUIT_CONSTANT_NET_DRIVER_MISMATCH"),
                  "常量驱动值与网络值不一致：返回驱动诊断");
            check_generator_rejects(circuit, "常量驱动值与网络值不一致");
        }
    }

    {
        Module circuit = parsed.module;
        Net* constant = first_constant_net(circuit);
        check(constant != nullptr, "ID 冲突测试包含常量网络");
        if (constant) {
            Signal conflict;
            conflict.id = constant->id;
            conflict.name = "conflict_signal";
            conflict.kind = SignalKind::Wire;
            circuit.signals.push_back(conflict);

            const ValidationResult validation = validate_circuit(circuit);
            check(!validation.ok, "常量网络与 Signal ID 冲突：校验应失败");
            check(has_code(validation.diagnostics,
                           "CIRCUIT_CONSTANT_NET_SIGNAL_CONFLICT"),
                  "常量网络与 Signal ID 冲突：返回冲突诊断");
            check_generator_rejects(circuit, "常量网络与 Signal ID 冲突");
        }
    }

    const GenerateResult generated = generate_verilog(parsed.module);
    check(generated.ok, "合法 0 常量生成成功");
    if (generated.ok) {
        const ParseResult reparsed = parse_verilog(generated.verilog);
        check(reparsed.ok, "合法 0 常量生成代码可重新解析");
        if (reparsed.ok) {
            check_same_truth_table(parsed.module, reparsed.module,
                                   "合法 0 常量往返");
        }
    }
}

// ---- 4c) Verilog 保留字标识符 ----
void test_reserved_identifiers() {
    const auto check_rejected =
        [](const std::string& label, const Module& circuit,
           const std::string& code) {
            const ValidationResult validation = validate_circuit(circuit);
            check(!validation.ok, label + "：校验应失败");
            check(has_code(validation.diagnostics, code),
                  label + "：应包含诊断 " + code);

            const GenerateResult generated = generate_verilog(circuit);
            check(!generated.ok, label + "：生成应失败");
            check(generated.verilog.empty(), label + "：不输出代码");
        };

    for (const char* name : {"module", "wire", "input", "output", "assign",
                             "endmodule"}) {
        Module circuit = make_gate_circuit(GateKind::And);
        circuit.name = name;
        check_rejected(std::string("模块名保留字 ") + name, circuit,
                       "CIRCUIT_MODULE_NAME_RESERVED");
    }

    const ParseResult parsed = parse_verilog(
        "module m(input a, output y);\n"
        "    wire w;\n"
        "    assign w = a;\n"
        "    assign y = w;\n"
        "endmodule\n");
    check(parsed.ok, "保留字测试源码解析");
    if (!parsed.ok) return;

    {
        Module circuit = parsed.module;
        Signal* wireSignal = nullptr;
        Net* wireNet = nullptr;
        for (auto& signal : circuit.signals) {
            if (signal.id == "w") wireSignal = &signal;
        }
        for (auto& net : circuit.nets) {
            if (net.id == "w") wireNet = &net;
        }
        check(wireSignal != nullptr && wireNet != nullptr,
              "保留字测试包含内部 wire");
        if (wireSignal && wireNet) {
            wireSignal->name = "wire";
            wireNet->name = "wire";
            check_rejected("信号名保留字", circuit,
                           "CIRCUIT_SIGNAL_NAME_RESERVED");
        }
    }

    {
        Module circuit = parsed.module;
        check(!circuit.ports.empty(), "端口保留字测试包含端口");
        if (!circuit.ports.empty()) {
            circuit.ports.front().name = "input";
            check_rejected("端口名保留字", circuit,
                           "CIRCUIT_PORT_NAME_RESERVED");
        }
    }
}

// ---- 5) 双向往返 ----
void test_round_trips() {
    const ParseResult original = parse_verilog(kComplexSource);
    check(original.ok, "往返测试原始源码解析");
    if (!original.ok) return;

    const GenerateResult generated = generate_verilog(original.module);
    check(generated.ok, "Verilog -> 电路 -> Verilog 生成");
    check(generated.verilog.find("module combo") != std::string::npos,
          "生成代码保留模块名");
    check(generated.verilog.find("endmodule") != std::string::npos,
          "生成代码包含 endmodule");

    const ParseResult reparsed = parse_verilog(generated.verilog);
    check(reparsed.ok, "生成的 Verilog 可被本解析器重新解析");
    if (!reparsed.ok) return;

    check(input_port_names(original.module) ==
              input_port_names(reparsed.module),
          "往返后输入端口名称与顺序一致");
    check(output_port_names(original.module) ==
              output_port_names(reparsed.module),
          "往返后输出端口名称与顺序一致");
    check_same_truth_table(original.module, reparsed.module,
                           "Verilog -> 电路 -> Verilog");

    const GenerateResult secondGenerated =
        generate_verilog(reparsed.module);
    check(secondGenerated.ok, "第二次生成 Verilog");
    const ParseResult secondParsed = parse_verilog(secondGenerated.verilog);
    check(secondParsed.ok, "第二次生成的 Verilog 仍可解析");
    if (secondParsed.ok) {
        check_same_truth_table(original.module, secondParsed.module,
                               "多次往返");
    }
}

// ---- 6) 错误诊断 ----
void expect_parse_error(const std::string& source, const std::string& code,
                        DiagnosticKind kind, const std::string& label) {
    const ParseResult parsed = parse_verilog(source);
    check(!parsed.ok, label + "：解析应失败");
    const Diagnostic* diagnostic = find_diagnostic(parsed.diagnostics, code);
    check(diagnostic != nullptr, label + "：应包含诊断 " + code);
    if (diagnostic) {
        check(diagnostic->kind == kind, label + "：诊断分类正确");
        check(diagnostic->location.line > 0 &&
                  diagnostic->location.column > 0,
              label + "：诊断包含行号和列号");
    }
}

// ---- 6b) 直连别名驱动 ----
void test_alias_drives() {
    const auto check_alias_success =
        [](const std::string& source, const std::string& label) {
            const ParseResult parsed = parse_verilog(source);
            check(parsed.ok, label + "：解析应成功");
            if (!parsed.ok) return;

            const TruthTable table = build_truth_table(parsed.module);
            const auto zero = table.find("0");
            const auto one = table.find("1");
            check(zero != table.end() && zero->second == "0",
                  label + "：输入 0 时输出为 0");
            check(one != table.end() && one->second == "1",
                  label + "：输入 1 时输出为 1");

            const GenerateResult generated = generate_verilog(parsed.module);
            check(generated.ok, label + "：生成应成功");
            if (!generated.ok) return;

            const ParseResult reparsed = parse_verilog(generated.verilog);
            check(reparsed.ok, label + "：生成代码应可重新解析");
            if (reparsed.ok) {
                check_same_truth_table(parsed.module, reparsed.module,
                                       label + "：往返真值表一致");
            }
        };

    check_alias_success(
        "module m(input a, output y); assign y = a; endmodule",
        "输出直接连接输入端口");

    check_alias_success(
        "module m(input a, output y);\n"
        "    wire w1;\n"
        "    wire w2;\n"
        "    assign w1 = a;\n"
        "    assign w2 = w1;\n"
        "    assign y = w2;\n"
        "endmodule\n",
        "完整别名链");

    // 别名链的最终来源是门输出，验证解析器的 GateOutput 驱动识别。
    const ParseResult gateAlias = parse_verilog(
        "module m(input a, input b, output w, output y);\n"
        "    assign w = a & b;\n"
        "    assign y = w;\n"
        "endmodule\n");
    check(gateAlias.ok, "门输出作为别名源：解析应成功");
    if (gateAlias.ok) {
        const GenerateResult generated = generate_verilog(gateAlias.module);
        check(generated.ok, "门输出作为别名源：生成应成功");
        if (generated.ok) {
            const ParseResult reparsed = parse_verilog(generated.verilog);
            check(reparsed.ok, "门输出作为别名源：生成代码应可重新解析");
            if (reparsed.ok) {
                check_same_truth_table(gateAlias.module, reparsed.module,
                                       "门输出作为别名源：往返真值表一致");
            }
        }
    }

    // 别名链的最终来源是常量驱动。
    const ParseResult constantAlias = parse_verilog(
        "module m(input a, output y);\n"
        "    wire w;\n"
        "    assign w = 1'b1;\n"
        "    assign y = w;\n"
        "endmodule\n");
    check(constantAlias.ok, "常量作为别名源：解析应成功");
    if (constantAlias.ok) {
        const TruthTable table = build_truth_table(constantAlias.module);
        for (const auto& [inputs, outputs] : table) {
            (void)inputs;
            check(outputs == "1", "常量作为别名源：输出恒为 1");
        }
    }

    expect_parse_error(
        "module m(input a, output y);\n"
        "    wire w;\n"
        "    assign y = w;\n"
        "endmodule\n",
        "CIRCUIT_DRIVER_NET_UNRIVEN", DiagnosticKind::Model,
        "输出直接指向未驱动 wire");

    expect_parse_error(
        "module m(input a, output y);\n"
        "    wire w1;\n"
        "    wire w2;\n"
        "    assign w2 = w1;\n"
        "    assign y = w2;\n"
        "endmodule\n",
        "CIRCUIT_DRIVER_NET_UNRIVEN", DiagnosticKind::Model,
        "别名链中断");

    expect_parse_error(
        "module m(input a, output y);\n"
        "    wire w;\n"
        "    assign w = w;\n"
        "    assign y = w;\n"
        "endmodule\n",
        "CIRCUIT_CYCLIC_DEPENDENCY", DiagnosticKind::Model,
        "直连别名自环");

    expect_parse_error(
        "module m(input a, output y);\n"
        "    wire w1;\n"
        "    wire w2;\n"
        "    assign w1 = w2;\n"
        "    assign w2 = w1;\n"
        "    assign y = w1;\n"
        "endmodule\n",
        "CIRCUIT_CYCLIC_DEPENDENCY", DiagnosticKind::Model,
        "多级别名环");
}

void test_invalid_inputs() {
    expect_parse_error(
        R"(module m(
    input a,
    output y
);
    assign y = a & b;
endmodule
)",
        "VERILOG_UNDECLARED_SIGNAL", DiagnosticKind::Semantic,
        "未声明信号");

    const ParseResult undeclared = parse_verilog(
        R"(module m(
    input a,
    output y
);
    assign y = a & b;
endmodule
)");
    const Diagnostic* location =
        find_diagnostic(undeclared.diagnostics, "VERILOG_UNDECLARED_SIGNAL");
    check(location != nullptr && location->location.line == 5,
          "未声明信号诊断行号");
    check(location != nullptr && location->location.column > 0,
          "未声明信号诊断列号");

    expect_parse_error(
        R"(module m(a, y);
    input a;
    input a;
    output y;
    assign y = a;
endmodule
)",
        "VERILOG_DUPLICATE_DECLARATION", DiagnosticKind::Semantic,
        "重复声明");

    expect_parse_error(
        "module m(input a, output y); assign y = a &; endmodule",
        "VERILOG_SYNTAX_EXPECTED_EXPRESSION", DiagnosticKind::Syntax,
        "缺少操作数");

    expect_parse_error(
        "module m(input a, output y); assign y = a endmodule",
        "VERILOG_SYNTAX_EXPECTED_TOKEN", DiagnosticKind::Syntax,
        "缺少分号");

    expect_parse_error(
        "module m(input a, output y); assign a = ~a; endmodule",
        "VERILOG_ASSIGN_TO_INPUT", DiagnosticKind::Semantic,
        "驱动输入端口");

    expect_parse_error(
        R"(module m(input a, output y);
    assign y = a;
    assign y = ~a;
endmodule
)",
        "VERILOG_MULTIPLE_DRIVER", DiagnosticKind::Semantic,
        "多重驱动");

    expect_parse_error(
        "module m(input a, output y); endmodule",
        "CIRCUIT_OUTPUT_UNRIVEN", DiagnosticKind::Model,
        "输出没有驱动");

    expect_parse_error(
        R"(module m(input a, input b, output y);
    wire w1;
    wire w2;
    assign w1 = w2 & a;
    assign w2 = w1 | b;
    assign y = w1;
endmodule
)",
        "CIRCUIT_CYCLIC_DEPENDENCY", DiagnosticKind::Model,
        "循环依赖");

    expect_parse_error(
        "module m(input a, output y); reg x; endmodule",
        "VERILOG_UNSUPPORTED_SYNTAX", DiagnosticKind::Syntax,
        "不支持的 reg 语法");

    expect_parse_error(
        "module m(input [1:0] a, output y); endmodule",
        "VERILOG_LEX_UNSUPPORTED_VECTOR", DiagnosticKind::Lexical,
        "不支持向量");

    expect_parse_error(
        "module m(input a, output y); assign y = 2'b0; endmodule",
        "VERILOG_LEX_UNSUPPORTED_NUMBER", DiagnosticKind::Lexical,
        "不支持多位常量");

    expect_parse_error(
        "module m(input a, output y); /* 没有闭合",
        "VERILOG_LEX_UNTERMINATED_COMMENT", DiagnosticKind::Lexical,
        "未闭合块注释");

    expect_parse_error(
        R"(module m(a, y);
    input a;
    assign y = a;
endmodule
)",
        "VERILOG_PORT_DIRECTION_MISSING", DiagnosticKind::Semantic,
        "端口缺少方向");

    expect_parse_error(
        R"(module m(a);
    input a;
    wire b;
    input b;
endmodule
)",
        "VERILOG_PORT_NOT_IN_LIST", DiagnosticKind::Semantic,
        "端口声明不在端口列表");

    expect_parse_error(
        R"(module first(input a, output y);
    assign y = a;
endmodule
module second(input a, output y);
    assign y = a;
endmodule
)",
        "VERILOG_SYNTAX_MULTIPLE_MODULES", DiagnosticKind::Syntax,
        "多个模块");
}

// ---- 7) 注释与空白 ----
void test_comments_and_whitespace() {
    const ParseResult parsed = parse_verilog(
        "// 行注释\r\n"
        "module m(input a, input b, output y);\r\n"
        "    /* 块注释\r\n"
        "       跨行 */\r\n"
        "    assign y = a & b; // 行尾注释\r\n"
        "endmodule\r\n");
    check(parsed.ok, "注释、换行和 CRLF 能被正确忽略");
    if (parsed.ok) {
        check(parsed.module.ports.size() == 3, "注释模块端口数量");
    }
}

// ---- 8) 生成器拒绝非法模型 ----
void test_generator_rejects_invalid_model() {
    Module circuit = make_gate_circuit(GateKind::And);
    check(!circuit.gates.empty(), "非法模型测试前门存在");
    if (circuit.gates.empty()) return;

    circuit.gates.front().inputs.pop_back(); // AND 只剩一个输入，破坏输入数量规则
    const GenerateResult generated = generate_verilog(circuit);
    check(!generated.ok, "非法模型生成应失败");
    check(generated.verilog.empty(), "非法模型不输出代码");
    check(has_code(generated.diagnostics, "CIRCUIT_GATE_ARITY"),
          "非法模型返回门输入数量诊断");

    const ValidationResult validation = validate_circuit(circuit);
    check(!validation.ok, "非法模型校验应失败");
}

} // namespace

int main() {
    test_individual_gates();
    test_precedence_and_parentheses();
    test_multi_level_multi_io();
    test_multiple_assignments_and_shared_signal();
    test_constants();
    test_constant_model_invariants();
    test_reserved_identifiers();
    test_round_trips();
    test_alias_drives();
    test_invalid_inputs();
    test_comments_and_whitespace();
    test_generator_rejects_invalid_model();

    std::cout << checks << " checks, " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}

#pragma once

#include "../include/VerilogKeywords.hpp"
#include "../include/VerilatorTypes.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace Verilator {

// 第一阶段支持的门类型。NAND/NOR/XNOR 在数据模型中有独立类型，
// 但 Verilog 子集没有对应的二元运算符，生成时会展开为等价的逻辑表达式。
enum class GateKind {
    And,
    Or,
    Not,
    Nand,
    Nor,
    Xor,
    Xnor
};

enum class SignalKind {
    Input,
    Output,
    Wire
};

enum class PortDirection {
    Input,
    Output
};

enum class PinDirection {
    Input,
    Output
};

enum class LogicValue {
    Zero,
    One
};

// 驱动源类型：门输出、外部输入端口、常量、或另一个网络（assign 直连）。
enum class DriverKind {
    GateOutput,
    Port,
    Constant,
    Net
};

// 接收端类型：门输入引脚或输出端口。
enum class TerminalKind {
    GateInput,
    Port
};

const char* gate_kind_name(GateKind kind);
const char* signal_kind_name(SignalKind kind);
std::size_t gate_input_arity(GateKind kind);
const char* logic_value_literal(LogicValue value);

// 引脚有稳定 id、名称和方向。第一阶段门输入引脚固定命名为 in0/in1，
// 输出引脚固定命名为 out。
struct Pin {
    std::string id;
    std::string name;
    PinDirection direction = PinDirection::Input;
};

struct LogicGate {
    std::string id;
    GateKind kind = GateKind::And;
    std::vector<Pin> inputs;
    Pin output;
};

// 信号是设计中的命名网络。id 与 name 在第一阶段相同，保留 id 字段是为了
// 后续接入画布时仍能使用稳定标识而不是显示名称。
struct Signal {
    std::string id;
    std::string name;
    SignalKind kind = SignalKind::Wire;
    SourceLocation location;
};

struct Port {
    std::string name;
    PortDirection direction = PortDirection::Input;
    SourceLocation location;
};

struct Driver {
    DriverKind kind = DriverKind::GateOutput;
    std::string ownerId;   // 门 id / 端口名 / 源网络 id
    std::string pinId;     // 门输出引脚 id（通常为 out）
    LogicValue constantValue = LogicValue::Zero;
    SourceLocation location;
};

struct Terminal {
    TerminalKind kind = TerminalKind::GateInput;
    std::string ownerId;   // 门 id / 端口名
    std::string pinId;     // 门输入引脚 id，或端口的 in/out
    SourceLocation location;
};

// Net 显式保存连接关系：driver 是驱动源，sinks 是全部接收端。
// 不依赖任何坐标或几何位置。
struct Net {
    std::string id;
    std::string name;
    bool isConstant = false;
    LogicValue constantValue = LogicValue::Zero;
    SourceLocation location;
    std::vector<Driver> drivers;
    std::vector<Terminal> sinks;
};

struct Module {
    std::string name;
    SourceLocation location;
    std::vector<Port> ports;
    std::vector<Signal> signals;
    std::vector<LogicGate> gates;
    std::vector<Net> nets;
};

inline const Port* find_port(const Module& module, const std::string& name) {
    for (const auto& port : module.ports) {
        if (port.name == name) return &port;
    }
    return nullptr;
}

inline const Signal* find_signal(const Module& module, const std::string& name) {
    for (const auto& signal : module.signals) {
        if (signal.name == name || signal.id == name) return &signal;
    }
    return nullptr;
}

inline const LogicGate* find_gate(const Module& module, const std::string& id) {
    for (const auto& gate : module.gates) {
        if (gate.id == id) return &gate;
    }
    return nullptr;
}

inline const Net* find_net(const Module& module, const std::string& id) {
    for (const auto& net : module.nets) {
        if (net.id == id) return &net;
    }
    return nullptr;
}

inline Net* find_net(Module& module, const std::string& id) {
    for (auto& net : module.nets) {
        if (net.id == id) return &net;
    }
    return nullptr;
}

inline bool is_valid_verilog_identifier(const std::string& name) {
    if (name.empty()) return false;
    const unsigned char first = static_cast<unsigned char>(name.front());
    if (!(first == '_' || (first >= 'a' && first <= 'z') ||
          (first >= 'A' && first <= 'Z'))) {
        return false;
    }
    for (const unsigned char c : name) {
        if (c == '_' || c == '$' ||
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) {
            continue;
        }
        return false;
    }
    return true;
}

inline bool is_reserved_verilog_identifier(const std::string& name) {
    return is_verilog_keyword(name);
}

} // namespace Verilator

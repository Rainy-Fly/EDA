#include "CircuitModel.hpp"

namespace Verilator {

const char* gate_kind_name(GateKind kind) {
    switch (kind) {
        case GateKind::And:  return "AND";
        case GateKind::Or:   return "OR";
        case GateKind::Not:  return "NOT";
        case GateKind::Nand: return "NAND";
        case GateKind::Nor:  return "NOR";
        case GateKind::Xor:  return "XOR";
        case GateKind::Xnor: return "XNOR";
    }
    return "UNKNOWN";
}

const char* signal_kind_name(SignalKind kind) {
    switch (kind) {
        case SignalKind::Input:  return "input";
        case SignalKind::Output: return "output";
        case SignalKind::Wire:   return "wire";
    }
    return "unknown";
}

std::size_t gate_input_arity(GateKind kind) {
    return kind == GateKind::Not ? 1u : 2u;
}

const char* logic_value_literal(LogicValue value) {
    return value == LogicValue::One ? "1'b1" : "1'b0";
}

} // namespace Verilator

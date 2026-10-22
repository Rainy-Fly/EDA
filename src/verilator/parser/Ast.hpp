#pragma once

#include "../include/VerilatorTypes.hpp"
#include "../model/CircuitModel.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Verilator {
namespace Ast {

// 表达式树是 Verilog 解析和逻辑电路转换之间的中间结构。
enum class ExpressionKind {
    Identifier,
    Constant,
    Not,
    Binary
};

enum class BinaryOperator {
    And,
    Or,
    Xor,
    Xnor
};

struct Expression {
    ExpressionKind kind = ExpressionKind::Identifier;
    SourceLocation location;
    std::string text;
    BinaryOperator binaryOperator = BinaryOperator::And;
    std::unique_ptr<Expression> left;
    std::unique_ptr<Expression> right;
};

using ExpressionPtr = std::unique_ptr<Expression>;

struct PortDeclaration {
    std::string name;
    bool hasDirection = false;
    PortDirection direction = PortDirection::Input;
    SourceLocation location;
};

struct SignalDeclaration {
    SignalKind kind = SignalKind::Wire;
    std::string name;
    SourceLocation location;
};

struct Assignment {
    std::string target;
    SourceLocation targetLocation;
    ExpressionPtr expression;
    SourceLocation location;
};

struct Module {
    std::string name;
    SourceLocation location;
    std::vector<PortDeclaration> ports;
    std::vector<SignalDeclaration> declarations;
    std::vector<Assignment> assignments;
};

} // namespace Ast
} // namespace Verilator

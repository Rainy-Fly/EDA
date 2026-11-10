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

// 时序块：always @(posedge clk) q <= d;
// 当前子集只支持“边沿触发的单个非阻塞赋值”（D触发器）。
struct TimingBlock {
    std::string clockName;          //触发时钟信号名
    bool posedge = true;            //仅支持 posedge（negedge 报不支持）
    std::string target;             //被赋值信号（reg，即触发器 Q）
    SourceLocation targetLocation;
    ExpressionPtr expression;       //D 输入表达式
    SourceLocation location;
};

struct Module {
    std::string name;
    SourceLocation location;
    std::vector<PortDeclaration> ports;
    std::vector<SignalDeclaration> declarations;
    std::vector<Assignment> assignments;
    std::vector<TimingBlock> timingBlocks;
};

} // namespace Ast
} // namespace Verilator

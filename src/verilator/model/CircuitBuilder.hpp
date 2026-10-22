#pragma once

#include "../include/VerilatorTypes.hpp"
#include "../parser/Ast.hpp"
#include "CircuitModel.hpp"

#include <vector>

namespace Verilator {

struct BuildResult {
    Module module;
    std::vector<Diagnostic> diagnostics;
};

// 把语法树转换为显式连接关系的逻辑电路模型，并完成声明、端口方向和
// 多重驱动等语义检查。结构合法性由 CircuitValidator 继续检查。
BuildResult build_circuit(const Ast::Module& astModule);

} // namespace Verilator

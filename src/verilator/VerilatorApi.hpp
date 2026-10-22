#pragma once

#include "include/VerilatorTypes.hpp"
#include "model/CircuitModel.hpp"

#include <string>
#include <vector>

namespace Verilator {

struct ParseResult {
    bool ok = false;
    Module module;
    std::vector<Diagnostic> diagnostics;
};

struct GenerateResult {
    bool ok = false;
    std::string verilog;
    std::vector<Diagnostic> diagnostics;
};

struct ValidationResult {
    bool ok = false;
    std::vector<Diagnostic> diagnostics;
};

// Verilog 源码 -> 逻辑电路模型。
// 不支持或非法的语法会返回带行号、列号和分类的错误，不会静默跳过。
ParseResult parse_verilog(const std::string& source);

// 逻辑电路模型 -> Verilog 源码。模型不合法时不会生成代码。
GenerateResult generate_verilog(const Module& module);

// 检查逻辑电路模型的引脚连接、驱动、门类型和组合环。
ValidationResult validate_circuit(const Module& module);

} // namespace Verilator

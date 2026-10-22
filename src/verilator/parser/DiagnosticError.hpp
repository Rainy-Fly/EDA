#pragma once

#include "../include/VerilatorTypes.hpp"

#include <exception>
#include <utility>

namespace Verilator {

// 词法/语法解析内部使用异常中断；公共 API 会把它转换成 ParseResult 中的诊断。
class DiagnosticError : public std::exception {
public:
    explicit DiagnosticError(Diagnostic diagnostic)
        : diagnostic_(std::move(diagnostic)) {}

    const Diagnostic& diagnostic() const noexcept { return diagnostic_; }
    const char* what() const noexcept override { return diagnostic_.message.c_str(); }

private:
    Diagnostic diagnostic_;
};

} // namespace Verilator

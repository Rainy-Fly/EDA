#pragma once

#include <string>
#include <vector>

namespace Verilator {

// 源码位置：行号和列号均从 1 开始。词法、语法、语义和模型诊断都会尽量带上位置。
struct SourceLocation {
    int line = 0;
    int column = 0;
};

// 诊断分类，对应《Verilator规划.md》第 7 节的要求。
enum class DiagnosticKind {
    Lexical,
    Syntax,
    Semantic,
    Model
};

enum class DiagnosticSeverity {
    Error,
    Warning
};

struct Diagnostic {
    DiagnosticKind kind = DiagnosticKind::Syntax;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string code;
    std::string message;
    SourceLocation location;
};

const char* to_string(DiagnosticKind kind);
const char* to_string(DiagnosticSeverity severity);

Diagnostic make_diagnostic(DiagnosticKind kind, DiagnosticSeverity severity,
                           std::string code, std::string message,
                           SourceLocation location);

Diagnostic make_error(DiagnosticKind kind, std::string code, std::string message,
                      SourceLocation location);

bool has_error(const std::vector<Diagnostic>& diagnostics);

} // namespace Verilator

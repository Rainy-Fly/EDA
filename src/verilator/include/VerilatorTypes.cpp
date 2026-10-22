#include "VerilatorTypes.hpp"

#include <utility>

namespace Verilator {

const char* to_string(DiagnosticKind kind) {
    switch (kind) {
        case DiagnosticKind::Lexical:  return "lexical";
        case DiagnosticKind::Syntax:   return "syntax";
        case DiagnosticKind::Semantic: return "semantic";
        case DiagnosticKind::Model:    return "model";
    }
    return "unknown";
}

const char* to_string(DiagnosticSeverity severity) {
    switch (severity) {
        case DiagnosticSeverity::Error:   return "error";
        case DiagnosticSeverity::Warning: return "warning";
    }
    return "unknown";
}

Diagnostic make_diagnostic(DiagnosticKind kind, DiagnosticSeverity severity,
                           std::string code, std::string message,
                           SourceLocation location) {
    Diagnostic diagnostic;
    diagnostic.kind = kind;
    diagnostic.severity = severity;
    diagnostic.code = std::move(code);
    diagnostic.message = std::move(message);
    diagnostic.location = location;
    return diagnostic;
}

Diagnostic make_error(DiagnosticKind kind, std::string code, std::string message,
                      SourceLocation location) {
    return make_diagnostic(kind, DiagnosticSeverity::Error, std::move(code),
                           std::move(message), location);
}

bool has_error(const std::vector<Diagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::Error) return true;
    }
    return false;
}

} // namespace Verilator

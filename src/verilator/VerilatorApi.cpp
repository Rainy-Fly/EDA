#include "VerilatorApi.hpp"

#include "generator/VerilogGenerator.hpp"
#include "model/CircuitBuilder.hpp"
#include "model/CircuitValidator.hpp"
#include "parser/DiagnosticError.hpp"
#include "parser/Lexer.hpp"
#include "parser/Parser.hpp"

#include <utility>

namespace Verilator {

ParseResult parse_verilog(const std::string& source) {
    ParseResult result;

    try {
        Lexer lexer(source);
        Parser parser(lexer.tokenize());
        Ast::Module astModule = parser.parse_module();

        BuildResult built = build_circuit(astModule);
        result.module = std::move(built.module);
        result.diagnostics = std::move(built.diagnostics);

        const std::vector<Diagnostic> modelDiagnostics =
            validate_module(result.module);
        result.diagnostics.insert(result.diagnostics.end(),
                                  modelDiagnostics.begin(),
                                  modelDiagnostics.end());
    } catch (const DiagnosticError& error) {
        result.diagnostics.push_back(error.diagnostic());
    }

    result.ok = !has_error(result.diagnostics);
    return result;
}

GenerateResult generate_verilog(const Module& module) {
    GenerateResult result;

    result.diagnostics = validate_module(module);
    if (has_error(result.diagnostics)) {
        result.ok = false;
        return result;
    }

    result.verilog = generate_verilog_source(module);
    result.ok = true;
    return result;
}

ValidationResult validate_circuit(const Module& module) {
    ValidationResult result;
    result.diagnostics = validate_module(module);
    result.ok = !has_error(result.diagnostics);
    return result;
}

} // namespace Verilator

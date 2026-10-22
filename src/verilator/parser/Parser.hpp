#pragma once

#include "Ast.hpp"
#include "Token.hpp"

#include <string>
#include <vector>

namespace Verilator {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    Ast::Module parse_module();

private:
    const Token& current() const;
    const Token& peek(std::size_t offset) const;
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    const Token& advance();
    const Token& expect(TokenKind kind, const std::string& expectation);
    const Token& expect_identifier(const std::string& expectation);

    void parse_port_list(Ast::Module& module);
    void parse_declaration(Ast::Module& module, SignalKind kind);
    Ast::Assignment parse_assignment();

    Ast::ExpressionPtr parse_expression();
    Ast::ExpressionPtr parse_or();
    Ast::ExpressionPtr parse_xor();
    Ast::ExpressionPtr parse_and();
    Ast::ExpressionPtr parse_unary();
    Ast::ExpressionPtr parse_primary();

    [[noreturn]] void fail(const std::string& code, const std::string& message,
                           SourceLocation location) const;

    std::vector<Token> tokens_;
    std::size_t index_ = 0;
};

} // namespace Verilator

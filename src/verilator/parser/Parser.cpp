#include "Parser.hpp"

#include "DiagnosticError.hpp"

#include <memory>
#include <utility>

namespace Verilator {
namespace {

Ast::ExpressionPtr make_identifier(const Token& token) {
    auto expression = std::make_unique<Ast::Expression>();
    expression->kind = Ast::ExpressionKind::Identifier;
    expression->location = token.location;
    expression->text = token.text;
    return expression;
}

Ast::ExpressionPtr make_constant(const Token& token) {
    auto expression = std::make_unique<Ast::Expression>();
    expression->kind = Ast::ExpressionKind::Constant;
    expression->location = token.location;
    expression->text = token.text;
    return expression;
}

Ast::ExpressionPtr make_not(Ast::ExpressionPtr operand, SourceLocation location) {
    auto expression = std::make_unique<Ast::Expression>();
    expression->kind = Ast::ExpressionKind::Not;
    expression->location = location;
    expression->left = std::move(operand);
    return expression;
}

Ast::ExpressionPtr make_binary(Ast::BinaryOperator op, Ast::ExpressionPtr left,
                               Ast::ExpressionPtr right, SourceLocation location) {
    auto expression = std::make_unique<Ast::Expression>();
    expression->kind = Ast::ExpressionKind::Binary;
    expression->location = location;
    expression->binaryOperator = op;
    expression->left = std::move(left);
    expression->right = std::move(right);
    return expression;
}

} // namespace

Parser::Parser(std::vector<Token> tokens)
    : tokens_(std::move(tokens)) {
    if (tokens_.empty()) {
        tokens_.push_back(Token{TokenKind::EndOfFile, "", SourceLocation{1, 1}});
    }
}

const Token& Parser::current() const {
    return tokens_[index_];
}

const Token& Parser::peek(std::size_t offset) const {
    const std::size_t position = index_ + offset;
    if (position >= tokens_.size()) return tokens_.back();
    return tokens_[position];
}

bool Parser::check(TokenKind kind) const {
    return current().kind == kind;
}

bool Parser::match(TokenKind kind) {
    if (!check(kind)) return false;
    advance();
    return true;
}

const Token& Parser::advance() {
    const Token& token = tokens_[index_];
    if (index_ + 1 < tokens_.size()) ++index_;
    return token;
}

const Token& Parser::expect(TokenKind kind, const std::string& expectation) {
    if (check(kind)) return advance();

    if (current().kind == TokenKind::UnsupportedKeyword) {
        fail("VERILOG_UNSUPPORTED_SYNTAX",
             "不支持的 Verilog 语法 '" + current().text + "'。",
             current().location);
    }
    fail("VERILOG_SYNTAX_EXPECTED_TOKEN",
         "期望 " + expectation + "，实际遇到 " + token_display(current()) + "。",
         current().location);
}

const Token& Parser::expect_identifier(const std::string& expectation) {
    if (check(TokenKind::Identifier)) return advance();

    if (current().kind == TokenKind::UnsupportedKeyword) {
        fail("VERILOG_UNSUPPORTED_SYNTAX",
             "不支持的 Verilog 语法 '" + current().text + "'。",
             current().location);
    }
    fail("VERILOG_SYNTAX_EXPECTED_IDENTIFIER",
         "期望" + expectation + "，实际遇到 " + token_display(current()) + "。",
         current().location);
}

Ast::Module Parser::parse_module() {
    Ast::Module module;
    module.location = current().location;

    expect(TokenKind::KeywordModule, "'module'");
    const Token& name = expect_identifier("模块名");
    module.name = name.text;

    expect(TokenKind::LeftParen, "'('");
    parse_port_list(module);
    expect(TokenKind::RightParen, "')'");
    expect(TokenKind::Semicolon, "';'");

    while (!check(TokenKind::EndOfFile) &&
           !check(TokenKind::KeywordEndmodule)) {
        if (check(TokenKind::KeywordInput)) {
            parse_declaration(module, SignalKind::Input);
        } else if (check(TokenKind::KeywordOutput)) {
            parse_declaration(module, SignalKind::Output);
        } else if (check(TokenKind::KeywordWire)) {
            parse_declaration(module, SignalKind::Wire);
        } else if (check(TokenKind::KeywordAssign)) {
            module.assignments.push_back(parse_assignment());
        } else if (check(TokenKind::UnsupportedKeyword)) {
            fail("VERILOG_UNSUPPORTED_SYNTAX",
                 "不支持的 Verilog 语法 '" + current().text + "'。",
                 current().location);
        } else {
            fail("VERILOG_SYNTAX_UNEXPECTED_TOKEN",
                 "模块内部只支持 input/output/wire/assign，实际遇到 " +
                     token_display(current()) + "。",
                 current().location);
        }
    }

    expect(TokenKind::KeywordEndmodule, "'endmodule'");
    match(TokenKind::Semicolon); // 宽松接受 endmodule; 的写法

    if (!check(TokenKind::EndOfFile)) {
        if (check(TokenKind::KeywordModule)) {
            fail("VERILOG_SYNTAX_MULTIPLE_MODULES",
                 "第一阶段只支持一个模块，endmodule 之后不能再有 module。",
                 current().location);
        }
        fail("VERILOG_SYNTAX_UNEXPECTED_TOKEN",
             "endmodule 之后出现额外内容 " + token_display(current()) + "。",
             current().location);
    }

    return module;
}

void Parser::parse_port_list(Ast::Module& module) {
    if (check(TokenKind::RightParen)) return;

    bool hasDirection = false;
    PortDirection direction = PortDirection::Input;

    while (true) {
        if (match(TokenKind::KeywordInput)) {
            hasDirection = true;
            direction = PortDirection::Input;
        } else if (match(TokenKind::KeywordOutput)) {
            hasDirection = true;
            direction = PortDirection::Output;
        }

        const Token& name = expect_identifier("端口名");
        Ast::PortDeclaration declaration;
        declaration.name = name.text;
        declaration.location = name.location;
        declaration.hasDirection = hasDirection;
        declaration.direction = direction;
        module.ports.push_back(std::move(declaration));

        if (match(TokenKind::Comma)) {
            if (check(TokenKind::RightParen)) {
                fail("VERILOG_SYNTAX_UNEXPECTED_TOKEN",
                     "端口列表末尾多余的逗号。", current().location);
            }
            continue;
        }
        break;
    }
}

void Parser::parse_declaration(Ast::Module& module, SignalKind kind) {
    advance(); // input / output / wire

    while (true) {
        const Token& name = expect_identifier("信号名");
        module.declarations.push_back(
            Ast::SignalDeclaration{kind, name.text, name.location});

        if (!match(TokenKind::Comma)) break;
        if (check(TokenKind::Semicolon)) {
            fail("VERILOG_SYNTAX_UNEXPECTED_TOKEN",
                 "信号声明末尾多余的逗号。", current().location);
        }
    }

    expect(TokenKind::Semicolon, "';'");
}

Ast::Assignment Parser::parse_assignment() {
    Ast::Assignment assignment;
    assignment.location = current().location;
    advance(); // assign

    const Token& target = expect_identifier("assign 目标信号");
    assignment.target = target.text;
    assignment.targetLocation = target.location;

    expect(TokenKind::Equals, "'='");
    assignment.expression = parse_expression();
    expect(TokenKind::Semicolon, "';'");
    return assignment;
}

Ast::ExpressionPtr Parser::parse_expression() {
    return parse_or();
}

Ast::ExpressionPtr Parser::parse_or() {
    Ast::ExpressionPtr expression = parse_xor();
    while (check(TokenKind::Pipe)) {
        const SourceLocation location = current().location;
        advance();
        Ast::ExpressionPtr right = parse_xor();
        expression = make_binary(Ast::BinaryOperator::Or, std::move(expression),
                                 std::move(right), location);
    }
    return expression;
}

Ast::ExpressionPtr Parser::parse_xor() {
    Ast::ExpressionPtr expression = parse_and();
    while (check(TokenKind::Caret) || check(TokenKind::Xnor)) {
        const SourceLocation location = current().location;
        const Ast::BinaryOperator op = check(TokenKind::Caret)
                                           ? Ast::BinaryOperator::Xor
                                           : Ast::BinaryOperator::Xnor;
        advance();
        Ast::ExpressionPtr right = parse_and();
        expression = make_binary(op, std::move(expression), std::move(right),
                                 location);
    }
    return expression;
}

Ast::ExpressionPtr Parser::parse_and() {
    Ast::ExpressionPtr expression = parse_unary();
    while (check(TokenKind::Ampersand)) {
        const SourceLocation location = current().location;
        advance();
        Ast::ExpressionPtr right = parse_unary();
        expression = make_binary(Ast::BinaryOperator::And, std::move(expression),
                                 std::move(right), location);
    }
    return expression;
}

Ast::ExpressionPtr Parser::parse_unary() {
    if (check(TokenKind::Tilde)) {
        const SourceLocation location = current().location;
        advance();
        return make_not(parse_unary(), location);
    }
    return parse_primary();
}

Ast::ExpressionPtr Parser::parse_primary() {
    if (check(TokenKind::Identifier)) {
        return make_identifier(advance());
    }
    if (check(TokenKind::Number)) {
        return make_constant(advance());
    }
    if (check(TokenKind::LeftParen)) {
        advance();
        Ast::ExpressionPtr expression = parse_expression();
        expect(TokenKind::RightParen, "')'");
        return expression;
    }

    if (check(TokenKind::UnsupportedKeyword)) {
        fail("VERILOG_UNSUPPORTED_SYNTAX",
             "不支持的 Verilog 语法 '" + current().text + "'。",
             current().location);
    }
    fail("VERILOG_SYNTAX_EXPECTED_EXPRESSION",
         "期望表达式，实际遇到 " + token_display(current()) + "。",
         current().location);
}

void Parser::fail(const std::string& code, const std::string& message,
                  SourceLocation location) const {
    throw DiagnosticError(
        make_error(DiagnosticKind::Syntax, code, message, location));
}

} // namespace Verilator

#include "Lexer.hpp"

#include "DiagnosticError.hpp"
#include "../include/VerilogKeywords.hpp"

#include <cctype>
#include <string>
#include <unordered_map>
#include <utility>

namespace Verilator {
namespace {

bool is_identifier_start(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return uc == '_' || std::isalpha(uc) != 0;
}

bool is_identifier_continue(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return uc == '_' || uc == '$' || std::isalnum(uc) != 0;
}

const std::unordered_map<std::string, TokenKind>& keyword_table() {
    static const std::unordered_map<std::string, TokenKind> table = {
        {std::string(kKeywordModule), TokenKind::KeywordModule},
        {std::string(kKeywordEndmodule), TokenKind::KeywordEndmodule},
        {std::string(kKeywordInput), TokenKind::KeywordInput},
        {std::string(kKeywordOutput), TokenKind::KeywordOutput},
        {std::string(kKeywordWire), TokenKind::KeywordWire},
        {std::string(kKeywordAssign), TokenKind::KeywordAssign},
    };
    return table;
}

} // namespace

Lexer::Lexer(std::string source)
    : source_(std::move(source)) {}

char Lexer::peek(std::size_t offset) const {
    const std::size_t position = index_ + offset;
    if (position >= source_.size()) return '\0';
    return source_[position];
}

bool Lexer::at_end() const {
    return index_ >= source_.size();
}

void Lexer::advance() {
    if (at_end()) return;
    const char c = source_[index_++];
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
}

void Lexer::skip_trivia() {
    while (!at_end()) {
        const char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
            c == '\v') {
            advance();
            continue;
        }
        if (c == '/' && peek(1) == '/') {
            advance();
            advance();
            while (!at_end() && peek() != '\n') advance();
            continue;
        }
        if (c == '/' && peek(1) == '*') {
            const SourceLocation location{line_, column_};
            advance();
            advance();
            bool closed = false;
            while (!at_end()) {
                if (peek() == '*' && peek(1) == '/') {
                    advance();
                    advance();
                    closed = true;
                    break;
                }
                advance();
            }
            if (!closed) {
                fail("VERILOG_LEX_UNTERMINATED_COMMENT",
                     "块注释没有闭合，缺少 '*/'。", location);
            }
            continue;
        }
        break;
    }
}

Token Lexer::read_identifier() {
    const SourceLocation location{line_, column_};
    const std::size_t start = index_;
    advance();
    while (!at_end() && is_identifier_continue(peek())) advance();

    const std::string text = source_.substr(start, index_ - start);
    const auto keyword = keyword_table().find(text);
    if (keyword != keyword_table().end()) {
        return Token{keyword->second, text, location};
    }
    if (is_unsupported_verilog_keyword(text)) {
        return Token{TokenKind::UnsupportedKeyword, text, location};
    }
    return Token{TokenKind::Identifier, text, location};
}

Token Lexer::read_number() {
    const SourceLocation location{line_, column_};
    const std::size_t start = index_;
    while (!at_end() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
        advance();
    }
    const std::string widthText = source_.substr(start, index_ - start);

    if (peek() != '\'') {
        fail("VERILOG_LEX_UNSUPPORTED_NUMBER",
             "只支持单比特常量 1'b0 和 1'b1，不支持 '" + widthText +
                 "' 这样的多位或十进制常量。",
             location);
    }
    advance(); // '
    if (peek() != 'b' && peek() != 'B') {
        fail("VERILOG_LEX_UNSUPPORTED_NUMBER",
             "只支持二进制常量 1'b0 和 1'b1。", location);
    }
    advance(); // b

    if (peek() != '0' && peek() != '1') {
        fail("VERILOG_LEX_UNSUPPORTED_NUMBER",
             "只支持常量 1'b0 和 1'b1，不支持 x、z 或其他位值。", location);
    }
    const char bit = peek();
    advance();

    if (!at_end() && (std::isalnum(static_cast<unsigned char>(peek())) != 0 ||
                      peek() == '_')) {
        fail("VERILOG_LEX_UNSUPPORTED_NUMBER",
             "只支持单比特常量 1'b0 和 1'b1。", location);
    }

    int width = 0;
    for (const char c : widthText) {
        width = width * 10 + (c - '0');
        if (width > 1000000) {
            fail("VERILOG_LEX_UNSUPPORTED_NUMBER",
                 "只支持单比特常量 1'b0 和 1'b1。", location);
        }
    }
    if (width != 1) {
        fail("VERILOG_LEX_UNSUPPORTED_NUMBER",
             "只支持 1 位常量，当前宽度为 " + widthText + "。", location);
    }

    return Token{TokenKind::Number, std::string(1, bit), location};
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (true) {
        skip_trivia();
        if (at_end()) break;

        const SourceLocation location{line_, column_};
        const char c = peek();

        if (is_identifier_start(c)) {
            tokens.push_back(read_identifier());
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            tokens.push_back(read_number());
            continue;
        }

        switch (c) {
            case '~':
                if (peek(1) == '^') {
                    advance();
                    advance();
                    tokens.push_back(Token{TokenKind::Xnor, "~^", location});
                } else {
                    advance();
                    tokens.push_back(Token{TokenKind::Tilde, "~", location});
                }
                break;
            case '^':
                if (peek(1) == '~') {
                    advance();
                    advance();
                    tokens.push_back(Token{TokenKind::Xnor, "^~", location});
                } else {
                    advance();
                    tokens.push_back(Token{TokenKind::Caret, "^", location});
                }
                break;
            case '&':
                advance();
                tokens.push_back(Token{TokenKind::Ampersand, "&", location});
                break;
            case '|':
                advance();
                tokens.push_back(Token{TokenKind::Pipe, "|", location});
                break;
            case '(':
                advance();
                tokens.push_back(Token{TokenKind::LeftParen, "(", location});
                break;
            case ')':
                advance();
                tokens.push_back(Token{TokenKind::RightParen, ")", location});
                break;
            case ',':
                advance();
                tokens.push_back(Token{TokenKind::Comma, ",", location});
                break;
            case ';':
                advance();
                tokens.push_back(Token{TokenKind::Semicolon, ";", location});
                break;
            case '=':
                advance();
                tokens.push_back(Token{TokenKind::Equals, "=", location});
                break;
            case '[':
            case ']':
            case ':':
                fail("VERILOG_LEX_UNSUPPORTED_VECTOR",
                     "第一阶段不支持多位向量、位选和部分选择，遇到字符 '" +
                         std::string(1, c) + "'。",
                     location);
            default:
                fail("VERILOG_LEX_UNEXPECTED_CHARACTER",
                     "无法识别的字符 '" + std::string(1, c) + "'。", location);
        }
    }

    tokens.push_back(Token{TokenKind::EndOfFile, "", SourceLocation{line_, column_}});
    return tokens;
}

void Lexer::fail(const std::string& code, const std::string& message,
                 SourceLocation location) const {
    throw DiagnosticError(
        make_error(DiagnosticKind::Lexical, code, message, location));
}

} // namespace Verilator

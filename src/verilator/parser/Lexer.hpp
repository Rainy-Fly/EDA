#pragma once

#include "Token.hpp"

#include <string>
#include <vector>

namespace Verilator {

class Lexer {
public:
    explicit Lexer(std::string source);

    // 返回以 EndOfFile 结尾的记号序列。遇到词法错误时抛出 DiagnosticError。
    std::vector<Token> tokenize();

private:
    char peek(std::size_t offset = 0) const;
    bool at_end() const;
    void advance();
    void skip_trivia();

    Token read_identifier();
    Token read_number();

    [[noreturn]] void fail(const std::string& code, const std::string& message,
                           SourceLocation location) const;

    std::string source_;
    std::size_t index_ = 0;
    int line_ = 1;
    int column_ = 1;
};

} // namespace Verilator

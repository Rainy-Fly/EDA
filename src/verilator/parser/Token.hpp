#pragma once

#include "../include/VerilatorTypes.hpp"

#include <string>

namespace Verilator {

enum class TokenKind {
    Identifier,
    Number,

    KeywordModule,
    KeywordEndmodule,
    KeywordInput,
    KeywordOutput,
    KeywordWire,
    KeywordAssign,
    KeywordAlways,
    KeywordReg,
    KeywordPosedge,

    UnsupportedKeyword,

    Tilde,        // ~
    Ampersand,    // &
    Pipe,         // |
    Caret,        // ^
    Xnor,         // ~^ 或 ^~

    LeftParen,
    RightParen,
    Comma,
    Semicolon,
    Equals,
    LessEqual,    // <=（非阻塞赋值）
    At,           // @
    EndOfFile
};

struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    std::string text;
    SourceLocation location;
};

inline const char* token_kind_name(TokenKind kind) {
    switch (kind) {
        case TokenKind::Identifier:         return "标识符";
        case TokenKind::Number:             return "常量";
        case TokenKind::KeywordModule:      return "module";
        case TokenKind::KeywordEndmodule:   return "endmodule";
        case TokenKind::KeywordInput:       return "input";
        case TokenKind::KeywordOutput:      return "output";
        case TokenKind::KeywordWire:        return "wire";
        case TokenKind::KeywordAssign:      return "assign";
        case TokenKind::KeywordAlways:      return "always";
        case TokenKind::KeywordReg:         return "reg";
        case TokenKind::KeywordPosedge:     return "posedge";
        case TokenKind::UnsupportedKeyword: return "不支持的关键字";
        case TokenKind::Tilde:              return "~";
        case TokenKind::Ampersand:          return "&";
        case TokenKind::Pipe:               return "|";
        case TokenKind::Caret:              return "^";
        case TokenKind::Xnor:               return "~^/^~";
        case TokenKind::LeftParen:          return "(";
        case TokenKind::RightParen:         return ")";
        case TokenKind::Comma:              return ",";
        case TokenKind::Semicolon:          return ";";
        case TokenKind::Equals:             return "=";
        case TokenKind::LessEqual:          return "<=";
        case TokenKind::At:                 return "@";
        case TokenKind::EndOfFile:          return "文件结束";
    }
    return "未知记号";
}

inline std::string token_display(const Token& token) {
    if (!token.text.empty()) return "'" + token.text + "'";
    return token_kind_name(token.kind);
}

} // namespace Verilator

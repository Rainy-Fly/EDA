#pragma once

#include <string_view>

namespace Verilator {

// 当前 Verilog 子集识别并保留的关键字。Lexer 和模型校验共享这份定义，
// 避免两处各自维护关键字列表而产生不一致。
inline constexpr std::string_view kKeywordModule = "module";
inline constexpr std::string_view kKeywordEndmodule = "endmodule";
inline constexpr std::string_view kKeywordInput = "input";
inline constexpr std::string_view kKeywordOutput = "output";
inline constexpr std::string_view kKeywordWire = "wire";
inline constexpr std::string_view kKeywordAssign = "assign";

inline constexpr std::string_view kSupportedVerilogKeywords[] = {
    kKeywordModule, kKeywordEndmodule, kKeywordInput,
    kKeywordOutput, kKeywordWire,     kKeywordAssign,
};

inline constexpr std::string_view kUnsupportedVerilogKeywords[] = {
    "always",      "initial",    "begin",      "end",
    "reg",         "logic",      "if",         "else",
    "case",        "casex",      "casez",      "endcase",
    "default",     "for",        "while",      "repeat",
    "forever",     "function",   "endfunction", "task",
    "endtask",     "parameter",  "localparam", "generate",
    "endgenerate", "inout",      "tri",        "integer",
    "real",        "time",       "signed",     "unsigned",
    "posedge",     "negedge",    "and",        "or",
    "not",         "xor",        "xnor",       "nand",
    "nor",         "buf",        "bufif0",     "bufif1",
    "notif0",      "notif1",     "pmos",       "nmos",
    "cmos",        "tran",       "supply0",    "supply1",
    "specify",     "endspecify", "primitive",  "endprimitive",
    "genvar",      "fork",       "join",       "wait",
    "disable",     "event",
};

inline bool is_supported_verilog_keyword(std::string_view name) {
    for (const std::string_view keyword : kSupportedVerilogKeywords) {
        if (name == keyword) return true;
    }
    return false;
}

inline bool is_unsupported_verilog_keyword(std::string_view name) {
    for (const std::string_view keyword : kUnsupportedVerilogKeywords) {
        if (name == keyword) return true;
    }
    return false;
}

inline bool is_verilog_keyword(std::string_view name) {
    return is_supported_verilog_keyword(name) ||
           is_unsupported_verilog_keyword(name);
}

} // namespace Verilator

#pragma once

#include "../model/CircuitModel.hpp"

#include <string>

namespace Verilator {

// 调用方必须先通过 validate_module。公共入口 generate_verilog 会先做校验。
std::string generate_verilog_source(const Module& module);

} // namespace Verilator

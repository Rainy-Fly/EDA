#pragma once

#include "CircuitModel.hpp"

#include <vector>

namespace Verilator {

// 只检查逻辑电路模型本身是否合法，不依赖 Verilog 源码。
std::vector<Diagnostic> validate_module(const Module& module);

} // namespace Verilator

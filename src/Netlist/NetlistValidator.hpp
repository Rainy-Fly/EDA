#pragma once

#include "Netlist.hpp"

#include <vector>

namespace NetlistData {

// Checks structural integrity only. Simulation- or synthesis-specific rules
// (such as driver counts, floating inputs, or combinational loops) belong to
// capability-aware validators and must not be imposed here.
std::vector<Diagnostic> validate(const Netlist& netlist);

} // namespace NetlistData

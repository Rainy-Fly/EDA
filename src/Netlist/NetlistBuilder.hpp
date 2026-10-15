#pragma once

#include "LibrarySnapshot.hpp"
#include "Netlist.hpp"
#include "ProjectModelSnapshot.hpp"

#include <optional>
#include <string>
#include <vector>

namespace NetlistData {

struct NetlistBuildOptions {
    std::string generatorVersion = "1.0.0";
};

struct NetlistBuildResult {
    std::optional<Netlist> value;
    std::vector<Diagnostic> diagnostics;

    bool ok() const;
};

class NetlistBuilder {
public:
    // Consumes owning snapshots only; does not parse XML, access UI state, or mutate inputs.
    static NetlistBuildResult build(const ProjectModelSnapshot& project,
                                    const LibrarySnapshot& libraries,
                                    const NetlistBuildOptions& options = {});
};

} // namespace NetlistData

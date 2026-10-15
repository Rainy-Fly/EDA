#pragma once

#include "Netlist.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace NetlistData {

// A pin width in the library is either a positive constant or the name of an
// integer parameter whose effective value is resolved for this instance.
using PinWidth = std::variant<unsigned int, std::string>;

struct LibraryPinDefinition {
    std::string id;
    std::string name;
    std::string direction; // input, output, inout
    PinWidth width = 1u;
    int x = 0;
    int y = 0;
};

struct LibraryComponentDefinition {
    std::string libraryId;
    std::string type;
    std::string name;
    std::string category;
    Json::Value parameterSchema = Json::objectValue;
    std::vector<LibraryPinDefinition> pins;
    Json::Value synthModel = Json::objectValue;
    // Optional, schema-versioned behavioral model. Kept opaque in this layer.
    Json::Value simulationModel;
};

// Owning snapshot of exactly the library versions declared by the project.
struct LibrarySnapshot {
    std::vector<LibraryReference> libraries;
    std::vector<LibraryComponentDefinition> components;
};

} // namespace NetlistData

#pragma once

#include "Netlist.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace NetlistData {

struct Point {
    int x = 0;
    int y = 0;
};

struct ProjectPort {
    std::string id;
    std::string name;
    std::string direction; // in, out, inout
    unsigned int width = 1;
    Point position;
};

struct ComponentPinEndpoint {
    std::string componentId;
    std::string pinId;
};
struct PortEndpoint {
    std::string portId;
};
struct JunctionEndpoint {
    std::string junctionId;
};
struct FreeEndpoint {
    Point position;
};
using WireEndpoint = std::variant<ComponentPinEndpoint, PortEndpoint, JunctionEndpoint, FreeEndpoint>;

struct ProjectComponent {
    std::string id;
    std::string libraryId; // Filled by the project/library resolver; not inferred by the builder.
    std::string type;
    std::string displayName;
    Point anchor;
    int rotation = 0;
    Json::Value parameterOverrides = Json::objectValue; // Values are already parsed into JSON scalar types.
};

struct ProjectJunction {
    std::string id;
    Point position;
};

struct ProjectWire {
    std::string id;
    WireEndpoint start;
    WireEndpoint end;
    std::vector<Point> vias; // Layout only; never used to infer connectivity.
};

// Immutable-by-convention, owning DTO exported by the XML/project module.
// The digest must be computed using the same canonical XML 2.0 representation
// for disk-loaded and in-memory snapshots.
struct ProjectModelSnapshot {
    std::string schemaVersion;
    std::string projectId;
    std::string xmlDigest;
    std::vector<LibraryReference> libraries;
    std::vector<ProjectPort> ports;
    std::vector<ProjectComponent> components;
    std::vector<ProjectJunction> junctions;
    std::vector<ProjectWire> wires;
    bool topologyVerified = false;
};

} // namespace NetlistData

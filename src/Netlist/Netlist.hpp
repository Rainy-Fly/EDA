#pragma once

#include <json/json.h>

#include <string>
#include <vector>

namespace NetlistData {

struct Diagnostic {
    std::string severity;
    std::string code;
    std::string message;
    std::vector<std::string> objectIds;
    std::string pinId;
};

struct LibraryReference {
    std::string id;
    std::string version;
};

struct NetlistSource {
    std::string projectId;
    std::string xmlDigest;
    std::vector<LibraryReference> libraries;
    std::string generatorVersion;
};

struct Port {
    std::string id;
    std::string name;
    std::string direction;
    unsigned int width = 1;
};

struct InstancePin {
    std::string id;
    std::string direction;
    unsigned int width = 1;
};

struct Instance {
    std::string id;
    std::string type;
    Json::Value parameters = Json::objectValue;
    // Keep the complete model object so versioned simulation model extensions
    // survive a read/write cycle without being interpreted by the JSON layer.
    Json::Value model = Json::objectValue;
    std::vector<InstancePin> pins;
};

struct Terminal {
    std::string ownerKind;
    std::string ownerId;
    std::string pinId;
};

struct Net {
    std::string id;
    unsigned int width = 1;
    std::vector<Terminal> terminals;
};

struct Netlist {
    std::string schemaVersion = "1.0";
    NetlistSource source;
    std::vector<Port> ports;
    std::vector<Instance> instances;
    std::vector<Net> nets;
    std::vector<Diagnostic> diagnostics;
};

} // namespace NetlistData

#include "../src/Netlist/NetlistJson.hpp"
#include "../src/Netlist/NetlistBuilder.hpp"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << '\n';
        ++failures;
    }
}

const char* valid_json = R"json({
  "schemaVersion": "1.0",
  "source": {
    "projectId": "project-1",
    "xmlDigest": "sha256:abc",
    "libraries": [{"id": "tinyeda.basic", "version": "1.0.0"}],
    "generatorVersion": "1.0.0"
  },
  "ports": [{"id": "A", "name": "A", "direction": "in", "width": 1}],
  "instances": [{
    "id": "u1",
    "type": "NOT",
    "parameters": {},
    "model": {"kind": "primitive", "operator": "not"},
    "pins": [
      {"id": "A", "direction": "input", "width": 1},
      {"id": "Y", "direction": "output", "width": 1}
    ]
  }],
  "nets": [
    {"id": "n0", "width": 1, "terminals": [
      {"ownerKind": "port", "ownerId": "A"},
      {"ownerKind": "instance", "ownerId": "u1", "pinId": "A"}
    ]},
    {"id": "n1", "width": 1, "terminals": [
      {"ownerKind": "instance", "ownerId": "u1", "pinId": "Y"}
    ]}
  ],
  "diagnostics": []
})json";

bool contains_code(const NetlistData::ReadResult& result, const std::string& code) {
    for (const auto& diagnostic : result.diagnostics)
        if (diagnostic.code == code) return true;
    return false;
}

bool contains_code(const std::vector<NetlistData::Diagnostic>& diagnostics, const std::string& code) {
  for (const auto& diagnostic : diagnostics)
    if (diagnostic.code == code) return true;
  return false;
}

std::pair<NetlistData::ProjectModelSnapshot, NetlistData::LibrarySnapshot> builder_fixture() {
  using namespace NetlistData;
  ProjectModelSnapshot project;
  project.schemaVersion = "2.0";
  project.projectId = "project-1";
  project.xmlDigest = "sha256:fixture";
  project.topologyVerified = true;
  project.libraries.push_back({"tinyeda.basic", "1.0.0"});
  project.ports = {
    {"A", "A", "in", 1, {0, 0}},
    {"F", "F", "out", 1, {100, 0}}
  };
  project.components.push_back({"u1", "tinyeda.basic", "NOT", "Inverter", {40, 0}, 0, Json::objectValue});
  project.junctions.push_back({"j1", {70, 0}});
  project.wires = {
    {"w1", PortEndpoint{"A"}, ComponentPinEndpoint{"u1", "A"}, {{20, 20}}},
    {"w2", ComponentPinEndpoint{"u1", "Y"}, JunctionEndpoint{"j1"}, {{55, 20}}},
    {"w3", JunctionEndpoint{"j1"}, PortEndpoint{"F"}, {{85, 20}}}
  };

  LibrarySnapshot libraries;
  libraries.libraries.push_back({"tinyeda.basic", "1.0.0"});
  LibraryComponentDefinition inverter;
  inverter.libraryId = "tinyeda.basic";
  inverter.type = "NOT";
  inverter.name = "NOT";
  inverter.parameterSchema = Json::objectValue;
  inverter.pins = {
    {"A", "A", "input", 1u, -30, 0},
    {"Y", "Y", "output", 1u, 30, 0}
  };
  inverter.synthModel = Json::Value(Json::objectValue);
  inverter.synthModel["kind"] = "primitive";
  inverter.synthModel["operator"] = "not";
  libraries.components.push_back(std::move(inverter));
  return {std::move(project), std::move(libraries)};
}

void test_builder() {
  using namespace NetlistData;
  auto [project, libraries] = builder_fixture();
  auto built = NetlistBuilder::build(project, libraries);
  check(built.ok(), "XML 2.0 snapshot converts to a netlist");
  if (built.ok()) {
    check(built.value->instances.size() == 1, "builder emits resolved component model");
    check(built.value->nets.size() == 2, "junction branch produces one shared net");
    check(built.value->nets[0].terminals.size() == 2 && built.value->nets[1].terminals.size() == 2,
        "component and top-level endpoints form expected nets");
    check(built.value->nets[0].id == "n0" && built.value->nets[1].id == "n1",
        "generated net ids are stable");
  }

  ProjectModelSnapshot crossing;
  crossing.schemaVersion = "2.0";
  crossing.xmlDigest = "sha256:crossing";
  crossing.topologyVerified = true;
  crossing.ports = {
    {"A", "A", "in", 1, {0, 0}}, {"F", "F", "out", 1, {100, 0}},
    {"B", "B", "in", 1, {0, 100}}, {"G", "G", "out", 1, {100, 100}}
  };
  crossing.wires = {
    {"wa", PortEndpoint{"A"}, PortEndpoint{"F"}, {{50, 50}}},
    {"wb", PortEndpoint{"B"}, PortEndpoint{"G"}, {{50, 50}}}
  };
  auto crossingResult = NetlistBuilder::build(crossing, LibrarySnapshot{});
  check(crossingResult.ok() && crossingResult.value->nets.size() == 2,
      "geometric crossings/via overlap do not merge separate nets");

  auto invalid = builder_fixture();
  invalid.first.wires[0].start = PortEndpoint{"missing"};
  auto invalidResult = NetlistBuilder::build(invalid.first, invalid.second);
  check(!invalidResult.ok() && contains_code(invalidResult.diagnostics, "NETLIST_BUILD_PORT_REFERENCE_INVALID"),
      "invalid XML endpoint reference is diagnosed");

  ProjectModelSnapshot freeWire;
  freeWire.schemaVersion = "2.0";
  freeWire.xmlDigest = "sha256:free";
  freeWire.topologyVerified = true;
  freeWire.ports.push_back({"A", "A", "in", 1, {0, 0}});
  freeWire.wires.push_back({"wf", PortEndpoint{"A"}, FreeEndpoint{{50, 0}}, {}});
  auto freeResult = NetlistBuilder::build(freeWire, LibrarySnapshot{});
  check(freeResult.ok() && freeResult.value->nets.size() == 1 &&
        contains_code(freeResult.diagnostics, "NETLIST_BUILD_FREE_ENDPOINT"),
      "free wire end is diagnosed without inventing a connection");

  ProjectModelSnapshot parameterized;
  parameterized.schemaVersion = "2.0";
  parameterized.xmlDigest = "sha256:parameterized";
  parameterized.topologyVerified = true;
  parameterized.libraries.push_back({"tinyeda.basic", "1.0.0"});
  ProjectComponent gate{"g1", "tinyeda.basic", "AND", "AND gate", {0, 0}, 0, Json::objectValue};
  gate.parameterOverrides["DataBits"] = 4;
  parameterized.components.push_back(std::move(gate));
  LibrarySnapshot parameterizedLibraries;
  parameterizedLibraries.libraries.push_back({"tinyeda.basic", "1.0.0"});
  LibraryComponentDefinition andGate;
  andGate.libraryId = "tinyeda.basic";
  andGate.type = "AND";
  andGate.parameterSchema["DataBits"]["type"] = "integer";
  andGate.parameterSchema["DataBits"]["default"] = 1;
  andGate.parameterSchema["DataBits"]["min"] = 1;
  andGate.pins = {
    {"A", "A", "input", std::string("DataBits"), -30, -10},
    {"B", "B", "input", std::string("DataBits"), -30, 10},
    {"Y", "Y", "output", std::string("DataBits"), 30, 0}
  };
  andGate.synthModel["kind"] = "primitive";
  andGate.synthModel["operator"] = "and";
  parameterizedLibraries.components.push_back(std::move(andGate));
  auto parameterizedResult = NetlistBuilder::build(parameterized, parameterizedLibraries);
  check(parameterizedResult.ok() && parameterizedResult.value->nets.size() == 3 &&
        parameterizedResult.value->nets[0].width == 4,
      "builder resolves parameterized pin widths without imposing synthesis width limits");

  parameterized.components[0].parameterOverrides["DataBits"] = 0;
  auto invalidParameter = NetlistBuilder::build(parameterized, parameterizedLibraries);
  check(!invalidParameter.ok() &&
        contains_code(invalidParameter.diagnostics, "NETLIST_BUILD_PARAMETER_OUT_OF_RANGE"),
      "parameter constraints are checked before netlist generation");
}

} // namespace

int main() {
    using NetlistData::NetlistJson;

    auto parsed = NetlistJson::parse(valid_json);
    check(parsed.ok(), "valid standalone netlist parses");
    if (parsed.ok()) {
        std::vector<NetlistData::Diagnostic> diagnostics;
        const std::string serialized = NetlistJson::serialize(*parsed.value, &diagnostics);
        check(diagnostics.empty(), "valid netlist serializes");
        auto reparsed = NetlistJson::parse(serialized);
        check(reparsed.ok(), "serialized netlist reparses");
        check(serialized == NetlistJson::serialize(*reparsed.value), "serialization is deterministic");
    }

    auto unknown_version = NetlistJson::parse(R"({"schemaVersion":"2.0"})");
    check(!unknown_version.ok() && contains_code(unknown_version, "NETLIST_SCHEMA_UNSUPPORTED"),
          "unknown schema version is rejected");

    std::string unknown_field(valid_json);
    const auto insertion = unknown_field.find("\"schemaVersion\": \"1.0\"");
    unknown_field.insert(insertion, "\"futureField\": true,\n  ");
    auto unknown = NetlistJson::parse(unknown_field);
    check(!unknown.ok() && contains_code(unknown, "NETLIST_UNKNOWN_FIELD"),
          "unknown top-level field is not silently discarded");

    auto duplicateTerminalModel = NetlistJson::parse(valid_json);
    std::vector<NetlistData::Diagnostic> duplicateDiagnostics;
    if (duplicateTerminalModel.ok()) {
      duplicateTerminalModel.value->nets.push_back(duplicateTerminalModel.value->nets.back());
      NetlistJson::serialize(*duplicateTerminalModel.value, &duplicateDiagnostics);
    }
    bool duplicateRejected = false;
    for (const auto& diagnostic : duplicateDiagnostics)
      duplicateRejected |= diagnostic.code == "NETLIST_TERMINAL_MULTIPLE_NETS";
    check(duplicateRejected, "terminal cannot belong to multiple nets");

    std::string unknown_operator(valid_json);
    const auto op = unknown_operator.find("\"operator\": \"not\"");
    unknown_operator.replace(op, std::string("\"operator\": \"not\"").size(), "\"operator\": \"magic\"");
    auto model = NetlistJson::parse(unknown_operator);
    check(!model.ok() && contains_code(model, "NETLIST_PRIMITIVE_OPERATOR_UNKNOWN"),
          "unknown primitive operator is rejected");

        std::string extension_model(valid_json);
        const auto kind = extension_model.find("\"kind\": \"primitive\"");
        extension_model.replace(kind, std::string("\"kind\": \"primitive\"").size(),
                "\"kind\": \"simulation-model-v1\"");
        auto extension = NetlistJson::parse(extension_model);
        bool extensionWarned = false;
        for (const auto& diagnostic : extension.diagnostics)
        extensionWarned |= diagnostic.code == "NETLIST_MODEL_KIND_UNVERIFIED" && diagnostic.severity == "warning";
        check(extension.ok() && extensionWarned,
          "unknown simulation model is preserved with a runtime-support warning");

            test_builder();

            if (failures == 0) std::cout << "Netlist JSON and builder self-test passed.\n";
    return failures == 0 ? 0 : 1;
}

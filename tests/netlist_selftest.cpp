#include "../src/Netlist/NetlistJson.hpp"
#include "../src/Netlist/NetlistBuilder.hpp"
#include "../src/Netlist/PcbNetlistBuilder.hpp"
#include "../src/Netlist/KiCadNetlistExporter.hpp"

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

NetlistData::PcbProjectSnapshot pcb_fixture() {
  using namespace NetlistData;
  PcbProjectSnapshot project;
  project.projectName = "pcb-demo";

  auto component = [&](std::string id, std::string type, std::string reference,
                       std::string prefix, std::string value, std::string footprint,
                       int x, int y, std::vector<std::pair<std::string, std::string>> pins) {
    PcbSnapshotComponent result;
    result.instanceId = std::move(id);
    result.type = std::move(type);
    result.reference = std::move(reference);
    result.referencePrefix = std::move(prefix);
    result.value = std::move(value);
    result.footprint = std::move(footprint);
    for (std::size_t i = 0; i < pins.size(); ++i) {
      PcbSnapshotPin pin;
      pin.id = pins[i].first;
      pin.name = pins[i].first;
      pin.number = pins[i].second;
      pin.position = PcbPoint{x + (i == 0 ? -50 : 50), y};
      result.pins.push_back(std::move(pin));
    }
    project.components.push_back(std::move(result));
  };

  component("10", "Resistor", "R1", "R", "10k",
            "Resistor_SMD:R_0603_1608Metric", 0, 0, {{"A", "1"}, {"B", "2"}});
  component("20", "Resistor", "R2", "R", "1k",
            "Resistor_SMD:R_0603_1608Metric", 200, 0, {{"A", "1"}, {"B", "2"}});
  component("30", "Connector", "J1", "J", "Conn_01x02",
            "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
            100, 200, {{"A", "1"}, {"B", "2"}});

  project.wires.push_back({"w1", {{50, 0}, {150, 0}}, false});
  project.wires.push_back({"w2", {{250, 0}, {250, 200}}, false});
  project.wires.push_back({"w3", {{250, 200}, {50, 200}}, false});
  return project;
}

NetlistData::PcbSnapshotComponent pcb_single_pin(const std::string& id,
                                                 const std::string& reference,
                                                 int x, int y) {
  NetlistData::PcbSnapshotComponent component;
  component.instanceId = id;
  component.type = "TestPart";
  component.reference = reference;
  component.value = "1";
  component.footprint = "Test:Footprint";
  NetlistData::PcbSnapshotPin pin;
  pin.id = "1";
  pin.name = "1";
  pin.number = "1";
  pin.position = NetlistData::PcbPoint{x, y};
  component.pins.push_back(std::move(pin));
  return component;
}

void test_pcb_builder_and_exporter() {
  using namespace NetlistData;
  auto project = pcb_fixture();
  auto built = PcbNetlistBuilder::build(project);
  check(built.ok(), "PCB builder accepts a valid project snapshot");
  if (!built.ok()) return;

  check(built.value->components.size() == 3, "PCB netlist keeps physical components");
  check(built.value->components[0].reference == "J1" &&
        built.value->components[1].reference == "R1" &&
        built.value->components[2].reference == "R2",
        "PCB components are emitted in deterministic reference order");
  check(built.value->nets.size() == 2, "wires form the expected PCB nets");
  check(built.value->nets[0].name == "Net-(J1-Pad1)" &&
        built.value->nets[1].name == "Net-(R1-Pad2)",
        "unnamed nets use KiCad Net-(<ref>-Pad<n>) names");

  bool mapped = false;
  for (const auto& pin : built.value->components[1].pins)
    mapped |= pin.id == "B" && pin.number == "2";
  check(mapped, "logic pin id maps to the physical pad number");
  check(contains_code(built.diagnostics, "PCB_NETLIST_UNCONNECTED_PIN"),
        "unconnected pin is reported instead of silently dropped");

  KiCadNetlistExportOptions options;
  options.date = "2026-10-22";
  options.sourcePath = "/tmp/pcb-demo.xml";
  std::vector<Diagnostic> diagnostics;
  const std::string text = KiCadNetlistExporter::serialize(*built.value, options, &diagnostics);
  check(!text.empty() && diagnostics.empty(), "valid PCB netlist serializes without diagnostics");
  check(text.rfind("(export (version \"E\")", 0) == 0, "KiCad export header/version is emitted");
  check(text.find("(ref \"R1\")") != std::string::npos, "component reference is emitted");
  check(text.find("(footprint \"Resistor_SMD:R_0603_1608Metric\")") != std::string::npos,
        "component footprint is emitted");
  check(text.find("(node (ref \"R1\") (pin \"2\") (pintype \"passive\"))") != std::string::npos,
        "net node uses reference and physical pad number");

  // 无仿真模型：PCB 快照/构建器没有 model 概念，电阻和连接器仍被保留。
  check(built.value->components[1].type == "Resistor", "components without models are exported");
}

void test_pcb_reference_assignment() {
  using namespace NetlistData;
  auto project = pcb_fixture();
  for (auto& component : project.components) component.reference.clear();
  auto built = PcbNetlistBuilder::build(project);
  check(built.ok(), "auto reference assignment succeeds with prefixes");
  if (!built.ok()) return;
  check(built.value->components[0].reference == "J1" &&
        built.value->components[1].reference == "R1" &&
        built.value->components[2].reference == "R2",
        "references are assigned by instance id order per prefix");

  auto mixed = pcb_fixture();
  for (auto& component : mixed.components) component.reference.clear();
  mixed.components[0].reference = "R1";
  auto mixedResult = PcbNetlistBuilder::build(mixed);
  check(mixedResult.ok() && mixedResult.value->components[2].reference == "R2",
        "auto numbering skips an explicitly assigned reference");
}

void test_pcb_connectivity_rules() {
  using namespace NetlistData;
  PcbProjectSnapshot crossing;
  crossing.projectName = "crossing";
  crossing.components = {
      pcb_single_pin("1", "A1", 0, 0), pcb_single_pin("2", "A2", 100, 100),
      pcb_single_pin("3", "B1", 0, 100), pcb_single_pin("4", "B2", 100, 0)};
  crossing.wires = {
      {"wa", {{0, 0}, {100, 100}}, false},
      {"wb", {{0, 100}, {100, 0}}, false}};
  auto crossingResult = PcbNetlistBuilder::build(crossing);
  check(crossingResult.ok() && crossingResult.value->nets.size() == 2,
        "crossing wires do not merge separate PCB nets");

  PcbProjectSnapshot touching;
  touching.projectName = "touching";
  touching.components = {
      pcb_single_pin("1", "A1", 0, 0), pcb_single_pin("2", "A2", 100, 0),
      pcb_single_pin("3", "A3", 50, 0)};
  touching.wires = {{"w", {{0, 0}, {100, 0}}, false}};
  auto touchingResult = PcbNetlistBuilder::build(touching);
  check(touchingResult.ok() && touchingResult.value->nets.size() == 1 &&
        touchingResult.value->nets[0].nodes.size() == 2 &&
        contains_code(touchingResult.diagnostics, "PCB_NETLIST_PIN_ON_WIRE_SEGMENT"),
        "pin on a wire segment is diagnosed but not connected");

  PcbProjectSnapshot bus;
  bus.components = {pcb_single_pin("1", "A1", 0, 0), pcb_single_pin("2", "A2", 100, 0)};
  bus.wires = {{"bus1", {{0, 0}, {100, 0}}, true}};
  auto busResult = PcbNetlistBuilder::build(bus);
  check(busResult.ok() && contains_code(busResult.diagnostics, "PCB_NETLIST_BUS_NOT_SUPPORTED"),
        "bus objects are excluded with a diagnostic");
}

void test_pcb_diagnostics() {
  using namespace NetlistData;

  auto duplicate = pcb_fixture();
  duplicate.components[1].reference = "R1";
  auto duplicateResult = PcbNetlistBuilder::build(duplicate);
  check(!duplicateResult.ok() &&
        contains_code(duplicateResult.diagnostics, "PCB_NETLIST_REFERENCE_DUPLICATE"),
        "duplicate references are rejected");

  auto missingFootprint = pcb_fixture();
  missingFootprint.components[0].footprint.clear();
  auto footprintResult = PcbNetlistBuilder::build(missingFootprint);
  check(!footprintResult.ok() &&
        contains_code(footprintResult.diagnostics, "PCB_NETLIST_FOOTPRINT_MISSING"),
        "missing footprint is diagnosed per component");

  auto missingNumber = pcb_fixture();
  missingNumber.components[0].pins[0].number.clear();
  auto numberResult = PcbNetlistBuilder::build(missingNumber);
  check(!numberResult.ok() &&
        contains_code(numberResult.diagnostics, "PCB_NETLIST_PIN_NUMBER_MISSING"),
        "missing physical pad number is diagnosed per pin");

  auto missingPrefix = pcb_fixture();
  missingPrefix.components[0].reference.clear();
  missingPrefix.components[0].referencePrefix.clear();
  auto prefixResult = PcbNetlistBuilder::build(missingPrefix);
  check(!prefixResult.ok() &&
        contains_code(prefixResult.diagnostics, "PCB_NETLIST_REFERENCE_PREFIX_MISSING"),
        "missing reference information is diagnosed");

  auto defaultValue = pcb_fixture();
  defaultValue.components[0].value.clear();
  auto valueResult = PcbNetlistBuilder::build(defaultValue);
  check(valueResult.ok() &&
        contains_code(valueResult.diagnostics, "PCB_NETLIST_VALUE_DEFAULTED") &&
        valueResult.value->components[1].value == "Resistor",
        "missing value falls back to the library type with a warning");
}

void test_kicad_exporter_errors_and_escaping() {
  using namespace NetlistData;
  auto built = PcbNetlistBuilder::build(pcb_fixture());
  if (!built.ok()) {
    check(false, "exporter test requires a valid PCB netlist");
    return;
  }

  KiCadNetlistExportOptions options;
  options.date = "2026-10-22";

  auto netlist = *built.value;
  netlist.components[1].value = "10k \"q\" \\ x";
  netlist.nets[0].name = "Net-(A\"B\\)";
  std::vector<Diagnostic> diagnostics;
  const std::string text = KiCadNetlistExporter::serialize(netlist, options, &diagnostics);
  check(!text.empty() && diagnostics.empty(), "special characters still serialize");
  check(text.find("10k \\\"q\\\" \\\\ x") != std::string::npos,
        "quotes and backslashes in values are escaped");
  check(text.find("(name \"Net-(A\\\"B\\\\)\")") != std::string::npos,
        "quotes and backslashes in net names are escaped");

  auto invalid = *built.value;
  invalid.nets[0].nodes[0].pinNumber = "99";
  std::vector<Diagnostic> invalidDiagnostics;
  const std::string invalidText =
      KiCadNetlistExporter::serialize(invalid, options, &invalidDiagnostics);
  check(invalidText.empty() &&
        contains_code(invalidDiagnostics, "KICAD_NETLIST_NODE_PIN_MISSING"),
        "exporter rejects nodes that reference unknown pads");

  const KiCadNetlistWriteResult writeResult =
      KiCadNetlistExporter::write(*built.value,
                                  "/tmp/tinyeda_netlist_missing_dir_20261022/out.net", options);
  check(!writeResult.ok() &&
        contains_code(writeResult.diagnostics, "KICAD_NETLIST_WRITE_FAILED"),
        "file write failure returns a specific diagnostic");
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

            test_pcb_builder_and_exporter();
            test_pcb_reference_assignment();
            test_pcb_connectivity_rules();
            test_pcb_diagnostics();
            test_kicad_exporter_errors_and_escaping();

            if (failures == 0) std::cout << "Netlist JSON and builder self-test passed.\n";
    return failures == 0 ? 0 : 1;
}

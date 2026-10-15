#include "NetlistBuilder.hpp"

#include "NetlistValidator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace NetlistData {
namespace {

using TerminalKey = std::tuple<std::string, std::string, std::string>;
using ComponentKey = std::pair<std::string, std::string>;

void add_diagnostic(std::vector<Diagnostic>& out, const std::string& severity,
                    const std::string& code, const std::string& message,
                    const std::string& objectId = {}, const std::string& pinId = {}) {
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.code = code;
    diagnostic.message = message;
    if (!objectId.empty()) diagnostic.objectIds.push_back(objectId);
    diagnostic.pinId = pinId;
    out.push_back(std::move(diagnostic));
}

void error(std::vector<Diagnostic>& out, const std::string& code,
           const std::string& message, const std::string& objectId = {},
           const std::string& pinId = {}) {
    add_diagnostic(out, "error", code, message, objectId, pinId);
}

void warning(std::vector<Diagnostic>& out, const std::string& code,
             const std::string& message, const std::string& objectId = {},
             const std::string& pinId = {}) {
    add_diagnostic(out, "warning", code, message, objectId, pinId);
}

bool has_errors(const std::vector<Diagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) {
        return d.severity == "error";
    });
}

bool has_model(const Json::Value& model) {
    return model.isObject() && model.isMember("kind") && model["kind"].isString() &&
           !model["kind"].asString().empty();
}

bool is_integer(const Json::Value& value) {
    return value.isInt64() || value.isUInt64();
}

bool value_matches_type(const Json::Value& value, const std::string& type) {
    if (type == "string" || type == "enum") return value.isString();
    if (type == "integer") return is_integer(value);
    if (type == "number") return value.isNumeric();
    if (type == "boolean") return value.isBool();
    return false;
}

bool validate_parameter_value(const Json::Value& value, const Json::Value& definition,
                              const std::string& name, const std::string& instanceId,
                              std::vector<Diagnostic>& diagnostics) {
    if (!definition.isObject() || !definition.isMember("type") || !definition["type"].isString()) {
        error(diagnostics, "NETLIST_BUILD_PARAMETER_SCHEMA_INVALID",
              "Parameter definition must contain a string type.", instanceId, name);
        return false;
    }
    const std::string type = definition["type"].asString();
    if (!value_matches_type(value, type)) {
        error(diagnostics, "NETLIST_BUILD_PARAMETER_TYPE_MISMATCH",
              "Parameter value does not match its declared type.", instanceId, name);
        return false;
    }

    if (type == "enum") {
        const Json::Value* values = nullptr;
        if (definition.isMember("values") && definition["values"].isArray()) values = &definition["values"];
        else if (definition.isMember("enum") && definition["enum"].isArray()) values = &definition["enum"];
        if (!values) {
            error(diagnostics, "NETLIST_BUILD_ENUM_VALUES_MISSING",
                  "Enum parameter definition requires an allowed values array.", instanceId, name);
            return false;
        }
        bool found = false;
        for (const auto& candidate : *values)
            found |= candidate.isString() && candidate.asString() == value.asString();
        if (!found) {
            error(diagnostics, "NETLIST_BUILD_PARAMETER_OUT_OF_RANGE",
                  "Enum parameter value is not in its allowed values.", instanceId, name);
            return false;
        }
        return true;
    }

    if (value.isNumeric()) {
        const double numeric = value.asDouble();
        if (!std::isfinite(numeric)) {
            error(diagnostics, "NETLIST_BUILD_PARAMETER_NOT_FINITE",
                  "Numeric parameter value must be finite.", instanceId, name);
            return false;
        }
        for (const char* bound : {"min", "max"}) {
            if (!definition.isMember(bound)) continue;
            if (!definition[bound].isNumeric()) {
                error(diagnostics, "NETLIST_BUILD_PARAMETER_SCHEMA_INVALID",
                      std::string("Parameter ") + bound + " constraint must be numeric.", instanceId, name);
                return false;
            }
            const double limit = definition[bound].asDouble();
            if ((std::string(bound) == "min" && numeric < limit) ||
                (std::string(bound) == "max" && numeric > limit)) {
                error(diagnostics, "NETLIST_BUILD_PARAMETER_OUT_OF_RANGE",
                      "Numeric parameter value is outside its allowed range.", instanceId, name);
                return false;
            }
        }
    }
    return true;
}

bool resolve_parameters(const Json::Value& schema, const Json::Value& overrides,
                        const std::string& instanceId, Json::Value& effective,
                        std::vector<Diagnostic>& diagnostics) {
    if (!schema.isObject() || !overrides.isObject()) {
        error(diagnostics, "NETLIST_BUILD_PARAMETERS_INVALID",
              "Parameter schema and instance overrides must be objects.", instanceId);
        return false;
    }

    effective = Json::Value(Json::objectValue);
    bool valid = true;
    for (const auto& name : schema.getMemberNames()) {
        const Json::Value& definition = schema[name];
        if (!definition.isObject() || !definition.isMember("default")) {
            error(diagnostics, "NETLIST_BUILD_PARAMETER_DEFAULT_MISSING",
                  "Each parameter definition must provide a default value.", instanceId, name);
            valid = false;
            continue;
        }
        const Json::Value& value = overrides.isMember(name) ? overrides[name] : definition["default"];
        effective[name] = value;
        valid &= validate_parameter_value(value, definition, name, instanceId, diagnostics);
    }
    for (const auto& name : overrides.getMemberNames()) {
        if (!schema.isMember(name)) {
            error(diagnostics, "NETLIST_BUILD_PARAMETER_UNKNOWN",
                  "Instance overrides an undeclared parameter.", instanceId, name);
            valid = false;
        }
    }
    return valid;
}

bool resolve_width(const PinWidth& spec, const Json::Value& parameters,
                   const std::string& instanceId, const std::string& pinId,
                   unsigned int& width, std::vector<Diagnostic>& diagnostics) {
    if (std::holds_alternative<unsigned int>(spec)) {
        width = std::get<unsigned int>(spec);
    } else {
        const std::string& parameterName = std::get<std::string>(spec);
        if (!parameters.isMember(parameterName) || !is_integer(parameters[parameterName])) {
            error(diagnostics, "NETLIST_BUILD_PIN_WIDTH_UNRESOLVED",
                  "Pin width parameter is missing or is not an integer.", instanceId, pinId);
            return false;
        }
        const Json::Value& value = parameters[parameterName];
        if (value.isInt64()) {
            const Json::Int64 number = value.asInt64();
            if (number <= 0 || static_cast<Json::UInt64>(number) > std::numeric_limits<unsigned int>::max()) {
                error(diagnostics, "NETLIST_BUILD_PIN_WIDTH_INVALID",
                      "Resolved pin width must be a positive supported integer.", instanceId, pinId);
                return false;
            }
            width = static_cast<unsigned int>(number);
        } else {
            const Json::UInt64 number = value.asUInt64();
            if (number == 0 || number > std::numeric_limits<unsigned int>::max()) {
                error(diagnostics, "NETLIST_BUILD_PIN_WIDTH_INVALID",
                      "Resolved pin width must be a positive supported integer.", instanceId, pinId);
                return false;
            }
            width = static_cast<unsigned int>(number);
        }
    }
    if (width == 0) {
        error(diagnostics, "NETLIST_BUILD_PIN_WIDTH_INVALID",
              "Pin width must be positive.", instanceId, pinId);
        return false;
    }
    return true;
}

class DisjointSet {
public:
    std::size_t add() {
        const std::size_t id = parent_.size();
        parent_.push_back(id);
        rank_.push_back(0);
        return id;
    }

    std::size_t find(std::size_t value) {
        if (parent_[value] != value) parent_[value] = find(parent_[value]);
        return parent_[value];
    }

    void unite(std::size_t left, std::size_t right) {
        left = find(left);
        right = find(right);
        if (left == right) return;
        if (rank_[left] < rank_[right]) std::swap(left, right);
        parent_[right] = left;
        if (rank_[left] == rank_[right]) ++rank_[left];
    }

private:
    std::vector<std::size_t> parent_;
    std::vector<unsigned int> rank_;
};

struct PreparedInstance {
    const ProjectComponent* project = nullptr;
    const LibraryComponentDefinition* definition = nullptr;
    Instance instance;
};

const LibraryComponentDefinition* find_definition(
    const LibrarySnapshot& snapshot, const std::string& libraryId, const std::string& type) {
    const auto it = std::find_if(snapshot.components.begin(), snapshot.components.end(),
        [&](const LibraryComponentDefinition& item) {
            return item.libraryId == libraryId && item.type == type;
        });
    return it == snapshot.components.end() ? nullptr : &*it;
}

bool same_endpoint(const WireEndpoint& left, const WireEndpoint& right) {
    if (left.index() != right.index()) return false;
    if (const auto* a = std::get_if<ComponentPinEndpoint>(&left)) {
        const auto& b = std::get<ComponentPinEndpoint>(right);
        return a->componentId == b.componentId && a->pinId == b.pinId;
    }
    if (const auto* a = std::get_if<PortEndpoint>(&left))
        return a->portId == std::get<PortEndpoint>(right).portId;
    if (const auto* a = std::get_if<JunctionEndpoint>(&left))
        return a->junctionId == std::get<JunctionEndpoint>(right).junctionId;
    const auto& a = std::get<FreeEndpoint>(left).position;
    const auto& b = std::get<FreeEndpoint>(right).position;
    return a.x == b.x && a.y == b.y;
}

void append_unique(std::vector<Diagnostic>& target, const Diagnostic& diagnostic) {
    const auto same = [&](const Diagnostic& other) {
        return other.severity == diagnostic.severity && other.code == diagnostic.code &&
               other.objectIds == diagnostic.objectIds && other.pinId == diagnostic.pinId;
    };
    if (std::find_if(target.begin(), target.end(), same) == target.end()) target.push_back(diagnostic);
}

} // namespace

bool NetlistBuildResult::ok() const {
    return value.has_value() && !has_errors(diagnostics);
}

NetlistBuildResult NetlistBuilder::build(const ProjectModelSnapshot& project,
                                         const LibrarySnapshot& libraries,
                                         const NetlistBuildOptions& options) {
    NetlistBuildResult result;
    if (project.schemaVersion != "2.0")
        error(result.diagnostics, "NETLIST_BUILD_XML_VERSION_UNSUPPORTED",
              "Only XML project schema 2.0 can be converted to a netlist.");
    if (!project.topologyVerified)
        error(result.diagnostics, "NETLIST_BUILD_TOPOLOGY_UNVERIFIED",
              "Project topology has not been validated as explicit XML 2.0 connectivity.");
    if (project.xmlDigest.empty())
        error(result.diagnostics, "NETLIST_BUILD_SOURCE_DIGEST_MISSING",
              "Project snapshot must provide a stable XML digest.");
    if (options.generatorVersion.empty())
        error(result.diagnostics, "NETLIST_BUILD_GENERATOR_VERSION_MISSING",
              "Generator version must be specified.");

    std::map<std::string, std::string> projectLibraries;
    for (const auto& library : project.libraries) {
        if (library.id.empty() || library.version.empty())
            error(result.diagnostics, "NETLIST_BUILD_LIBRARY_REFERENCE_INVALID",
                  "Project library id and version must be non-empty.", library.id);
        if (!projectLibraries.emplace(library.id, library.version).second)
            error(result.diagnostics, "NETLIST_BUILD_LIBRARY_ID_DUPLICATE",
                  "Project declares a library id more than once.", library.id);
    }
    std::map<std::string, std::string> snapshotLibraries;
    for (const auto& library : libraries.libraries) {
        if (!snapshotLibraries.emplace(library.id, library.version).second)
            error(result.diagnostics, "NETLIST_BUILD_LIBRARY_ID_DUPLICATE",
                  "Library snapshot contains a duplicate library id.", library.id);
    }
    if (projectLibraries != snapshotLibraries)
        error(result.diagnostics, "NETLIST_BUILD_LIBRARY_SNAPSHOT_MISMATCH",
              "Resolved library snapshot does not exactly match project library ids and versions.");

    std::unordered_map<std::string, const ProjectPort*> portsById;
    std::unordered_set<std::string> portNames;
    for (const auto& port : project.ports) {
        if (port.id.empty() || !portsById.emplace(port.id, &port).second)
            error(result.diagnostics, "NETLIST_BUILD_PORT_ID_DUPLICATE",
                  "Top-level port id is empty or duplicated.", port.id);
        if (port.name.empty() || !portNames.insert(port.name).second)
            error(result.diagnostics, "NETLIST_BUILD_PORT_NAME_DUPLICATE",
                  "Top-level port name is empty or duplicated.", port.id);
        if (port.direction != "in" && port.direction != "out" && port.direction != "inout")
            error(result.diagnostics, "NETLIST_BUILD_PORT_DIRECTION_INVALID",
                  "Top-level port direction is invalid.", port.id);
        if (port.width == 0)
            error(result.diagnostics, "NETLIST_BUILD_PORT_WIDTH_INVALID",
                  "Top-level port width must be positive.", port.id);
    }

    std::unordered_map<std::string, const ProjectComponent*> componentsById;
    for (const auto& component : project.components) {
        if (component.id.empty() || !componentsById.emplace(component.id, &component).second)
            error(result.diagnostics, "NETLIST_BUILD_COMPONENT_ID_DUPLICATE",
                  "Component instance id is empty or duplicated.", component.id);
        if (component.rotation != 0 && component.rotation != 90 &&
            component.rotation != 180 && component.rotation != 270)
            error(result.diagnostics, "NETLIST_BUILD_ROTATION_INVALID",
                  "Component rotation must be 0, 90, 180, or 270 degrees.", component.id);
    }

    std::unordered_map<std::string, const ProjectJunction*> junctionsById;
    for (const auto& junction : project.junctions) {
        if (junction.id.empty() || !junctionsById.emplace(junction.id, &junction).second)
            error(result.diagnostics, "NETLIST_BUILD_JUNCTION_ID_DUPLICATE",
                  "Junction id is empty or duplicated.", junction.id);
    }

    std::unordered_set<std::string> directlyConnectedComponents;
    std::map<TerminalKey, unsigned int> endpointCounts;
    std::unordered_set<std::string> wireIds;
    for (const auto& wire : project.wires) {
        if (wire.id.empty() || !wireIds.insert(wire.id).second)
            error(result.diagnostics, "NETLIST_BUILD_WIRE_ID_DUPLICATE",
                  "Wire id is empty or duplicated.", wire.id);
        if (same_endpoint(wire.start, wire.end))
            error(result.diagnostics, "NETLIST_BUILD_WIRE_SAME_ENDPOINT",
                  "Wire start and end must not reference the same endpoint.", wire.id);

        for (const WireEndpoint* endpoint : {&wire.start, &wire.end}) {
            if (const auto* component = std::get_if<ComponentPinEndpoint>(endpoint)) {
                directlyConnectedComponents.insert(component->componentId);
                ++endpointCounts[TerminalKey{"instance", component->componentId, component->pinId}];
            } else if (const auto* port = std::get_if<PortEndpoint>(endpoint)) {
                if (!portsById.contains(port->portId))
                    error(result.diagnostics, "NETLIST_BUILD_PORT_REFERENCE_INVALID",
                          "Wire endpoint references an unknown top-level port.", wire.id, port->portId);
            } else if (const auto* junction = std::get_if<JunctionEndpoint>(endpoint)) {
                if (!junctionsById.contains(junction->junctionId))
                    error(result.diagnostics, "NETLIST_BUILD_JUNCTION_REFERENCE_INVALID",
                          "Wire endpoint references an unknown junction.", wire.id, junction->junctionId);
            } else {
                warning(result.diagnostics, "NETLIST_BUILD_FREE_ENDPOINT",
                        "Wire has an explicit free endpoint and does not create an electrical connection there.", wire.id);
            }
        }
    }
    for (const auto& [terminal, count] : endpointCounts) {
        if (count > 1) {
            const auto& [ownerKind, ownerId, pinId] = terminal;
            (void)ownerKind;
            error(result.diagnostics, "NETLIST_BUILD_PIN_REFERENCED_BY_MULTIPLE_WIRES",
                  "A component pin is directly referenced by multiple wires; use an explicit junction topology.",
                  ownerId, pinId);
        }
    }

    std::map<ComponentKey, const LibraryComponentDefinition*> definitions;
    for (const auto& definition : libraries.components) {
        if (!definitions.emplace(ComponentKey{definition.libraryId, definition.type}, &definition).second)
            error(result.diagnostics, "NETLIST_BUILD_COMPONENT_DEFINITION_DUPLICATE",
                  "Library snapshot contains duplicate library/type definitions.", definition.type);
        if (!projectLibraries.contains(definition.libraryId))
            error(result.diagnostics, "NETLIST_BUILD_DEFINITION_FROM_UNDECLARED_LIBRARY",
                  "Library snapshot contains a definition from a library not declared by the project.",
                  definition.type);
        if (definition.type.empty())
            error(result.diagnostics, "NETLIST_BUILD_COMPONENT_TYPE_EMPTY",
                  "Library component type must be non-empty.", definition.libraryId);
    }

    Netlist netlist;
    netlist.source.projectId = project.projectId;
    netlist.source.xmlDigest = project.xmlDigest;
    netlist.source.libraries = project.libraries;
    std::sort(netlist.source.libraries.begin(), netlist.source.libraries.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    netlist.source.generatorVersion = options.generatorVersion;

    for (const auto& port : project.ports) {
        netlist.ports.push_back(Port{port.id, port.name, port.direction, port.width});
    }

    std::unordered_map<std::string, PreparedInstance> preparedInstances;
    for (const auto& component : project.components) {
        const LibraryComponentDefinition* definition = find_definition(libraries, component.libraryId, component.type);
        if (!definition) {
            if (directlyConnectedComponents.contains(component.id)) {
                error(result.diagnostics, "NETLIST_BUILD_COMPONENT_TYPE_UNKNOWN",
                      "Connected component type does not resolve in the declared library snapshot.", component.id);
            } else {
                warning(result.diagnostics, "NETLIST_BUILD_UNUSED_COMPONENT_UNKNOWN",
                        "Unconnected component has no resolved library definition and is omitted.", component.id);
            }
            continue;
        }

        Json::Value effectiveParameters;
        resolve_parameters(definition->parameterSchema, component.parameterOverrides,
                           component.id, effectiveParameters, result.diagnostics);

        Json::Value model;
        if (has_model(definition->simulationModel)) {
            model = definition->simulationModel;
        } else if (definition->synthModel.isObject() &&
                   definition->synthModel.isMember("kind") &&
                   definition->synthModel["kind"].isString() &&
                   definition->synthModel["kind"].asString() == "primitive") {
            // The primitive operator is machine-readable library behavior, not
            // display text. Preserve the operator as the generic Netlist model.
            model = definition->synthModel;
        }

        if (!has_model(model)) {
            if (directlyConnectedComponents.contains(component.id)) {
                error(result.diagnostics, "NETLIST_BUILD_SIMULATION_MODEL_MISSING",
                      "Connected component has no explicit machine-readable model.", component.id);
            } else {
                warning(result.diagnostics, "NETLIST_BUILD_UNUSED_UNMODELED_COMPONENT",
                        "Unconnected component has no executable model and is omitted.", component.id);
            }
            continue;
        }

        Instance instance;
        instance.id = component.id;
        instance.type = component.type;
        instance.parameters = std::move(effectiveParameters);
        instance.model = std::move(model);
        std::unordered_set<std::string> pinIds;
        for (const auto& pin : definition->pins) {
            if (pin.id.empty() || !pinIds.insert(pin.id).second) {
                error(result.diagnostics, "NETLIST_BUILD_PIN_ID_DUPLICATE",
                      "Library pin id is empty or duplicated for this component type.", component.id, pin.id);
                continue;
            }
            unsigned int width = 0;
            if (!resolve_width(pin.width, instance.parameters, component.id, pin.id,
                               width, result.diagnostics)) continue;
            if (pin.direction != "input" && pin.direction != "output" && pin.direction != "inout") {
                error(result.diagnostics, "NETLIST_BUILD_PIN_DIRECTION_INVALID",
                      "Library pin direction is invalid.", component.id, pin.id);
                continue;
            }
            instance.pins.push_back(InstancePin{pin.id, pin.direction, width});
        }
        preparedInstances.emplace(component.id, PreparedInstance{&component, definition, std::move(instance)});
    }

    DisjointSet disjointSet;
    std::map<TerminalKey, std::size_t> terminalNodes;
    std::unordered_map<std::string, std::size_t> junctionNodes;
    std::map<TerminalKey, unsigned int> terminalWidths;
    for (const auto& port : project.ports) {
        const TerminalKey key{"port", port.id, {}};
        terminalNodes.emplace(key, disjointSet.add());
        terminalWidths.emplace(key, port.width);
    }
    for (const auto& [componentId, prepared] : preparedInstances) {
        for (const auto& pin : prepared.instance.pins) {
            const TerminalKey key{"instance", componentId, pin.id};
            terminalNodes.emplace(key, disjointSet.add());
            terminalWidths.emplace(key, pin.width);
        }
    }
    for (const auto& junction : project.junctions)
        junctionNodes.emplace(junction.id, disjointSet.add());

    auto node_for_endpoint = [&](const WireEndpoint& endpoint, const std::string& wireId)
        -> std::optional<std::size_t> {
        if (const auto* component = std::get_if<ComponentPinEndpoint>(&endpoint)) {
            const auto componentIt = preparedInstances.find(component->componentId);
            if (componentIt == preparedInstances.end()) {
                if (componentsById.contains(component->componentId) &&
                    directlyConnectedComponents.contains(component->componentId)) {
                    // The component already has a more specific diagnostic (unknown type or model).
                    return std::nullopt;
                }
                error(result.diagnostics, "NETLIST_BUILD_COMPONENT_REFERENCE_INVALID",
                      "Wire endpoint references an unknown component instance.", wireId, component->pinId);
                return std::nullopt;
            }
            const TerminalKey key{"instance", component->componentId, component->pinId};
            const auto pinIt = terminalNodes.find(key);
            if (pinIt == terminalNodes.end()) {
                error(result.diagnostics, "NETLIST_BUILD_PIN_REFERENCE_INVALID",
                      "Wire endpoint references an unknown pin on the component.", component->componentId, component->pinId);
                return std::nullopt;
            }
            return pinIt->second;
        }
        if (const auto* port = std::get_if<PortEndpoint>(&endpoint)) {
            const TerminalKey key{"port", port->portId, {}};
            const auto it = terminalNodes.find(key);
            if (it == terminalNodes.end()) return std::nullopt;
            return it->second;
        }
        if (const auto* junction = std::get_if<JunctionEndpoint>(&endpoint)) {
            const auto it = junctionNodes.find(junction->junctionId);
            if (it == junctionNodes.end()) return std::nullopt;
            return it->second;
        }
        return std::nullopt;
    };

    for (const auto& wire : project.wires) {
        const auto start = node_for_endpoint(wire.start, wire.id);
        const auto end = node_for_endpoint(wire.end, wire.id);
        if (start && end) disjointSet.unite(*start, *end);
    }

    std::map<std::size_t, std::vector<Terminal>> terminalsByRoot;
    std::map<std::size_t, std::vector<unsigned int>> widthsByRoot;
    for (const auto& [key, node] : terminalNodes) {
        const auto& [ownerKind, ownerId, pinId] = key;
        const std::size_t root = disjointSet.find(node);
        terminalsByRoot[root].push_back(Terminal{ownerKind, ownerId, pinId});
        widthsByRoot[root].push_back(terminalWidths.at(key));
    }

    struct PendingNet {
        std::vector<Terminal> terminals;
        unsigned int width = 1;
        TerminalKey sortKey;
    };
    std::vector<PendingNet> pendingNets;
    for (auto& [root, terminals] : terminalsByRoot) {
        (void)root;
        std::sort(terminals.begin(), terminals.end(), [](const Terminal& a, const Terminal& b) {
            if (a.ownerKind != b.ownerKind) return a.ownerKind < b.ownerKind;
            if (a.ownerId != b.ownerId) return a.ownerId < b.ownerId;
            return a.pinId < b.pinId;
        });
        const TerminalKey sortKey{terminals.front().ownerKind, terminals.front().ownerId, terminals.front().pinId};

        const auto& widths = widthsByRoot.at(disjointSet.find(
            terminalNodes.at(TerminalKey{terminals.front().ownerKind, terminals.front().ownerId, terminals.front().pinId})));
        unsigned int width = widths.empty() ? 1u : widths.front();
        for (const auto terminalWidth : widths) {
            if (terminalWidth != width) {
                error(result.diagnostics, "NETLIST_BUILD_NET_WIDTH_MISMATCH",
                      "Connected terminals have different widths.", terminals.front().ownerId,
                      terminals.front().pinId);
            }
        }
        pendingNets.push_back(PendingNet{std::move(terminals), width, sortKey});
    }
    std::sort(pendingNets.begin(), pendingNets.end(), [](const PendingNet& a, const PendingNet& b) {
        return a.sortKey < b.sortKey;
    });
    for (std::size_t i = 0; i < pendingNets.size(); ++i) {
        Net net;
        net.id = "n" + std::to_string(i);
        net.width = pendingNets[i].width;
        net.terminals = std::move(pendingNets[i].terminals);
        netlist.nets.push_back(std::move(net));
    }

    for (auto& [id, prepared] : preparedInstances) {
        (void)id;
        netlist.instances.push_back(std::move(prepared.instance));
    }
    std::sort(netlist.ports.begin(), netlist.ports.end(), [](const Port& a, const Port& b) { return a.id < b.id; });
    std::sort(netlist.instances.begin(), netlist.instances.end(), [](const Instance& a, const Instance& b) { return a.id < b.id; });

    if (!has_errors(result.diagnostics)) {
        netlist.diagnostics = result.diagnostics;
        for (const auto& diagnostic : validate(netlist)) append_unique(result.diagnostics, diagnostic);
        if (!has_errors(result.diagnostics)) {
            netlist.diagnostics.clear();
            for (const auto& diagnostic : result.diagnostics)
                if (diagnostic.severity != "error") netlist.diagnostics.push_back(diagnostic);
            result.value = std::move(netlist);
        }
    }
    return result;
}

} // namespace NetlistData

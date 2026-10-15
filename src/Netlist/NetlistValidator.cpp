#include "NetlistValidator.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace NetlistData {
namespace {

void add(std::vector<Diagnostic>& out, const char* code, const std::string& message,
         const std::string& objectId = {}, const std::string& pinId = {}) {
    Diagnostic d;
    d.severity = "error";
    d.code = code;
    d.message = message;
    if (!objectId.empty()) d.objectIds.push_back(objectId);
    d.pinId = pinId;
    out.push_back(std::move(d));
}

void add_warning(std::vector<Diagnostic>& out, const char* code, const std::string& message,
                 const std::string& objectId) {
    Diagnostic d;
    d.severity = "warning";
    d.code = code;
    d.message = message;
    d.objectIds.push_back(objectId);
    out.push_back(std::move(d));
}

bool non_empty(const std::string& value) {
    return !value.empty();
}

bool valid_direction(const std::string& direction, bool port) {
    if (port) return direction == "in" || direction == "out" || direction == "inout";
    return direction == "input" || direction == "output" || direction == "inout";
}

} // namespace

std::vector<Diagnostic> validate(const Netlist& netlist) {
    std::vector<Diagnostic> diagnostics;

    if (netlist.schemaVersion != "1.0")
        add(diagnostics, "NETLIST_SCHEMA_UNSUPPORTED", "Unsupported netlist schema version.");
    if (!non_empty(netlist.source.xmlDigest))
        add(diagnostics, "NETLIST_SOURCE_XML_DIGEST_REQUIRED", "Source XML digest is required.");
    if (!non_empty(netlist.source.generatorVersion))
        add(diagnostics, "NETLIST_GENERATOR_VERSION_REQUIRED", "Generator version is required.");

    std::unordered_set<std::string> libraryIds;
    for (const auto& lib : netlist.source.libraries) {
        if (!non_empty(lib.id) || !non_empty(lib.version))
            add(diagnostics, "NETLIST_LIBRARY_INVALID", "Library id and version must be non-empty.", lib.id);
        if (!libraryIds.insert(lib.id).second)
            add(diagnostics, "NETLIST_LIBRARY_DUPLICATE", "Library id is duplicated.", lib.id);
    }
    if (!std::is_sorted(netlist.source.libraries.begin(), netlist.source.libraries.end(),
                        [](const auto& a, const auto& b) { return a.id < b.id; }))
        add(diagnostics, "NETLIST_LIBRARIES_UNSORTED", "Library references must be sorted by id.");

    std::unordered_map<std::string, const Port*> ports;
    std::unordered_set<std::string> portNames;
    for (const auto& port : netlist.ports) {
        if (!non_empty(port.id) || !ports.emplace(port.id, &port).second)
            add(diagnostics, "NETLIST_PORT_ID_DUPLICATE", "Port id is empty or duplicated.", port.id);
        if (!non_empty(port.name) || !portNames.insert(port.name).second)
            add(diagnostics, "NETLIST_PORT_NAME_DUPLICATE", "Port name is empty or duplicated.", port.id);
        if (!valid_direction(port.direction, true))
            add(diagnostics, "NETLIST_PORT_DIRECTION_INVALID", "Port direction is invalid.", port.id);
        if (port.width == 0)
            add(diagnostics, "NETLIST_PORT_WIDTH_INVALID", "Port width must be positive.", port.id);
    }

    std::unordered_map<std::string, const Instance*> instances;
    for (const auto& instance : netlist.instances) {
        if (!non_empty(instance.id) || !instances.emplace(instance.id, &instance).second)
            add(diagnostics, "NETLIST_INSTANCE_ID_DUPLICATE", "Instance id is empty or duplicated.", instance.id);
        if (!non_empty(instance.type))
            add(diagnostics, "NETLIST_INSTANCE_TYPE_REQUIRED", "Instance type is required.", instance.id);
        if (!instance.parameters.isObject())
            add(diagnostics, "NETLIST_PARAMETERS_INVALID", "Instance parameters must be an object.", instance.id);
        if (!instance.model.isObject() || !instance.model.isMember("kind") || !instance.model["kind"].isString())
            add(diagnostics, "NETLIST_MODEL_INVALID", "Instance model must be an object with a string kind.", instance.id);
        else if (instance.model["kind"].asString() == "primitive") {
            if (!instance.model.isMember("operator") || !instance.model["operator"].isString()) {
                add(diagnostics, "NETLIST_PRIMITIVE_OPERATOR_REQUIRED", "Primitive model requires a string operator.", instance.id);
            } else {
                static const std::unordered_set<std::string> operators = {
                    "and", "or", "not", "nand", "nor", "xor", "xnor", "buf"
                };
                if (!operators.contains(instance.model["operator"].asString()))
                    add(diagnostics, "NETLIST_PRIMITIVE_OPERATOR_UNKNOWN", "Primitive operator is unknown.", instance.id);
            }
        } else {
            // Preserve extensible simulation models, but do not imply that this
            // generic netlist layer knows how to execute them.
            add_warning(diagnostics, "NETLIST_MODEL_KIND_UNVERIFIED",
                        "Model extension is preserved; the selected runtime must verify support.", instance.id);
        }

        std::unordered_set<std::string> pinIds;
        for (const auto& pin : instance.pins) {
            if (!non_empty(pin.id) || !pinIds.insert(pin.id).second)
                add(diagnostics, "NETLIST_PIN_ID_DUPLICATE", "Pin id is empty or duplicated within its instance.", instance.id, pin.id);
            if (!valid_direction(pin.direction, false))
                add(diagnostics, "NETLIST_PIN_DIRECTION_INVALID", "Pin direction is invalid.", instance.id, pin.id);
            if (pin.width == 0)
                add(diagnostics, "NETLIST_PIN_WIDTH_INVALID", "Pin width must be positive.", instance.id, pin.id);
        }
    }

    std::unordered_set<std::string> netIds;
    std::set<std::tuple<std::string, std::string, std::string>> assignedTerminals;
    for (const auto& net : netlist.nets) {
        if (!non_empty(net.id) || !netIds.insert(net.id).second)
            add(diagnostics, "NETLIST_NET_ID_DUPLICATE", "Net id is empty or duplicated.", net.id);
        if (net.width == 0)
            add(diagnostics, "NETLIST_NET_WIDTH_INVALID", "Net width must be positive.", net.id);
        if (net.terminals.empty())
            add(diagnostics, "NETLIST_NET_TERMINALS_EMPTY", "Net must contain at least one terminal.", net.id);

        for (const auto& terminal : net.terminals) {
            const auto key = std::make_tuple(terminal.ownerKind, terminal.ownerId, terminal.pinId);
            if (!assignedTerminals.insert(key).second)
                add(diagnostics, "NETLIST_TERMINAL_MULTIPLE_NETS", "A terminal belongs to more than one net.", terminal.ownerId, terminal.pinId);

            unsigned int terminalWidth = 0;
            if (terminal.ownerKind == "port") {
                const auto it = ports.find(terminal.ownerId);
                if (it == ports.end()) {
                    add(diagnostics, "NETLIST_TERMINAL_PORT_UNKNOWN", "Terminal references an unknown port.", terminal.ownerId);
                } else if (!terminal.pinId.empty()) {
                    add(diagnostics, "NETLIST_TERMINAL_PORT_PIN_INVALID", "Port terminal must not specify pinId.", terminal.ownerId, terminal.pinId);
                } else {
                    terminalWidth = it->second->width;
                }
            } else if (terminal.ownerKind == "instance") {
                const auto it = instances.find(terminal.ownerId);
                if (it == instances.end()) {
                    add(diagnostics, "NETLIST_TERMINAL_INSTANCE_UNKNOWN", "Terminal references an unknown instance.", terminal.ownerId, terminal.pinId);
                } else {
                    const auto pin = std::find_if(it->second->pins.begin(), it->second->pins.end(),
                        [&terminal](const InstancePin& p) { return p.id == terminal.pinId; });
                    if (pin == it->second->pins.end())
                        add(diagnostics, "NETLIST_TERMINAL_PIN_UNKNOWN", "Terminal references an unknown instance pin.", terminal.ownerId, terminal.pinId);
                    else
                        terminalWidth = pin->width;
                }
            } else {
                add(diagnostics, "NETLIST_TERMINAL_OWNER_KIND_INVALID", "Terminal ownerKind must be port or instance.", terminal.ownerId, terminal.pinId);
            }
            if (terminalWidth != 0 && terminalWidth != net.width)
                add(diagnostics, "NETLIST_TERMINAL_WIDTH_MISMATCH", "Terminal width differs from its net width.", net.id, terminal.pinId);
        }
    }

    for (const auto& [id, port] : ports) {
        (void)port;
        if (!assignedTerminals.contains(std::make_tuple(std::string("port"), id, std::string{})))
            add(diagnostics, "NETLIST_PORT_TERMINAL_MISSING", "Port is not assigned to a net.", id);
    }
    for (const auto& [instanceId, instance] : instances) {
        for (const auto& pin : instance->pins) {
            if (!assignedTerminals.contains(std::make_tuple(std::string("instance"), instanceId, pin.id)))
                add(diagnostics, "NETLIST_PIN_TERMINAL_MISSING", "Instance pin is not assigned to a net.", instanceId, pin.id);
        }
    }

    for (const auto& diagnostic : netlist.diagnostics) {
        if (!non_empty(diagnostic.severity) || !non_empty(diagnostic.code) || !non_empty(diagnostic.message))
            add(diagnostics, "NETLIST_DIAGNOSTIC_INVALID", "Stored diagnostics require severity, code, and message.");
    }

    return diagnostics;
}

} // namespace NetlistData

#include "NetlistJson.hpp"

#include "NetlistValidator.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <unordered_set>

namespace NetlistData {
namespace {

Diagnostic make_diagnostic(const std::string& code, const std::string& message) {
    Diagnostic d;
    d.severity = "error";
    d.code = code;
    d.message = message;
    return d;
}

bool has_errors(const std::vector<Diagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& diagnostic) {
        return diagnostic.severity == "error";
    });
}

bool has_only_members(const Json::Value& value, std::initializer_list<const char*> allowed,
                      const std::string& path, std::vector<Diagnostic>& diagnostics) {
    bool valid = true;
    std::unordered_set<std::string> names;
    for (const char* name : allowed) names.emplace(name);
    for (const auto& name : value.getMemberNames()) {
        if (!names.contains(name)) {
            diagnostics.push_back(make_diagnostic("NETLIST_UNKNOWN_FIELD", "Unknown field at " + path + ": " + name));
            valid = false;
        }
    }
    return valid;
}

bool require_string(const Json::Value& object, const char* key, std::string& output,
                    const std::string& path, std::vector<Diagnostic>& diagnostics,
                    bool optional = false) {
    if (!object.isMember(key)) {
        if (optional) return true;
        diagnostics.push_back(make_diagnostic("NETLIST_FIELD_REQUIRED", "Missing string field: " + path + "." + key));
        return false;
    }
    if (!object[key].isString()) {
        diagnostics.push_back(make_diagnostic("NETLIST_FIELD_TYPE_INVALID", "Expected string at " + path + "." + key));
        return false;
    }
    output = object[key].asString();
    return true;
}

bool require_width(const Json::Value& object, const char* key, unsigned int& output,
                   const std::string& path, std::vector<Diagnostic>& diagnostics) {
    if (!object.isMember(key) || !object[key].isUInt()) {
        diagnostics.push_back(make_diagnostic("NETLIST_FIELD_TYPE_INVALID", "Expected positive integer at " + path + "." + key));
        return false;
    }
    output = object[key].asUInt();
    if (output == 0) diagnostics.push_back(make_diagnostic("NETLIST_WIDTH_INVALID", "Width must be positive at " + path + "." + key));
    return true;
}

bool require_array(const Json::Value& object, const char* key, const Json::Value*& output,
                   const std::string& path, std::vector<Diagnostic>& diagnostics) {
    if (!object.isMember(key) || !object[key].isArray()) {
        diagnostics.push_back(make_diagnostic("NETLIST_FIELD_TYPE_INVALID", "Expected array at " + path + "." + key));
        return false;
    }
    output = &object[key];
    return true;
}

bool parse_diagnostics(const Json::Value& array, Netlist& netlist, std::vector<Diagnostic>& errors) {
    if (!array.isArray()) {
        errors.push_back(make_diagnostic("NETLIST_FIELD_TYPE_INVALID", "Expected array at diagnostics."));
        return false;
    }
    for (Json::ArrayIndex i = 0; i < array.size(); ++i) {
        const Json::Value& value = array[i];
        const std::string path = "diagnostics[" + std::to_string(i) + "]";
        if (!value.isObject()) {
            errors.push_back(make_diagnostic("NETLIST_DIAGNOSTIC_INVALID", "Expected object at " + path));
            continue;
        }
        has_only_members(value, {"severity", "code", "message", "objectIds", "pinId"}, path, errors);
        Diagnostic d;
        require_string(value, "severity", d.severity, path, errors);
        require_string(value, "code", d.code, path, errors);
        require_string(value, "message", d.message, path, errors);
        require_string(value, "pinId", d.pinId, path, errors, true);
        if (value.isMember("objectIds")) {
            if (!value["objectIds"].isArray()) {
                errors.push_back(make_diagnostic("NETLIST_FIELD_TYPE_INVALID", "Expected array at " + path + ".objectIds"));
            } else {
                for (const auto& id : value["objectIds"]) {
                    if (!id.isString()) errors.push_back(make_diagnostic("NETLIST_FIELD_TYPE_INVALID", "Expected string in " + path + ".objectIds"));
                    else d.objectIds.push_back(id.asString());
                }
            }
        }
        netlist.diagnostics.push_back(std::move(d));
    }
    return true;
}

bool parse_netlist(const Json::Value& root, Netlist& netlist, std::vector<Diagnostic>& errors) {
    if (!root.isObject()) {
        errors.push_back(make_diagnostic("NETLIST_ROOT_INVALID", "Netlist JSON root must be an object."));
        return false;
    }
    has_only_members(root, {"schemaVersion", "source", "ports", "instances", "nets", "diagnostics"}, "$", errors);
    require_string(root, "schemaVersion", netlist.schemaVersion, "$", errors);

    if (netlist.schemaVersion != "1.0") {
        errors.push_back(make_diagnostic("NETLIST_SCHEMA_UNSUPPORTED", "Unsupported schemaVersion: " + netlist.schemaVersion));
        return false;
    }

    if (!root.isMember("source") || !root["source"].isObject()) {
        errors.push_back(make_diagnostic("NETLIST_FIELD_TYPE_INVALID", "Expected object at source."));
        return false;
    }
    const Json::Value& source = root["source"];
    has_only_members(source, {"projectId", "xmlDigest", "libraries", "generatorVersion"}, "source", errors);
    require_string(source, "projectId", netlist.source.projectId, "source", errors, true);
    require_string(source, "xmlDigest", netlist.source.xmlDigest, "source", errors);
    require_string(source, "generatorVersion", netlist.source.generatorVersion, "source", errors);

    const Json::Value* libraries = nullptr;
    if (require_array(source, "libraries", libraries, "source", errors)) {
        for (Json::ArrayIndex i = 0; i < libraries->size(); ++i) {
            const auto& value = (*libraries)[i];
            const std::string path = "source.libraries[" + std::to_string(i) + "]";
            if (!value.isObject()) {
                errors.push_back(make_diagnostic("NETLIST_LIBRARY_INVALID", "Expected object at " + path));
                continue;
            }
            has_only_members(value, {"id", "version"}, path, errors);
            LibraryReference lib;
            require_string(value, "id", lib.id, path, errors);
            require_string(value, "version", lib.version, path, errors);
            netlist.source.libraries.push_back(std::move(lib));
        }
    }

    const Json::Value* ports = nullptr;
    if (require_array(root, "ports", ports, "$", errors)) {
        for (Json::ArrayIndex i = 0; i < ports->size(); ++i) {
            const auto& value = (*ports)[i];
            const std::string path = "ports[" + std::to_string(i) + "]";
            if (!value.isObject()) {
                errors.push_back(make_diagnostic("NETLIST_PORT_INVALID", "Expected object at " + path));
                continue;
            }
            has_only_members(value, {"id", "name", "direction", "width"}, path, errors);
            Port port;
            require_string(value, "id", port.id, path, errors);
            require_string(value, "name", port.name, path, errors);
            require_string(value, "direction", port.direction, path, errors);
            require_width(value, "width", port.width, path, errors);
            netlist.ports.push_back(std::move(port));
        }
    }

    const Json::Value* instances = nullptr;
    if (require_array(root, "instances", instances, "$", errors)) {
        for (Json::ArrayIndex i = 0; i < instances->size(); ++i) {
            const auto& value = (*instances)[i];
            const std::string path = "instances[" + std::to_string(i) + "]";
            if (!value.isObject()) {
                errors.push_back(make_diagnostic("NETLIST_INSTANCE_INVALID", "Expected object at " + path));
                continue;
            }
            has_only_members(value, {"id", "type", "parameters", "model", "pins"}, path, errors);
            Instance instance;
            require_string(value, "id", instance.id, path, errors);
            require_string(value, "type", instance.type, path, errors);
            if (!value.isMember("parameters") || !value["parameters"].isObject())
                errors.push_back(make_diagnostic("NETLIST_PARAMETERS_INVALID", "Expected object at " + path + ".parameters"));
            else instance.parameters = value["parameters"];
            if (!value.isMember("model") || !value["model"].isObject())
                errors.push_back(make_diagnostic("NETLIST_MODEL_INVALID", "Expected object at " + path + ".model"));
            else instance.model = value["model"];

            const Json::Value* pins = nullptr;
            if (require_array(value, "pins", pins, path, errors)) {
                for (Json::ArrayIndex p = 0; p < pins->size(); ++p) {
                    const auto& pinValue = (*pins)[p];
                    const std::string pinPath = path + ".pins[" + std::to_string(p) + "]";
                    if (!pinValue.isObject()) {
                        errors.push_back(make_diagnostic("NETLIST_PIN_INVALID", "Expected object at " + pinPath));
                        continue;
                    }
                    has_only_members(pinValue, {"id", "direction", "width"}, pinPath, errors);
                    InstancePin pin;
                    require_string(pinValue, "id", pin.id, pinPath, errors);
                    require_string(pinValue, "direction", pin.direction, pinPath, errors);
                    require_width(pinValue, "width", pin.width, pinPath, errors);
                    instance.pins.push_back(std::move(pin));
                }
            }
            netlist.instances.push_back(std::move(instance));
        }
    }

    const Json::Value* nets = nullptr;
    if (require_array(root, "nets", nets, "$", errors)) {
        for (Json::ArrayIndex i = 0; i < nets->size(); ++i) {
            const auto& value = (*nets)[i];
            const std::string path = "nets[" + std::to_string(i) + "]";
            if (!value.isObject()) {
                errors.push_back(make_diagnostic("NETLIST_NET_INVALID", "Expected object at " + path));
                continue;
            }
            has_only_members(value, {"id", "width", "terminals"}, path, errors);
            Net net;
            require_string(value, "id", net.id, path, errors);
            require_width(value, "width", net.width, path, errors);
            const Json::Value* terminals = nullptr;
            if (require_array(value, "terminals", terminals, path, errors)) {
                for (Json::ArrayIndex t = 0; t < terminals->size(); ++t) {
                    const auto& terminalValue = (*terminals)[t];
                    const std::string terminalPath = path + ".terminals[" + std::to_string(t) + "]";
                    if (!terminalValue.isObject()) {
                        errors.push_back(make_diagnostic("NETLIST_TERMINAL_INVALID", "Expected object at " + terminalPath));
                        continue;
                    }
                    has_only_members(terminalValue, {"ownerKind", "ownerId", "pinId"}, terminalPath, errors);
                    Terminal terminal;
                    require_string(terminalValue, "ownerKind", terminal.ownerKind, terminalPath, errors);
                    require_string(terminalValue, "ownerId", terminal.ownerId, terminalPath, errors);
                    require_string(terminalValue, "pinId", terminal.pinId, terminalPath, errors, true);
                    net.terminals.push_back(std::move(terminal));
                }
            }
            netlist.nets.push_back(std::move(net));
        }
    }
    if (root.isMember("diagnostics")) parse_diagnostics(root["diagnostics"], netlist, errors);

    return errors.empty();
}

Json::Value to_json(const Netlist& netlist) {
    Json::Value root(Json::objectValue);
    root["schemaVersion"] = netlist.schemaVersion;

    Json::Value source(Json::objectValue);
    if (!netlist.source.projectId.empty()) source["projectId"] = netlist.source.projectId;
    source["xmlDigest"] = netlist.source.xmlDigest;
    source["generatorVersion"] = netlist.source.generatorVersion;
    auto libraries = netlist.source.libraries;
    std::sort(libraries.begin(), libraries.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    source["libraries"] = Json::Value(Json::arrayValue);
    for (const auto& lib : libraries) {
        Json::Value item(Json::objectValue);
        item["id"] = lib.id;
        item["version"] = lib.version;
        source["libraries"].append(std::move(item));
    }
    root["source"] = std::move(source);

    auto ports = netlist.ports;
    std::sort(ports.begin(), ports.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    root["ports"] = Json::Value(Json::arrayValue);
    for (const auto& port : ports) {
        Json::Value item(Json::objectValue);
        item["id"] = port.id;
        item["name"] = port.name;
        item["direction"] = port.direction;
        item["width"] = port.width;
        root["ports"].append(std::move(item));
    }

    auto instances = netlist.instances;
    std::sort(instances.begin(), instances.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    root["instances"] = Json::Value(Json::arrayValue);
    for (auto& instance : instances) {
        std::sort(instance.pins.begin(), instance.pins.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        Json::Value item(Json::objectValue);
        item["id"] = instance.id;
        item["type"] = instance.type;
        item["parameters"] = instance.parameters;
        item["model"] = instance.model;
        item["pins"] = Json::Value(Json::arrayValue);
        for (const auto& pin : instance.pins) {
            Json::Value pinItem(Json::objectValue);
            pinItem["id"] = pin.id;
            pinItem["direction"] = pin.direction;
            pinItem["width"] = pin.width;
            item["pins"].append(std::move(pinItem));
        }
        root["instances"].append(std::move(item));
    }

    auto nets = netlist.nets;
    std::sort(nets.begin(), nets.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    root["nets"] = Json::Value(Json::arrayValue);
    for (auto& net : nets) {
        std::sort(net.terminals.begin(), net.terminals.end(), [](const auto& a, const auto& b) {
            if (a.ownerKind != b.ownerKind) return a.ownerKind < b.ownerKind;
            if (a.ownerId != b.ownerId) return a.ownerId < b.ownerId;
            return a.pinId < b.pinId;
        });
        Json::Value item(Json::objectValue);
        item["id"] = net.id;
        item["width"] = net.width;
        item["terminals"] = Json::Value(Json::arrayValue);
        for (const auto& terminal : net.terminals) {
            Json::Value terminalItem(Json::objectValue);
            terminalItem["ownerKind"] = terminal.ownerKind;
            terminalItem["ownerId"] = terminal.ownerId;
            if (!terminal.pinId.empty()) terminalItem["pinId"] = terminal.pinId;
            item["terminals"].append(std::move(terminalItem));
        }
        root["nets"].append(std::move(item));
    }

    auto diagnostics = netlist.diagnostics;
    std::sort(diagnostics.begin(), diagnostics.end(), [](const auto& a, const auto& b) {
        if (a.code != b.code) return a.code < b.code;
        if (a.objectIds != b.objectIds) return a.objectIds < b.objectIds;
        return a.pinId < b.pinId;
    });
    root["diagnostics"] = Json::Value(Json::arrayValue);
    for (const auto& diagnostic : diagnostics) {
        Json::Value item(Json::objectValue);
        item["severity"] = diagnostic.severity;
        item["code"] = diagnostic.code;
        item["message"] = diagnostic.message;
        if (!diagnostic.objectIds.empty()) {
            item["objectIds"] = Json::Value(Json::arrayValue);
            for (const auto& id : diagnostic.objectIds) item["objectIds"].append(id);
        }
        if (!diagnostic.pinId.empty()) item["pinId"] = diagnostic.pinId;
        root["diagnostics"].append(std::move(item));
    }
    return root;
}

} // namespace

ReadResult NetlistJson::read(const std::filesystem::path& path) {
    ReadResult result;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        result.diagnostics.push_back(make_diagnostic("NETLIST_FILE_OPEN_FAILED", "Unable to open netlist JSON file."));
        return result;
    }
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad()) {
        result.diagnostics.push_back(make_diagnostic("NETLIST_FILE_READ_FAILED", "Unable to read netlist JSON file."));
        return result;
    }
    return parse(contents);
}

ReadResult NetlistJson::parse(const std::string& jsonText) {
    ReadResult result;
    Json::CharReaderBuilder builder;
    builder["failIfExtra"] = true;
    builder["rejectDupKeys"] = true;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value root;
    std::string parseErrors;
    if (!reader->parse(jsonText.data(), jsonText.data() + jsonText.size(), &root, &parseErrors)) {
        result.diagnostics.push_back(make_diagnostic("NETLIST_JSON_PARSE_FAILED", parseErrors));
        return result;
    }

    Netlist netlist;
    parse_netlist(root, netlist, result.diagnostics);
    if (result.diagnostics.empty()) {
        result.diagnostics = validate(netlist);
    }
    if (!has_errors(result.diagnostics)) result.value = std::move(netlist);
    return result;
}

WriteResult NetlistJson::write(const Netlist& netlist, const std::filesystem::path& path) {
    WriteResult result;
    const std::string contents = serialize(netlist, &result.diagnostics);
    if (!result.ok()) return result;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        result.diagnostics.push_back(make_diagnostic("NETLIST_FILE_OPEN_FAILED", "Unable to open destination for writing."));
        return result;
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.flush();
    if (!output) result.diagnostics.push_back(make_diagnostic("NETLIST_FILE_WRITE_FAILED", "Unable to write netlist JSON file."));
    return result;
}

std::string NetlistJson::serialize(const Netlist& netlist, std::vector<Diagnostic>* diagnostics) {
    auto validation = validate(netlist);
    if (diagnostics) *diagnostics = validation;
    if (has_errors(validation)) return {};
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    builder["commentStyle"] = "None";
    return Json::writeString(builder, to_json(netlist)) + "\n";
}

} // namespace NetlistData

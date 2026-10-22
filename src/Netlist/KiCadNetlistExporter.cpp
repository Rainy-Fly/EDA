#include "KiCadNetlistExporter.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace NetlistData {
namespace {

bool has_errors(const std::vector<Diagnostic>& diagnostics) {
    for (const Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == "error") return true;
    }
    return false;
}

void add_error(std::vector<Diagnostic>& diagnostics, const std::string& code,
               const std::string& message, const std::string& objectId = {}) {
    Diagnostic diagnostic;
    diagnostic.severity = "error";
    diagnostic.code = code;
    diagnostic.message = message;
    if (!objectId.empty()) diagnostic.objectIds.push_back(objectId);
    diagnostics.push_back(std::move(diagnostic));
}

void add_warning(std::vector<Diagnostic>& diagnostics, const std::string& code,
                 const std::string& message, const std::string& objectId = {}) {
    Diagnostic diagnostic;
    diagnostic.severity = "warning";
    diagnostic.code = code;
    diagnostic.message = message;
    if (!objectId.empty()) diagnostic.objectIds.push_back(objectId);
    diagnostics.push_back(std::move(diagnostic));
}

std::string today() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%d");
    return out.str();
}

std::string quote(const std::string& text) {
    std::string result;
    result.reserve(text.size() + 2);
    result.push_back('"');
    for (const char character : text) {
        switch (character) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result.push_back(character); break;
        }
    }
    result.push_back('"');
    return result;
}

bool validate(const PcbNetlist& netlist, std::vector<Diagnostic>& diagnostics) {
    std::unordered_map<std::string, const PcbComponent*> componentsByReference;
    for (const auto& component : netlist.components) {
        if (component.reference.empty()) {
            add_error(diagnostics, "KICAD_NETLIST_REFERENCE_EMPTY",
                      "A PCB component has an empty reference.", component.instanceId);
        } else if (!componentsByReference.emplace(component.reference, &component).second) {
            add_error(diagnostics, "KICAD_NETLIST_REFERENCE_DUPLICATE",
                      "Duplicate PCB component reference: " + component.reference,
                      component.reference);
        }
        if (component.value.empty()) {
            add_error(diagnostics, "KICAD_NETLIST_VALUE_EMPTY",
                      "A PCB component has an empty value.", component.reference);
        }
        if (component.footprint.empty()) {
            add_error(diagnostics, "KICAD_NETLIST_FOOTPRINT_EMPTY",
                      "A PCB component has no footprint.", component.reference);
        }
        std::set<std::string> pinNumbers;
        for (const auto& pin : component.pins) {
            if (pin.number.empty()) {
                add_error(diagnostics, "KICAD_NETLIST_PIN_NUMBER_EMPTY",
                          "A PCB component pin has no physical pad number.", component.reference);
            } else if (!pinNumbers.insert(pin.number).second) {
                add_error(diagnostics, "KICAD_NETLIST_PIN_NUMBER_DUPLICATE",
                          "Two pins of a component use the same pad number: " + pin.number,
                          component.reference);
            }
        }
    }

    std::set<std::string> netIds;
    std::set<std::string> netNames;
    for (const auto& net : netlist.nets) {
        if (net.id.empty() || !netIds.insert(net.id).second) {
            add_error(diagnostics, "KICAD_NETLIST_NET_ID_DUPLICATE",
                      "Net code is empty or duplicated.", net.name);
        }
        if (net.name.empty()) {
            add_error(diagnostics, "KICAD_NETLIST_NET_NAME_EMPTY", "A PCB net has an empty name.", net.id);
        } else if (!netNames.insert(net.name).second) {
            add_error(diagnostics, "KICAD_NETLIST_NET_NAME_DUPLICATE",
                      "Duplicate PCB net name: " + net.name, net.name);
        }
        for (const auto& node : net.nodes) {
            const auto it = componentsByReference.find(node.reference);
            if (it == componentsByReference.end()) {
                add_error(diagnostics, "KICAD_NETLIST_NODE_COMPONENT_MISSING",
                          "Net node references an unknown component: " + node.reference, net.name);
                continue;
            }
            const bool pinExists = std::any_of(it->second->pins.begin(), it->second->pins.end(),
                                               [&](const PcbPin& pin) {
                                                   return pin.number == node.pinNumber;
                                               });
            if (!pinExists) {
                add_error(diagnostics, "KICAD_NETLIST_NODE_PIN_MISSING",
                          "Net node references an unknown pad " + node.pinNumber +
                              " on component " + node.reference,
                          net.name);
            }
        }
    }

    if (netlist.components.empty()) {
        add_warning(diagnostics, "KICAD_NETLIST_NO_COMPONENTS",
                    "The PCB netlist contains no components.");
    }
    return !has_errors(diagnostics);
}

} // namespace

bool KiCadNetlistWriteResult::ok() const {
    return !has_errors(diagnostics);
}

std::string KiCadNetlistExporter::serialize(const PcbNetlist& netlist,
                                            const KiCadNetlistExportOptions& options,
                                            std::vector<Diagnostic>* diagnostics) {
    std::vector<Diagnostic> localDiagnostics;
    std::vector<Diagnostic>& sink = diagnostics ? *diagnostics : localDiagnostics;

    if (!validate(netlist, sink)) return {};

    const std::string date = options.date.empty() ? today() : options.date;
    const std::string tool = options.toolName.empty() ? "TinyEDA" : options.toolName;

    std::ostringstream out;
    out << "(export (version \"E\")\n";
    out << "  (design\n";
    if (!options.sourcePath.empty()) {
        out << "    (source " << quote(options.sourcePath) << ")\n";
    }
    out << "    (date " << quote(date) << ")\n";
    out << "    (tool " << quote(tool) << ")\n";
    out << "    (sheet (number \"1\") (name \"/\") (tstamps \"/\"))\n";
    out << "  )\n";

    out << "  (components\n";
    for (const auto& component : netlist.components) {
        out << "    (comp\n";
        out << "      (ref " << quote(component.reference) << ")\n";
        out << "      (value " << quote(component.value) << ")\n";
        out << "      (footprint " << quote(component.footprint) << ")\n";
        out << "    )\n";
    }
    out << "  )\n";

    out << "  (nets\n";
    for (const auto& net : netlist.nets) {
        out << "    (net\n";
        out << "      (code " << quote(net.id) << ")\n";
        out << "      (name " << quote(net.name) << ")\n";
        for (const auto& node : net.nodes) {
            out << "      (node (ref " << quote(node.reference) << ")"
                << " (pin " << quote(node.pinNumber) << ")";
            if (!node.pinType.empty()) {
                out << " (pintype " << quote(node.pinType) << ")";
            }
            out << ")\n";
        }
        out << "    )\n";
    }
    out << "  )\n";
    out << ")\n";
    return out.str();
}

KiCadNetlistWriteResult KiCadNetlistExporter::write(const PcbNetlist& netlist,
                                                     const std::string& path,
                                                     const KiCadNetlistExportOptions& options) {
    KiCadNetlistWriteResult result;
    const std::string text = serialize(netlist, options, &result.diagnostics);
    if (has_errors(result.diagnostics)) return result;

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        add_error(result.diagnostics, "KICAD_NETLIST_WRITE_FAILED",
                  "Could not open the output file for writing: " + path, path);
        return result;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    if (!output) {
        add_error(result.diagnostics, "KICAD_NETLIST_WRITE_FAILED",
                  "Failed while writing the output file: " + path, path);
    }
    return result;
}

} // namespace NetlistData

#include "OrCadNetlistExporter.hpp"

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

// 与 KiCad 导出器一致的校验规则（诊断码带 ORCAD 前缀）。
bool validate(const PcbNetlist& netlist, std::vector<Diagnostic>& diagnostics) {
    std::unordered_map<std::string, const PcbComponent*> componentsByReference;
    for (const auto& component : netlist.components) {
        if (component.reference.empty()) {
            add_error(diagnostics, "ORCAD_NETLIST_REFERENCE_EMPTY",
                      "A PCB component has an empty reference.", component.instanceId);
        } else if (!componentsByReference.emplace(component.reference, &component).second) {
            add_error(diagnostics, "ORCAD_NETLIST_REFERENCE_DUPLICATE",
                      "Duplicate PCB component reference: " + component.reference,
                      component.reference);
        }
        if (component.value.empty()) {
            add_error(diagnostics, "ORCAD_NETLIST_VALUE_EMPTY",
                      "A PCB component has an empty value.", component.reference);
        }
        if (component.footprint.empty()) {
            add_error(diagnostics, "ORCAD_NETLIST_FOOTPRINT_EMPTY",
                      "A PCB component has no footprint.", component.reference);
        }
        std::set<std::string> pinNumbers;
        for (const auto& pin : component.pins) {
            if (pin.number.empty()) {
                add_error(diagnostics, "ORCAD_NETLIST_PIN_NUMBER_EMPTY",
                          "A PCB component pin has no physical pad number.", component.reference);
            } else if (!pinNumbers.insert(pin.number).second) {
                add_error(diagnostics, "ORCAD_NETLIST_PIN_NUMBER_DUPLICATE",
                          "Two pins of a component use the same pad number: " + pin.number,
                          component.reference);
            }
        }
    }

    std::set<std::string> netIds;
    for (const auto& net : netlist.nets) {
        if (net.id.empty() || !netIds.insert(net.id).second) {
            add_error(diagnostics, "ORCAD_NETLIST_NET_ID_DUPLICATE",
                      "Net code is empty or duplicated.", net.name);
        }
        for (const auto& node : net.nodes) {
            const auto it = componentsByReference.find(node.reference);
            if (it == componentsByReference.end()) {
                add_error(diagnostics, "ORCAD_NETLIST_NODE_COMPONENT_MISSING",
                          "Net node references an unknown component: " + node.reference, net.name);
                continue;
            }
            const bool pinExists = std::any_of(it->second->pins.begin(), it->second->pins.end(),
                                               [&](const PcbPin& pin) {
                                                   return pin.number == node.pinNumber;
                                               });
            if (!pinExists) {
                add_error(diagnostics, "ORCAD_NETLIST_NODE_PIN_MISSING",
                          "Net node references an unknown pad " + node.pinNumber +
                              " on component " + node.reference,
                          net.name);
            }
        }
    }

    if (netlist.components.empty()) {
        add_warning(diagnostics, "ORCAD_NETLIST_NO_COMPONENTS",
                    "The PCB netlist contains no components.");
    }
    return !has_errors(diagnostics);
}

// 一个引脚所属网络（按位号+焊盘号匹配），找不到返回 nullptr。
const PcbNet* net_for_pin(const PcbNetlist& netlist, const PcbComponent& component,
                          const PcbPin& pin) {
    for (const auto& net : netlist.nets) {
        for (const auto& node : net.nodes) {
            if (node.reference == component.reference &&
                node.pinNumber == pin.number) {
                return &net;
            }
        }
    }
    return nullptr;
}

// OrCAD PCB2 里引脚的网络名：
//   多节点网络 -> 网络名（无名字网络用 $N-0<序号>）；
//   单节点网络 / 未连接引脚 -> "?"。
// code 是该网络在 nets 中的 1-based 序号（与参考实现 $N-0<code> 一致）。
std::string pin_net_name(const PcbNet* net, std::size_t code) {
    if (!net) return "?";
    if (net->nodes.size() < 2) return "?";
    if (net->name.empty()) {
        std::ostringstream out;
        out << "$N-0" << code;
        return out.str();
    }
    return net->name;
}

std::string tstamp_of(std::size_t index) {
    std::ostringstream out;
    out << std::hex << std::setw(8) << std::setfill('0') << (index + 1);
    return out.str();
}

// KiCad 的 OrCAD PCB2 读取器（LEGACY_NETLIST_READER::loadComponent）把元件行
// 第 2 个词整体当作封装名（fpid.SetLibItemName），不会解析 "库:封装" 前缀，
// 然后在封装库表中全库搜索同名封装。因此这里剥离 "库:" 前缀只输出封装名，
// 否则 "Package_DIP:DIP-14_W7.62mm" 会被当成一个封装名而找不到。
std::string orcad_footprint_name(const std::string& footprint) {
    const std::size_t colon = footprint.find(':');
    if (colon == std::string::npos) return footprint;
    return footprint.substr(colon + 1);
}

} // namespace

bool OrCadNetlistWriteResult::ok() const {
    return !has_errors(diagnostics);
}

std::string OrCadNetlistExporter::serialize(const PcbNetlist& netlist,
                                            const OrCadNetlistExportOptions& options,
                                            std::vector<Diagnostic>* diagnostics) {
    std::vector<Diagnostic> localDiagnostics;
    std::vector<Diagnostic>& sink = diagnostics ? *diagnostics : localDiagnostics;

    if (!validate(netlist, sink)) return {};

    const std::string date = options.date.empty() ? today() : options.date;
    const std::string tool = options.toolName.empty() ? "TinyEDA" : options.toolName;

    std::ostringstream out;
    // 头部：`( { Eeschema Netlist Version 1.1  <date>` 换行 `<tool>}`
    out << "( { Eeschema Netlist Version 1.1  " << date << "\n";
    out << tool << "}\n";

    // 元件清单：每元件一个 `( <tstamp> <footprint> <ref> <value>` 块
    for (std::size_t i = 0; i < netlist.components.size(); ++i) {
        const PcbComponent& component = netlist.components[i];
        out << " ( " << tstamp_of(i) << ' '
            << (component.footprint.empty() ? "$noname"
                                            : orcad_footprint_name(component.footprint))
            << ' '
            << component.reference << ' '
            << (component.value.empty() ? "\"~\"" : component.value) << "\n";
        for (const auto& pin : component.pins) {
            const PcbNet* net = net_for_pin(netlist, component, pin);
            const std::size_t code = net ? 1 + std::distance(
                netlist.nets.data(), net) : 0;
            out << "  (  " << pin.number << ' ' << pin_net_name(net, code) << " )\n";
        }
        out << " )\n";
    }

    // 文件尾
    out << ")\n*\n";
    return out.str();
}

OrCadNetlistWriteResult OrCadNetlistExporter::write(const PcbNetlist& netlist,
                                                    const std::string& path,
                                                    const OrCadNetlistExportOptions& options) {
    OrCadNetlistWriteResult result;
    const std::string text = serialize(netlist, options, &result.diagnostics);
    if (has_errors(result.diagnostics)) return result;

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        add_error(result.diagnostics, "ORCAD_NETLIST_WRITE_FAILED",
                  "Could not open the output file for writing: " + path, path);
        return result;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    if (!output) {
        add_error(result.diagnostics, "ORCAD_NETLIST_WRITE_FAILED",
                  "Failed while writing the output file: " + path, path);
    }
    return result;
}

} // namespace NetlistData

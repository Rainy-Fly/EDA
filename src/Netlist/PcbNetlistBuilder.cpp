#include "PcbNetlistBuilder.hpp"

#include <algorithm>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace NetlistData {
namespace {

bool same_diagnostic(const Diagnostic& left, const Diagnostic& right) {
    return left.severity == right.severity && left.code == right.code &&
           left.objectIds == right.objectIds && left.pinId == right.pinId;
}

struct DiagnosticSink {
    std::vector<Diagnostic>& out;

    void add(const Diagnostic& diagnostic) {
        if (std::find_if(out.begin(), out.end(), [&](const Diagnostic& existing) {
                return same_diagnostic(existing, diagnostic);
            }) == out.end())
            out.push_back(diagnostic);
    }

    void error(const std::string& code, const std::string& message,
               const std::string& objectId = {}, const std::string& pinId = {}) {
        Diagnostic diagnostic;
        diagnostic.severity = "error";
        diagnostic.code = code;
        diagnostic.message = message;
        if (!objectId.empty()) diagnostic.objectIds.push_back(objectId);
        diagnostic.pinId = pinId;
        add(diagnostic);
    }

    void warning(const std::string& code, const std::string& message,
                 const std::string& objectId = {}, const std::string& pinId = {}) {
        Diagnostic diagnostic;
        diagnostic.severity = "warning";
        diagnostic.code = code;
        diagnostic.message = message;
        if (!objectId.empty()) diagnostic.objectIds.push_back(objectId);
        diagnostic.pinId = pinId;
        add(diagnostic);
    }

    bool has_errors() const {
        return std::any_of(out.begin(), out.end(), [](const Diagnostic& diagnostic) {
            return diagnostic.severity == "error";
        });
    }
};

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

struct NodeIndex {
    std::unordered_map<std::string, std::size_t> byKey;
    DisjointSet set;

    std::size_t get(const std::string& key) {
        const auto it = byKey.find(key);
        if (it != byKey.end()) return it->second;
        const std::size_t id = set.add();
        byKey.emplace(key, id);
        return id;
    }

    std::optional<std::size_t> find(const std::string& key) const {
        const auto it = byKey.find(key);
        if (it == byKey.end()) return std::nullopt;
        return it->second;
    }
};

std::string pin_key(const std::string& instanceId, const std::string& pinId) {
    return "pin:" + instanceId + ':' + pinId;
}

std::string coord_key(const PcbPoint& point) {
    return "coord:" + std::to_string(point.x) + ':' + std::to_string(point.y);
}

// 点是否严格落在线段内部（不含端点）。
bool point_inside_segment(const PcbPoint& point, const PcbPoint& a, const PcbPoint& b) {
    if (a == b) return false;
    const long long dx = static_cast<long long>(b.x) - a.x;
    const long long dy = static_cast<long long>(b.y) - a.y;
    const long long px = static_cast<long long>(point.x) - a.x;
    const long long py = static_cast<long long>(point.y) - a.y;
    if (dx * py - dy * px != 0) return false;
    const long long dot = px * dx + py * dy;
    const long long lengthSquared = dx * dx + dy * dy;
    return dot > 0 && dot < lengthSquared;
}

bool is_bus(const PcbSnapshotWire& wire) {
    return wire.bus;
}

} // namespace

bool operator==(const PcbPoint& left, const PcbPoint& right) {
    return left.x == right.x && left.y == right.y;
}

bool PcbNetlistBuildResult::ok() const {
    if (!value.has_value()) return false;
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& diagnostic) {
        return diagnostic.severity == "error";
    });
}

PcbNetlistBuildResult PcbNetlistBuilder::build(const PcbProjectSnapshot& project) {
    PcbNetlistBuildResult result;
    DiagnosticSink sink{result.diagnostics};
    for (const Diagnostic& diagnostic : project.diagnostics) sink.add(diagnostic);

    std::vector<PcbSnapshotComponent> components = project.components;

    // ---- 元件 ID / 类型 ----
    std::unordered_set<std::string> instanceIds;
    for (const auto& component : components) {
        if (component.instanceId.empty() || !instanceIds.insert(component.instanceId).second) {
            sink.error("PCB_NETLIST_COMPONENT_ID_DUPLICATE",
                       "Component instance id is empty or duplicated.", component.instanceId);
        }
        if (component.type.empty()) {
            sink.error("PCB_NETLIST_COMPONENT_TYPE_MISSING",
                       "Component type is required for PCB export.", component.instanceId);
        }
        if (!component.exportable) {
            sink.warning("PCB_NETLIST_NON_PHYSICAL_COMPONENT",
                         "Non-physical symbol participates in connectivity but is not exported as a PCB component.",
                         component.instanceId);
        }
    }

    // ---- 位号：显式位号优先；否则按前缀确定性编号 ----
    std::vector<std::size_t> order(components.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        return components[left].instanceId < components[right].instanceId;
    });
    std::map<std::string, unsigned int> prefixCounters;
    std::set<std::string> assignedReferences;
    for (const auto& component : components) {
        if (component.exportable && !component.reference.empty()) {
            assignedReferences.insert(component.reference);
        }
    }
    for (const std::size_t index : order) {
        auto& component = components[index];
        if (!component.exportable) continue;
        if (!component.reference.empty()) continue;
        if (component.referencePrefix.empty()) {
            sink.error("PCB_NETLIST_REFERENCE_PREFIX_MISSING",
                       "Component has no explicit reference and no reference prefix.",
                       component.instanceId);
            continue;
        }
        std::string candidate;
        do {
            candidate = component.referencePrefix + std::to_string(++prefixCounters[component.referencePrefix]);
        } while (!assignedReferences.insert(candidate).second);
        component.reference = std::move(candidate);
    }

    std::unordered_map<std::string, std::string> referenceOwners;
    for (const auto& component : components) {
        if (!component.exportable || component.reference.empty()) continue;
        const auto [it, inserted] = referenceOwners.emplace(component.reference, component.instanceId);
        if (!inserted) {
            sink.error("PCB_NETLIST_REFERENCE_DUPLICATE",
                       "Reference is used by more than one component: " + it->second,
                       component.instanceId);
        }
    }

    // ---- 元件值 / 封装 / 引脚 ----
    for (auto& component : components) {
        if (component.exportable && component.value.empty()) {
            component.value = component.type;
            sink.warning("PCB_NETLIST_VALUE_DEFAULTED",
                         "Component has no value; the library type is used as the value.",
                         component.instanceId);
        }
        if (component.exportable && component.footprint.empty()) {
            sink.error("PCB_NETLIST_FOOTPRINT_MISSING",
                       "Component has no footprint; a PCB netlist cannot place it.",
                       component.instanceId);
        }
        if (component.exportable && component.pins.empty()) {
            sink.error("PCB_NETLIST_COMPONENT_NO_PINS",
                       "Component has no pins and cannot participate in a PCB net.",
                       component.instanceId);
        }

        std::unordered_set<std::string> pinIds;
        std::unordered_set<std::string> pinNumbers;
        for (const auto& pin : component.pins) {
            if (pin.id.empty() || !pinIds.insert(pin.id).second) {
                sink.error("PCB_NETLIST_PIN_ID_DUPLICATE",
                           "Component pin id is empty or duplicated.",
                           component.instanceId, pin.id);
            }
            if (!component.exportable) continue;
            if (pin.number.empty()) {
                sink.error("PCB_NETLIST_PIN_NUMBER_MISSING",
                           "Pin has no physical pad number; add a pin_numbers mapping in the component metadata.",
                           component.instanceId, pin.id);
            } else if (!pinNumbers.insert(pin.number).second) {
                sink.error("PCB_NETLIST_PIN_NUMBER_DUPLICATE",
                           "Two pins of the same component use the same physical pad number.",
                           component.instanceId, pin.id);
            }
        }
    }

    // ---- 导线基础校验 ----
    std::unordered_set<std::string> wireIds;
    for (const auto& wire : project.wires) {
        if (wire.id.empty() || !wireIds.insert(wire.id).second) {
            sink.error("PCB_NETLIST_WIRE_ID_DUPLICATE",
                       "Wire id is empty or duplicated.", wire.id);
        }
        if (is_bus(wire)) {
            sink.warning("PCB_NETLIST_BUS_NOT_SUPPORTED",
                         "Bus objects are not exported to a PCB netlist.", wire.id);
            continue;
        }
        if (wire.points.size() < 2) {
            sink.error("PCB_NETLIST_WIRE_TOO_SHORT",
                       "Wire requires at least two points.", wire.id);
        }
    }

    // ---- 连通性：只认导线首尾端点与引脚坐标的精确重合 ----
    NodeIndex nodes;
    std::map<std::pair<std::string, std::string>, std::size_t> pinNodes;
    std::map<std::string, std::size_t> pinAtCoordinate;
    for (const auto& component : components) {
        for (const auto& pin : component.pins) {
            const std::size_t pinNode = nodes.get(pin_key(component.instanceId, pin.id));
            pinNodes[{component.instanceId, pin.id}] = pinNode;
            // 引脚彼此精确重合视为直接接触（与 KiCad 的引脚接触语义一致）。
            const auto [it, inserted] = pinAtCoordinate.emplace(coord_key(pin.position), pinNode);
            if (!inserted) nodes.set.unite(it->second, pinNode);
        }
    }

    struct ActiveWire {
        const PcbSnapshotWire* wire = nullptr;
        std::size_t start = 0;
        std::size_t end = 0;
    };
    std::vector<ActiveWire> wires;
    for (const auto& wire : project.wires) {
        if (is_bus(wire) || wire.points.size() < 2) continue;
        const std::size_t start = nodes.get(coord_key(wire.points.front()));
        const std::size_t end = nodes.get(coord_key(wire.points.back()));
        nodes.set.unite(start, end);
        wires.push_back(ActiveWire{&wire, start, end});
    }

    for (const auto& component : components) {
        for (const auto& pin : component.pins) {
            const auto coordinate = nodes.find(coord_key(pin.position));
            if (!coordinate) continue;
            nodes.set.unite(pinNodes.at({component.instanceId, pin.id}), *coordinate);
        }
    }

    // 引脚/线端落在其它导线中段：几何碰触不建立连接，必须提示。
    for (const auto& component : components) {
        for (const auto& pin : component.pins) {
            for (const auto& active : wires) {
                const auto& points = active.wire->points;
                for (std::size_t i = 1; i < points.size(); ++i) {
                    if (point_inside_segment(pin.position, points[i - 1], points[i])) {
                        sink.warning("PCB_NETLIST_PIN_ON_WIRE_SEGMENT",
                                     "Pin touches a wire segment without a wire endpoint; no connection is created.",
                                     component.instanceId, pin.id);
                    }
                }
            }
        }
    }
    for (const auto& active : wires) {
        for (const PcbPoint& endpoint : {active.wire->points.front(), active.wire->points.back()}) {
            for (const auto& other : wires) {
                if (other.wire->id == active.wire->id) continue;
                const auto& points = other.wire->points;
                for (std::size_t i = 1; i < points.size(); ++i) {
                    if (point_inside_segment(endpoint, points[i - 1], points[i])) {
                        sink.warning("PCB_NETLIST_WIRE_ENDPOINT_ON_SEGMENT",
                                     "Wire endpoint touches another wire segment without a shared endpoint; no connection is created.",
                                     active.wire->id);
                    }
                }
            }
        }
    }

    // ---- 汇总终端 ----
    std::map<std::size_t, std::vector<PcbNode>> terminals;
    for (const auto& component : components) {
        if (!component.exportable) continue;
        for (const auto& pin : component.pins) {
            if (pin.number.empty()) continue;
            const auto root = nodes.set.find(pinNodes.at({component.instanceId, pin.id}));
            PcbNode node;
            node.reference = component.reference;
            node.pinNumber = pin.number;
            terminals[root].push_back(std::move(node));
        }
    }

    std::map<std::size_t, std::string> wireRootIds;
    for (const auto& active : wires) {
        wireRootIds.emplace(nodes.set.find(active.start), active.wire->id);
    }
    for (const auto& [root, wireId] : wireRootIds) {
        if (!terminals.contains(root)) {
            sink.warning("PCB_NETLIST_WIRE_WITHOUT_PIN",
                         "Wire is not connected to any component pin and is omitted from the PCB netlist.",
                         wireId);
        }
    }

    // ---- 生成网络 ----
    std::vector<PcbNet> nets;
    std::set<std::string> usedNames;
    for (auto& [root, nodesOfNet] : terminals) {
        (void)root;
        std::sort(nodesOfNet.begin(), nodesOfNet.end(), [](const PcbNode& left, const PcbNode& right) {
            if (left.reference != right.reference) return left.reference < right.reference;
            return left.pinNumber < right.pinNumber;
        });
        if (nodesOfNet.size() == 1) {
            sink.warning("PCB_NETLIST_UNCONNECTED_PIN",
                         "Pin is not connected to any other pin and is omitted from the PCB netlist.",
                         nodesOfNet.front().reference, nodesOfNet.front().pinNumber);
            continue;
        }
        PcbNet net;
        const std::string base = "Net-(" + nodesOfNet.front().reference + "-Pad" + nodesOfNet.front().pinNumber + ")";
        net.name = base;
        unsigned int suffix = 1;
        while (!usedNames.insert(net.name).second) {
            net.name = base + "#" + std::to_string(++suffix);
        }
        net.nodes = std::move(nodesOfNet);
        nets.push_back(std::move(net));
    }

    std::sort(nets.begin(), nets.end(), [](const PcbNet& left, const PcbNet& right) {
        if (left.name != right.name) return left.name < right.name;
        return left.nodes.size() < right.nodes.size();
    });
    for (std::size_t i = 0; i < nets.size(); ++i) {
        nets[i].id = std::to_string(i + 1);
    }

    // ---- 输出元件 ----
    std::vector<PcbComponent> exported;
    for (const auto& component : components) {
        if (!component.exportable) continue;
        PcbComponent out;
        out.instanceId = component.instanceId;
        out.type = component.type;
        out.reference = component.reference;
        out.value = component.value;
        out.footprint = component.footprint;
        out.pins.reserve(component.pins.size());
        for (const auto& pin : component.pins) {
            PcbPin outPin;
            outPin.id = pin.id;
            outPin.name = pin.name;
            outPin.number = pin.number;
            out.pins.push_back(std::move(outPin));
        }
        std::sort(out.pins.begin(), out.pins.end(), [](const PcbPin& left, const PcbPin& right) {
            return left.id < right.id;
        });
        exported.push_back(std::move(out));
    }
    std::sort(exported.begin(), exported.end(), [](const PcbComponent& left, const PcbComponent& right) {
        return left.reference < right.reference;
    });

    if (!sink.has_errors()) {
        PcbNetlist netlist;
        netlist.projectName = project.projectName;
        netlist.components = std::move(exported);
        netlist.nets = std::move(nets);
        result.value = std::move(netlist);
    }
    return result;
}

} // namespace NetlistData

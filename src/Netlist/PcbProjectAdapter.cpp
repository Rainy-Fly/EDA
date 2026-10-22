#include "PcbProjectAdapter.hpp"

#include "../Canva/CanvasItem.hpp"
#include "../metadata/MetaDataJson.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace NetlistData {
namespace {

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

std::vector<std::string> split(const std::string& text, const std::string& separators) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find_first_of(separators, start);
        const std::string token = trim(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (!token.empty()) result.push_back(token);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

// pin_numbers 支持两种写法：
//   "A=1;B=2"      按引脚 ID 显式映射
//   "1,2"          按元数据/画布引脚顺序位置映射
struct PinNumberSpec {
    std::map<std::string, std::string> byName;
    std::vector<std::string> positional;

    bool empty() const { return byName.empty() && positional.empty(); }

    std::string number_for(const std::string& pinId, std::size_t index) const {
        if (!byName.empty()) {
            const auto it = byName.find(pinId);
            return it == byName.end() ? std::string() : it->second;
        }
        return index < positional.size() ? positional[index] : std::string();
    }
};

PinNumberSpec parse_pin_numbers(const std::string& text) {
    PinNumberSpec spec;
    const std::string trimmed = trim(text);
    if (trimmed.empty()) return spec;

    for (const std::string& token : split(trimmed, ";,")) {
        const std::size_t equal = token.find('=');
        if (equal == std::string::npos) {
            spec.positional.push_back(token);
        } else {
            const std::string key = trim(token.substr(0, equal));
            const std::string value = trim(token.substr(equal + 1));
            if (!key.empty() && !value.empty()) spec.byName.emplace(key, value);
        }
    }
    if (!spec.byName.empty()) spec.positional.clear();
    return spec;
}

std::string param_of(const DealProjectXML::ComponentData& component,
                     const MetaData* metadata, const std::string& key) {
    const auto it = component.params.find(key);
    if (it != component.params.end()) return it->second;
    return metadata ? metadata->get_param(key) : std::string();
}

} // namespace

PcbProjectSnapshot BuildPcbSnapshotFromProject(const DealProjectXML::ProjectData& project) {
    PcbProjectSnapshot snapshot;
    snapshot.projectName = project.info.name;

    std::unordered_set<std::string> usedPinIds;
    for (const auto& componentData : project.components) {
        PcbSnapshotComponent component;
        component.instanceId = std::to_string(componentData.id);
        component.type = componentData.type;

        std::unique_ptr<MetaData> metadata(load_metadata(componentData.type));
        if (!metadata && !componentData.name.empty()) metadata.reset(load_metadata(componentData.name));

        component.category = !componentData.category.empty()
                                 ? componentData.category
                                 : (metadata ? metadata->get_category() : std::string());
        component.exportable = component.category != "Ground" && component.category != "Source";

        component.reference = param_of(componentData, metadata.get(), "reference");
        component.referencePrefix = param_of(componentData, metadata.get(), "reference_prefix");
        component.value = param_of(componentData, metadata.get(), "value");
        component.footprint = param_of(componentData, metadata.get(), "footprint");

        if (!metadata) {
            Diagnostic diagnostic;
            diagnostic.severity = "error";
            diagnostic.code = "PCB_NETLIST_COMPONENT_METADATA_MISSING";
            diagnostic.message = "Component metadata could not be loaded; pins and PCB attributes are unknown.";
            diagnostic.objectIds.push_back(component.instanceId);
            snapshot.diagnostics.push_back(std::move(diagnostic));
            snapshot.components.push_back(std::move(component));
            continue;
        }

        // 只读复用画布的引脚布局规则；不构造 CanvasItem，避免消耗全局元件 id。
        const std::vector<Pin> pins = item_pins_for(metadata.get(), componentData.name);
        const PinNumberSpec pinNumbers = parse_pin_numbers(param_of(componentData, metadata.get(), "pin_numbers"));

        usedPinIds.clear();
        for (std::size_t index = 0; index < pins.size(); ++index) {
            const Pin& pin = pins[index];
            PcbSnapshotPin snapshotPin;
            snapshotPin.name = pin.name;
            snapshotPin.id = pin.name;
            if (snapshotPin.id.empty() || !usedPinIds.insert(snapshotPin.id).second) {
                snapshotPin.id = pin.name + "#" + std::to_string(index + 1);
                usedPinIds.insert(snapshotPin.id);
            }
            snapshotPin.number = pinNumbers.number_for(pin.name, index);
            snapshotPin.position = PcbPoint{componentData.x + pin.offset.x,
                                            componentData.y + pin.offset.y};
            component.pins.push_back(std::move(snapshotPin));
        }

        snapshot.components.push_back(std::move(component));
    }

    for (const auto& wireData : project.wires) {
        PcbSnapshotWire wire;
        wire.id = std::to_string(wireData.id);
        wire.bus = wireData.type == "BUS";
        wire.points.reserve(wireData.points.size());
        for (const wxPoint& point : wireData.points) {
            wire.points.push_back(PcbPoint{point.x, point.y});
        }
        snapshot.wires.push_back(std::move(wire));
    }

    return snapshot;
}

} // namespace NetlistData

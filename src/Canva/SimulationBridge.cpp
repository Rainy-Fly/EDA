#include"./SimulationBridge.hpp"

#include<map>
#include<memory>
#include<string>
#include<vector>

#include<wx/gdicmn.h>

#include"../DealProjectXML/DealProjectXML.hpp"
#include"../metadata/MetaDataJson.hpp"
#include"./CanvasItem.hpp"    // transform_pin_offset / item_pins_for / MetaData
#include"./MetaData.hpp"

namespace SimulationBridge{
namespace{

// 一个元件定义里的引脚模板（未变换的相对偏移 + 方向）
struct PinTemplate{
    std::string id;
    std::string direction;
    wxPoint offset;
};

// 逻辑门的引脚方向兜底（元数据没有显式 pins 时才用）
std::string guess_direction(const std::string& type, const std::string& pinId){
    const bool gate = (type == "AND" || type == "OR" || type == "NOT" ||
                       type == "NAND" || type == "NOR" || type == "XOR");
    if(!gate) return "passive";
    if(pinId == "OUT" || pinId.rfind("OUT", 0) == 0) return "output";
    return "input";
}

// 取某个类型的引脚模板：优先显式 pins（带 direction），否则用 item_pins_for 的启发式布局
std::vector<PinTemplate> pins_of(const MetaData* metadata, const std::string& type,
                                 const std::string& name){
    std::vector<PinTemplate> pins;
    if(!metadata) return pins;

    if(!metadata->pins.empty()){
        for(const MetaDataPin& def : metadata->pins)
            pins.push_back({def.id, def.direction, wxPoint(def.x, def.y)});
        return pins;
    }
    //旧元数据：退回启发式布局（没有方向信息，按类型推断）
    for(const Pin& pin : item_pins_for(metadata, name))
        pins.push_back({pin.id, guess_direction(type, pin.id), pin.offset});
    return pins;
}

} // namespace

Simulation::SimProject BuildSimulationProject(const DealProjectXML::ProjectData& project){
    Simulation::SimProject sim;

    //同一种元件的引脚模板只解析一次（load_metadata 每次都会读 JSON）
    std::map<std::string, std::vector<PinTemplate>> pinCache;

    for(const DealProjectXML::ComponentData& component : project.components){
        Simulation::SimComponent out;
        out.id = component.id;
        out.type = component.type;
        out.category = component.category;
        out.name = component.name;
        out.params = component.params;

        const std::string key = !component.type.empty() ? component.type : component.name;
        auto cached = pinCache.find(key);
        if(cached == pinCache.end()){
            std::vector<PinTemplate> templates;
            MetaData* metadata = load_metadata(key);
            if(metadata){
                templates = pins_of(metadata, component.type, component.name);
                delete metadata;
            }
            cached = pinCache.insert({key, std::move(templates)}).first;
        }

        for(const PinTemplate& pin : cached->second){
            const wxPoint offset = transform_pin_offset(pin.offset, component.rotation,
                                                        component.mirror);
            Simulation::SimPinRef ref;
            ref.id = pin.id;
            ref.direction = pin.direction;
            ref.position = Simulation::SimPoint(component.x + offset.x,
                                                component.y + offset.y);
            out.pins.push_back(std::move(ref));
        }
        sim.components.push_back(std::move(out));
    }

    for(const DealProjectXML::WireData& wire : project.wires){
        Simulation::SimWire out;
        out.id = wire.id;
        out.type = wire.type;
        for(const wxPoint& point : wire.points)
            out.points.push_back(Simulation::SimPoint(point.x, point.y));

        const Simulation::SimPoint front = out.points.empty()
            ? Simulation::SimPoint() : out.points.front();
        const Simulation::SimPoint back = out.points.empty()
            ? Simulation::SimPoint() : out.points.back();

        const auto convert = [](const DealProjectXML::WireEndpointData& source,
                                const Simulation::SimPoint& position){
            Simulation::SimEndpoint endpoint;
            endpoint.kind = source.kind;
            endpoint.componentId = source.component_id;
            endpoint.pinId = source.pin_id;
            endpoint.junctionId = source.junction_id;
            endpoint.labelId = source.label_id;
            endpoint.position = position;
            return endpoint;
        };
        out.start = convert(wire.start, front);
        out.end = convert(wire.end, back);
        sim.wires.push_back(std::move(out));
    }

    for(const DealProjectXML::JunctionData& junction : project.junctions)
        sim.junctionPositions.push_back(Simulation::SimPoint(junction.x, junction.y));

    for(const DealProjectXML::LabelData& label : project.labels){
        Simulation::SimLabel out;
        out.id = label.id;
        out.text = label.text;
        out.scope = label.scope;
        out.position = Simulation::SimPoint(label.x, label.y);
        sim.labels.push_back(std::move(out));
    }

    for(const DealProjectXML::NoConnectData& noConnect : project.no_connects)
        sim.noConnects.push_back({noConnect.component_id, noConnect.pin_id});

    return sim;
}

} // namespace SimulationBridge

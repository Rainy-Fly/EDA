#include"./DealProjectXML.hpp"
#include"../Canva/Canvas.hpp"
#include"../Canva/CanvasItem.hpp"
#include"../Canva/ItemSVG.hpp"
#include"../Canva/MetaData.hpp"
#include"../metadata/MetaDataJson.hpp"

#include<map>
#include<set>
#include<unordered_map>
#include<utility>

namespace{

// 把 XML 参数应用到元数据；逻辑门的专用参数走类型化 setter。
void apply_param(MetaData* md, const std::string& key, const std::string& value){
    if(!md) return;
    auto to_int = [&](int def){
        try{ return std::stoi(value); }catch(...){ return def; }
    };
    if(LogicGateMetaData* gate = dynamic_cast<LogicGateMetaData*>(md)){
        if(key == "DataBits"){ gate->set_data_bits(to_int(gate->get_data_bits())); return; }
        if(key == "inputs"){ gate->set_inputs(to_int(gate->get_inputs())); return; }
        if(key == "outputs"){ gate->set_outputs(to_int(gate->get_outputs())); return; }
    }
    md->params[key] = value;
}

struct PinAt{
    int component_id = 0;
    std::string pin_id;
    bool ambiguous = false;
};

// 把导线端点写成 XML 2.0 的显式引用；无法解析时写 free。
DealProjectXML::WireEndpointData resolve_endpoint(
    const wxPoint& point,
    const WireEndpointRef& stored,
    const std::map<std::pair<int,int>, PinAt>& pins,
    const std::map<std::pair<int,int>, std::string>& junctions,
    const std::map<std::pair<int,int>, std::string>& labels,
    const std::set<int>& componentIds,
    const std::set<std::string>& junctionIds,
    const std::set<std::string>& labelIds,
    const std::set<std::pair<int,std::string>>& validPins)
{
    DealProjectXML::WireEndpointData ep;
    ep.kind = stored.kind;
    ep.component_id = stored.component_id;
    ep.pin_id = stored.pin_id;
    ep.junction_id = stored.junction_id;
    ep.label_id = stored.label_id;

    // 显式引用仍然有效：保留（移动元件后端点引用比坐标更可靠）。
    if(ep.kind == "pin" && componentIds.count(ep.component_id) &&
       validPins.count({ep.component_id, ep.pin_id})) return ep;
    if(ep.kind == "junction" && junctionIds.count(ep.junction_id)) return ep;
    if(ep.kind == "label" && labelIds.count(ep.label_id)) return ep;
    if(ep.kind == "free") return ep;

    ep = {};
    const auto key = std::make_pair(point.x, point.y);
    auto pinIt = pins.find(key);
    if(pinIt != pins.end() && !pinIt->second.ambiguous){
        ep.kind = "pin";
        ep.component_id = pinIt->second.component_id;
        ep.pin_id = pinIt->second.pin_id;
        return ep;
    }
    auto junctionIt = junctions.find(key);
    if(junctionIt != junctions.end() && !junctionIt->second.empty()){
        ep.kind = "junction";
        ep.junction_id = junctionIt->second;
        return ep;
    }
    auto labelIt = labels.find(key);
    if(labelIt != labels.end() && !labelIt->second.empty()){
        ep.kind = "label";
        ep.label_id = labelIt->second;
        return ep;
    }
    ep.kind = "free";
    return ep;
}

} // namespace

// ================= 画布互转 =================

DealProjectXML::ProjectData DealProjectXML::CollectFromCanvas(const Canvas* canvas,
                                                              const ProjectInfo& info){
    ProjectData data;
    data.schema_version = "2.0";
    data.info = info;
    if(!canvas) return data;
    data.offset = canvas->get_offset();
    data.scale = canvas->get_scale();
    data.grid_step = canvas->get_grid_step();

    //---- 引脚坐标索引：用于把旧版几何端点升级为 2.0 显式引用 ----
    std::map<std::pair<int,int>, PinAt> pinsAt;
    std::set<std::pair<int,std::string>> validPins;
    std::set<int> componentIds;
    for(const CanvasItem* item : canvas->get_items()){
        if(dynamic_cast<const Linking*>(item)) continue;
        componentIds.insert(item->id);
        for(const Pin& pin : item_pins(*item)){
            validPins.insert({item->id, pin.id});
            const int x = item->coords_x + pin.offset.x;
            const int y = item->coords_y + pin.offset.y;
            const auto key = std::make_pair(x, y);
            auto it = pinsAt.find(key);
            if(it == pinsAt.end()){
                pinsAt[key] = PinAt{item->id, pin.id, false};
            }else if(it->second.component_id != item->id || it->second.pin_id != pin.id){
                it->second.ambiguous = true;   //同一坐标多个引脚：不臆测连接
            }
        }
    }

    std::map<std::pair<int,int>, std::string> junctionsAt;
    std::set<std::string> junctionIds;
    for(const CanvasJunction& j : canvas->junctions){
        junctionIds.insert(j.id);
        const auto key = std::make_pair(j.position.x, j.position.y);
        auto it = junctionsAt.find(key);
        if(it == junctionsAt.end()) junctionsAt[key] = j.id;
        else it->second.clear();   //同坐标多个 junction：视为不明确
    }
    std::map<std::pair<int,int>, std::string> labelsAt;
    std::set<std::string> labelIds;
    for(const CanvasLabel& l : canvas->labels){
        labelIds.insert(l.id);
        const auto key = std::make_pair(l.position.x, l.position.y);
        auto it = labelsAt.find(key);
        if(it == labelsAt.end()) labelsAt[key] = l.id;
        else it->second.clear();
    }

    //---- 项目引用的库版本：优先使用加载时保留的列表，否则从元件定义推导 ----
    data.libraries.reserve(canvas->libraries.size());
    for(const CanvasLibrary& lib : canvas->libraries)
        data.libraries.push_back({lib.id, lib.version});
    if(data.libraries.empty()){
        std::set<std::pair<std::string,std::string>> seen;
        for(const CanvasItem* item : canvas->get_items()){
            if(dynamic_cast<const Linking*>(item) || !item->metadata) continue;
            const std::string id = item->metadata->library_id.empty()
                                       ? "builtin" : item->metadata->library_id;
            const std::string version = item->metadata->library_version.empty()
                                            ? "1.0" : item->metadata->library_version;
            if(seen.insert({id, version}).second) data.libraries.push_back({id, version});
        }
    }

    data.junctions.reserve(canvas->junctions.size());
    for(const CanvasJunction& j : canvas->junctions)
        data.junctions.push_back({j.id, j.position.x, j.position.y});
    data.labels.reserve(canvas->labels.size());
    for(const CanvasLabel& l : canvas->labels)
        data.labels.push_back({l.id, l.text, l.scope, l.position.x, l.position.y});
    data.no_connects.reserve(canvas->no_connects.size());
    for(const CanvasNoConnect& nc : canvas->no_connects)
        data.no_connects.push_back({nc.component_id, nc.pin_id});

    for(const CanvasItem* item : canvas->get_items()){
        if(const Linking* wire = dynamic_cast<const Linking*>(item)){
            WireData wd;
            wd.id = item->id;
            wd.type = (item->metadata && !item->metadata->get_type().empty())
                          ? item->metadata->get_type() : "WIRE";
            wd.name = item->name;
            if(item->metadata) wd.params = item->metadata->params;
            wd.points = wire->points;
            // 显式引用优先；否则按首/尾点与当前引脚/junction/label 几何解析。
            if(!wd.points.empty()){
                wd.start = resolve_endpoint(wd.points.front(), wire->start_endpoint,
                                            pinsAt, junctionsAt, labelsAt,
                                            componentIds, junctionIds, labelIds, validPins);
                wd.end = resolve_endpoint(wd.points.back(), wire->end_endpoint,
                                          pinsAt, junctionsAt, labelsAt,
                                          componentIds, junctionIds, labelIds, validPins);
            }
            data.wires.push_back(std::move(wd));
        }else{
            ComponentData cd;
            cd.id = item->id;
            cd.type = (item->metadata && !item->metadata->get_type().empty())
                          ? item->metadata->get_type() : item_type_name(item->type);
            if(cd.type.empty()) cd.type = item->name;
            cd.name = item->name;
            if(item->metadata){
                cd.library_id = item->metadata->library_id;
                cd.definition_version = item->metadata->definition_version;
                cd.category = item->metadata->get_category();
                cd.description = item->metadata->get_description();
                cd.params = item->metadata->params;
            }
            // 实例级属性从 params 中移出，避免同一信息存在两个来源。
            // 属性栏目前仍直接编辑 metadata->params，因此实例字段为空时回退到参数值，
            // 保证“在属性栏改过的值”也能写进类型化字段。
            auto meta_param = [&](const char* key){
                return item->metadata ? item->metadata->get_param(key) : std::string();
            };
            cd.params.erase("reference");
            cd.params.erase("value");
            cd.params.erase("footprint");
            cd.params.erase("reference_prefix");
            cd.params.erase("pin_numbers");
            cd.params.erase("rotation");
            cd.params.erase("mirror");
            cd.reference = !item->reference.empty() ? item->reference : meta_param("reference");
            cd.value = !item->value.empty() ? item->value : meta_param("value");
            cd.footprint = !item->footprint.empty() ? item->footprint : meta_param("footprint");
            cd.rotation = item->rotation;
            cd.mirror = item->mirror;
            cd.pin_overrides.reserve(item->pin_number_overrides.size());
            for(const auto& kv : item->pin_number_overrides)
                cd.pin_overrides.push_back({kv.first, kv.second});
            cd.x = item->coords_x;
            cd.y = item->coords_y;
            data.components.push_back(std::move(cd));
        }
    }
    return data;
}

void DealProjectXML::ApplyToCanvas(Canvas* canvas, const ProjectData& data){
    if(!canvas) return;
    canvas->clear_all_items();

    // 电气注解与库版本不属于画布图元，直接恢复到画布项目级状态。
    canvas->libraries.clear();
    for(const LibraryData& lib : data.libraries)
        canvas->libraries.push_back({lib.id, lib.version});
    canvas->junctions.clear();
    for(const JunctionData& j : data.junctions)
        canvas->junctions.push_back({j.id, wxPoint(j.x, j.y)});
    canvas->labels.clear();
    for(const LabelData& l : data.labels)
        canvas->labels.push_back({l.id, l.text, l.scope, wxPoint(l.x, l.y)});
    canvas->no_connects.clear();
    for(const NoConnectData& nc : data.no_connects)
        canvas->no_connects.push_back({nc.component_id, nc.pin_id});

    // 元件（门/通用元件）：按类型串创建（类型串是元数据最可靠的键）
    for(const ComponentData& cd : data.components){
        if(cd.type.empty()) continue;
        CanvasItem* item = ItemSVG(cd.type);
        if(!item) continue;
        item->name = cd.name.empty() ? cd.type : cd.name;
        item->coords_x = cd.x;
        item->coords_y = cd.y;
        CanvasItem::reserve_id(cd.id);
        item->id = cd.id;
        item->rotation = ((cd.rotation % 360) + 360) % 360;
        item->mirror = (cd.mirror == "x" || cd.mirror == "y") ? cd.mirror : "none";
        item->reference = cd.reference;
        item->value = cd.value;
        item->footprint = cd.footprint;
        item->pin_number_overrides.clear();
        for(const PinOverrideData& pin : cd.pin_overrides)
            item->pin_number_overrides[pin.id] = pin.number;
        if(item->metadata){
            if(!cd.description.empty()) item->metadata->set_description(cd.description);
            for(const auto& kv : cd.params) apply_param(item->metadata, kv.first, kv.second);
            // 让属性栏继续能显示/编辑实例覆盖值。
            if(!cd.reference.empty()) item->metadata->params["reference"] = cd.reference;
            if(!cd.value.empty()) item->metadata->params["value"] = cd.value;
            if(!cd.footprint.empty()) item->metadata->params["footprint"] = cd.footprint;
        }
        canvas->add_item(item);
    }

    // 导线/总线：折点不足2个的不还原
    for(const WireData& wd : data.wires){
        if(wd.type.empty() || wd.points.size() < 2) continue;
        CanvasItem* item = ItemSVG(wd.type);   //"WIRE"->导线元数据；"BUS"->总线
        Linking* wire = dynamic_cast<Linking*>(item);
        if(!wire){ delete item; continue; }    //类型不是导线：跳过
        // 修正元数据：ItemSVG对"BUS"会先命中"WIRE"元数据，这里按类型串重取
        if(!wire->metadata || wire->metadata->get_type() != wd.type){
            if(MetaData* md = load_metadata(wd.type)){
                delete wire->metadata;
                wire->metadata = md;
            }
        }
        wire->name = wd.name.empty() ? wd.type : wd.name;
        wire->points = wd.points;
        wire->start_endpoint = {wd.start.kind, wd.start.component_id, wd.start.pin_id,
                                wd.start.junction_id, wd.start.label_id};
        wire->end_endpoint = {wd.end.kind, wd.end.component_id, wd.end.pin_id,
                              wd.end.junction_id, wd.end.label_id};
        CanvasItem::reserve_id(wd.id);
        wire->id = wd.id;
        if(wire->metadata){
            for(const auto& kv : wd.params) apply_param(wire->metadata, kv.first, kv.second);
        }
        canvas->add_item(wire);
    }

    // 视口：缩放必须为正（异常值按1.0处理）
    const float s = (data.scale >= 0.01f) ? data.scale : 1.0f;
    canvas->set_view(data.offset, s);
    canvas->Refresh();
}

bool DealProjectXML::SaveProject(Canvas* canvas, const std::string& path,
                                 const ProjectInfo& info){
    if(!canvas || path.empty()) return false;
    return WriteXml(CollectFromCanvas(canvas, info), path);
}

bool DealProjectXML::LoadProject(Canvas* canvas, const std::string& path, ProjectInfo* info){
    if(!canvas || path.empty()) return false;
    ProjectData data;
    if(!ReadXml(path, &data)) return false;   //解析失败：画布保持不变
    if(info) *info = data.info;
    ApplyToCanvas(canvas, data);
    return true;
}

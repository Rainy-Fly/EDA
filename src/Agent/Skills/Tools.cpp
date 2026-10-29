#include"./Skills.hpp"
#include"../../Canva/Canvas.hpp"
#include"../../Canva/CanvasItem.hpp"
#include"../../Canva/ItemSVG.hpp"
#include"../../metadata/MetaDataJson.hpp"
#include"../../DealProjectXML/DealProjectXML.hpp"
#include"../../Netlist/PcbProjectAdapter.hpp"
#include"../../Netlist/PcbNetlistBuilder.hpp"

#include<cmath>
#include<set>
#include<string>

// 画布工具的具体实现（src/Agent/Skills/）：
// 把 list.xml 里声明的 handler 绑定到真正操作画布的 C++ 函数。
// 注意：这些函数会触碰 wxWidgets 控件（add_item 会创建子窗口），
// 只能由主线程调用——Controller 通过“工具桥”把执行派发回主线程。
namespace Agent{

namespace{

// 网格吸附（与画布一致：GridStep=10 的倍数）
int snap(int v){
    return (int)std::lround((float)v / 10.0f) * 10;
}

int num_to_int(const Json::Value& v){
    return (int)std::lround(v.asDouble());
}

// 取元件类型串（元数据type优先，兜底item_type_name）
std::string item_type_str(const CanvasItem* item){
    if(item->metadata && !item->metadata->get_type().empty()){
        return item->metadata->get_type();
    }
    return item_type_name(item->type);
}

// 元件各引脚的绝对逻辑坐标。
// item_pins() 返回的 offset 已经按实例的 rotation/mirror 变换过（见 CanvasItem.hpp），
// 所以锚点 + offset 就是画在画布上的引脚位置——AI 直接用这些点当 connect_wire 的折点即可接上。
Json::Value pins_json(const CanvasItem& item){
    Json::Value pins(Json::arrayValue);
    for(const Pin& p : item_pins(item)){
        Json::Value pj;
        pj["id"]     = p.id;
        pj["name"]   = p.name;
        pj["number"] = p.number;
        pj["x"]      = item.coords_x + p.offset.x;
        pj["y"]      = item.coords_y + p.offset.y;
        pins.append(pj);
    }
    return pins;
}

// 导线端点引用 -> JSON（让模型知道这根线电气上连到了哪个引脚/junction/标签）
Json::Value endpoint_json(const WireEndpointRef& ep){
    Json::Value j(Json::objectValue);
    if(ep.kind.empty()) return j;      //旧格式：只有几何，没有电气端点
    j["kind"] = ep.kind;
    if(ep.kind == "pin"){
        j["component"] = ep.component_id;
        j["pin"] = ep.pin_id;
    }else if(ep.kind == "junction"){
        j["junction"] = ep.junction_id;
    }else if(ep.kind == "label"){
        j["label"] = ep.label_id;
    }
    return j;
}

// 元件实例字段（旋转/镜像/位号/值/封装/坐标/引脚）-> JSON。
// 修改类工具返回它，模型就能立刻知道改完以后的真实状态（尤其引脚位置变了）。
Json::Value component_state_json(const CanvasItem& item){
    Json::Value j;
    j["ok"]        = true;
    j["id"]        = item.id;
    j["type"]      = item_type_str(&item);
    j["x"]         = item.coords_x;
    j["y"]         = item.coords_y;
    j["rotation"]  = item.rotation;
    j["mirror"]    = item.mirror;
    j["reference"] = item.reference;
    j["value"]     = item.value;
    j["footprint"] = item.footprint;
    j["pins"]      = pins_json(item);
    return j;
}

// 直接改实例字段后收尾：按当前 scale 重绘位图（旋转/镜像会改变渲染）+ 标记项目已修改 + 重画。
void finish_item_change(Canvas* canvas, CanvasItem* item){
    item->update_bitmap(canvas->get_scale());
    canvas->notify_changed();
    canvas->Refresh();
}

// 网表构建器的诊断码分类：哪些是“电路连错了”（模型该去修），
// 哪些只是“PCB 导出不够完整”（缺封装/缺位号等，不代表电路有问题）。
// 分类依据是 PcbNetlistBuilder.cpp 里实际会产出的那组码。
bool is_connectivity_code(const std::string& code){
    static const std::set<std::string> kConnectivity = {
        "PCB_NETLIST_UNCONNECTED_PIN",            //引脚没接任何东西
        "PCB_NETLIST_WIRE_ENDPOINT_ON_SEGMENT",   //线端搭在另一条线中间（T 形未连接）
        "PCB_NETLIST_PIN_ON_WIRE_SEGMENT",        //引脚落在导线中间（没有端点相接）
        "PCB_NETLIST_WIRE_WITHOUT_PIN",           //导线两端都没接引脚
        "PCB_NETLIST_ENDPOINT_PIN_MISSING",       //端点声明引用了不存在的引脚
        "PCB_NETLIST_ENDPOINT_JUNCTION_MISSING",  //端点声明引用了不存在的 junction
        "PCB_NETLIST_ENDPOINT_LABEL_MISSING",     //端点声明引用了不存在的标签
        "PCB_NETLIST_ENDPOINT_KIND_UNKNOWN",      //端点 kind 非法
        "PCB_NETLIST_JUNCTION_WITHOUT_WIRE",      //junction 没被任何导线用到
        "PCB_NETLIST_LABEL_WITHOUT_WIRE",         //标签没被任何导线用到
        "PCB_NETLIST_WIRE_TOO_SHORT",             //导线折点少于 2
        "PCB_NETLIST_WIRE_ZERO_LENGTH",           //导线长度为 0
        "PCB_NETLIST_WIRE_ID_DUPLICATE",
        "PCB_NETLIST_JUNCTION_ID_EMPTY",
        "PCB_NETLIST_JUNCTION_ID_DUPLICATE",
        "PCB_NETLIST_LABEL_ID_EMPTY",
        "PCB_NETLIST_LABEL_ID_DUPLICATE",
        "PCB_NETLIST_LABEL_TEXT_EMPTY",
    };
    return kConnectivity.count(code) != 0;
}

} // namespace

// 注册全部画布工具；canvas 为操作对象（传nullptr时工具统一返回错误）
void RegisterCanvasTools(ToolRegistry& reg, Canvas* canvas){

    //---- 只读：画布现状 ----
    reg.bind("get_canvas_state", [canvas](const Json::Value&)->std::string{
        Json::Value out;
        if(!canvas){
            out["ok"] = false; out["error"] = "画布不可用";
            return out.toStyledString();
        }
        const wxPoint off = canvas->get_offset();
        out["view"]["offset_x"] = off.x;
        out["view"]["offset_y"] = off.y;
        out["view"]["scale"]    = canvas->get_scale();
        out["view"]["grid_step"]= canvas->get_grid_step();

        Json::Value comps(Json::arrayValue), wires(Json::arrayValue);
        for(const CanvasItem* item : canvas->get_items()){
            Json::Value j;
            j["id"] = item->id;
            j["name"] = item->name;
            if(const Linking* w = dynamic_cast<const Linking*>(item)){
                j["type"] = item_type_str(item);
                Json::Value pts(Json::arrayValue);
                for(const wxPoint& p : w->points){
                    Json::Value pt(Json::arrayValue);
                    pt.append(p.x); pt.append(p.y);
                    pts.append(pt);
                }
                j["points"] = pts;
                //电气端点：这根线两极到底接在哪个元件的哪个引脚/junction/标签上
                j["start"] = endpoint_json(w->start_endpoint);
                j["end"]   = endpoint_json(w->end_endpoint);
                wires.append(j);
            }else{
                j["type"] = item_type_str(item);
                j["x"] = item->coords_x;
                j["y"] = item->coords_y;
                //实例状态：改属性/旋转之前模型需要先知道现状
                j["rotation"]  = item->rotation;
                j["mirror"]    = item->mirror;
                j["reference"] = item->reference;
                j["value"]     = item->value;
                j["footprint"] = item->footprint;
                if(item->metadata){
                    j["category"] = item->metadata->get_category();
                    Json::Value params(Json::objectValue);
                    for(const auto& kv : item->metadata->params) params[kv.first] = kv.second;
                    j["params"] = params;
                }
                //引脚绝对坐标：画导线时把这里的 (x,y) 直接当折点就是接在引脚上
                j["pins"] = pins_json(*item);
                comps.append(j);
            }
        }
        out["components"] = comps;
        out["wires"] = wires;
        return out.toStyledString();
    });

    //---- 只读：可放置元件清单 ----
    reg.bind("list_available_components", [](const Json::Value&)->std::string{
        Json::Value arr(Json::arrayValue);
        for(const MetaData* m : load_all_metadata()){
            Json::Value j;
            j["type"]     = m->get_type();
            j["name"]     = m->get_name();
            j["name_en"]  = m->get_name_en();
            j["category"] = m->get_category();
            j["id"]       = m->get_id();
            arr.append(j);
        }
        return arr.toStyledString();
    });

    //---- 只读：连通性自查（复用 src/Netlist 的校验，只引用、不改它）----
    reg.bind("check_connectivity", [canvas](const Json::Value&)->std::string{
        Json::Value out;
        out["ok"] = false;
        if(!canvas){ out["error"] = "画布不可用"; return out.toStyledString(); }

        //画布 -> 项目数据（只读快照）-> PCB 快照 -> 网表构建（自带诊断）
        const DealProjectXML::ProjectInfo info;
        const DealProjectXML::ProjectData project = DealProjectXML::CollectFromCanvas(canvas, info);
        const NetlistData::PcbProjectSnapshot snapshot =
            NetlistData::BuildPcbSnapshotFromProject(project);
        const NetlistData::PcbNetlistBuildResult built =
            NetlistData::PcbNetlistBuilder::build(snapshot);

        Json::Value nets(Json::arrayValue);
        int componentCount = 0;
        if(built.value.has_value()){
            componentCount = (int)built.value->components.size();
            for(const NetlistData::PcbNet& net : built.value->nets){
                Json::Value n;
                n["name"] = net.name.empty() ? net.id : net.name;
                Json::Value nodes(Json::arrayValue);
                for(const NetlistData::PcbNode& node : net.nodes){
                    nodes.append(node.reference + "." + node.pinNumber);
                }
                n["nodes"] = nodes;
                nets.append(n);
            }
        }
        out["component_count"] = componentCount;
        out["net_count"] = nets.size();
        out["nets"] = nets;

        //画布上的元件总数，以及“不参与网表”的非物理符号（接地/电源）。
        //不说明的话，用户会疑惑“明明放了 4 个元件，为什么只报 2 个”。
        int canvasComponentCount = 0;
        Json::Value ignored(Json::arrayValue);
        for(const CanvasItem* item : canvas->get_items()){
            if(dynamic_cast<const Linking*>(item)) continue;   //导线不算元件
            ++canvasComponentCount;
            const std::string cat = item->metadata ? item->metadata->get_category() : std::string();
            if(cat == "Ground" || cat == "Source"){
                Json::Value j;
                j["id"] = item->id;
                j["type"] = item_type_str(item);
                j["category"] = cat;
                j["reason"] = "非物理符号（接地/电源），不参与 PCB 网表，它的连线不会出现在 nets 里";
                ignored.append(j);
            }
        }
        out["canvas_component_count"] = canvasComponentCount;
        out["non_physical_ignored"] = ignored;

        //诊断分成两类：电路连错了（problems，模型该去修）vs PCB 导出不够完整（hints）
        Json::Value problems(Json::arrayValue);
        Json::Value hints(Json::arrayValue);
        int unconnected = 0;
        for(const NetlistData::Diagnostic& d : built.diagnostics){
            Json::Value item;
            item["code"] = d.code;
            item["message"] = d.message;
            Json::Value objects(Json::arrayValue);
            for(const std::string& id : d.objectIds) objects.append(id);
            if(!objects.empty()) item["objects"] = objects;
            if(!d.pinId.empty()) item["pin"] = d.pinId;
            if(is_connectivity_code(d.code)){
                problems.append(item);
                if(d.code == "PCB_NETLIST_UNCONNECTED_PIN") ++unconnected;
            }else{
                hints.append(item);
            }
        }
        out["problems"] = problems;
        out["hints"] = hints;
        out["ok"] = problems.empty();
        out["summary"] = "画布元件 " + std::to_string(canvasComponentCount) +
                         " 个（其中非物理符号 " + std::to_string(ignored.size()) +
                         " 个不参与网表）、网络 " + std::to_string(nets.size()) +
                         " 个；连通性问题 " + std::to_string(problems.size()) +
                         " 条（悬空引脚 " + std::to_string(unconnected) +
                         " 个）、其它提示 " + std::to_string(hints.size()) + " 条";
        return out.toStyledString();
    });

    //---- 写：放置元件 ----
    reg.bind("place_component", [canvas](const Json::Value& args)->std::string{
        Json::Value out;
        out["ok"] = false;
        if(!canvas){ out["error"] = "画布不可用"; return out.toStyledString(); }
        if(!args.isObject() || !args.isMember("type") || !args["type"].isString() ||
           !args.isMember("x") || !args.isMember("y")){
            out["error"] = "参数应为 {type,x,y}";
            return out.toStyledString();
        }
        const std::string type = args["type"].asString();
        CanvasItem* item = ItemSVG(type);
        if(!item){
            out["error"] = "无法创建元件: " + type;
            return out.toStyledString();
        }
        item->coords_x = snap(num_to_int(args["x"]));
        item->coords_y = snap(num_to_int(args["y"]));
        canvas->add_item(item);
        out["ok"] = true;
        out["id"] = item->id;
        return out.toStyledString();
    });

    //---- 写：画导线（自动正交拐角，与手绘一致）----
    reg.bind("connect_wire", [canvas](const Json::Value& args)->std::string{
        Json::Value out;
        out["ok"] = false;
        if(!canvas){ out["error"] = "画布不可用"; return out.toStyledString(); }
        if(!args.isMember("points") || !args["points"].isArray() ||
           args["points"].size() < 2){
            out["error"] = "参数应为 {points:[[x,y],...]}（至少2点）";
            return out.toStyledString();
        }
        std::vector<wxPoint> pts;
        for(const Json::Value& p : args["points"]){
            if(p.isArray() && p.size() >= 2){
                pts.push_back(wxPoint(snap(num_to_int(p[0])), snap(num_to_int(p[1]))));
            }
        }
        if(pts.size() < 2){
            out["error"] = "折点不足";
            return out.toStyledString();
        }
        std::vector<wxPoint> clean;
        for(const wxPoint& p : pts){
            if(clean.empty() || clean.back() != p) clean.push_back(p);
        }
        if(clean.size() >= 2) clean = orthogonal_expand(clean);

        CanvasItem* item = ItemSVG("导线");
        Linking* wire = dynamic_cast<Linking*>(item);
        if(!wire){
            delete item;
            out["error"] = "导线创建失败";
            return out.toStyledString();
        }
        wire->points = clean;
        canvas->add_item(wire);
        out["ok"] = true;
        out["id"] = wire->id;
        return out.toStyledString();
    });

    //---- 写：移动元件 ----
    reg.bind("move_component", [canvas](const Json::Value& args)->std::string{
        Json::Value out;
        out["ok"] = false;
        if(!canvas){ out["error"] = "画布不可用"; return out.toStyledString(); }
        if(!args.isMember("id") || !args.isMember("x") || !args.isMember("y")){
            out["error"] = "参数应为 {id,x,y}";
            return out.toStyledString();
        }
        CanvasItem* item = canvas->find_item_by_id(num_to_int(args["id"]));
        if(!item){
            out["error"] = "找不到id=" + std::to_string(num_to_int(args["id"]));
            return out.toStyledString();
        }
        if(dynamic_cast<Linking*>(item)){
            out["error"] = "导线不支持移动（请删除后重画）";
            return out.toStyledString();
        }
        item->coords_x = snap(num_to_int(args["x"]));
        item->coords_y = snap(num_to_int(args["y"]));
        item->update_bitmap(canvas->get_scale());   //按当前scale重绘位图（画布自绘，位置在绘制时算）
        canvas->notify_changed();                   //AI 挪了元件也算修改，标题栏要出现“未保存”
        canvas->Refresh();
        out["ok"] = true;
        return out.toStyledString();
    });

    //---- 写：删除元件/导线 ----
    reg.bind("delete_component", [canvas](const Json::Value& args)->std::string{
        Json::Value out;
        out["ok"] = false;
        if(!canvas){ out["error"] = "画布不可用"; return out.toStyledString(); }
        if(!args.isMember("id")){
            out["error"] = "参数应为 {id}";
            return out.toStyledString();
        }
        CanvasItem* item = canvas->find_item_by_id(num_to_int(args["id"]));
        if(!item){
            out["error"] = "找不到id=" + std::to_string(num_to_int(args["id"]));
            return out.toStyledString();
        }
        canvas->remove_item(item);
        out["ok"] = true;
        return out.toStyledString();
    });

    //---- 写：调整视口 ----
    reg.bind("set_view", [canvas](const Json::Value& args)->std::string{
        Json::Value out;
        out["ok"] = false;
        if(!canvas){ out["error"] = "画布不可用"; return out.toStyledString(); }
        if(!args.isMember("offset_x") || !args.isMember("offset_y") || !args.isMember("scale")){
            out["error"] = "参数应为 {offset_x,offset_y,scale}";
            return out.toStyledString();
        }
        const int    ox = num_to_int(args["offset_x"]);
        const int    oy = num_to_int(args["offset_y"]);
        const double sc = args["scale"].asDouble();
        canvas->set_view(wxPoint(ox, oy), (float)sc);
        out["ok"] = true;
        return out.toStyledString();
    });

    //---- 写：改实例属性（位号/值/封装/参数）----
    reg.bind("set_component_property", [canvas](const Json::Value& args)->std::string{
        Json::Value out;
        out["ok"] = false;
        if(!canvas){ out["error"] = "画布不可用"; return out.toStyledString(); }
        if(!args.isMember("id")){
            out["error"] = "参数应为 {id, reference?, value?, footprint?, params?}";
            return out.toStyledString();
        }
        CanvasItem* item = canvas->find_item_by_id(num_to_int(args["id"]));
        if(!item){
            out["error"] = "找不到id=" + std::to_string(num_to_int(args["id"]));
            return out.toStyledString();
        }
        if(dynamic_cast<Linking*>(item)){
            out["error"] = "导线没有这些属性（值/位号/封装只对元件有效）";
            return out.toStyledString();
        }
        bool changed = false;
        if(args.isMember("reference") && args["reference"].isString()){
            item->reference = args["reference"].asString();
            changed = true;
        }
        if(args.isMember("value") && args["value"].isString()){
            item->value = args["value"].asString();
            changed = true;
        }
        if(args.isMember("footprint") && args["footprint"].isString()){
            item->footprint = args["footprint"].asString();
            changed = true;
        }
        if(args.isMember("params") && args["params"].isObject() && item->metadata){
            Json::StreamWriterBuilder writer;
            writer["indentation"] = "";
            for(auto it = args["params"].begin(); it != args["params"].end(); ++it){
                const std::string key = it.key().asString();
                if(key.empty()) continue;
                //数字/布尔按 JSON 文本存，字符串按原样存（与属性栏写参数的行为一致）
                item->metadata->params[key] =
                    it->isString() ? it->asString() : Json::writeString(writer, *it);
                changed = true;
            }
        }
        if(!changed){
            out["error"] = "没有可改的字段（reference/value/footprint/params 至少给一个）";
            return out.toStyledString();
        }
        finish_item_change(canvas, item);
        return component_state_json(*item).toStyledString();
    });

    //---- 写：旋转（绝对角度，不是增量）----
    reg.bind("rotate_component", [canvas](const Json::Value& args)->std::string{
        Json::Value out;
        out["ok"] = false;
        if(!canvas){ out["error"] = "画布不可用"; return out.toStyledString(); }
        if(!args.isMember("id") || !args.isMember("rotation")){
            out["error"] = "参数应为 {id, rotation}";
            return out.toStyledString();
        }
        const int rotation = num_to_int(args["rotation"]);
        if(rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270){
            out["error"] = "rotation 必须是 0/90/180/270";
            return out.toStyledString();
        }
        CanvasItem* item = canvas->find_item_by_id(num_to_int(args["id"]));
        if(!item){
            out["error"] = "找不到id=" + std::to_string(num_to_int(args["id"]));
            return out.toStyledString();
        }
        if(dynamic_cast<Linking*>(item)){
            out["error"] = "导线不支持旋转（请重画）";
            return out.toStyledString();
        }
        item->rotation = rotation;
        finish_item_change(canvas, item);
        return component_state_json(*item).toStyledString();
    });

    //---- 写：镜像（none/x/y）----
    reg.bind("mirror_component", [canvas](const Json::Value& args)->std::string{
        Json::Value out;
        out["ok"] = false;
        if(!canvas){ out["error"] = "画布不可用"; return out.toStyledString(); }
        if(!args.isMember("id") || !args.isMember("mirror") || !args["mirror"].isString()){
            out["error"] = "参数应为 {id, mirror}，mirror 取 none/x/y";
            return out.toStyledString();
        }
        const std::string mirror = args["mirror"].asString();
        if(mirror != "none" && mirror != "x" && mirror != "y"){
            out["error"] = "mirror 必须是 none/x/y";
            return out.toStyledString();
        }
        CanvasItem* item = canvas->find_item_by_id(num_to_int(args["id"]));
        if(!item){
            out["error"] = "找不到id=" + std::to_string(num_to_int(args["id"]));
            return out.toStyledString();
        }
        if(dynamic_cast<Linking*>(item)){
            out["error"] = "导线不支持镜像（请重画）";
            return out.toStyledString();
        }
        item->mirror = mirror;
        finish_item_change(canvas, item);
        return component_state_json(*item).toStyledString();
    });
}

} // namespace Agent

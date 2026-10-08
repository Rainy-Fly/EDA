#include"./Skills.hpp"
#include"../../Canva/Canvas.hpp"
#include"../../Canva/CanvasItem.hpp"
#include"../../Canva/ItemSVG.hpp"
#include"../../metadata/MetaDataJson.hpp"

#include<cmath>

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
                wires.append(j);
            }else{
                j["type"] = item_type_str(item);
                j["x"] = item->coords_x;
                j["y"] = item->coords_y;
                if(item->metadata){
                    j["category"] = item->metadata->get_category();
                    Json::Value params(Json::objectValue);
                    for(const auto& kv : item->metadata->params) params[kv.first] = kv.second;
                    j["params"] = params;
                }
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
        item->update_ui(canvas->get_scale(), canvas->get_offset());
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
}

} // namespace Agent

#include"./DealProjectXML.hpp"
#include"../Canva/Canvas.hpp"
#include"../Canva/CanvasItem.hpp"
#include"../Canva/ItemSVG.hpp"
#include"../Canva/MetaData.hpp"
#include"../metadata/MetaDataJson.hpp"

#include<wx/xml/xml.h>
#include<ctime>

namespace{

// ---- 小工具 ----
wxString to_wx(const std::string& s){ return wxString::FromUTF8(s.c_str()); }
std::string from_wx(const wxString& s){ return s.ToUTF8().data(); }

// 取元素属性；缺失返回默认值
wxString attr(const wxXmlNode* n, const char* key, const wxString& def = wxEmptyString){
    wxString v;
    if(n->GetAttribute(wxString::FromUTF8(key), &v)) return v;
    return def;
}

// 元素文本内容（text/cdata子节点拼接）
wxString text_of(const wxXmlNode* n){
    wxString t;
    for(const wxXmlNode* c = n->GetChildren(); c; c = c->GetNext()){
        const wxXmlNodeType ty = c->GetType();
        if(ty == wxXML_TEXT_NODE || ty == wxXML_CDATA_SECTION_NODE) t += c->GetContent();
    }
    return t;
}

wxXmlNode* new_element(const char* name){
    return new wxXmlNode(wxXML_ELEMENT_NODE, wxString::FromUTF8(name));
}

void add_text_child(wxXmlNode* parent, const char* name, const std::string& value){
    wxXmlNode* n = new_element(name);
    n->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, to_wx(value)));
    parent->AddChild(n);
}

void add_param_child(wxXmlNode* parent, const std::pair<std::string,std::string>& kv){
    wxXmlNode* p = new_element("param");
    p->AddAttribute(wxString::FromUTF8("key"), to_wx(kv.first));
    p->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, to_wx(kv.second)));
    parent->AddChild(p);
}

wxXmlNode* add_point_child(wxXmlNode* parent, const wxPoint& pt){
    wxXmlNode* p = new_element("point");
    p->AddAttribute(wxString::FromUTF8("x"), wxString::Format(wxT("%d"), pt.x));
    p->AddAttribute(wxString::FromUTF8("y"), wxString::Format(wxT("%d"), pt.y));
    parent->AddChild(p);
    return p;
}

// 当前时间（ISO 8601，不含时区偏移的本地时间）
std::string now_iso(){
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

// 把参数写入元数据：门类的inputs/outputs/DataBits走类型化setter，其余写入通用params
void apply_param(MetaData* md, const std::string& key, const std::string& value){
    if(!md) return;
    auto to_int = [&](int def)->int{
        try{ return std::stoi(value); }catch(...){ return def; }
    };
    if(LogicGateMetaData* g = dynamic_cast<LogicGateMetaData*>(md)){
        if(key == "inputs"){   int v = to_int(g->get_inputs());   if(v > 0) g->set_inputs(v);   return; }
        if(key == "outputs"){  int v = to_int(g->get_outputs());  if(v > 0) g->set_outputs(v);  return; }
        if(key == "DataBits"){ int v = to_int(g->get_data_bits());if(v > 0) g->set_data_bits(v);return; }
    }
    md->params[key] = value;
}

} // namespace

// ================= 纯XML读写 =================

bool DealProjectXML::WriteXml(const ProjectData& data, const std::string& path){
    if(path.empty()) return false;
    wxXmlDocument doc;
    wxXmlNode* root = new_element("teda-project");
    root->AddAttribute(wxString::FromUTF8("schema-version"), wxString::FromUTF8("1.0"));
    doc.SetRoot(root);

    //---- info：项目信息 + 视口 ----
    wxXmlNode* info = new_element("info");
    add_text_child(info, "name",        data.info.name.empty() ? "Untitled" : data.info.name);
    add_text_child(info, "author",      data.info.author);
    add_text_child(info, "created",     data.info.created.empty() ? now_iso() : data.info.created);
    add_text_child(info, "modified",    now_iso());
    add_text_child(info, "description", data.info.description);
    wxXmlNode* view = new_element("view");
    view->AddAttribute(wxString::FromUTF8("offset-x"), wxString::Format(wxT("%d"), data.offset.x));
    view->AddAttribute(wxString::FromUTF8("offset-y"), wxString::Format(wxT("%d"), data.offset.y));
    view->AddAttribute(wxString::FromUTF8("scale"),    wxString::Format(wxT("%g"), (double)data.scale));
    view->AddAttribute(wxString::FromUTF8("grid-step"),wxString::Format(wxT("%d"), data.grid_step));
    info->AddChild(view);
    root->AddChild(info);

    //---- circuit：元件 + 导线 ----
    wxXmlNode* circuit = new_element("circuit");
    for(const ComponentData& c : data.components){
        wxXmlNode* n = new_element("component");
        n->AddAttribute(wxString::FromUTF8("id"),   wxString::Format(wxT("%d"), c.id));
        n->AddAttribute(wxString::FromUTF8("type"), to_wx(c.type));
        n->AddAttribute(wxString::FromUTF8("name"), to_wx(c.name));
        if(!c.category.empty()) n->AddAttribute(wxString::FromUTF8("category"), to_wx(c.category));
        n->AddAttribute(wxString::FromUTF8("x"),    wxString::Format(wxT("%d"), c.x));
        n->AddAttribute(wxString::FromUTF8("y"),    wxString::Format(wxT("%d"), c.y));
        if(!c.description.empty()) add_text_child(n, "description", c.description);
        for(const auto& kv : c.params) add_param_child(n, kv);
        circuit->AddChild(n);
    }
    for(const WireData& w : data.wires){
        wxXmlNode* n = new_element("wire");
        n->AddAttribute(wxString::FromUTF8("id"),   wxString::Format(wxT("%d"), w.id));
        n->AddAttribute(wxString::FromUTF8("type"), to_wx(w.type));
        if(!w.name.empty()) n->AddAttribute(wxString::FromUTF8("name"), to_wx(w.name));
        for(const auto& kv : w.params) add_param_child(n, kv);
        for(const wxPoint& pt : w.points) add_point_child(n, pt);
        circuit->AddChild(n);
    }
    root->AddChild(circuit);

    return doc.Save(to_wx(path), 2);
}

bool DealProjectXML::ReadXml(const std::string& path, ProjectData* data){
    if(path.empty() || !data) return false;
    wxXmlDocument doc;
    if(!doc.Load(to_wx(path))) return false;
    wxXmlNode* root = doc.GetRoot();
    if(!root || root->GetName() != wxT("teda-project")) return false;

    ProjectData out;
    //容错解析：未知元素/未知属性忽略不报错（与“缺失不报错”的容错原则一致）
    for(const wxXmlNode* child = root->GetChildren(); child; child = child->GetNext()){
        if(child->GetType() != wxXML_ELEMENT_NODE) continue;
        if(child->GetName() == wxT("info")){
            for(const wxXmlNode* c = child->GetChildren(); c; c = c->GetNext()){
                if(c->GetType() != wxXML_ELEMENT_NODE) continue;
                const wxString cn = c->GetName();
                if(cn == wxT("name"))        out.info.name        = from_wx(text_of(c));
                else if(cn == wxT("author")) out.info.author      = from_wx(text_of(c));
                else if(cn == wxT("created"))out.info.created     = from_wx(text_of(c));
                else if(cn == wxT("modified"))out.info.modified   = from_wx(text_of(c));
                else if(cn == wxT("description"))out.info.description = from_wx(text_of(c));
                else if(cn == wxT("view")){
                    long v;
                    if(attr(c, "offset-x").ToLong(&v)) out.offset.x = (int)v;
                    if(attr(c, "offset-y").ToLong(&v)) out.offset.y = (int)v;
                    double d;
                    if(attr(c, "scale").ToDouble(&d)) out.scale = (float)d;
                    if(attr(c, "grid-step").ToLong(&v)) out.grid_step = (int)v;
                }
            }
        }else if(child->GetName() == wxT("circuit")){
            for(const wxXmlNode* c = child->GetChildren(); c; c = c->GetNext()){
                if(c->GetType() != wxXML_ELEMENT_NODE) continue;
                if(c->GetName() == wxT("component")){
                    ComponentData cd;
                    long v;
                    if(attr(c, "id").ToLong(&v)) cd.id = (int)v;
                    cd.type = from_wx(attr(c, "type"));
                    cd.name = from_wx(attr(c, "name"));
                    cd.category = from_wx(attr(c, "category"));
                    if(attr(c, "x").ToLong(&v)) cd.x = (int)v;
                    if(attr(c, "y").ToLong(&v)) cd.y = (int)v;
                    for(const wxXmlNode* cc = c->GetChildren(); cc; cc = cc->GetNext()){
                        if(cc->GetType() != wxXML_ELEMENT_NODE) continue;
                        if(cc->GetName() == wxT("description")) cd.description = from_wx(text_of(cc));
                        else if(cc->GetName() == wxT("param")){
                            cd.params[from_wx(attr(cc, "key"))] = from_wx(text_of(cc));
                        }
                    }
                    out.components.push_back(std::move(cd));
                }else if(c->GetName() == wxT("wire")){
                    WireData wd;
                    long v;
                    if(attr(c, "id").ToLong(&v)) wd.id = (int)v;
                    wd.type = from_wx(attr(c, "type"));
                    wd.name = from_wx(attr(c, "name"));
                    for(const wxXmlNode* cc = c->GetChildren(); cc; cc = cc->GetNext()){
                        if(cc->GetType() != wxXML_ELEMENT_NODE) continue;
                        if(cc->GetName() == wxT("param")){
                            wd.params[from_wx(attr(cc, "key"))] = from_wx(text_of(cc));
                        }else if(cc->GetName() == wxT("point")){
                            wxPoint p;
                            if(attr(cc, "x").ToLong(&v)) p.x = (int)v;
                            if(attr(cc, "y").ToLong(&v)) p.y = (int)v;
                            wd.points.push_back(p);
                        }
                    }
                    out.wires.push_back(std::move(wd));
                }
            }
        }
    }
    *data = std::move(out);
    return true;
}

// ================= 画布互转 =================

DealProjectXML::ProjectData DealProjectXML::CollectFromCanvas(const Canvas* canvas,
                                                              const ProjectInfo& info){
    ProjectData data;
    data.info = info;
    if(!canvas) return data;
    data.offset = canvas->get_offset();
    data.scale = canvas->get_scale();
    data.grid_step = canvas->get_grid_step();

    for(const CanvasItem* item : canvas->get_items()){
        if(const Linking* wire = dynamic_cast<const Linking*>(item)){
            WireData wd;
            wd.id = item->id;
            wd.type = (item->metadata && !item->metadata->get_type().empty())
                          ? item->metadata->get_type() : "WIRE";
            wd.name = item->name;
            if(item->metadata) wd.params = item->metadata->params;
            wd.points = wire->points;
            data.wires.push_back(std::move(wd));
        }else{
            ComponentData cd;
            cd.id = item->id;
            cd.type = (item->metadata && !item->metadata->get_type().empty())
                          ? item->metadata->get_type() : item_type_name(item->type);
            if(cd.type.empty()) cd.type = item->name;
            cd.name = item->name;
            if(item->metadata){
                cd.category = item->metadata->get_category();
                cd.description = item->metadata->get_description();
                cd.params = item->metadata->params;
            }
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
        if(item->metadata){
            if(!cd.description.empty()) item->metadata->set_description(cd.description);
            for(const auto& kv : cd.params) apply_param(item->metadata, kv.first, kv.second);
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

#include "./DealProjectXML.hpp"

#include <wx/xml/xml.h>
#include <ctime>
#include <set>
#include <unordered_set>

#if defined(_MSC_VER) || defined(__MINGW32__)
    #define localtime_r(timep, result) localtime_s(result, timep)
#endif

namespace{

constexpr const char* kCurrentSchema = "2.0";

wxString to_wx(const std::string& s){ return wxString::FromUTF8(s.c_str()); }
std::string from_wx(const wxString& s){ return s.ToUTF8().data(); }

wxString attr(const wxXmlNode* n, const char* key, const wxString& def = wxEmptyString){
    wxString v;
    if(n->GetAttribute(wxString::FromUTF8(key), &v)) return v;
    return def;
}

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

void add_attr(wxXmlNode* n, const char* key, const std::string& value){
    n->AddAttribute(wxString::FromUTF8(key), to_wx(value));
}

void add_int_attr(wxXmlNode* n, const char* key, int value){
    n->AddAttribute(wxString::FromUTF8(key), wxString::Format(wxT("%d"), value));
}

void add_param_child(wxXmlNode* parent, const std::pair<std::string,std::string>& kv){
    wxXmlNode* p = new_element("param");
    p->AddAttribute(wxString::FromUTF8("key"), to_wx(kv.first));
    p->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, to_wx(kv.second)));
    parent->AddChild(p);
}

void add_point_child(wxXmlNode* parent, const wxPoint& pt){
    wxXmlNode* p = new_element("point");
    add_int_attr(p, "x", pt.x);
    add_int_attr(p, "y", pt.y);
    parent->AddChild(p);
}

std::string now_iso(){
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

void write_endpoint(wxXmlNode* parent, const char* side,
                    const DealProjectXML::WireEndpointData& ep){
    if(ep.kind.empty()) return;   //旧版数据没有显式端点：保持“仅几何”语义
    wxXmlNode* n = new_element("endpoint");
    add_attr(n, "side", side);
    add_attr(n, "kind", ep.kind);
    if(ep.kind == "pin"){
        add_int_attr(n, "component", ep.component_id);
        add_attr(n, "pin", ep.pin_id);
    }else if(ep.kind == "junction"){
        add_attr(n, "junction", ep.junction_id);
    }else if(ep.kind == "label"){
        add_attr(n, "label", ep.label_id);
    }
    parent->AddChild(n);
}

DealProjectXML::WireEndpointData read_endpoint(const wxXmlNode* n){
    DealProjectXML::WireEndpointData ep;
    ep.kind = from_wx(attr(n, "kind"));
    long v = 0;
    if(attr(n, "component").ToLong(&v)) ep.component_id = (int)v;
    ep.pin_id = from_wx(attr(n, "pin"));
    ep.junction_id = from_wx(attr(n, "junction"));
    ep.label_id = from_wx(attr(n, "label"));
    return ep;
}

// 旧版 pin_numbers: "A=1;B=2" 或 "1,2"。
// 这里把实例参数迁移为显式 <pin> 覆盖，避免 2.0 中同时存在两种焊盘号来源。
void migrate_legacy_pin_numbers(const std::string& text,
                                std::vector<DealProjectXML::PinOverrideData>& out){
    std::string token;
    std::vector<std::string> positional;
    auto flush = [&](){
        if(token.empty()) return;
        const std::size_t eq = token.find('=');
        if(eq != std::string::npos){
            const std::string key = token.substr(0, eq);
            const std::string value = token.substr(eq + 1);
            if(!key.empty() && !value.empty()) out.push_back({key, value});
        }else{
            positional.push_back(token);
        }
        token.clear();
    };
    for(char ch : text){
        if(ch == ';' || ch == ',') flush();
        else if(ch != ' ' && ch != '\t') token.push_back(ch);
    }
    flush();
    if(out.empty()){
        for(std::size_t i = 0; i < positional.size(); ++i)
            out.push_back({std::string(1, (char)('A' + i)), positional[i]});
    }
}

} // namespace

namespace DealProjectXML{

bool WriteXml(const ProjectData& data, const std::string& path){
    if(path.empty()) return false;
    wxXmlDocument doc;
    wxXmlNode* root = new_element("teda-project");
    add_attr(root, "schema-version", kCurrentSchema);
    doc.SetRoot(root);

    //---- info：项目信息 + 视口 ----
    wxXmlNode* info = new_element("info");
    add_text_child(info, "name",        data.info.name.empty() ? "Untitled" : data.info.name);
    add_text_child(info, "author",      data.info.author);
    add_text_child(info, "created",     data.info.created.empty() ? now_iso() : data.info.created);
    add_text_child(info, "modified",    now_iso());
    add_text_child(info, "description", data.info.description);
    wxXmlNode* view = new_element("view");
    add_int_attr(view, "offset-x", data.offset.x);
    add_int_attr(view, "offset-y", data.offset.y);
    view->AddAttribute(wxString::FromUTF8("scale"), wxString::Format(wxT("%g"), (double)data.scale));
    add_int_attr(view, "grid-step", data.grid_step);
    info->AddChild(view);
    root->AddChild(info);

    //---- libraries：项目引用的元件库版本 ----
    wxXmlNode* libraries = new_element("libraries");
    if(data.libraries.empty()){
        wxXmlNode* lib = new_element("library");
        add_attr(lib, "id", "builtin");
        add_attr(lib, "version", "1.0");
        libraries->AddChild(lib);
    }else{
        for(const LibraryData& libData : data.libraries){
            wxXmlNode* lib = new_element("library");
            add_attr(lib, "id", libData.id.empty() ? "builtin" : libData.id);
            add_attr(lib, "version", libData.version.empty() ? "1.0" : libData.version);
            libraries->AddChild(lib);
        }
    }
    root->AddChild(libraries);

    //---- circuit：元件 + 导线 + 电气注解 ----
    wxXmlNode* circuit = new_element("circuit");
    for(const ComponentData& c : data.components){
        wxXmlNode* n = new_element("component");
        add_int_attr(n, "id", c.id);
        add_attr(n, "type", c.type);
        add_attr(n, "library", c.library_id.empty() ? "builtin" : c.library_id);
        add_attr(n, "definition-version", c.definition_version.empty() ? "1" : c.definition_version);
        add_attr(n, "name", c.name);
        if(!c.category.empty()) add_attr(n, "category", c.category);
        add_int_attr(n, "x", c.x);
        add_int_attr(n, "y", c.y);
        add_int_attr(n, "rotation", c.rotation);
        add_attr(n, "mirror", c.mirror.empty() ? "none" : c.mirror);
        if(!c.reference.empty()) add_attr(n, "reference", c.reference);
        if(!c.value.empty()) add_attr(n, "value", c.value);
        if(!c.footprint.empty()) add_attr(n, "footprint", c.footprint);
        if(!c.description.empty()) add_text_child(n, "description", c.description);
        for(const auto& kv : c.params) add_param_child(n, kv);
        for(const PinOverrideData& pin : c.pin_overrides){
            wxXmlNode* p = new_element("pin");
            add_attr(p, "id", pin.id);
            add_attr(p, "number", pin.number);
            n->AddChild(p);
        }
        circuit->AddChild(n);
    }
    for(const WireData& w : data.wires){
        wxXmlNode* n = new_element("wire");
        add_int_attr(n, "id", w.id);
        add_attr(n, "type", w.type);
        if(!w.name.empty()) add_attr(n, "name", w.name);
        for(const auto& kv : w.params) add_param_child(n, kv);
        for(const wxPoint& pt : w.points) add_point_child(n, pt);
        write_endpoint(n, "start", w.start);
        write_endpoint(n, "end", w.end);
        circuit->AddChild(n);
    }
    for(const JunctionData& j : data.junctions){
        wxXmlNode* n = new_element("junction");
        add_attr(n, "id", j.id);
        add_int_attr(n, "x", j.x);
        add_int_attr(n, "y", j.y);
        circuit->AddChild(n);
    }
    for(const LabelData& l : data.labels){
        wxXmlNode* n = new_element("label");
        add_attr(n, "id", l.id);
        add_attr(n, "text", l.text);
        add_attr(n, "scope", l.scope.empty() ? "local" : l.scope);
        add_int_attr(n, "x", l.x);
        add_int_attr(n, "y", l.y);
        circuit->AddChild(n);
    }
    for(const NoConnectData& nc : data.no_connects){
        wxXmlNode* n = new_element("no-connect");
        add_int_attr(n, "component", nc.component_id);
        add_attr(n, "pin", nc.pin_id);
        circuit->AddChild(n);
    }
    root->AddChild(circuit);

    return doc.Save(to_wx(path), 2);
}

bool ReadXml(const std::string& path, ProjectData* data){
    if(path.empty() || !data) return false;
    wxXmlDocument doc;
    if(!doc.Load(to_wx(path))) return false;
    wxXmlNode* root = doc.GetRoot();
    if(!root || root->GetName() != wxT("teda-project")) return false;

    // 版本识别：支持 1.0（旧）与 2.0（当前）；1.1 视为 1.0 兼容读取。
    std::string version = from_wx(attr(root, "schema-version", "1.0"));
    if(version != "1.0" && version != "1.1" && version != "2.0") return false;

    ProjectData out;
    out.schema_version = version;
    std::unordered_set<int> instanceIds;
    std::set<std::string> junctionIds;
    std::set<std::string> labelIds;

    // 容错解析：未知元素/属性忽略；结构错误（重复实例 ID）返回 false。
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
        }else if(child->GetName() == wxT("libraries")){
            for(const wxXmlNode* c = child->GetChildren(); c; c = c->GetNext()){
                if(c->GetType() != wxXML_ELEMENT_NODE || c->GetName() != wxT("library")) continue;
                LibraryData lib;
                lib.id = from_wx(attr(c, "id", "builtin"));
                lib.version = from_wx(attr(c, "version", "1.0"));
                out.libraries.push_back(std::move(lib));
            }
        }else if(child->GetName() == wxT("circuit")){
            for(const wxXmlNode* c = child->GetChildren(); c; c = c->GetNext()){
                if(c->GetType() != wxXML_ELEMENT_NODE) continue;
                if(c->GetName() == wxT("component")){
                    ComponentData cd;
                    long v;
                    if(attr(c, "id").ToLong(&v)) cd.id = (int)v;
                    if(!instanceIds.insert(cd.id).second) return false;
                    cd.type = from_wx(attr(c, "type"));
                    cd.library_id = from_wx(attr(c, "library", "builtin"));
                    cd.definition_version = from_wx(attr(c, "definition-version", "1"));
                    cd.name = from_wx(attr(c, "name"));
                    cd.category = from_wx(attr(c, "category"));
                    if(attr(c, "x").ToLong(&v)) cd.x = (int)v;
                    if(attr(c, "y").ToLong(&v)) cd.y = (int)v;
                    if(attr(c, "rotation").ToLong(&v)) cd.rotation = (int)v;
                    cd.mirror = from_wx(attr(c, "mirror", "none"));
                    cd.reference = from_wx(attr(c, "reference"));
                    cd.value = from_wx(attr(c, "value"));
                    cd.footprint = from_wx(attr(c, "footprint"));

                    for(const wxXmlNode* cc = c->GetChildren(); cc; cc = cc->GetNext()){
                        if(cc->GetType() != wxXML_ELEMENT_NODE) continue;
                        if(cc->GetName() == wxT("description")){
                            cd.description = from_wx(text_of(cc));
                        }else if(cc->GetName() == wxT("param")){
                            cd.params[from_wx(attr(cc, "key"))] = from_wx(text_of(cc));
                        }else if(cc->GetName() == wxT("pin")){
                            const std::string id = from_wx(attr(cc, "id"));
                            const std::string number = from_wx(attr(cc, "number"));
                            if(!id.empty() && !number.empty())
                                cd.pin_overrides.push_back({id, number});
                        }
                    }

                    // 1.0 兼容迁移：旧版把实例属性/连线覆盖存成 param。
                    auto take_param = [&](const char* key, std::string& target){
                        auto it = cd.params.find(key);
                        if(it == cd.params.end()) return false;
                        if(target.empty()) target = it->second;
                        cd.params.erase(it);
                        return true;
                    };
                    take_param("reference", cd.reference);
                    take_param("value", cd.value);
                    take_param("footprint", cd.footprint);
                    std::string rotationText, mirrorText;
                    if(take_param("rotation", rotationText)){
                        try{ cd.rotation = std::stoi(rotationText); }catch(...){}
                    }
                    if(take_param("mirror", mirrorText) && !mirrorText.empty()) cd.mirror = mirrorText;
                    std::string legacyPins;
                    if(take_param("pin_numbers", legacyPins)){
                        migrate_legacy_pin_numbers(legacyPins, cd.pin_overrides);
                    }
                    // reference_prefix 是定义级属性，不属于实例覆盖。
                    cd.params.erase("reference_prefix");

                    out.components.push_back(std::move(cd));
                }else if(c->GetName() == wxT("wire")){
                    WireData wd;
                    long v;
                    if(attr(c, "id").ToLong(&v)) wd.id = (int)v;
                    if(!instanceIds.insert(wd.id).second) return false;
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
                        }else if(cc->GetName() == wxT("endpoint")){
                            const std::string side = from_wx(attr(cc, "side"));
                            if(side == "start") wd.start = read_endpoint(cc);
                            else if(side == "end") wd.end = read_endpoint(cc);
                        }
                    }
                    out.wires.push_back(std::move(wd));
                }else if(c->GetName() == wxT("junction")){
                    JunctionData j;
                    long v;
                    j.id = from_wx(attr(c, "id"));
                    if(j.id.empty() || !junctionIds.insert(j.id).second) return false;
                    if(attr(c, "x").ToLong(&v)) j.x = (int)v;
                    if(attr(c, "y").ToLong(&v)) j.y = (int)v;
                    out.junctions.push_back(std::move(j));
                }else if(c->GetName() == wxT("label")){
                    LabelData l;
                    long v;
                    l.id = from_wx(attr(c, "id"));
                    if(l.id.empty() || !labelIds.insert(l.id).second) return false;
                    l.text = from_wx(attr(c, "text"));
                    l.scope = from_wx(attr(c, "scope", "local"));
                    if(attr(c, "x").ToLong(&v)) l.x = (int)v;
                    if(attr(c, "y").ToLong(&v)) l.y = (int)v;
                    out.labels.push_back(std::move(l));
                }else if(c->GetName() == wxT("no-connect")){
                    NoConnectData nc;
                    long v;
                    if(attr(c, "component").ToLong(&v)) nc.component_id = (int)v;
                    nc.pin_id = from_wx(attr(c, "pin"));
                    if(nc.component_id > 0 && !nc.pin_id.empty())
                        out.no_connects.push_back(std::move(nc));
                }
            }
        }
    }
    *data = std::move(out);
    return true;
}

} // namespace DealProjectXML

#include"./CircuitFile.hpp"
#include"../Canva/Canvas.hpp"
#include"../Canva/MetaData.hpp"
#include"../metadata/MetaDataJson.hpp"

#include<wx/xml/xml.h>
#include<wx/filefn.h>

#include<algorithm>
#include<string>

const char* const CircuitSchemaVersion = "1.0";

namespace{

// wxString <-> std::string 一律显式走 UTF-8。
// 不能图省事用 ToStdString()/隐式构造：它们按当前区域设置转换（中文Windows上是GBK），
// 而程序里其它地方的 std::string（元数据JSON里的键值、元件名、ItemSVG的名字比较）都是UTF-8，
// 混用会导致中文乱码、按type查不到元数据、以及“GBK再UTF-8”的双重编码
std::string to_utf8(const wxString& s){
    const wxScopedCharBuffer buf = s.utf8_str();
    return std::string(buf.data(), buf.length());
}

wxString from_utf8(const std::string& s){
    return wxString::FromUTF8(s.c_str(), s.length());
}

// ---------- 读取属性/文本的小工具：缺失或非法一律回落到默认值，不报错 ----------

wxString attr_str(wxXmlNode* node, const wxString& name){
    return node->GetAttribute(name, wxString());
}

int attr_int(wxXmlNode* node, const wxString& name, int def){
    const wxString s = attr_str(node, name);
    if(s.empty()) return def;
    long v = 0;
    if(!s.ToLong(&v)) return def;   //非数字：用默认值
    return (int)v;
}

double attr_double(wxXmlNode* node, const wxString& name, double def){
    const wxString s = attr_str(node, name);
    if(s.empty()) return def;
    double v = 0;
    if(!s.ToDouble(&v)) return def;
    return v;
}

//元素文本内容（去掉首尾空白；<name>..</name> 这类简单元素用）
wxString text_of(wxXmlNode* node){
    wxString s = node->GetNodeContent();
    s.Trim(true);
    s.Trim(false);
    return s;
}

//画一层缩放的写法：与README示例一致地写成 "1.0" 这种带小数点的形式
wxString format_scale(double v){
    wxString s = wxString::Format(wxT("%g"), v);
    if(!s.Contains(wxT('.')) && !s.Contains(wxT('e')) && !s.Contains(wxT('E'))){
        s += wxT(".0");
    }
    return s;
}

//追加一个元素子节点。
//注意 wxWidgets 的一个坑：带 parent 的 wxXmlNode 构造函数是把新节点插到父节点子列表的
//【最前面】（源码见 xml.cpp：m_next = m_parent->m_children; m_parent->m_children = this;），
//而 AddChild 才是追加到【末尾】。用前者会让文件里所有层级的元素顺序整体颠倒，
//所以这里统一“先建无父节点的节点，再 AddChild 追加”，保证文件顺序 = 代码里的构造顺序
wxXmlNode* append_element(wxXmlNode* parent, const wxString& tag){
    wxXmlNode* node = new wxXmlNode(wxXML_ELEMENT_NODE, tag);
    parent->AddChild(node);
    return node;
}

//追加一个文本子节点（元素的文本内容，如 <param key="x">值</param> 里的“值”）
void append_text_node(wxXmlNode* parent, const wxString& text){
    wxXmlNode* node = new wxXmlNode(wxXML_TEXT_NODE, wxString(), text);
    parent->AddChild(node);
}

//写一个只含文本的元素：<name>值</name>（值为空时不写子文本，序列化成 <name/>）
wxXmlNode* add_text_element(wxXmlNode* parent, const wxString& tag, const wxString& value){
    wxXmlNode* node = append_element(parent, tag);
    if(!value.empty()) append_text_node(node, value);
    return node;
}

//把类型串去重后拼成 "A/B/C"，便于在提示信息里列出问题条目
wxString join_types(const std::vector<std::string>& types){
    std::vector<wxString> unique;
    for(const std::string& t : types){
        const wxString s = from_utf8(t);
        if(std::find(unique.begin(), unique.end(), s) == unique.end()) unique.push_back(s);
    }
    wxString joined;
    for(size_t i = 0; i < unique.size(); ++i){
        if(i) joined += wxT("/");
        joined += unique[i];
    }
    return joined;
}

} // namespace

//================ 画布 <-> 内存表示 ================

void circuit_export(const Canvas& canvas, CircuitDocument& out){
    CircuitDocument doc;
    //视口
    doc.has_view = true;
    doc.offset_coords = canvas.get_offset_coords();
    doc.scale = canvas.get_scale();
    doc.grid_step = Canvas::grid_step();

    int fallback_id = 0;   //万一有元件的id非法，给个不会与正常id冲突的编号
    for(CanvasItem* item : canvas.items()){
        CircuitItemDef def;
        def.id = item->id > 0 ? item->id : ++fallback_id;
        fallback_id = std::max(fallback_id, def.id);
        def.name = item->name;

        MetaData* md = item->metadata;
        if(md){
            def.type = md->get_type();
            def.category = md->get_category();
        }

        if(Linking* wire = dynamic_cast<Linking*>(item)){
            //导线/总线：位置由折点决定，没有 x/y
            def.is_wire = true;
            def.points = wire->points;
            if(def.type.empty()){
                //元数据缺失：按显示名兜底（总线/导线）
                def.type = (item->name == "总线") ? "BUS" : "WIRE";
            }
        }else{
            def.x = item->coords_x;
            def.y = item->coords_y;
            if(def.type.empty()){
                //元数据缺失（type串无从得知）：只能退回枚举名，如 "NAND"/"GENERIC"
                def.type = item_type_name(item->type);
            }
        }

        if(md){
            //描述：存在就写出去（写入的内容会覆盖加载时元数据的默认描述）。
            //导线的子元素按README只允许 param/point，所以不写 description
            const std::string& desc = md->get_description();
            if(!def.is_wire && !desc.empty()){
                def.has_description = true;
                def.description = desc;
            }
            //通用参数：MetaData::params 是有序map，逐个写成 <param>，保证文件内容稳定
            for(const auto& kv : md->get_params()){
                def.params.emplace_back(kv.first, kv.second);
            }
        }

        doc.items.push_back(std::move(def));
    }
    out = std::move(doc);
}

wxString circuit_import(const CircuitDocument& doc, Canvas& canvas){
    canvas.clear_items();   //旧项目先清空（调用方已保证文件读完了）

    wxString warning;
    std::vector<std::string> unresolved;   //元数据JSON里找不到的类型串
    std::vector<std::string> uncreated;    //连元件都建不出来的（类型串和名字都不可用）

    //文件里没写 id（或写了非法值）时，用当前最大id往后顺延，保证唯一
    int next_free_id = 1;
    for(const CircuitItemDef& def : doc.items){
        if(def.id > 0) next_free_id = std::max(next_free_id, def.id + 1);
    }

    for(const CircuitItemDef& def : doc.items){
        //查找名：元件用类型串（ItemSVG按元数据类型串找SVG与元数据），
        //导线用中文名（ItemSVG靠名字区分导线/总线）
        std::string lookup = def.is_wire ? def.name : def.type;
        if(lookup.empty()) lookup = def.name.empty() ? def.type : def.name;
        if(lookup.empty()){
            uncreated.push_back(def.is_wire ? std::string("<wire>") : std::string("<component>"));
            continue;
        }

        const int id = def.id > 0 ? def.id : next_free_id++;
        CanvasItem* item = canvas.add_item(lookup, def.x, def.y, id, def.name);
        if(!item){
            uncreated.push_back(def.type.empty() ? lookup : def.type);
            continue;
        }

        //导线折点：与finish_wire同样在拿到元件后填入
        if(def.is_wire){
            if(Linking* wire = dynamic_cast<Linking*>(item)){
                wire->points = def.points;
            }
        }

        //按 type 直接恢复元数据：README 里 type 是主键。
        //不用CanvasItem::load_config，因为它先按枚举名查（导线/总线同属WIRE枚举会串位）
        if(!def.type.empty()){
            MetaData* by_type = load_metadata(def.type);
            if(by_type){
                if(item->metadata && item->metadata->get_type() == def.type){
                    delete by_type;   //ItemSVG已按同一个键查到同一条定义，不用替换
                }else{
                    delete item->metadata;   //替换掉按枚举名/名字查到的那份
                    item->metadata = by_type;
                }
            }else{
                unresolved.push_back(def.type);
            }
        }

        //元数据：描述 + 参数
        MetaData* md = item->metadata;
        if(!md && (def.has_description || !def.params.empty() || !def.type.empty())){
            //元数据JSON里没有这条定义：按文件内容建一份（按README“type找不到时按空元数据创建”），
            //这样文件里带的描述与参数不会丢
            md = new MetaData(def.name, "", 0, def.type, def.category, "");
            item->metadata = md;
        }
        if(md){
            //名字缺失时用元数据里的中文名补上（README里name是冗余字段，便于阅读）
            if(def.name.empty() && !md->get_name().empty()) item->name = md->get_name();
            if(def.has_description) md->set_description(def.description);
            //文件里的参数优先；文件没写的键保留JSON默认值（容忍手工精简过的文件）
            for(const auto& kv : def.params){
                md->params[kv.first] = kv.second;
            }
            //逻辑门：把参数同步回类型化字段，否则属性栏读的是JSON里的旧值，
            //会和参数表里文件带回来的值对不上（缺键时用当前值兜底，而不是硬编码默认值）
            if(LogicGateMetaData* gate = dynamic_cast<LogicGateMetaData*>(md)){
                auto param_int = [&](const char* key, int def_value){
                    auto it = md->params.find(key);
                    if(it == md->params.end()) return def_value;
                    try{ return std::stoi(it->second); }catch(...){ return def_value; }
                };
                gate->set_inputs   (param_int("inputs",   gate->get_inputs()));
                gate->set_outputs  (param_int("outputs",  gate->get_outputs()));
                gate->set_data_bits(param_int("DataBits", gate->get_data_bits()));
            }
        }
    }

    //视口：文件里没有 view 就回到默认视角（与手工写的文件表现一致）
    if(doc.has_view){
        canvas.set_offset_coords(doc.offset_coords);
        canvas.set_scale((float)doc.scale);
    }else{
        canvas.set_offset_coords(wxPoint(0, 0));
        canvas.set_scale(1.0f);
    }
    canvas.Refresh();

    if(!unresolved.empty()){
        warning += wxString::FromUTF8("以下元件类型在 src/metadata 里找不到定义，已按空元数据加载：") +
                   join_types(unresolved) + wxT("\n");
    }
    if(!uncreated.empty()){
        warning += wxString::FromUTF8("以下条目既没有可用的 type 也没有 name，无法创建，已跳过：") +
                   join_types(uncreated) + wxT("\n");
    }
    return warning;
}

//================ 内存表示 <-> xml 文件 ================

bool circuit_write(const CircuitDocument& doc, const wxString& path, wxString& error){
    error.clear();

    wxXmlDocument xml;
    xml.SetVersion(wxT("1.0"));
    //显式用UTF-8写：元件名/描述都是中文，不指定编码时按本地代码页写出会乱码
    xml.SetFileEncoding(wxT("UTF-8"));

    wxXmlNode* root = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("teda-project"));
    root->AddAttribute(wxT("schema-version"), from_utf8(CircuitSchemaVersion));
    xml.SetRoot(root);

    //---------- <info> 项目信息 ----------
    wxXmlNode* info = append_element(root, wxT("info"));
    add_text_element(info, wxT("name"), from_utf8(doc.name));
    if(!doc.author.empty())
        add_text_element(info, wxT("author"), from_utf8(doc.author));
    add_text_element(info, wxT("created"), from_utf8(doc.created));
    if(!doc.modified.empty())
        add_text_element(info, wxT("modified"), from_utf8(doc.modified));
    if(!doc.description.empty())
        add_text_element(info, wxT("description"), from_utf8(doc.description));

    if(doc.has_view){
        wxXmlNode* view = append_element(info, wxT("view"));
        view->AddAttribute(wxT("offset-x"), wxString::Format(wxT("%d"), doc.offset_coords.x));
        view->AddAttribute(wxT("offset-y"), wxString::Format(wxT("%d"), doc.offset_coords.y));
        view->AddAttribute(wxT("scale"), format_scale(doc.scale));
        view->AddAttribute(wxT("grid-step"), wxString::Format(wxT("%d"), doc.grid_step));
    }

    //---------- <circuit> 电路图 ----------
    wxXmlNode* circuit = append_element(root, wxT("circuit"));
    for(const CircuitItemDef& it : doc.items){
        wxXmlNode* node = append_element(circuit, it.is_wire ? wxT("wire") : wxT("component"));
        node->AddAttribute(wxT("id"), wxString::Format(wxT("%d"), it.id));
        node->AddAttribute(wxT("type"), from_utf8(it.type));
        if(!it.name.empty()) node->AddAttribute(wxT("name"), from_utf8(it.name));

        if(it.is_wire){
            //导线：只有 param 与 point（位置由折点决定）
            for(const auto& kv : it.params){
                wxXmlNode* param = append_element(node, wxT("param"));
                param->AddAttribute(wxT("key"), from_utf8(kv.first));
                if(!kv.second.empty()) append_text_node(param, from_utf8(kv.second));
            }
            for(const wxPoint& p : it.points){
                wxXmlNode* point = append_element(node, wxT("point"));
                point->AddAttribute(wxT("x"), wxString::Format(wxT("%d"), p.x));
                point->AddAttribute(wxT("y"), wxString::Format(wxT("%d"), p.y));
            }
        }else{
            if(!it.category.empty())
                node->AddAttribute(wxT("category"), from_utf8(it.category));
            node->AddAttribute(wxT("x"), wxString::Format(wxT("%d"), it.x));
            node->AddAttribute(wxT("y"), wxString::Format(wxT("%d"), it.y));
            if(it.has_description)
                add_text_element(node, wxT("description"),
                                 from_utf8(it.description));
            for(const auto& kv : it.params){
                wxXmlNode* param = append_element(node, wxT("param"));
                param->AddAttribute(wxT("key"), from_utf8(kv.first));
                if(!kv.second.empty()) append_text_node(param, from_utf8(kv.second));
            }
        }
    }

    if(!xml.Save(path)){
        error = wxString::FromUTF8("写入文件失败（路径不存在、文件被占用或没有写权限）：") + path;
        return false;
    }
    return true;
}

bool circuit_read(const wxString& path, CircuitDocument& out,
                  wxString& error, wxString& warning){
    error.clear();
    warning.clear();

    if(!wxFileExists(path)){
        error = wxString::FromUTF8("文件不存在：") + path;
        return false;
    }

    wxXmlDocument xml;
    //Load 依据文件头的 encoding 声明解码，UTF-8 中文正常；解析失败返回false
    if(!xml.Load(path)){
        error = wxString::FromUTF8("无法解析该文件（不是有效的 XML）：") + path;
        return false;
    }

    wxXmlNode* root = xml.GetRoot();
    if(!root || root->GetName() != wxT("teda-project")){
        error = wxString::FromUTF8("不是 TinyEDA 项目文件（缺少 <teda-project> 根节点）：") + path;
        return false;
    }

    //版本：只提示不拒绝。以后新增的元素对当前版本来说是“多出来的”，会被忽略
    const wxString version = root->GetAttribute(wxT("schema-version"), wxT(""));
    if(!version.empty() && version != from_utf8(CircuitSchemaVersion)){
        warning += wxString::FromUTF8("文件格式版本为 ") + version +
                   wxString::FromUTF8("，当前程序按 ") +
                   from_utf8(CircuitSchemaVersion) +
                   wxString::FromUTF8(" 读取，无法识别的部分会被忽略。\n");
    }

    CircuitDocument doc;
    int bad_wires = 0;   //折点不足2的导线（画不出来）

    for(wxXmlNode* node = root->GetChildren(); node; node = node->GetNext()){
        if(node->GetType() != wxXML_ELEMENT_NODE) continue;   //跳过注释等非元素节点
        const wxString tag = node->GetName();

        if(tag == wxT("info")){
            for(wxXmlNode* f = node->GetChildren(); f; f = f->GetNext()){
                if(f->GetType() != wxXML_ELEMENT_NODE) continue;
                const wxString name = f->GetName();
                if(name == wxT("name"))             doc.name = to_utf8(text_of(f));
                else if(name == wxT("author"))      doc.author = to_utf8(text_of(f));
                else if(name == wxT("created"))     doc.created = to_utf8(text_of(f));
                else if(name == wxT("modified"))    doc.modified = to_utf8(text_of(f));
                else if(name == wxT("description")) doc.description = to_utf8(text_of(f));
                else if(name == wxT("view")){
                    doc.has_view = true;
                    doc.offset_coords = wxPoint(attr_int(f, wxT("offset-x"), 0),
                                                attr_int(f, wxT("offset-y"), 0));
                    doc.scale = attr_double(f, wxT("scale"), 1.0);
                    doc.grid_step = attr_int(f, wxT("grid-step"), 10);
                    if(doc.grid_step != Canvas::grid_step()){
                        warning += wxString::Format(
                            wxString::FromUTF8("文件里的网格步长(%d)与当前程序(%d)不同，坐标可能对不齐。\n"),
                            doc.grid_step, Canvas::grid_step());
                    }
                }
                //其它子元素：忽略（向前兼容）
            }
        }else if(tag == wxT("circuit")){
            for(wxXmlNode* c = node->GetChildren(); c; c = c->GetNext()){
                if(c->GetType() != wxXML_ELEMENT_NODE) continue;
                const wxString name = c->GetName();
                const bool is_wire = (name == wxT("wire"));
                if(!is_wire && name != wxT("component")) continue;   //忽略未知元素

                CircuitItemDef def;
                def.is_wire = is_wire;
                def.id = attr_int(c, wxT("id"), 0);
                def.type = to_utf8(attr_str(c, wxT("type")));
                def.name = to_utf8(attr_str(c, wxT("name")));
                if(!is_wire){
                    def.category = to_utf8(attr_str(c, wxT("category")));
                    def.x = attr_int(c, wxT("x"), 0);
                    def.y = attr_int(c, wxT("y"), 0);
                }

                for(wxXmlNode* child = c->GetChildren(); child; child = child->GetNext()){
                    if(child->GetType() != wxXML_ELEMENT_NODE) continue;
                    const wxString child_name = child->GetName();
                    if(child_name == wxT("description")){
                        //README给wire定义的子元素只有param/point，导线不读description
                        if(!is_wire){
                            def.has_description = true;
                            def.description = to_utf8(text_of(child));
                        }
                    }else if(child_name == wxT("param")){
                        const wxString key = attr_str(child, wxT("key"));
                        if(key.empty()) continue;   //没有key的param没法用，忽略
                        def.params.emplace_back(to_utf8(key),
                                                to_utf8(text_of(child)));
                    }else if(child_name == wxT("point")){
                        if(is_wire){
                            def.points.push_back(wxPoint(attr_int(child, wxT("x"), 0),
                                                         attr_int(child, wxT("y"), 0)));
                        }
                    }
                    //其它子元素：忽略（向前兼容）
                }

                if(is_wire && def.points.size() < 2){
                    ++bad_wires;   //折点不足的导线画不出来：跳过
                    continue;
                }
                doc.items.push_back(std::move(def));
            }
        }
        //其它节点：忽略（向前兼容，README说未来会加 annotation/子电路等）
    }

    out = std::move(doc);

    if(bad_wires > 0){
        warning += wxString::Format(
            wxString::FromUTF8("有 %d 条导线的折点少于2个（无法绘制），已跳过。\n"), bad_wires);
    }
    return true;
}

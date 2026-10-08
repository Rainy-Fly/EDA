#include"./Skills.hpp"

#include<wx/xml/xml.h>
#include<memory>   //std::unique_ptr（jsoncpp CharReader）

namespace{

std::string to_utf8(const wxString& s){
    const wxScopedCharBuffer buf = s.utf8_str();
    return std::string(buf.data(), buf.length());
}

// 元素文本内容（text/cdata 子节点拼接）
wxString text_of(const wxXmlNode* n){
    wxString t;
    for(const wxXmlNode* c = n->GetChildren(); c; c = c->GetNext()){
        const wxXmlNodeType ty = c->GetType();
        if(ty == wxXML_TEXT_NODE || ty == wxXML_CDATA_SECTION_NODE) t += c->GetContent();
    }
    return t;
}

// 解析 JSON 字符串；失败返回 false
bool parse_json(const std::string& s, Json::Value& out){
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errs;
    return reader->parse(s.data(), s.data() + s.size(), &out, &errs);
}

} // namespace

namespace Agent{

bool LoadToolDefs(const std::string& xml_path, std::vector<ToolDef>& out){
    out.clear();
    wxXmlDocument doc;
    if(!doc.Load(wxString::FromUTF8(xml_path))) return false;
    wxXmlNode* root = doc.GetRoot();
    if(!root || root->GetName() != wxT("agent-tools")) return false;

    for(wxXmlNode* n = root->GetChildren(); n; n = n->GetNext()){
        if(n->GetType() != wxXML_ELEMENT_NODE || n->GetName() != wxT("tool")) continue;
        ToolDef t;
        t.name = to_utf8(n->GetAttribute(wxT("name"), wxString()));
        t.handler = to_utf8(n->GetAttribute(wxT("handler"), wxString()));
        if(t.name.empty()) continue;             //无名的工具跳过
        if(t.handler.empty()) t.handler = t.name;  //未指定处理函数名：默认同名

        for(wxXmlNode* c = n->GetChildren(); c; c = c->GetNext()){
            if(c->GetType() != wxXML_ELEMENT_NODE) continue;
            const wxString tag = c->GetName();
            if(tag == wxT("description"))       t.description = to_utf8(text_of(c));
            else if(tag == wxT("when-to-use"))  t.when_to_use = to_utf8(text_of(c));
            else if(tag == wxT("parameters")){
                Json::Value v;
                if(parse_json(to_utf8(text_of(c)), v)) t.parameters = v;
                else t.parameters = Json::objectValue;   //参数损坏：按空对象
            }
            //其它元素忽略
        }
        if(!t.parameters.isObject()) t.parameters = Json::objectValue;
        out.push_back(std::move(t));
    }
    return true;
}

void ToolRegistry::bind(const std::string& handler, Handler fn){
    handlers_[handler] = std::move(fn);
}

bool ToolRegistry::has(const std::string& handler) const{
    return handlers_.count(handler) > 0;
}

std::string ToolRegistry::run(const std::string& handler, const Json::Value& args) const{
    auto it = handlers_.find(handler);
    if(it == handlers_.end()){
        Json::Value err;
        err["ok"] = false;
        err["error"] = "未注册的工具处理函数: " + handler;
        return err.toStyledString();
    }
    try{
        return it->second(args);
    }catch(const std::exception& e){
        Json::Value err;
        err["ok"] = false;
        err["error"] = std::string("工具执行异常: ") + e.what();
        return err.toStyledString();
    }
}

} // namespace Agent

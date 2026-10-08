#include"./Context.hpp"

namespace Agent{

Context::Context(){
    messages_ = Json::arrayValue;
}

void Context::clear(){
    messages_ = Json::arrayValue;
}

void Context::push(const Json::Value& msg){
    messages_.append(msg);
}

void Context::add_system(const std::string& text){
    Json::Value m;
    m["role"] = "system";
    m["content"] = text;
    push(m);
}

void Context::add_user(const std::string& text){
    Json::Value m;
    m["role"] = "user";
    m["content"] = text;
    push(m);
}

void Context::add_assistant_text(const std::string& text){
    Json::Value m;
    m["role"] = "assistant";
    m["content"] = text;
    push(m);
}

void Context::add_assistant_tool_calls(const std::string& content, const Json::Value& tool_calls){
    Json::Value m;
    m["role"] = "assistant";
    m["content"] = content;          //多数模型该回合 content 为空串
    m["tool_calls"] = tool_calls;    //[{id,type,function:{name,arguments}}]
    push(m);
}

void Context::add_tool_result(const std::string& tool_call_id,
                              const std::string& name, const std::string& result_json){
    Json::Value m;
    m["role"] = "tool";
    m["tool_call_id"] = tool_call_id;
    m["name"] = name;                       //部分实现要求 name；带上无害
    m["content"] = result_json;             //工具结果按字符串塞回（模型自行解读JSON）
    push(m);
}

} // namespace Agent

#pragma once
#include<json/json.h>

// 对话上下文管理（src/Agent/）
// 维护 OpenAI 兼容的 messages 数组：system/user/assistant/tool 各角色消息，
// 其中工具调用回合 = 一条带 tool_calls 的 assistant 消息 + 若干条 tool 结果消息。
namespace Agent{

class Context{
public:
    Context();
    void clear();                                  //清空全部消息

    void add_system(const std::string& text);      //系统提示（追加；通常开头一条即可）
    void add_user(const std::string& text);        //用户消息
    void add_assistant_text(const std::string& text);              //纯文本回复
    //带工具调用的回复：content 为文本（可为空），tool_calls 为
    //[{id,type:"function",function:{name,arguments}},...]
    void add_assistant_tool_calls(const std::string& content, const Json::Value& tool_calls);
    //工具执行结果（对应某条 tool_call_id）
    void add_tool_result(const std::string& tool_call_id,
                         const std::string& name, const std::string& result_json);

    const Json::Value& messages() const { return messages_; }   //发给 API 的数组
    size_t size() const { return messages_.size(); }

private:
    void push(const Json::Value& msg);

    Json::Value messages_;   //Json::arrayValue
};

} // namespace Agent

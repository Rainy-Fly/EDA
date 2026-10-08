#pragma once
#include<string>
#include<functional>
#include<json/json.h>

// 收发信息（src/Agent/）
// 通过 HTTPS 调用 OpenAI 兼容的 /chat/completions 接口（DeepSeek 默认），
// 一次性（非流式）拿到完整回复。本函数是阻塞调用，应在工作线程里使用，
// 网络错误不会抛异常，统一放进 ChatResponse 返回。
namespace Agent{

// 一次对话接口的回复
struct ChatResponse{
    bool ok = false;           //网络/HTTP/JSON 是否成功
    std::string error;         //失败原因（ok=false 时）
    std::string content;       //assistant 文本（可能是空串）
    Json::Value tool_calls;    //工具调用数组（[{id,type,function:{name,arguments}}]，无则空）
    Json::Value raw;           //完整原始响应（调试用）
    std::string finish_reason; //"stop"/"tool_calls"/...
};

// 调用对话接口。base_url 形如 https://api.deepseek.com（自动拼 /chat/completions）；
// api_key 可为空（本地模型如 ollama 不需要）；messages 为 OpenAI 兼容消息数组；
// tools 为工具定义数组（可空数组表示不启用工具）；
// abort 非空时，每次传输前回调：返回true则中断请求（用于程序退出时避免阻塞）
ChatResponse ChatCompletions(const std::string& base_url,
                             const std::string& api_key,
                             const std::string& model,
                             const Json::Value& messages,
                             const Json::Value& tools,
                             const std::function<bool()>& abort = {});

} // namespace Agent

#include"./Communicate.hpp"

#include<curl/curl.h>
#include<memory>

namespace{

// libcurl 全局初始化（进程内只做一次；不调用 global_cleanup，程序退出交给系统回收）
void ensure_curl_init(){
    static const bool init = []{
        curl_global_init(CURL_GLOBAL_DEFAULT);
        return true;
    }();
    (void)init;
}

// libcurl 写回调：把响应体累积进 std::string
size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata){
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

// libcurl 进度回调：abort 返回true时中断（非0退出码）。
// 注意：判断中断必须写 (*abort)() 真正调用函数；
// 写 (abort && *abort) 时 *abort 在布尔上下文只会调 operator bool()（判非空），
// 只要函数非空就恒为 true，会把所有请求立即中断（CURLE_ABORTED_BY_CALLBACK）
int progress_cb(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t){
    auto* abort = static_cast<const std::function<bool()>*>(userdata);
    if(!abort) return 0;              //没有中断回调：永不中断
    return (*abort)() ? 1 : 0;        //真正调用回调，按返回值决定是否中断
}

} // namespace

namespace Agent{

ChatResponse ChatCompletions(const std::string& base_url,
                             const std::string& api_key,
                             const std::string& model,
                             const Json::Value& messages,
                             const Json::Value& tools,
                             const std::function<bool()>& abort){
    ChatResponse resp;
    ensure_curl_init();

    // 组请求体
    Json::Value body;
    body["model"] = model.empty() ? "deepseek-chat" : model;
    body["messages"] = messages;
    if(tools.isArray() && !tools.empty()){
        body["tools"] = tools;
        body["tool_choice"] = "auto";
    }
    const std::string payload = body.toStyledString();

    // URL：base_url + /chat/completions（去掉多余的末尾斜杠）
    std::string url = base_url;
    while(!url.empty() && url.back() == '/') url.pop_back();
    url += "/chat/completions";

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(
        curl_easy_init(), &curl_easy_cleanup);
    if(!curl){
        resp.error = "curl_easy_init 失败";
        return resp;
    }

    // 请求头
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if(!api_key.empty()){
        headers = curl_slist_append(headers,
            (std::string("Authorization: Bearer ") + api_key).c_str());
    }
    std::string response_body;

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)payload.size());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 15L);   //建连超时15s
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 180L);         //整体超时180s
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "TinyEDA-Agent/0.1");
    //自动解压 gzip 等：不设置的话，若网络环境强制压缩，响应体会变成乱码解析失败
    curl_easy_setopt(curl.get(), CURLOPT_ACCEPT_ENCODING, "");
    if(abort){
        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);    //启用进度回调以便中断
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, progress_cb);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &abort);
    }

    const CURLcode rc = curl_easy_perform(curl.get());
    curl_slist_free_all(headers);

    if(rc != CURLE_OK){
        resp.error = std::string("网络请求失败: ") + curl_easy_strerror(rc);
        return resp;
    }

    long http_code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);

    // 非2xx：先给错误，尽量带出服务器信息。
    // 注意响应体可能不是 JSON（例如 DeepSeek 不带 key 时返回纯文本
    // "Authentication Fails (governor)"），不能先解析 JSON 再报错
    if(http_code < 200 || http_code >= 300){
        std::string detail;
        Json::Value root;
        {
            Json::CharReaderBuilder builder;
            std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
            std::string errs;
            if(reader->parse(response_body.data(),
                             response_body.data() + response_body.size(),
                             &root, &errs) && root.isObject()){
                if(root.isMember("error") && root["error"].isObject()){
                    const Json::Value& e = root["error"];
                    if(e.isMember("message")) detail = e["message"].asString();
                    if(e.isMember("type"))    detail += std::string(" (") + e["type"].asString() + ")";
                }else if(root.isMember("message")){
                    detail = root["message"].asString();
                }
            }
        }
        if(detail.empty()){
            // 响应体不是JSON：把原文贴出来（截断），一眼能看出是谁在拒绝
            if(!response_body.empty()){
                detail = response_body;
                if(detail.size() > 200) detail = detail.substr(0, 200) + "…(截断)";
            }else{
                detail = "(空响应体)";
            }
        }
        resp.error = "HTTP " + std::to_string(http_code) + ": " + detail;
        if(http_code == 401){
            resp.error += api_key.empty()
                ? "（未提供 API Key：请在设置面板填写并点击“保存并设为当前”）"
                : "（API Key 无效或已失效，请检查设置）";
        }
        return resp;
    }

    // 2xx：解析 JSON 响应
    Json::Value root;
    {
        Json::CharReaderBuilder builder;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        std::string errs;
        if(!reader->parse(response_body.data(), response_body.data() + response_body.size(),
                          &root, &errs)){
            resp.error = "响应不是合法JSON: " + errs + " (HTTP " + std::to_string(http_code) + ")";
            return resp;
        }
    }
    resp.raw = root;

    // 取 choices[0].message
    const Json::Value& choices = root["choices"];
    if(!choices.isArray() || choices.empty()){
        resp.error = "响应缺少 choices";
        return resp;
    }
    const Json::Value& message = choices[0]["message"];
    if(!message.isObject()){
        resp.error = "响应缺少 message";
        return resp;
    }
    if(message.isMember("content") && message["content"].isString()){
        resp.content = message["content"].asString();
    }
    if(message.isMember("tool_calls")){
        resp.tool_calls = message["tool_calls"];
    }
    if(choices[0].isMember("finish_reason") && choices[0]["finish_reason"].isString()){
        resp.finish_reason = choices[0]["finish_reason"].asString();
    }
    resp.ok = true;
    return resp;
}

} // namespace Agent

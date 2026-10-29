#include"./Controller.hpp"
#include"./Communicate.hpp"
#include"../Canva/Canvas.hpp"
#include"../Settings/AppSettings.hpp"

#include<memory>   //std::unique_ptr（jsoncpp CharReader）
#include<set>
#include<string>

namespace Agent{

wxDEFINE_EVENT(wxEVT_AGENT, AgentEvent);
wxDEFINE_EVENT(wxEVT_TOOL_EXECUTE, ToolExecuteRequestEvent);

AgentEvent::AgentEvent(AgentEventKind kind, const wxString& payload)
    : wxEvent(0, wxEVT_AGENT), kind_(kind), payload_(payload){}

ToolExecuteRequestEvent::ToolExecuteRequestEvent(std::shared_ptr<ToolRequest> req)
    : wxEvent(0, wxEVT_TOOL_EXECUTE), req_(std::move(req)){}

namespace{

// JSON 单行输出（聊天日志里显示参数/结果用）
std::string json_one_line(const Json::Value& v){
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    return Json::writeString(b, v);
}

// 结果摘要：截断到 max_len 字符，避免日志刷屏
std::string summarize(const std::string& s, size_t max_len = 160){
    if(s.size() <= max_len) return s;
    return s.substr(0, max_len) + "...(截断)";
}

// 本地模型（ollama / llama.cpp 等）通常不需要 API Key；远程接口没 key 必然被拒（401）
bool is_local_endpoint(const std::string& base_url){
    return base_url.find("127.0.0.1") != std::string::npos ||
           base_url.find("localhost") != std::string::npos ||
           base_url.find("0.0.0.0")   != std::string::npos;
}

} // namespace

Controller::Controller(wxEvtHandler* ui_target, Canvas* canvas)
    : ui_target_(ui_target), canvas_(canvas),
      shutdown_(std::make_shared<std::atomic<bool>>(false)){
    //默认配置：DeepSeek（设置面板可改/新增）
    profile_.name     = "DeepSeek";
    profile_.base_url = "https://api.deepseek.com";
    profile_.model    = "deepseek-chat";
    profile_.api_key  = "";

    //从用户配置加载“当前选中”的 API：设置面板保存过的 key/模型等，重启后自动生效
    //（否则每次启动 Controller 都用上面空的默认 key，发消息必被 401 拒）
    const AppSettings::Settings saved = AppSettings::Load();
    if(!saved.active_api.empty()){
        for(const AppSettings::ApiProfile& p : saved.apis){
            if(p.name == saved.active_api){
                profile_ = p;
                break;
            }
        }
    }

    //系统提示（多轮对话一直保留）
    context_.add_system(
        "你是 TinyEDA（电路设计画布工具）内嵌的 AI 助手。你可以调用工具查看并修改画布："
        "查看画布状态、查询可放置元件清单、连通性自查、放置/移动/删除元件、绘制导线、"
        "修改元件属性（位号/值/封装）、旋转/镜像、调整视口。"
        "规则：用中文回答，简洁直接；动手前先调用 get_canvas_state 或 "
        "list_available_components 了解现状；工具参数必须严格符合工具定义的格式；"
        "放置元件前若不确定类型串，先查 list_available_components。"
        "\n连线要点：connect_wire 的折点必须用 get_canvas_state 返回的该元件 pins 里的"
        "绝对坐标（引脚是网格点），否则接不上引脚、导出会报悬空。"
        "\n效率要点：尽量把一轮改动一次做完，然后再调用 check_connectivity 自查一次；"
        "不要每改一个元件就自查一遍。同一个工具+同样参数重复调用没有意义"
        "（会被自动跳过），如果两次得到的结果相同，请停下来说明结论或询问用户。");

    //注册工具处理函数
    RegisterCanvasTools(registry_, canvas_);

    //加载工具注册表（list.xml）；失败则本会话不启用工具（纯聊天）
    const wxString root = AppSettings::ProjectRootDir();
    const std::string defs_path = std::string(root.utf8_str().data()) +
                                  "/src/Agent/Skills/list.xml";
    LoadToolDefs(defs_path, tool_defs_);

    //工具桥事件（工作线程请求主线程执行工具）
    Bind(wxEVT_TOOL_EXECUTE, &Controller::OnToolExecute, this);
}

Controller::~Controller(){
    *shutdown_ = true;                 //先唤醒等待中的工具桥
    stop_ = true;
    if(worker_.joinable()) worker_.join();
}

void Controller::SetProfile(const AppSettings::ApiProfile& profile){
    std::lock_guard<std::mutex> lk(profile_mtx_);
    profile_ = profile;
}

void Controller::Stop(){
    stop_ = true;
}

bool Controller::StartChat(const std::string& user_text){
    if(busy_.exchange(true)) return false;   //忙：忽略新消息
    stop_ = false;
    //上一轮线程若已结束，先回收（对 joinable 线程直接赋值会 std::terminate）
    if(worker_.joinable()) worker_.join();
    worker_ = std::thread(&Controller::RunConversation, this, user_text);
    return true;
}

void Controller::Post(AgentEventKind kind, const std::string& payload){
    if(!ui_target_) return;
    wxQueueEvent(ui_target_, new AgentEvent(kind, wxString::FromUTF8(payload)));
}

//工具桥：请求主线程执行工具并等待结果（析构时会被唤醒返回）
std::string Controller::ExecuteToolOnMain(const std::string& handler, const Json::Value& args){
    auto req = std::make_shared<ToolRequest>();
    req->handler = handler;
    req->args = args;
    wxQueueEvent(this, new ToolExecuteRequestEvent(req));
    {
        std::unique_lock<std::mutex> lk(req->mtx);
        req->cv.wait(lk, [&]{ return req->done || shutdown_->load(); });
    }
    if(!req->done){
        return "{\"ok\":false,\"error\":\"程序正在退出，工具未执行\"}";
    }
    return req->result;
}

void Controller::OnToolExecute(ToolExecuteRequestEvent& e){
    auto req = e.Request();
    if(!req) return;
    req->result = registry_.run(req->handler, req->args);
    {
        std::lock_guard<std::mutex> lk(req->mtx);
        req->done = true;
    }
    req->cv.notify_one();
}

void Controller::RunConversation(const std::string& user_text){
    context_.add_user(user_text);
    Post(AgentEventKind::UserEcho, user_text);
    Post(AgentEventKind::Started, "");

    //API Key 为空时先给一句能看懂的话：否则请求发出去只会拿到服务端的 HTTP 401，
    //用户根本看不出是“没填 key”还是“key 无效/没额度”
    {
        std::lock_guard<std::mutex> lk(profile_mtx_);
        if(profile_.api_key.empty() && !is_local_endpoint(profile_.base_url)){
            Post(AgentEventKind::Error,
                 "当前 API 配置「" + profile_.name + "」没有填写 API Key。"
                 "请到右侧「设置」页填好 Key，再点「保存并设为当前」。");
            Post(AgentEventKind::Finished, "");
            busy_ = false;
            return;
        }
    }

    //由 list.xml 组工具定义数组（发给 API 的函数定义）
    Json::Value tools(Json::arrayValue);
    for(const ToolDef& t : tool_defs_){
        Json::Value fn;
        fn["type"] = "function";
        fn["function"]["name"] = t.name;
        std::string desc = t.description;
        if(!t.when_to_use.empty()) desc += "\n何时使用：" + t.when_to_use;
        fn["function"]["description"] = desc;
        fn["function"]["parameters"] = t.parameters;
        tools.append(fn);
    }

    //只读工具集合（list.xml 里 read-only="true"）：它们的重复调用是正常流程，
    //不参与下面的“原地打转”跳过逻辑
    std::set<std::string> read_only_tools;
    for(const ToolDef& t : tool_defs_){
        if(t.read_only) read_only_tools.insert(t.name);
    }

    //工具调用最多轮数。一次完整任务常常是“看现状 -> 改 -> 自查 -> 再修”，
    //6 轮太紧（画几个元件+连线+自查就容易打满），放宽到 12；配合下面的
    //“重复调用跳过”和“打满时改为要总结”，既不会无限循环，也不会半路甩错误。
    const int MaxRounds = 12;
    bool done_by_text = false;
    bool done_by_error = false;
    std::set<std::string> executed_calls;   //本轮已执行过的“写”操作，用于识别原地打转
    std::vector<std::string> call_log;      //本轮实际执行过的调用（打满轮数时做本地汇总）
    for(int round = 0; round < MaxRounds && !stop_.load(); ++round){
        AppSettings::ApiProfile profile;
        {
            std::lock_guard<std::mutex> lk(profile_mtx_);
            profile = profile_;
        }

        const ChatResponse resp = ChatCompletions(
            profile.base_url, profile.api_key, profile.model,
            context_.messages(), tools,
            [this]{ return shutdown_->load(); });

        if(!resp.ok){
            Post(AgentEventKind::Error, resp.error);
            done_by_error = true;
            break;
        }

        if(!resp.tool_calls.isArray() || resp.tool_calls.empty()){
            //纯文本回复：本轮结束
            context_.add_assistant_text(resp.content);
            Post(AgentEventKind::AssistantText, resp.content);
            done_by_text = true;
            break;
        }

        //有工具调用：记入上下文并逐个执行
        context_.add_assistant_tool_calls(resp.content, resp.tool_calls);
        for(const Json::Value& tc : resp.tool_calls){
            if(stop_.load()) break;
            if(!tc.isObject() || !tc.isMember("id") || !tc.isMember("function")) continue;
            const std::string id   = tc["id"].asString();
            const std::string name = tc["function"]["name"].asString();

            //解析 arguments（JSON字符串 -> Value）
            Json::Value args = Json::objectValue;
            if(tc["function"].isMember("arguments") && tc["function"]["arguments"].isString()){
                const std::string argstr = tc["function"]["arguments"].asString();
                Json::CharReaderBuilder builder;
                std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
                std::string errs;
                Json::Value v;
                if(reader->parse(argstr.data(), argstr.data() + argstr.size(), &v, &errs) &&
                   v.isObject()){
                    args = v;
                }
            }
            Post(AgentEventKind::ToolCall, name + "(" + json_one_line(args) + ")");

            //原地打转保护：只对“写”工具生效。
            //只读工具（查画布/查元件/连通性自查）的结果会随画布变化，
            //改完电路再查一次是正常流程，绝不能因为“参数相同”就跳过。
            const bool is_read_only = read_only_tools.count(name) != 0;
            const std::string call_key = name + "(" + json_one_line(args) + ")";
            if(!is_read_only && !executed_calls.insert(call_key).second){
                const std::string skipped =
                    "{\"ok\":false,\"error\":\"重复调用：与本轮之前某次写操作完全相同，已跳过。"
                    "请基于已有的结果继续，或改用不同的参数。\"}";
                context_.add_tool_result(id, name, skipped);
                Post(AgentEventKind::ToolResult, name + ": 重复调用，已跳过");
                continue;
            }

            const std::string result = ExecuteToolOnMain(name, args);
            //画布可能已经变了：清空写操作记录，避免把“改完再改一次”误判成打转
            if(!is_read_only) executed_calls.clear();
            call_log.push_back(call_key);
            context_.add_tool_result(id, name, result);
            Post(AgentEventKind::ToolResult, name + ": " + summarize(result));
        }
    }

    if(!done_by_text && !done_by_error && !stop_.load()){
        //打满轮数：给一份本地汇总（不再额外请求模型）。
        //上一版是“再发一次不带工具的请求让它总结”，结果模型把工具调用语法
        //（<｜｜DSML｜｜ …>）当普通文本吐了出来——那反而更像故障。
        std::string summary = "工具调用已达上限（" + std::to_string(MaxRounds) +
                              " 轮），已停止继续操作。本轮实际执行的调用：";
        if(call_log.empty()){
            summary += "（无）";
        }else{
            for(std::size_t i = 0; i < call_log.size(); ++i){
                if(i) summary += "；";
                summary += call_log[i];
            }
        }
        summary += "。你可以让我「检查连通性」或继续下一步。";
        Post(AgentEventKind::AssistantText, summary);
    }
    Post(AgentEventKind::Finished, "");
    busy_ = false;
}

} // namespace Agent

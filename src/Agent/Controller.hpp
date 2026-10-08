#pragma once
#include<wx/wx.h>
#include<thread>
#include<atomic>
#include<mutex>
#include<condition_variable>
#include<memory>
#include<string>

#include"../Settings/AppSettings.hpp"
#include"./Skills/Skills.hpp"
#include"./Context.hpp"

class Canvas;

namespace Agent{

//========================================================
// 推给 UI 的事件（ChatPanel 接收并展示）
//========================================================
enum class AgentEventKind{
    Started,       //开始处理用户消息
    UserEcho,      //回显用户消息（payload=原文）
    ToolCall,      //AI 请求调用工具（payload="name(args)"）
    ToolResult,    //工具执行结果（payload="name: 结果摘要"）
    AssistantText, //AI 文本回复（payload=文本）
    Error,         //错误（payload=错误信息）
    Finished,      //本轮对话结束（UI 可恢复输入）
};

// 事件类型必须先声明（wxDEFINE_EVENT 在 .cpp 里给出定义）。
// 注意：wxDECLARE_EVENT 第二参是事件类（需完整类型），因此放在类定义之后；
// 构造函数不内联引用 wxEVT_AGENT，改在 .cpp 里定义。

class AgentEvent : public wxEvent{
public:
    AgentEvent(AgentEventKind kind, const wxString& payload);   //在 .cpp 定义
    wxEvent* Clone() const override { return new AgentEvent(*this); }
    AgentEventKind Kind() const { return kind_; }
    const wxString& Payload() const { return payload_; }
private:
    AgentEventKind kind_;
    wxString payload_;
};

wxDECLARE_EVENT(wxEVT_AGENT, AgentEvent);

//========================================================
// 工具桥：工作线程 -> 主线程 的工具执行请求
//========================================================
struct ToolRequest{
    std::string handler;
    Json::Value args;
    std::string result;
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
};

class ToolExecuteRequestEvent : public wxEvent{
public:
    ToolExecuteRequestEvent(std::shared_ptr<ToolRequest> req);   //在 .cpp 定义
    wxEvent* Clone() const override { return new ToolExecuteRequestEvent(*this); }
    std::shared_ptr<ToolRequest> Request() const { return req_; }
private:
    std::shared_ptr<ToolRequest> req_;
};

wxDECLARE_EVENT(wxEVT_TOOL_EXECUTE, ToolExecuteRequestEvent);

//========================================================
// 调度层
//========================================================
// 用户发送消息 -> 后台线程跑“上下文+API+工具调用”循环 -> 过程/结果通过
// AgentEvent 发给 UI。工具调用不直接在工作线程执行（工具会触碰画布控件），
// 而是通过工具桥（事件+条件变量）派发回主线程执行并等待结果。
class Controller : public wxEvtHandler{
public:
    //ui_target：接收 AgentEvent 的窗口（如 ChatPanel）；canvas：工具操作对象
    Controller(wxEvtHandler* ui_target, Canvas* canvas);
    ~Controller();

    //设置面板改配置后调用（下一次请求生效）
    void SetProfile(const AppSettings::ApiProfile& profile);
    const AppSettings::ApiProfile& Profile() const { return profile_; }

    //用户发送消息（主线程调用；忙时忽略并返回false）
    bool StartChat(const std::string& user_text);
    bool Busy() const { return busy_; }
    void Stop();   //请求停止（不中断正在进行的网络请求，下个回合生效）

private:
    void RunConversation(const std::string& user_text);   //工作线程入口
    std::string ExecuteToolOnMain(const std::string& handler, const Json::Value& args);
    void OnToolExecute(ToolExecuteRequestEvent& e);
    void Post(AgentEventKind kind, const std::string& payload);

    wxEvtHandler* ui_target_;
    Canvas* canvas_;
    AppSettings::ApiProfile profile_;
    Context context_;
    ToolRegistry registry_;
    std::vector<ToolDef> tool_defs_;          //来自 Skills/list.xml
    std::thread worker_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> stop_{false};
    std::shared_ptr<std::atomic<bool>> shutdown_;   //析构时唤醒工具桥等待
    std::mutex profile_mtx_;                          //保护 profile_
};

} // namespace Agent

#pragma once
#include<wx/wx.h>
#include<wx/html/htmlwin.h>
#include<vector>
#include<string>
#include"../Controller.hpp"

namespace Agent{

// 聊天气泡用的 HTML 窗口：注册内存文件系统与圆形头像位图，
// 使 <img src="memory:avatar_user.png"> 能显示内存里的圆形纯色头像
class ChatHtmlWindow : public wxHtmlWindow{
public:
    ChatHtmlWindow(wxWindow* parent);
};

// 聊天窗口：类似聊天软件的气泡样式。
// 用户消息靠右（蓝头像+浅蓝气泡），AI 靠左（绿头像+白气泡），
// 工具调用为独立区块（默认展开，点头部可折叠），错误为红边气泡。
// 整页 HTML 渲染（wxHtmlWindow），每次事件后重建页面并滚动到底部。
class ChatPanel : public wxPanel{
public:
    ChatPanel(wxWindow* parent);
    void SetController(Controller* ctrl){ ctrl_ = ctrl; }

private:
    struct ChatMsg{
        enum class Kind{ User, AI, Tool, Error, Thinking };
        Kind kind = Kind::AI;
        std::string text;    //用户/AI文本、工具名、错误文本
        std::string args;    //工具参数（单行JSON）
        std::string result;  //工具结果（原始JSON）
        bool expanded = true;   //工具块是否展开
    };

    void OnAgentEvent(AgentEvent& e);
    void OnSend(wxCommandEvent& e);
    void OnLinkClicked(wxHtmlLinkEvent& e);
    void Append(ChatMsg::Kind kind, const std::string& text);
    void RemoveThinking();   //移除"AI 正在思考…"占位行
    void RebuildHtml();
    void ScrollToBottom();
    static std::string EscapeHtml(const std::string& s);
    static std::string Text2Html(const std::string& s);

    Controller* ctrl_ = nullptr;
    ChatHtmlWindow* html_;
    wxTextCtrl* input_;
    wxButton* send_btn_;
    std::vector<ChatMsg> msgs_;
};

} // namespace Agent

#include"./ChatPanel.hpp"

#include<wx/button.h>
#include<wx/sizer.h>
#include<wx/textctrl.h>
#include<wx/fs_mem.h>
#include<wx/filesys.h>
#include<wx/image.h>
#include<md4c-html.h>   // AI 回复的 Markdown -> HTML（md4c 第三方库）

namespace Agent{

namespace{

// 纯色圆形头像：wxImage 像素级画圆（无 DC/GTK 依赖，纯色、无文字）
wxImage MakeAvatar(const wxColour& c, int size = 36){
    wxImage img(size, size);
    img.InitAlpha();
    const double r = size / 2.0;
    for(int y = 0; y < size; ++y){
        for(int x = 0; x < size; ++x){
            const double dx = x + 0.5 - r, dy = y + 0.5 - r;
            if(dx * dx + dy * dy <= r * r){
                img.SetRGB(x, y, c.Red(), c.Green(), c.Blue());
                img.SetAlpha(x, y, 255);
            }else{
                img.SetAlpha(x, y, 0);
            }
        }
    }
    return img;
}

// 文本行截断（工具参数/结果太长时避免刷屏）
std::string Clamp(const std::string& s, size_t max_len){
    if(s.size() <= max_len) return s;
    return s.substr(0, max_len) + "…(截断)";
}

// std::string(UTF-8) -> wxString
wxString U8(const std::string& s){
    return wxString::FromUTF8(s.c_str(), s.size());
}

// Markdown -> HTML 片段（md4c-html）。
// 开启表格/删除线/任务列表扩展；禁用原始 HTML（NOHTMLBLOCKS/NOHTMLSPANS），
// 防止 AI 输出里夹带的 HTML 破坏界面布局
std::string MarkdownToHtml(const std::string& md){
    std::string html;
    md_html(md.data(), md.size(),
            [](const MD_CHAR* s, MD_SIZE n, void* userdata){
                static_cast<std::string*>(userdata)->append(s, n);
            },
            &html,
            MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS |
            MD_FLAG_NOHTMLBLOCKS | MD_FLAG_NOHTMLSPANS,
            MD_HTML_FLAG_SKIP_UTF8_BOM);
    return html;
}

} // namespace

ChatHtmlWindow::ChatHtmlWindow(wxWindow* parent) : wxHtmlWindow(parent){
    //全局注册一次：内存文件系统 + 两个圆形头像位图（用户蓝 / AI 绿）
    static const bool init = []{
        wxInitAllImageHandlers();
        wxFileSystem::AddHandler(new wxMemoryFSHandler);
        wxMemoryFSHandler::AddFile("avatar_user.png",
                                   MakeAvatar(wxColour(30, 120, 220)), wxBITMAP_TYPE_PNG);
        wxMemoryFSHandler::AddFile("avatar_ai.png",
                                   MakeAvatar(wxColour(60, 170, 90)),  wxBITMAP_TYPE_PNG);
        return true;
    }();
    (void)init;
}

ChatPanel::ChatPanel(wxWindow* parent) : wxPanel(parent){
    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

    html_ = new ChatHtmlWindow(this);
    root->Add(html_, 1, wxEXPAND | wxALL, 6);

    //输入行：单行输入 + 发送按钮
    wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
    input_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                            wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    row->Add(input_, 1, wxEXPAND | wxRIGHT, 6);
    send_btn_ = new wxButton(this, wxID_ANY, wxString::FromUTF8("发送"));
    row->Add(send_btn_, 0, wxEXPAND);
    root->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    SetSizer(root);

    //事件：AI 事件更新显示；发送按钮/回车发送；工具块链接折叠
    Bind(wxEVT_AGENT, &ChatPanel::OnAgentEvent, this);
    Bind(wxEVT_BUTTON, &ChatPanel::OnSend, this);
    input_->Bind(wxEVT_TEXT_ENTER, &ChatPanel::OnSend, this);
    Bind(wxEVT_HTML_LINK_CLICKED, &ChatPanel::OnLinkClicked, this);
}

// HTML 特殊字符转义
std::string ChatPanel::EscapeHtml(const std::string& s){
    std::string out;
    out.reserve(s.size());
    for(char ch : s){
        switch(ch){
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += ch;
        }
    }
    return out;
}

// 文本 -> HTML：转义 + 换行转 <br>
std::string ChatPanel::Text2Html(const std::string& s){
    std::string out = EscapeHtml(s);
    std::string replaced;
    replaced.reserve(out.size());
    for(char ch : out){
        if(ch == '\n') replaced += "<br>";
        else           replaced += ch;
    }
    return replaced;
}

void ChatPanel::Append(ChatMsg::Kind kind, const std::string& text){
    ChatMsg m;
    m.kind = kind;
    m.text = text;
    msgs_.push_back(std::move(m));
}

void ChatPanel::OnAgentEvent(AgentEvent& e){
    const std::string p = std::string(e.Payload().utf8_str().data());
    switch(e.Kind()){
        case AgentEventKind::Started:
            send_btn_->Disable();
            input_->Disable();
            Append(ChatMsg::Kind::Thinking, "AI 正在思考…");
            break;
        case AgentEventKind::UserEcho:
            Append(ChatMsg::Kind::User, p);
            break;
        case AgentEventKind::ToolCall:
            {
                //p = "name(args)"
                ChatMsg m;
                m.kind = ChatMsg::Kind::Tool;
                const size_t lp = p.find('(');
                m.text = (lp == std::string::npos) ? p : p.substr(0, lp);
                m.args = (lp == std::string::npos) ? "" : p.substr(lp + 1, p.size() - lp - 2);
                msgs_.push_back(std::move(m));
            }
            break;
        case AgentEventKind::ToolResult:
            //更新最后一条工具块的结果（p = "name: 结果摘要"）
            for(auto it = msgs_.rbegin(); it != msgs_.rend(); ++it){
                if(it->kind == ChatMsg::Kind::Tool){
                    const size_t colon = p.find(": ");
                    it->result = (colon == std::string::npos) ? p : p.substr(colon + 2);
                    break;
                }
            }
            break;
        case AgentEventKind::AssistantText:
            RemoveThinking();
            Append(ChatMsg::Kind::AI, p);
            break;
        case AgentEventKind::Error:
            RemoveThinking();
            Append(ChatMsg::Kind::Error, p);
            break;
        case AgentEventKind::Finished:
            RemoveThinking();
            send_btn_->Enable();
            input_->Enable();
            input_->SetFocus();
            break;
    }
    RebuildHtml();
    ScrollToBottom();
}

//移除"AI 正在思考…"占位行
void ChatPanel::RemoveThinking(){
    for(auto it = msgs_.begin(); it != msgs_.end(); ++it){
        if(it->kind == ChatMsg::Kind::Thinking){
            msgs_.erase(it);
            break;
        }
    }
}

void ChatPanel::RebuildHtml(){
    wxString h;
    h += wxT("<html><body bgcolor=\"#F2F3F5\">");
    for(int i = 0; i < (int)msgs_.size(); ++i){
        const ChatMsg& m = msgs_[i];
        switch(m.kind){
            case ChatMsg::Kind::User:
                //靠右：气泡 + 蓝色头像
                h += wxString::Format(
                    wxT("<table width=\"100%%\" cellspacing=\"2\" cellpadding=\"0\"><tr>"
                        "<td align=\"right\"><table cellspacing=\"0\" cellpadding=\"6\"><tr>"
                        "<td bgcolor=\"#D6EAF8\">%s</td>"
                        "<td><img src=\"memory:avatar_user.png\" width=\"36\" height=\"36\"></td>"
                        "</tr></table></td></tr></table>"),
                    U8(Text2Html(m.text)));
                break;

            case ChatMsg::Kind::AI:
                //靠左：绿色头像 + 白色气泡（AI 回复渲染 Markdown）
                h += wxString::Format(
                    wxT("<table width=\"100%%\" cellspacing=\"2\" cellpadding=\"0\"><tr>"
                        "<td><table cellspacing=\"0\" cellpadding=\"6\"><tr>"
                        "<td><img src=\"memory:avatar_ai.png\" width=\"36\" height=\"36\"></td>"
                        "<td bgcolor=\"#FFFFFF\">%s</td>"
                        "</tr></table></td></tr></table>"),
                    U8(MarkdownToHtml(m.text)));
                break;

            case ChatMsg::Kind::Tool:
                {
                    //工具调用块：浅黄底，默认展开显示参数+结果，点头部链接折叠/展开
                    wxString block;
                    block += wxT("<table width=\"100%\" cellspacing=\"2\" cellpadding=\"0\"><tr><td>"
                                 "<table cellspacing=\"0\" cellpadding=\"6\"><tr>"
                                 "<td bgcolor=\"#FFF6DD\">");
                    block += wxString::Format(
                        wxT("<b><a href=\"tool:%d\">%s</a></b>"),
                        i, U8(EscapeHtml(m.text)));
                    if(m.expanded){
                        if(!m.args.empty()){
                            block += wxT("<br><font color=\"#8a6d1a\">参数: </font>");
                            block += U8(EscapeHtml(Clamp(m.args, 200)));
                        }
                        if(!m.result.empty()){
                            block += wxT("<br><font color=\"#666666\">结果: </font>");
                            block += U8(EscapeHtml(Clamp(m.result, 400)));
                        }
                    }else{
                        block += wxT(" <font color=\"#8a6d1a\">(已折叠)</font>");
                    }
                    block += wxT("</td></tr></table></td></tr></table>");
                    h += block;
                }
                break;

            case ChatMsg::Kind::Error:
                {
                    wxString block;
                    block = wxT("<table width=\"100%\" cellspacing=\"2\" cellpadding=\"0\"><tr><td>"
                                "<table cellspacing=\"0\" cellpadding=\"6\"><tr>"
                                "<td bgcolor=\"#FDECEA\">");
                    block += wxT("<b><font color=\"#C0392B\">错误</font></b><br>");
                    block += wxT("<font color=\"#922B21\">");
                    block += U8(Text2Html(m.text));
                    block += wxT("</font></td></tr></table></td></tr></table>");
                    h += block;
                }
                break;

            case ChatMsg::Kind::Thinking:
                h += wxT("<table width=\"100%\" cellspacing=\"2\"><tr><td>"
                         "<font color=\"#888888\"><i>AI 正在思考…</i></font>"
                         "</td></tr></table>");
                break;
        }
    }
    h += wxT("</body></html>");
    html_->SetPage(h);
}

void ChatPanel::ScrollToBottom(){
    if(!html_) return;
    html_->Scroll(0, html_->GetVirtualSize().y);
}

void ChatPanel::OnLinkClicked(wxHtmlLinkEvent& e){
    const wxString href = e.GetLinkInfo().GetHref();
    if(href.StartsWith(wxT("tool:"))){
        long idx = -1;
        if(href.Mid(5).ToLong(&idx) && idx >= 0 && idx < (long)msgs_.size()){
            //记住滚动位置，切换展开状态后重建
            const int pos = html_->GetScrollPos(wxVERTICAL);
            msgs_[idx].expanded = !msgs_[idx].expanded;
            RebuildHtml();
            html_->SetScrollPos(wxVERTICAL, pos, true);
        }
    }
    //不 Skip：已处理，wxHtmlWindow 不会执行默认导航
}

void ChatPanel::OnSend(wxCommandEvent&){
    if(!ctrl_) return;
    if(ctrl_->Busy()){
        Append(ChatMsg::Kind::Error, "上一轮还在处理中，请稍候");
        RebuildHtml();
        ScrollToBottom();
        return;
    }
    const wxString text = input_->GetValue().Trim();
    if(text.empty()) return;
    input_->Clear();
    ctrl_->StartChat(std::string(text.utf8_str().data()));
}

} // namespace Agent

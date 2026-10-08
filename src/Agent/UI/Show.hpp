#pragma once
#include<wx/wx.h>
#include<wx/splitter.h>

namespace Agent{

// “AI 助手面板”显示开关：常驻在菜单栏（视图 -> AI 助手面板）。
// 隐藏时不占据空间（从 splitter 摘除，窗口本身保留），
// 显示时占据全局右侧，并可用分隔条拉伸宽度。
class Show{
public:
    //host：承载 work/agent 两个窗格的分隔器；work 为工作区，agent 为 AgentArea
    Show(wxSplitterWindow* host, wxWindow* work, wxWindow* agent);

    bool Visible() const { return visible_; }
    void Toggle();
    void SetVisible(bool on);

private:
    wxSplitterWindow* host_;
    wxWindow* work_;
    wxWindow* agent_;
    bool visible_ = false;
};

} // namespace Agent

#pragma once

#include <wx/panel.h>
#include <functional>

class wxBoxSizer;
class wxStaticText;
class ThemedButton;

// 统一卡片容器：页面底色上内缩一张圆角卡片，带 1px 抗锯齿描边和轻微阴影。
// 子面板（ToolBar/ExplorerPane/AttributeBar/AgentArea/VerilogPanel）改用它之后，
// 边界、留白和标题就能保持一致。
class CardPanel : public wxPanel{
public:
    CardPanel(wxWindow* parent, wxWindowID id = wxID_ANY,
              const wxString& title = wxEmptyString,
              bool closeable = false,
              int card_margin = -1);

    // 卡片内容区：派生类把自己的控件加到这里，不要直接 SetSizer。
    wxBoxSizer* Content() const { return body_sizer_; }

    void SetTitle(const wxString& title);
    void SetCloseHandler(std::function<void()> handler);
    void SetCardMargin(int margin);

private:
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);

    wxBoxSizer* root_sizer_ = nullptr;
    wxBoxSizer* body_sizer_ = nullptr;
    wxBoxSizer* header_sizer_ = nullptr;
    wxStaticText* title_text_ = nullptr;
    ThemedButton* close_button_ = nullptr;
    std::function<void()> close_handler_;
    wxString title_;
    int card_margin_ = 0;
    int content_padding_ = 0;
    bool closeable_ = false;
};

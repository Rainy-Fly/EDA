#pragma once

#include <wx/panel.h>
#include <wx/bitmap.h>
#include <wx/timer.h>
#include <functional>

// 自绘圆角按钮：圆角、柔和阴影、悬停/按压/选中动效在 MSW 与 GTK 下表现一致。
// 形状用 wxGraphicsContext 画（抗锯齿），文字/图标仍用 wxDC 画，避免中文发虚。
class ThemedButton : public wxPanel{
public:
    ThemedButton(wxWindow* parent, wxWindowID id, const wxString& label,
                 bool primary = false, const wxBitmap& bitmap = wxNullBitmap);
    ~ThemedButton() override;

    void SetSelected(bool selected);
    bool IsSelected() const { return selected_; }
    //禁用/启用时立即重画（聊天发送按钮在请求期间会 Disable/Enable）。
    bool Enable(bool enable = true) override;
    void SetClickHandler(std::function<void()> handler){ click_handler_ = std::move(handler); }
    void SetHoverHandler(std::function<void()> handler){ hover_handler_ = std::move(handler); }
    void SetLabel(const wxString& label);
    void SetAccessoryText(const wxString& text);
    void SetLeadingText(const wxString& text);
    void SetTextAlignment(int alignment);
    void SetBitmap(const wxBitmap& bitmap);

    // 扁平样式：默认不画底色/边框，悬停或选中时才出现（工具栏图标按钮用）。
    void SetFlat(bool flat = true);
    bool IsFlat() const { return flat_; }
    // 圆角半径（物理像素）；小于 0 表示使用默认值 FromDIP(8)。
    void SetCornerRadius(int radius);
    void SetShadowVisible(bool visible);

private:
    void OnPaint(wxPaintEvent& event);
    void OnMouseEnter(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnCaptureLost(wxMouseCaptureLostEvent& event);
    void OnFocus(wxFocusEvent& event);
    void OnTimer(wxTimerEvent& event);

    void StartAnimation();
    void SnapAnimation();
    void RefreshForStateChange();

    wxString label_;
    wxString accessory_text_;
    wxString leading_text_;
    wxBitmap bitmap_;
    std::function<void()> click_handler_;
    std::function<void()> hover_handler_;
    int text_alignment_ = wxALIGN_CENTER;
    int corner_radius_ = -1;
    bool primary_ = false;
    bool flat_ = false;
    bool shadow_ = true;

    bool hovered_ = false;
    bool pressed_ = false;
    bool selected_ = false;
    bool focused_ = false;

    // 0..1 的动画插值量：悬停、按压、选中各自独立过渡。
    double hover_mix_ = 0.0;
    double press_mix_ = 0.0;
    double select_mix_ = 0.0;
    double hover_target_ = 0.0;
    double press_target_ = 0.0;
    double select_target_ = 0.0;
    wxTimer anim_timer_;
};

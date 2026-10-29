#pragma once

#include <wx/panel.h>
#include <wx/bitmap.h>
#include <functional>

// 使用自绘圆角背景，确保 hover / press / selected 在 GTK 与 MSW 下都有一致反馈。
class ThemedButton : public wxPanel{
public:
    ThemedButton(wxWindow* parent, wxWindowID id, const wxString& label,
                 bool primary = false, const wxBitmap& bitmap = wxNullBitmap);

    void SetSelected(bool selected);
    bool IsSelected() const { return selected_; }
    void SetClickHandler(std::function<void()> handler){ click_handler_ = std::move(handler); }
    void SetHoverHandler(std::function<void()> handler){ hover_handler_ = std::move(handler); }
    void SetLabel(const wxString& label);
    void SetAccessoryText(const wxString& text);
    void SetLeadingText(const wxString& text);
    void SetTextAlignment(int alignment);
    void SetBitmap(const wxBitmap& bitmap);

protected:
    //自绘面板不像 wxButton 那样自带“按文字算宽度”的最佳尺寸：
    //不实现这个函数时，sizer 会按 wxPanel 的默认极小尺寸布局，
    //按钮就被压成几根竖条、文字没地方画（设置页那排按钮就是这样）。
    wxSize DoGetBestClientSize() const override;

private:
    void OnPaint(wxPaintEvent& event);
    void OnMouseEnter(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnCaptureLost(wxMouseCaptureLostEvent& event);
    void OnFocus(wxFocusEvent& event);

    wxString label_;
    wxString accessory_text_;
    wxString leading_text_;
    wxBitmap bitmap_;
    std::function<void()> click_handler_;
    std::function<void()> hover_handler_;
    int text_alignment_ = wxALIGN_CENTER;
    bool primary_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
    bool selected_ = false;
    bool focused_ = false;
};

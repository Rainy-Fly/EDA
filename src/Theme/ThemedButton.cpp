#include "ThemedButton.hpp"
#include "Theme.hpp"

#include <wx/dcbuffer.h>
#include <wx/dc.h>
#include <algorithm>
#include <utility>

ThemedButton::ThemedButton(wxWindow* parent, wxWindowID id, const wxString& label,
                           bool primary, const wxBitmap& bitmap)
    : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
      label_(label), bitmap_(bitmap), primary_(primary){
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(-1, 32));
    SetCursor(wxCursor(wxCURSOR_HAND));
    Bind(wxEVT_PAINT, &ThemedButton::OnPaint, this);
    Bind(wxEVT_ENTER_WINDOW, &ThemedButton::OnMouseEnter, this);
    Bind(wxEVT_LEAVE_WINDOW, &ThemedButton::OnMouseLeave, this);
    Bind(wxEVT_LEFT_DOWN, &ThemedButton::OnLeftDown, this);
    Bind(wxEVT_LEFT_UP, &ThemedButton::OnLeftUp, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &ThemedButton::OnCaptureLost, this);
    Bind(wxEVT_SET_FOCUS, &ThemedButton::OnFocus, this);
    Bind(wxEVT_KILL_FOCUS, &ThemedButton::OnFocus, this);
}

void ThemedButton::SetSelected(bool selected){
    if(selected_ == selected) return;
    selected_ = selected;
    Refresh();
}

// 按内容（前导文字 + 图标 + 文字 + 尾部文字）计算最佳宽度。
// 内边距与 OnPaint 里的布局一致：文字默认居中、最小 8px；这里每侧留 12px 更从容。
wxSize ThemedButton::DoGetBestClientSize() const{
    int width = 0;
    if(!leading_text_.empty()) width += GetTextExtent(leading_text_).x + 6;
    if(bitmap_.IsOk()){
        width += bitmap_.GetWidth();
        if(!label_.empty()) width += 6;
    }
    width += GetTextExtent(label_).x;
    if(!accessory_text_.empty()) width += GetTextExtent(accessory_text_).x + 6;

    width += 24;                    //左右各 12px 内边距
    width = std::max(width, 56);    //再短的文字也保证一个可点中的最小宽度
    return wxSize(width, 32);       //高度与 SetMinSize 保持一致
}

void ThemedButton::SetLabel(const wxString& label){
    label_ = label;
    Refresh();
}

void ThemedButton::SetAccessoryText(const wxString& text){
    accessory_text_ = text;
    Refresh();
}

void ThemedButton::SetLeadingText(const wxString& text){
    leading_text_ = text;
    Refresh();
}

void ThemedButton::SetTextAlignment(int alignment){
    text_alignment_ = alignment;
    Refresh();
}

void ThemedButton::SetBitmap(const wxBitmap& bitmap){
    bitmap_ = bitmap;
    Refresh();
}

void ThemedButton::OnPaint(wxPaintEvent&){
    wxAutoBufferedPaintDC dc(this);
    const wxSize size = GetClientSize();
    const wxColour parent_background = GetParent()
        ? GetParent()->GetBackgroundColour() : UiTheme::Page;
    dc.SetBackground(wxBrush(parent_background));
    dc.Clear();

    wxColour fill = primary_ ? UiTheme::Brand900 : UiTheme::Surface;
    wxColour border = primary_ ? UiTheme::Brand900 : UiTheme::Border;
    wxColour ink = primary_ ? UiTheme::Surface : UiTheme::TextSecondary;
    if(!IsEnabled()){
        fill = UiTheme::Subtle;
        border = UiTheme::BorderSubtle;
        ink = UiTheme::TextDisabled;
    }else if(pressed_){
        fill = primary_ ? UiTheme::Brand950 : UiTheme::Brand200;
        border = primary_ ? UiTheme::Brand950 : UiTheme::Brand700;
        ink = primary_ ? UiTheme::Surface : UiTheme::Brand900;
    }else if(selected_){
        fill = UiTheme::Brand100;
        border = UiTheme::Brand700;
        ink = UiTheme::Brand900;
    }else if(hovered_){
        fill = primary_ ? UiTheme::Brand800 : UiTheme::Brand100;
        border = primary_ ? UiTheme::Brand800 : UiTheme::BorderStrong;
        ink = primary_ ? UiTheme::Surface : UiTheme::Brand900;
    }

    dc.SetPen(wxPen(border, 1));
    dc.SetBrush(wxBrush(fill));
    dc.DrawRoundedRectangle(1, 1, size.x - 2, size.y - 2, 6);
    if(focused_ && IsEnabled()){
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(UiTheme::Brand500, 2));
        dc.DrawRoundedRectangle(2, 2, size.x - 4, size.y - 4, 6);
    }

    const wxSize label_size = dc.GetTextExtent(label_);
    const wxSize leading_size = dc.GetTextExtent(leading_text_);
    const wxSize accessory_size = dc.GetTextExtent(accessory_text_);
    int content_width = bitmap_.IsOk() ? bitmap_.GetWidth() : 0;
    if(content_width && !label_.empty()) content_width += 6;
    content_width += label_size.x;
    int x = text_alignment_ == wxALIGN_LEFT
        ? 10 : std::max(8, (size.x - content_width) / 2);
    if(bitmap_.IsOk()){
        const int y = (size.y - bitmap_.GetHeight()) / 2;
        dc.DrawBitmap(bitmap_, x, y, true);
        x += bitmap_.GetWidth() + (label_.empty() ? 0 : 6);
    }
    dc.SetTextForeground(ink);
    if(!leading_text_.empty()){
        dc.DrawText(leading_text_, 10, (size.y - leading_size.y) / 2);
    }
    const int label_x = text_alignment_ == wxALIGN_LEFT && !bitmap_.IsOk()
        ? x + (leading_text_.empty() ? 0 : 24)
        : x;
    dc.DrawText(label_, label_x, (size.y - label_size.y) / 2);
    if(!accessory_text_.empty()){
        dc.SetTextForeground(UiTheme::TextTertiary);
        dc.DrawText(accessory_text_, size.x - accessory_size.x - 10,
                    (size.y - accessory_size.y) / 2);
    }
}

void ThemedButton::OnMouseEnter(wxMouseEvent& event){
    hovered_ = true;
    Refresh();
    if(hover_handler_) hover_handler_();
    event.Skip();
}

void ThemedButton::OnMouseLeave(wxMouseEvent& event){
    hovered_ = false;
    if(!pressed_) Refresh();
    event.Skip();
}

void ThemedButton::OnLeftDown(wxMouseEvent& event){
    if(!IsEnabled()) return;
    pressed_ = true;
    if(!HasCapture()) CaptureMouse();
    SetFocus();
    Refresh();
}

void ThemedButton::OnLeftUp(wxMouseEvent& event){
    if(!pressed_) return;
    const bool activate = GetClientRect().Contains(event.GetPosition()) && IsEnabled();
    pressed_ = false;
    if(HasCapture()) ReleaseMouse();
    Refresh();
    if(activate){
        if(click_handler_){
            click_handler_();
        }else{
            wxCommandEvent command(wxEVT_BUTTON, GetId());
            command.SetEventObject(this);
            wxPostEvent(GetParent(), command);
        }
    }
}

void ThemedButton::OnCaptureLost(wxMouseCaptureLostEvent&){
    pressed_ = false;
    Refresh();
}

void ThemedButton::OnFocus(wxFocusEvent& event){
    focused_ = event.GetEventType() == wxEVT_SET_FOCUS;
    Refresh();
    event.Skip();
}

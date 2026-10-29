#include "ThemedButton.hpp"
#include "Theme.hpp"

#include <wx/dcbuffer.h>
#include <wx/dc.h>
#include <wx/graphics.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace{

double approach(double value, double target, double step){
    if(value < target) return std::min(target, value + step);
    if(value > target) return std::max(target, value - step);
    return value;
}

bool near(double a, double b){
    return std::abs(a - b) < 0.001;
}

} // namespace

ThemedButton::ThemedButton(wxWindow* parent, wxWindowID id, const wxString& label,
                           bool primary, const wxBitmap& bitmap)
    : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
      label_(label), bitmap_(bitmap), primary_(primary){
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(-1, FromDIP(34)));
    SetCursor(wxCursor(wxCURSOR_HAND));
    anim_timer_.SetOwner(this);
    Bind(wxEVT_PAINT, &ThemedButton::OnPaint, this);
    Bind(wxEVT_ENTER_WINDOW, &ThemedButton::OnMouseEnter, this);
    Bind(wxEVT_LEAVE_WINDOW, &ThemedButton::OnMouseLeave, this);
    Bind(wxEVT_LEFT_DOWN, &ThemedButton::OnLeftDown, this);
    Bind(wxEVT_LEFT_UP, &ThemedButton::OnLeftUp, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &ThemedButton::OnCaptureLost, this);
    Bind(wxEVT_SET_FOCUS, &ThemedButton::OnFocus, this);
    Bind(wxEVT_KILL_FOCUS, &ThemedButton::OnFocus, this);
    Bind(wxEVT_TIMER, &ThemedButton::OnTimer, this);
}

ThemedButton::~ThemedButton(){
    anim_timer_.Stop();
}

bool ThemedButton::Enable(bool enable){
    const bool changed = wxPanel::Enable(enable);
    if(changed){
        if(!enable){
            hover_target_ = 0.0;
            press_target_ = 0.0;
            SnapAnimation();
        }
        Refresh();
    }
    return changed;
}

void ThemedButton::SetSelected(bool selected){
    if(selected_ == selected) return;
    selected_ = selected;
    select_target_ = selected ? 1.0 : 0.0;
    StartAnimation();
    RefreshForStateChange();
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

void ThemedButton::SetFlat(bool flat){
    if(flat_ == flat) return;
    flat_ = flat;
    Refresh();
}

void ThemedButton::SetCornerRadius(int radius){
    corner_radius_ = radius;
    Refresh();
}

void ThemedButton::SetShadowVisible(bool visible){
    shadow_ = visible;
    Refresh();
}

void ThemedButton::StartAnimation(){
    if(!UiTheme::AnimationsEnabled() || !IsShownOnScreen()){
        SnapAnimation();
        return;
    }
    if(!anim_timer_.IsRunning()) anim_timer_.Start(16);
}

void ThemedButton::SnapAnimation(){
    hover_mix_ = hover_target_;
    press_mix_ = press_target_;
    select_mix_ = select_target_;
    if(anim_timer_.IsRunning()) anim_timer_.Stop();
}

void ThemedButton::RefreshForStateChange(){
    if(!UiTheme::AnimationsEnabled()) Refresh();
}

void ThemedButton::OnTimer(wxTimerEvent&){
    if(!IsEnabled()){
        hover_target_ = 0.0;
        press_target_ = 0.0;
    }

    const double hover_step = 16.0 / UiTheme::HoverMs;
    const double press_step = 16.0 / UiTheme::PressMs;
    const double select_step = 16.0 / UiTheme::SelectMs;

    hover_mix_ = approach(hover_mix_, hover_target_, hover_step);
    press_mix_ = approach(press_mix_, press_target_, press_step);
    select_mix_ = approach(select_mix_, select_target_, select_step);

    if(near(hover_mix_, hover_target_) &&
       near(press_mix_, press_target_) &&
       near(select_mix_, select_target_)){
        anim_timer_.Stop();
    }
    Refresh(false);
}

void ThemedButton::OnPaint(wxPaintEvent&){
    wxAutoBufferedPaintDC dc(this);
    const wxSize size = GetClientSize();
    if(size.x <= 0 || size.y <= 0) return;

    const wxColour parent_background = GetParent()
        ? GetParent()->GetBackgroundColour() : UiTheme::Page;
    dc.SetBackground(wxBrush(parent_background));
    dc.Clear();
    dc.SetFont(GetFont());

    const bool enabled = IsEnabled();
    const int inset = FromDIP(2);
    const int shadow_room = FromDIP(3);
    wxRect body(inset, inset, size.x - inset * 2, size.y - inset * 2 - shadow_room);
    if(body.height < FromDIP(10)){
        body = wxRect(inset, inset, size.x - inset * 2, size.y - inset * 2);
    }
    if(body.width <= 0 || body.height <= 0) return;

    const int radius = corner_radius_ >= 0
        ? corner_radius_ : FromDIP(UiTheme::RadiusButton);

    //---- 状态色：先取各状态基色，再按动画插值量混合 ----
    const wxColour idle_fill = primary_ ? UiTheme::Brand900
                              : (flat_ ? parent_background : UiTheme::Surface);
    const wxColour hover_fill = primary_ ? UiTheme::Brand800
                               : (flat_ ? UiTheme::Hover : UiTheme::Brand100);
    const wxColour press_fill = primary_ ? UiTheme::Brand950 : UiTheme::Pressed;
    const wxColour select_fill = UiTheme::Brand100;

    const wxColour idle_border = primary_ ? UiTheme::Brand900
                               : (flat_ ? parent_background : UiTheme::Border);
    const wxColour hover_border = primary_ ? UiTheme::Brand800 : UiTheme::BorderStrong;
    const wxColour press_border = primary_ ? UiTheme::Brand950 : UiTheme::Brand700;
    const wxColour select_border = UiTheme::Brand700;

    const wxColour idle_ink = primary_ ? UiTheme::Surface
                            : (flat_ ? UiTheme::TextSecondary : UiTheme::Text);
    const wxColour hover_ink = primary_ ? UiTheme::Surface : UiTheme::Brand900;
    const wxColour press_ink = primary_ ? UiTheme::Surface : UiTheme::Brand950;
    const wxColour select_ink = UiTheme::Brand900;

    wxColour fill = UiTheme::Mix(idle_fill, hover_fill, hover_mix_);
    fill = UiTheme::Mix(fill, press_fill, press_mix_);
    fill = UiTheme::Mix(fill, select_fill, select_mix_);

    wxColour border = UiTheme::Mix(idle_border, hover_border, hover_mix_);
    border = UiTheme::Mix(border, press_border, press_mix_);
    border = UiTheme::Mix(border, select_border, select_mix_);

    wxColour ink = UiTheme::Mix(idle_ink, hover_ink, hover_mix_);
    ink = UiTheme::Mix(ink, press_ink, press_mix_);
    ink = UiTheme::Mix(ink, select_ink, select_mix_);

    if(!enabled){
        fill = UiTheme::Subtle;
        border = UiTheme::BorderSubtle;
        ink = UiTheme::TextDisabled;
    }

    //---- 阴影：扁平按钮只在悬停/选中时出现；普通按钮常驻一层淡阴影 ----
    int shadow_alpha = 0;
    if(enabled && shadow_){
        const double level = std::max({hover_mix_, press_mix_, select_mix_});
        shadow_alpha = flat_
            ? static_cast<int>(std::lround(level * 16.0))
            : static_cast<int>(std::lround(12.0 + level * 8.0));
    }

    //---- 形状：GDI+ 抗锯齿；创建失败则回退原 GDI 圆角 ----
    bool shapes_drawn = false;
    if(wxGraphicsContext* gc = wxGraphicsContext::CreateFromUnknownDC(dc)){
        gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
        if(shadow_alpha > 0){
            UiTheme::DrawSoftShadow(*gc, body, radius, FromDIP(2),
                                    3, std::max(1, shadow_alpha));
        }

        const double border_width = FromDIP(1);
        gc->SetBrush(wxBrush(fill));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->DrawRoundedRectangle(body.x, body.y, body.width, body.height, radius);
        if(border.Alpha() > 0 && border != fill){
            gc->SetBrush(*wxTRANSPARENT_BRUSH);
            gc->SetPen(wxPen(border,
                             std::max(1, static_cast<int>(std::lround(border_width)))));
            gc->DrawRoundedRectangle(body.x, body.y, body.width, body.height, radius);
        }
        delete gc;
        shapes_drawn = true;
    }
    if(!shapes_drawn){
        dc.SetPen(wxPen(border, FromDIP(1)));
        dc.SetBrush(wxBrush(fill));
        dc.DrawRoundedRectangle(body.x, body.y, body.width, body.height, radius);
    }

    //焦点环用纯色 GDI 画，保证在 MSW 的 GDI 下也清晰可见。
    if(focused_ && enabled){
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(UiTheme::Brand500, FromDIP(2)));
        const wxRect ring = body.Deflate(FromDIP(2));
        dc.DrawRoundedRectangle(ring.x, ring.y, ring.width, ring.height,
                                std::max(0, radius - FromDIP(1)));
    }

    //---- 内容：图标 + 文字，仍由 GDI 绘制 ----
    const int press_offset = static_cast<int>(std::lround(press_mix_ * FromDIP(1)));
    const wxSize label_size = dc.GetTextExtent(label_);
    const wxSize leading_size = dc.GetTextExtent(leading_text_);
    const wxSize accessory_size = dc.GetTextExtent(accessory_text_);

    int content_width = bitmap_.IsOk() ? bitmap_.GetWidth() : 0;
    if(content_width && !label_.empty()) content_width += FromDIP(6);
    content_width += label_size.x;

    int x = text_alignment_ == wxALIGN_LEFT
        ? body.x + FromDIP(10)
        : body.x + std::max(FromDIP(4), (body.width - content_width) / 2);
    int y = body.y + (body.height - label_size.y) / 2 + press_offset;

    if(bitmap_.IsOk()){
        const int bitmap_y = body.y + (body.height - bitmap_.GetHeight()) / 2 + press_offset;
        dc.DrawBitmap(bitmap_, x, bitmap_y, true);
        x += bitmap_.GetWidth() + (label_.empty() ? 0 : FromDIP(6));
    }

    dc.SetTextForeground(ink);
    if(!leading_text_.empty()){
        dc.DrawText(leading_text_, body.x + FromDIP(10),
                    body.y + (body.height - leading_size.y) / 2 + press_offset);
    }

    const int label_x = text_alignment_ == wxALIGN_LEFT && !bitmap_.IsOk()
        ? x + (leading_text_.empty() ? 0 : FromDIP(24))
        : x;
    dc.DrawText(label_, label_x, y);

    if(!accessory_text_.empty()){
        dc.SetTextForeground(UiTheme::TextTertiary);
        dc.DrawText(accessory_text_,
                    body.x + body.width - accessory_size.x - FromDIP(10),
                    body.y + (body.height - accessory_size.y) / 2 + press_offset);
    }
}

void ThemedButton::OnMouseEnter(wxMouseEvent& event){
    hovered_ = true;
    hover_target_ = 1.0;
    StartAnimation();
    RefreshForStateChange();
    if(hover_handler_) hover_handler_();
    event.Skip();
}

void ThemedButton::OnMouseLeave(wxMouseEvent& event){
    hovered_ = false;
    hover_target_ = 0.0;
    StartAnimation();
    RefreshForStateChange();
    event.Skip();
}

void ThemedButton::OnLeftDown(wxMouseEvent& event){
    if(!IsEnabled()) return;
    pressed_ = true;
    press_target_ = 1.0;
    if(!HasCapture()) CaptureMouse();
    SetFocus();
    StartAnimation();
    RefreshForStateChange();
}

void ThemedButton::OnLeftUp(wxMouseEvent& event){
    if(!pressed_) return;
    const bool activate = GetClientRect().Contains(event.GetPosition()) && IsEnabled();
    pressed_ = false;
    press_target_ = 0.0;
    if(!GetClientRect().Contains(event.GetPosition())) hover_target_ = 0.0;
    if(HasCapture()) ReleaseMouse();
    StartAnimation();
    RefreshForStateChange();
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
    press_target_ = 0.0;
    StartAnimation();
    Refresh(false);
}

void ThemedButton::OnFocus(wxFocusEvent& event){
    focused_ = event.GetEventType() == wxEVT_SET_FOCUS;
    Refresh();
    event.Skip();
}

#include "Theme.hpp"

#include <wx/brush.h>
#include <wx/graphics.h>
#include <wx/pen.h>

#include <algorithm>
#include <cmath>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif

namespace UiTheme{

namespace{

double clamp01(double value){
    return std::max(0.0, std::min(1.0, value));
}

// 用当前 graphics context 画一个圆角矩形：先填充，再描边。
// 半径限制在短边的一半以内，避免绘制 API 收到过大半径时产生尖角。
void draw_rounded_rect(wxGraphicsContext& gc, const wxRect& rect, int radius,
                       const wxColour& fill, const wxColour& border, double border_width){
    if(rect.width <= 0 || rect.height <= 0) return;
    const double r = std::min<double>(radius,
        std::min(rect.width, rect.height) / 2.0);
    if(fill.IsOk() && fill.Alpha() > 0){
        gc.SetBrush(wxBrush(fill));
        gc.SetPen(*wxTRANSPARENT_PEN);
        gc.DrawRoundedRectangle(rect.x, rect.y, rect.width, rect.height, r);
    }
    if(border.IsOk() && border.Alpha() > 0 && border_width > 0){
        gc.SetBrush(*wxTRANSPARENT_BRUSH);
        gc.SetPen(wxPen(border, border_width));
        gc.DrawRoundedRectangle(rect.x, rect.y, rect.width, rect.height, r);
    }
}

} // namespace

wxColour Mix(const wxColour& a, const wxColour& b, double t){
    t = clamp01(t);
    const auto mix_channel = [t](unsigned char x, unsigned char y){
        return static_cast<unsigned char>(std::lround(x + (y - x) * t));
    };
    return wxColour(mix_channel(a.Red(), b.Red()),
                    mix_channel(a.Green(), b.Green()),
                    mix_channel(a.Blue(), b.Blue()),
                    mix_channel(a.Alpha(), b.Alpha()));
}

wxColour WithAlpha(const wxColour& color, unsigned char alpha){
    return wxColour(color.Red(), color.Green(), color.Blue(), alpha);
}

bool AnimationsEnabled(){
#ifdef __WXMSW__
    // SPI_GETCLIENTAREAANIMATION 在系统“关闭动画”时为 FALSE。
    // 取不到时按开启处理，不影响功能。
    BOOL enabled = TRUE;
    if(::SystemParametersInfo(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)){
        return enabled != FALSE;
    }
#endif
    return true;
}

void DrawSoftShadow(wxGraphicsContext& gc, const wxRect& rect, int radius,
                    int offsetY, int layers, int maxAlpha){
    if(rect.width <= 0 || rect.height <= 0) return;
    if(layers <= 0 || maxAlpha <= 0) return;

    // 从外到内叠加：外层更大更淡，内层更小更实。
    // 每一层都是一次带 alpha 的圆角矩形，叠加后形成柔和的边缘。
    for(int layer = layers - 1; layer >= 0; --layer){
        const int grow = layer;
        const double weight = 1.0 - static_cast<double>(layer) / (layers + 1);
        const int alpha = std::max(1, static_cast<int>(maxAlpha * weight * 0.55));
        const wxColour shadow = WithAlpha(ShadowColor,
            static_cast<unsigned char>(std::min(255, alpha)));
        const wxRect r(rect.x - grow, rect.y - grow + offsetY,
                       rect.width + grow * 2, rect.height + grow * 2);
        const double rr = std::min<double>(radius + grow,
            std::min(r.width, r.height) / 2.0);
        gc.SetBrush(wxBrush(shadow));
        gc.SetPen(*wxTRANSPARENT_PEN);
        gc.DrawRoundedRectangle(r.x, r.y, r.width, r.height, rr);
    }
}

void DrawCard(wxGraphicsContext& gc, const wxRect& rect, const CardStyle& style){
    if(rect.width <= 0 || rect.height <= 0) return;
    gc.SetAntialiasMode(wxANTIALIAS_DEFAULT);
    if(style.shadow){
        DrawSoftShadow(gc, rect, style.radius, 1, 2, 18);
    }
    draw_rounded_rect(gc, rect, style.radius, style.fill, style.border, 1.0);
}

} // namespace UiTheme

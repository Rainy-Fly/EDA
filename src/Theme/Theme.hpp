#pragma once

#include <wx/colour.h>
#include <wx/gdicmn.h>

class wxGraphicsContext;

// TinyEDA 的共享浅色主题。仿真电平颜色由画布单独管理，避免覆盖逻辑状态编码。
namespace UiTheme{

inline const wxColour Brand950(0x09, 0x3F, 0x44);
inline const wxColour Brand900(0x0B, 0x56, 0x5C);
inline const wxColour Brand800(0x0F, 0x6F, 0x77);
inline const wxColour Brand700(0x15, 0xAC, 0xB7);
inline const wxColour Brand500(0x48, 0xDF, 0xEA);
inline const wxColour Brand200(0xA4, 0xEF, 0xF4);
inline const wxColour Brand100(0xD1, 0xF7, 0xFA);
inline const wxColour Brand50(0xF2, 0xFD, 0xFE);

inline const wxColour Page(0xF6, 0xFA, 0xFB);
inline const wxColour Surface(0xFF, 0xFF, 0xFF);
inline const wxColour Subtle(0xEE, 0xF6, 0xF7);
inline const wxColour Hover(0xE8, 0xF6, 0xF8);
inline const wxColour Pressed(0xBF, 0xE9, 0xEE);
inline const wxColour BorderSubtle(0xE3, 0xEE, 0xF0);
inline const wxColour Border(0xCF, 0xE0, 0xE3);
inline const wxColour BorderStrong(0x9F, 0xC7, 0xCC);
inline const wxColour Text(0x0B, 0x2B, 0x2F);
inline const wxColour TextSecondary(0x4F, 0x6B, 0x70);
inline const wxColour TextTertiary(0x7F, 0x96, 0x9B);
inline const wxColour TextDisabled(0xA9, 0xB8, 0xBC);

inline const wxColour ShadowColor(0x09, 0x3F, 0x44, 36);
inline const wxColour FocusRing(0x15, 0xAC, 0xB7, 150);

inline const wxColour Success(0x1F, 0x7A, 0x4D);
inline const wxColour SuccessBg(0xE7, 0xF7, 0xEF);
inline const wxColour Warning(0x9A, 0x5A, 0x00);
inline const wxColour WarningBg(0xFF, 0xF4, 0xE0);
inline const wxColour Danger(0xC9, 0x36, 0x36);
inline const wxColour DangerBg(0xFD, 0xEC, 0xEC);

// 统一尺寸与动效节奏（绘制前用 wxWindow::FromDIP 换算成物理像素）。
inline constexpr int RadiusButton = 8;
inline constexpr int RadiusCard = 12;
inline constexpr int SpaceXs = 4;
inline constexpr int SpaceSm = 8;
inline constexpr int SpaceMd = 12;
inline constexpr int SpaceLg = 16;
inline constexpr int HoverMs = 140;
inline constexpr int PressMs = 80;
inline constexpr int SelectMs = 160;

// 颜色插值：t=0 取 a，t=1 取 b。动效使用它把两套状态色平滑过渡。
wxColour Mix(const wxColour& a, const wxColour& b, double t);
// 只替换 alpha，保留 RGB（用于阴影/焦点环）。
wxColour WithAlpha(const wxColour& color, unsigned char alpha);
// Windows 下尊重“在 Windows 中显示动画”设置；其他平台默认开启。
bool AnimationsEnabled();

struct CardStyle{
    wxColour fill = Surface;
    wxColour border = BorderSubtle;
    int radius = RadiusCard;
    bool shadow = true;
};

// 卡片与阴影都只画形状，不画文字；文字交给 wxDC，避免 GDI+ 文本发虚。
void DrawSoftShadow(wxGraphicsContext& gc, const wxRect& rect, int radius,
                    int offsetY, int layers, int maxAlpha);
void DrawCard(wxGraphicsContext& gc, const wxRect& rect, const CardStyle& style);

} // namespace UiTheme
#include "ThemedSplitter.hpp"
#include "Theme.hpp"

#include <wx/dc.h>
#include <wx/graphics.h>

#include <algorithm>

ThemedSplitter::ThemedSplitter(wxWindow* parent, wxWindowID id,
                               const wxPoint& pos, const wxSize& size,
                               long style)
    : wxSplitterWindow(parent, id, pos, size, style){
    SetBackgroundColour(UiTheme::Page);
    //分栏实时 resize 时先合成整棵子树再显示：MSW 上是 WS_EX_COMPOSITED。
    //Canvas 自己虽有 wxAutoBufferedPaintDC，但连续改尺寸时子窗口仍可能露出擦背景帧；
    //窗口级合成能消掉整块画布的白闪/黑闪，且不影响 wxSP_LIVE_UPDATE 的跟手性。
    SetDoubleBuffered(true);
}

void ThemedSplitter::OnEnterSash(){
    wxSplitterWindow::OnEnterSash();
    hot_ = true;
    Refresh(false);
}

void ThemedSplitter::OnLeaveSash(){
    wxSplitterWindow::OnLeaveSash();
    hot_ = false;
    Refresh(false);
}

void ThemedSplitter::DrawSash(wxDC& dc){
    if(!IsSplit()){
        hot_ = false;
        return;
    }
    if(IsSashInvisible()) return;
    const int sash_position = GetSashPosition();
    const int sash_size = GetSashSize();
    if(sash_position <= 0 || sash_size <= 0) return;

    const wxSize client = GetClientSize();
    if(client.x <= 0 || client.y <= 0) return;

    const bool vertical = GetSplitMode() == wxSPLIT_VERTICAL;
    const wxRect sash_rect = vertical
        ? wxRect(sash_position, 0, sash_size, client.y)
        : wxRect(0, sash_position, client.x, sash_size);

    //先擦掉上一次的抓手，再画当前状态；否则鼠标移出后旧胶囊会残留。
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(UiTheme::Page));
    dc.DrawRectangle(sash_rect);

    const bool dragging = (m_dragMode == wxSPLIT_DRAG_DRAGGING);
    const bool active = hot_ || dragging;
    const wxColour dot_colour = dragging ? UiTheme::Brand900
                              : (active ? UiTheme::Brand700 : UiTheme::BorderStrong);

    const int dot_size = std::max(2, FromDIP(2));
    const int dot_gap = std::max(4, FromDIP(5));
    const int center_x = sash_rect.x + sash_rect.width / 2;
    const int center_y = sash_rect.y + sash_rect.height / 2;

    //空闲时只画三个点；悬停/拖动时加一层浅色胶囊。
    if(active){
        const int pill_long = std::max(24, FromDIP(32));
        //胶囊不能比 sash 还宽，否则会被两侧子窗口裁掉
        const int pill_short = std::max(4, std::min(sash_size, FromDIP(9)));
        const wxRect pill = vertical
            ? wxRect(center_x - pill_short / 2, center_y - pill_long / 2,
                     pill_short, pill_long)
            : wxRect(center_x - pill_long / 2, center_y - pill_short / 2,
                     pill_long, pill_short);
        const wxColour pill_fill = dragging ? UiTheme::Brand200 : UiTheme::Brand100;
        const wxColour pill_border = dragging ? UiTheme::Brand700 : UiTheme::Brand200;
        const double pill_radius = std::min(pill.width, pill.height) / 2.0;

        bool drawn = false;
        if(wxGraphicsContext* gc = wxGraphicsContext::CreateFromUnknownDC(dc)){
            gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
            gc->SetBrush(wxBrush(pill_fill));
            gc->SetPen(wxPen(pill_border, 1));
            gc->DrawRoundedRectangle(pill.x, pill.y, pill.width, pill.height,
                                     pill_radius);
            delete gc;
            drawn = true;
        }
        if(!drawn){
            dc.SetPen(wxPen(pill_border, 1));
            dc.SetBrush(wxBrush(pill_fill));
            dc.DrawRoundedRectangle(pill, pill_radius);
        }
    }

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(dot_colour));
    for(int i = -1; i <= 1; ++i){
        const int cx = vertical ? center_x : center_x + i * dot_gap;
        const int cy = vertical ? center_y + i * dot_gap : center_y;
        dc.DrawCircle(cx, cy, dot_size);
    }
}

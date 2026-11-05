#pragma once

#include <wx/splitter.h>

// 去掉原生 3D 边框的分隔条：背景与页面同色，空闲时只显示三个小圆点，
// 悬停/拖动时显示圆角“抓手”，拖动仍使用 wxSP_LIVE_UPDATE 实时缩放。
class ThemedSplitter : public wxSplitterWindow{
public:
    ThemedSplitter(wxWindow* parent, wxWindowID id = wxID_ANY,
                   const wxPoint& pos = wxDefaultPosition,
                   const wxSize& size = wxDefaultSize,
                   long style = wxSP_LIVE_UPDATE);

protected:
    void DrawSash(wxDC& dc) override;
    void OnEnterSash() override;
    void OnLeaveSash() override;

private:
    bool hot_ = false;
};

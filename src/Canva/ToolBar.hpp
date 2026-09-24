#pragma once
#include<wx/wx.h>
#include"./CanvasItem.hpp"

class Canvas;

// 工具条：放置在画布上，包含基础元件按钮：与非门、与门、或门、或非门、异或门、非门、导线
class ToolBar : public wxPanel{
public:
    ToolBar(wxWindow* parent, Canvas* canvas);
    //取消当前按钮高亮（由Canvas在放置结束后调用）
    void clear_selection();
private:
    void add_tool(wxSizer* sizer, const std::string& label, ItemType type);
    void on_tool_click(const std::string& name, ItemType type, wxButton* btn);
    Canvas* canvas;
    wxButton* selected;   //当前高亮的按钮
};

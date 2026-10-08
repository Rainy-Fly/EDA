#pragma once
#include<wx/wx.h>
#include"./CanvasItem.hpp"

class Canvas;

// 工具栏动作工具（非元件，作用于已放置的元件）
enum class ToolAction{
    SELECT,   //选择：点击元件后跟随鼠标移动；右键取消跟随；Del删除
    DELETE,   //删除：点击元件即删除
    CLONE,    //克隆：点击元件复制出一个跟随鼠标的虚影，再点击放置
};

// 工具条：放置在画布上，包含基础元件按钮（与非门…导线）与动作工具按钮（选择/删除/克隆）
class ToolBar : public wxPanel{
public:
    ToolBar(wxWindow* parent, Canvas* canvas);
    //取消当前按钮高亮（由Canvas在放置结束后调用）
    void clear_selection();
private:
    void add_tool(wxSizer* sizer, const std::string& label, ItemType type);
    void add_action_button(wxSizer* sizer, const std::string& label, ToolAction action);
    void on_tool_click(const std::string& name, ItemType type, wxButton* btn);
    void on_action_click(const std::string& label, ToolAction action, wxButton* btn);
    Canvas* canvas;
    wxButton* selected;   //当前高亮的按钮
};

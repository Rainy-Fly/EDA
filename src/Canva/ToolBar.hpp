#pragma once
#include<wx/wx.h>
#include"./CanvasItem.hpp"

class Canvas;

// 工具栏动作工具（非元件，作用于已放置的元件）
// 注意：Windows 的 winnt.h 把 DELETE 定义成访问权限宏 (0x00010000L)，与本枚举的 DELETE 冲突，
// MSVC 下会报语法错误。这里只在声明枚举的两行内临时取消该宏，随后立刻恢复，
// 不影响其它使用该宏的系统头（GCC/MinGW 下 DELETE 未定义，这段代码自动跳过）
#ifdef DELETE
    #define TINYEDA_HAD_DELETE_MACRO
    #pragma push_macro("DELETE")
    #undef DELETE
#endif

enum class ToolAction{
    SELECT,   //选择：点击元件后跟随鼠标移动；右键取消跟随；Del删除
    ERASE,   //删除：点击元件即删除
    CLONE,    //克隆：点击元件复制出一个跟随鼠标的虚影，再点击放置
};

#ifdef TINYEDA_HAD_DELETE_MACRO
    #pragma pop_macro("DELETE")
    #undef TINYEDA_HAD_DELETE_MACRO
#endif

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

#pragma once
#include<wx/wx.h>
#include<wx/tglbtn.h>   //wxToggleButton：用按下态表示"当前工具"
#include"./CanvasItem.hpp"
#include"../Theme/CardPanel.hpp"
#include"../Theme/ThemedButton.hpp"

class Canvas;

// 工具栏动作工具（非元件，作用于已放置的元件）
// 枚举值用 ERASE 而不是 DELETE：Windows 的 winnt.h 把 DELETE 定义成访问权限宏
// (0x00010000L)，用 DELETE 作枚举名在 MSVC 下会报语法错误
enum class ToolAction{
    SELECT,   //选择：点击元件后跟随鼠标移动；右键取消跟随；Del删除
    ACTIVATE, //激活：单击开关/输入端子切换 0/1（只有此模式会改元件状态）
    ERASE,    //删除：点击元件即删除
    CLONE,    //克隆：点击元件复制出一个跟随鼠标的虚影，再点击放置
};

// 工具条（元件调色板）：放在主窗口左侧栏、资源树上方。
// **只显示 24px 图标**（类似 Logisim 顶部的图标工具栏），名字放在悬停提示里（鼠标停住才出现），
// 这样 10 个按钮两三行就能放下，垂直空间留给下面的资源树。
// 继承 CardPanel：外侧统一圆角卡片 + “元件”标题；按钮使用扁平 ThemedButton。
class ToolBar : public CardPanel{
public:
    ToolBar(wxWindow* parent, Canvas* canvas);
    //取消当前按钮高亮（由Canvas在放置结束后调用）
    void clear_selection();
private:
    //建一个 36x36 的扁平图标按钮（只有图标+提示），加进sizer并返回
    ThemedButton* make_button(wxSizer* sizer, const wxString& tooltip,
                              const wxBitmapBundle& icon);
    void add_tool(wxSizer* sizer, const std::string& label, ItemType type);
    void add_action_button(wxSizer* sizer, const std::string& label, ToolAction action);
    void on_tool_click(const std::string& name, ItemType type, ThemedButton* btn);
    void on_action_click(const std::string& label, ToolAction action, ThemedButton* btn);
    Canvas* canvas;
    ThemedButton* selected;   //当前选中的按钮（nullptr=没有选中工具）
};

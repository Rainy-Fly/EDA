#pragma once
#include<wx/wx.h>

// 自定义菜单项ID（Simulate菜单没有标准ID，从wxID_HIGHEST之后分配）
enum{
    ID_RUN_PAUSE = wxID_HIGHEST + 1,   //运行 / 暂停
    ID_TICK,                           //单步时钟
};

// 菜单栏：File（文件）/ Edit（编辑）/ Simulate（模拟）三个菜单。
// 目前仅UI（可点击），不实现实际功能，后续由主窗口绑定事件处理。
class MenuBar : public wxMenuBar{
public:
    MenuBar();
    ~MenuBar() override = default;
};

#pragma once
#include<wx/wx.h>

// 自定义菜单项ID（没有标准ID的菜单项，从wxID_HIGHEST之后分配）
enum{
    ID_RUN_PAUSE = wxID_HIGHEST + 1,   //运行 / 暂停
    ID_TICK,                           //单步时钟
    ID_SET_DEFAULT_DIR,                //文件 - 设置默认目录（存在配置文件里）
    ID_SHOW_AGENT,                     //视图 - 显示/隐藏 AI 助手面板
};

// 菜单栏：File（文件）/ Edit（编辑）/ Simulate（模拟）/ View（视图）四个菜单。
// 目前仅UI（可点击），不实现实际功能，后续由主窗口绑定事件处理。
class MenuBar : public wxMenuBar{
public:
    MenuBar();
    ~MenuBar() override = default;
};

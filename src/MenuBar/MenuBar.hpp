#pragma once
#include<wx/menu.h>

// 自定义菜单项ID（没有标准ID的菜单项，从 wxID_HIGHEST 之后分配）。
enum{
    ID_RUN_PAUSE = wxID_HIGHEST + 1,   //运行 / 暂停
    ID_TICK,                           //单步时钟
    ID_SIM_DIAGNOSTICS,                //仿真诊断（列出每个元件/网络的电平与诊断）
    ID_SET_DEFAULT_DIR,                //文件 - 设置默认目录（存在配置文件里）
    ID_EXPORT_KICAD_NETLIST,           //文件 - 导出 KiCad 网表
    ID_EXPORT_VERILOG,                 //文件 - 导出 Verilog（由画布/项目生成）
    ID_SHOW_AGENT,                     //功能面板 - 显示/隐藏 AI 助手面板
    ID_SHOW_VERILOG_PANEL,             //功能面板 - 显示/隐藏代码面板
};

// 使用 wxWidgets 原生菜单栏，保留平台默认的悬停切换、键盘导航和子菜单行为。
class MenuBar : public wxMenuBar{
public:
    MenuBar();
    ~MenuBar() override = default;

    //把面板实际可见性回写到「功能面板」菜单的勾选状态。
    //MainFrame 在构造、F11/F12、面板关闭按钮和菜单展开前都会调用它。
    void SyncPanelStates(bool agent_visible, bool verilog_visible);
};

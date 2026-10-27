#pragma once
#include<wx/wx.h>
#include<wx/splitter.h>
#include<wx/timer.h>
#include<memory>

class Canvas;
class ExplorerPane;
class AttributeBar;
class FileActions;
class ToolBar;
class MenuBar;

namespace Simulation{ class SimulationSession; }
namespace Agent{ class AgentArea; class Show; }
namespace VerilogPanelUI{ class VerilogPanel; }

// 主窗口布局（类似 Logisim）：
//   左侧栏   = 上：元件调色板(ToolBar：与非门…导线 + 选择/删除/克隆)
//              下：资源树(ExplorerPane，按类别列出通用元件)
//   中间上   = 画布(Canvas)
//   中间下   = 属性栏(AttributeBar)
//   最右侧   = 可折叠的 AI 助手面板（AgentArea，视图菜单/F12 切换）
//   底部状态栏 = ① 状态/提示 ② 光标逻辑坐标 ③ 缩放比例与网格
class MainFrame : public wxFrame{
public:
    MainFrame();
    //必须在.cpp里定义：FileActions在此只是前向声明，unique_ptr的析构需要完整类型
    ~MainFrame() override;
private:
    void OnToggleAgent(wxCommandEvent& e);   //视图菜单：显示/隐藏 AI 助手面板
    void OnToggleVerilogPanel(wxCommandEvent& e);   //Verilog菜单：显示/隐藏代码面板
    void SetVerilogPanelVisible(bool visible);

    //---- 仿真（模拟菜单）----
    void OnRunPause(wxCommandEvent& e);      //模拟 - 运行/暂停
    void OnTick(wxCommandEvent& e);          //模拟 - 单步时钟
    void OnSimulationDiagnostics(wxCommandEvent& e);  //模拟 - 仿真诊断（列出元件/网络电平）
    void OnSimulationTimer(wxTimerEvent& e); //去抖：内容变化后延迟重建仿真
    void ScheduleSimulationRebuild();        //安排一次重建（80ms 去抖，批量加载只算一次）
    void RebuildSimulation();                //采集画布 → 适配 → 求值 → 刷新
    void UpdateSimulationStatus();           //状态栏第4段：运行状态/网络数/错误数

    MenuBar* menu_bar;                 //自绘菜单栏：顶部命令入口
    wxSplitterWindow* splitter;        //外层分栏：左侧栏 / 右侧工作区
    wxSplitterWindow* work_splitter;   //中层分栏：画布区 / AI助手区（右侧可拉伸）
    wxSplitterWindow* right_splitter;  //内层分栏：上Canvas / 下AttributeBar
    wxSplitterWindow* canvas_splitter; //画布 / Verilog代码面板（右侧可挤出）
    wxPanel* left_panel;               //左侧栏容器：上元件调色板 + 下资源树
    ToolBar* palette;                  //元件调色板（与或非门…导线 + 选择/删除/克隆）
    ExplorerPane* explorer;
    Canvas* canvas;
    VerilogPanelUI::VerilogPanel* verilog_panel;   //画布右侧的Verilog代码面板
    AttributeBar* attribute_bar;
    Agent::AgentArea* agent_area;      //AI助手（对话+设置）
    bool verilog_visible = false;      //代码面板是否已挤出
    //仿真会话（画布只借指针着色；这里拥有它，必须在canvas之前创建、之后销毁）
    std::unique_ptr<Simulation::SimulationSession> simulation;
    wxTimer simulation_timer;          //内容变化后的去抖定时器
    //文件菜单功能（新建/打开/保存/另存为/退出）；菜单结构由MenuBar提供。
    //声明在最后 => 最先析构，保证它比canvas/attribute_bar先消失，不会留下悬空回调
    std::unique_ptr<FileActions> file_actions;
    //Agent面板显隐开关；放在 file_actions 之后 => 比 file_actions 更早析构
    std::unique_ptr<Agent::Show> show;
};

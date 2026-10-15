#pragma once
#include<wx/wx.h>
#include<wx/splitter.h>
#include<memory>

class Canvas;
class ExplorerPane;
class AttributeBar;
class FileActions;
class ToolBar;

namespace Agent{ class AgentArea; class Show; }

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

    wxSplitterWindow* splitter;        //外层分栏：左侧栏 / 右侧工作区
    wxSplitterWindow* work_splitter;   //中层分栏：画布区 / AI助手区（右侧可拉伸）
    wxSplitterWindow* right_splitter;  //内层分栏：上Canvas / 下AttributeBar
    wxPanel* left_panel;               //左侧栏容器：上元件调色板 + 下资源树
    ToolBar* palette;                  //元件调色板（与或非门…导线 + 选择/删除/克隆）
    ExplorerPane* explorer;
    Canvas* canvas;
    AttributeBar* attribute_bar;
    Agent::AgentArea* agent_area;      //AI助手（对话+设置）
    //文件菜单功能（新建/打开/保存/另存为/退出）；菜单结构由MenuBar提供。
    //声明在最后 => 最先析构，保证它比canvas/attribute_bar先消失，不会留下悬空回调
    std::unique_ptr<FileActions> file_actions;
    //Agent面板显隐开关；放在 file_actions 之后 => 比 file_actions 更早析构
    std::unique_ptr<Agent::Show> show;
};

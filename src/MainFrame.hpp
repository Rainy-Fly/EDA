#pragma once
#include<wx/wx.h>
#include<wx/splitter.h>
#include<memory>

class Canvas;
class ExplorerPane;
class AttributeBar;
class FileActions;

namespace Agent{ class AgentArea; class Show; }

// 演示用主窗口：左资源树(ExplorerPane) + 右上画布(Canvas) + 右下属性栏(AttributeBar)，
// 最右侧可折叠的 AI 助手面板（AgentArea，菜单“视图->AI助手面板”切换）。
class MainFrame : public wxFrame{
public:
    MainFrame();
    //必须在.cpp里定义：FileActions在此只是前向声明，unique_ptr的析构需要完整类型
    ~MainFrame() override;
private:
    void OnToggleAgent(wxCommandEvent& e);   //视图菜单：显示/隐藏 AI 助手面板

    wxSplitterWindow* splitter;        //外层分栏：左Explorer / 右面板
    wxSplitterWindow* work_splitter;   //中层分栏：画布区 / AI助手区（右侧可拉伸）
    wxSplitterWindow* right_splitter;  //内层分栏：上Canvas / 下AttributeBar
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

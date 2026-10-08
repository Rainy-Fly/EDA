#pragma once
#include<wx/wx.h>
#include<wx/splitter.h>
#include<memory>

class Canvas;
class ExplorerPane;
class AttributeBar;
class FileActions;

// 演示用主窗口：左资源树(ExplorerPane) + 右上画布(Canvas) + 右下属性栏(AttributeBar)，
// 用于验证各模块功能可以正常运行（Canvas/Explorer的代码不被修改）。
class MainFrame : public wxFrame{
public:
    MainFrame();
    //必须在.cpp里定义：FileActions在此只是前向声明，unique_ptr的析构需要完整类型
    ~MainFrame() override;
private:
    wxSplitterWindow* splitter;        //外层分栏：左Explorer / 右面板
    wxSplitterWindow* right_splitter;  //内层分栏：上Canvas / 下AttributeBar
    ExplorerPane* explorer;
    Canvas* canvas;
    AttributeBar* attribute_bar;
    //文件菜单功能（新建/打开/保存/另存为/退出）；菜单结构由MenuBar提供。
    //声明在最后 => 最先析构，保证它比canvas/attribute_bar先消失，不会留下悬空回调
    std::unique_ptr<FileActions> file_actions;
};

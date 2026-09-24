#pragma once
#include<wx/wx.h>
#include<wx/splitter.h>

class Canvas;
class ExplorerPane;
class AttributeBar;

// 演示用主窗口：左资源树(ExplorerPane) + 右上画布(Canvas) + 右下属性栏(AttributeBar)，
// 用于验证各模块功能可以正常运行（Canvas/Explorer的代码不被修改）。
class demoMainFrame : public wxFrame{
public:
    demoMainFrame();
    ~demoMainFrame() override = default;
private:
    wxSplitterWindow* splitter;        //外层分栏：左Explorer / 右面板
    wxSplitterWindow* right_splitter;  //内层分栏：上Canvas / 下AttributeBar
    ExplorerPane* explorer;
    Canvas* canvas;
    AttributeBar* attribute_bar;
};

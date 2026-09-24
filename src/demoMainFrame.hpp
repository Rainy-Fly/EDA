#pragma once
#include<wx/wx.h>
#include<wx/splitter.h>

class Canvas;
class ExplorerPane;

// 演示用主窗口：左侧资源树(ExplorerPane) + 右侧画布(Canvas)，
// 用于验证各模块功能可以正常运行（Canvas/Explorer的代码不被修改）
class demoMainFrame : public wxFrame{
public:
    demoMainFrame();
    ~demoMainFrame() override = default;
private:
    wxSplitterWindow* splitter;
    ExplorerPane* explorer;
    Canvas* canvas;
};

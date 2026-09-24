#pragma once
#include<wx/wx.h>

class Canvas;

// 演示用主窗口（其他同学的任务，这里仅用于验证Canvas可以正常运行）
class demoMainFrame : public wxFrame{
public:
    demoMainFrame();
    ~demoMainFrame() override = default;
private:
    Canvas* canvas;
};

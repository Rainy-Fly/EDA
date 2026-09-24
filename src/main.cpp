// 仅供你本地测试使用的 main.cpp
#include "wx/wx.h"
#include "ExplorerPane.h" // 引入你自己写的文件

// 主窗口类（模拟你队友最终负责的框架）
class MyFrame : public wxFrame
{
public:
    MyFrame(const wxString& title) 
        : wxFrame(NULL, wxID_ANY, title, wxDefaultPosition, wxSize(800, 600))
    {
        // 创建一个横向的布局管理器 (水平切分窗口)
        wxBoxSizer* frameSizer = new wxBoxSizer(wxHORIZONTAL);

        // ==========================================
        // 这就是你负责的 ExplorerPane，实例化并加入布局
        // ==========================================
        ExplorerPane* explorer = new ExplorerPane(this);
        
        // 比例设为 0 (宽度固定)，允许垂直拉伸 (wxEXPAND)，四周留 2 像素边距
        frameSizer->Add(explorer, 0, wxEXPAND | wxALL, 2);

        // ==========================================
        // 模拟右侧的 Canvas (画布)，代表你队友的工作
        // ==========================================
        wxPanel* dummyCanvas = new wxPanel(this, wxID_ANY);
        dummyCanvas->SetBackgroundColour(wxColour(240, 240, 240)); // 浅灰色背景加以区分
        
        // 比例设为 1，意味着它会霸占右侧所有剩余的空间
        frameSizer->Add(dummyCanvas, 1, wxEXPAND | wxALL, 2);

        // 应用布局
        SetSizer(frameSizer);
        Layout();
    }
};

// 应用程序类
class TestApp : public wxApp 
{
public:
    virtual bool OnInit() {
        // 实例化带有横向布局的主窗口
        MyFrame* frame = new MyFrame(wxT("EDA 界面集成测试"));
        frame->Show(true);
        return true;
    }
};

DECLARE_APP(TestApp)
IMPLEMENT_APP(TestApp)
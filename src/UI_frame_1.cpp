// ============================================================
// 第 0 步：包含头文件
// ============================================================
#include <wx/wx.h>
#include "mondrian.xpm"   // 注意用双引号  
#include <wx/splitter.h>
#include "Canva/Canvas.hpp"
#include "Explorer\ExplorerPane.h"

// ============================================================
// 第 1 步：定义三个面板类（必须在 MyFrame 之前，因为 MyFrame 要 new 它们）
// 它们都继承自 wxPanel，每个类必须有构造函数把 parent 传给 wxPanel
// ============================================================

// ---------- 1.1 左侧上方：元件库面板 ----------


// ---------- 1.2 左侧下方：属性表面板 ----------
class AttributeTable : public wxPanel
{
public:
    AttributeTable(wxWindow* parent)
        : wxPanel(parent, wxID_ANY)
    {
        // 以后往里加 wxPropertyGrid
    }
};




// ============================================================
// 第 2 步：定义主窗口类 MyFrame
// 继承自 wxFrame，负责创建并容纳三个面板
// ============================================================
class MyFrame : public wxFrame
{
public:
    MyFrame(const wxString& title);
    void OnQuit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);

private:
    ExplorerPane*   m_explorerPane;
    AttributeTable* m_attributeTable;
    Canvas*         m_canvas;

    DECLARE_EVENT_TABLE()
};


// ============================================================
// 第 3 步：定义应用程序类 MyApp
// ============================================================
class MyApp : public wxApp
{
public:
    virtual bool OnInit();
};


// ============================================================
// 第 4 步：实现 MyApp::OnInit
// ============================================================
bool MyApp::OnInit()
{
    MyFrame* frame = new MyFrame(wxT("EDA Editor"));
    frame->Show(true);
    return true;
}


// ============================================================
// 第 5 步：实现事件表
// ============================================================
BEGIN_EVENT_TABLE(MyFrame, wxFrame)
    EVT_MENU(wxID_ABOUT, MyFrame::OnAbout)
    EVT_MENU(wxID_EXIT,  MyFrame::OnQuit)
END_EVENT_TABLE()


// ============================================================
// 第 6 步：实现事件处理函数
// ============================================================
void MyFrame::OnAbout(wxCommandEvent& event)
{
    wxString msg;
    msg.Printf(wxT("Hello and welcome to DWLW !!!"));
    wxMessageBox(msg,
                 wxT("About Minimal"),
                 wxOK | wxICON_INFORMATION,
                 this);
}

void MyFrame::OnQuit(wxCommandEvent& event)
{
    Close();
}


// ============================================================
// 第 7 步：实现 MyFrame 构造函数（UI 组装核心）
// ============================================================
MyFrame::MyFrame(const wxString& title)
    : wxFrame(NULL, wxID_ANY, title, wxDefaultPosition, wxSize(1000, 700))
{
    // 7.1 设置窗口图标
    SetIcon(wxIcon(mondrian_xpm));

    // 7.2 创建菜单栏
    wxMenu* fileMenu = new wxMenu;
    wxMenu* helpMenu = new wxMenu;

    fileMenu->Append(wxID_EXIT, wxT("E&xit\tAlt-X"), wxT("Quit this program"));
    helpMenu->Append(wxID_ABOUT, wxT("&About...\tF1"), wxT("Show about dialog"));

    wxMenuBar* menuBar = new wxMenuBar();
    menuBar->Append(fileMenu, wxT("&File"));
    menuBar->Append(helpMenu, wxT("&Help"));
    SetMenuBar(menuBar);

    // 7.3 创建工具栏
    // 注意 wxWidgets 3.2 的参数顺序：ID, 位图, 文本
  wxToolBar* toolBar = CreateToolBar();
   toolBar->AddTool(wxID_NEW,  wxT("New"),  wxNullBitmap, wxT("New file"));
toolBar->AddTool(wxID_OPEN, wxT("Open"), wxNullBitmap, wxT("Open file"));
    toolBar->Realize();

    // 7.4 创建状态栏
    CreateStatusBar(2);
    SetStatusText(wxT("Welcome to wxWidgets!"), 0);

    // 7.5 创建三个面板
// 先创建两个 Splitter
// 步骤 1：创建外层 Splitter（左右分）
wxSplitterWindow* mainSplitter = new wxSplitterWindow(
    this, wxID_ANY);   // ← 加这里

wxSplitterWindow* leftSplitter = new wxSplitterWindow(
    mainSplitter, wxID_ANY);   // ← 和这里

    // 步骤 3：创建面板，parent 指向对应的 Splitter
    m_explorerPane   = new ExplorerPane(leftSplitter);
    m_attributeTable = new AttributeTable(leftSplitter);
    m_canvas         = new Canvas(mainSplitter);

    // 步骤 4：先把 mainSplitter 放进 Sizer，让它填满窗口
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
    topSizer->Add(mainSplitter, 1, wxEXPAND);
    SetSizer(topSizer);

    // 步骤 5：强制窗口立即布局，此时 mainSplitter 才有了真实尺寸
    Layout();

    // 步骤 6：现在 Splitter 有尺寸了，再执行分割
    leftSplitter->SplitHorizontally(m_explorerPane, m_attributeTable);
    mainSplitter->SplitVertically(leftSplitter, m_canvas);

    // 步骤 7：设置最小窗格大小
    leftSplitter->SetMinimumPaneSize(80);
    mainSplitter->SetMinimumPaneSize(150);

    // 步骤 8：设置初始分割位置（必须在 Split 之后）
    leftSplitter->SetSashPosition(450);
    mainSplitter->SetSashPosition(280);

}


// ============================================================
// 第 8 步：注册应用程序类
// ============================================================
wxIMPLEMENT_APP(MyApp);
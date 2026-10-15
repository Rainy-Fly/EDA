#include"./MainFrame.hpp"
#include"./Canva/Canvas.hpp"
#include"./Canva/ToolBar.hpp"
#include<wx/fontenum.h>
#include"./Explorer/ExplorerPane.h"
#include"./AttributeBar/AttributeBar.hpp"
#include"./MenuBar/MenuBar.hpp"
#include"./MenuBar/FileActions.hpp"
#include"./Agent/UI/AgentArea.hpp"
#include"./Agent/UI/Show.hpp"

//先摘掉属性栏的回调：它指向下面的FileActions，必须在成员析构前断开
MainFrame::~MainFrame(){
    if(attribute_bar) attribute_bar->set_edited_callback(nullptr);
}

namespace{

//状态栏是原生控件：GDI 对「中文 + 半角数字/字母」混排会按字符集分段做"字体链接"，
//中文段会被渲染得发虚发花（实测：纯中文清晰、纯 ASCII 清晰、一混排就花）。
//把整数转成全角数字就都在中文字符集里了，不再触发字体链接。
wxString full_width_int(int value){
    const wxString ascii = wxString::Format(wxT("%d"), value);
    static const wxChar* kFullDigits = wxT("０１２３４５６７８９");
    wxString out;
    for(size_t i = 0; i < ascii.length(); ++i){
        const wxChar ch = ascii[i];
        if(ch >= wxT('0') && ch <= wxT('9')) out += kFullDigits[ch - wxT('0')];
        else                                 out += ch;
    }
    return out;
}

} // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, wxString::FromUTF8("TinyEDA "),
              wxDefaultPosition, wxSize(1280, 800)),
      splitter(nullptr),
      work_splitter(nullptr),
      right_splitter(nullptr),
      left_panel(nullptr),
      palette(nullptr),
      explorer(nullptr),
      canvas(nullptr),
      attribute_bar(nullptr),
      agent_area(nullptr){
    //中文字体：wx 默认界面字体是 "MS Shell Dlg 2"（Tahoma），它没有中文字形，
    //GDI 只能用字体链接回退到宋体 —— 宋体小字号是位图字形，在状态栏那种窄字段里会发虚，
    //放大看就像重影/乱码。这里显式换成中文界面字体（微软雅黑，TrueType），中文才清晰。
    //先设再创建子控件，子控件才会继承；找不到字体时保持系统默认，不会出错
    wxFont ui_font;
    {
        wxString face = wxT("Microsoft YaHei UI");
        if(!wxFontEnumerator::IsValidFacename(face)) face = wxT("Microsoft YaHei");
        if(!wxFontEnumerator::IsValidFacename(face)) face = wxT("SimHei");
        if(wxFontEnumerator::IsValidFacename(face)){
            ui_font = GetFont();
            ui_font.SetFaceName(face);
            //关键：把字符集设成中文（GB2312）。否则 GDI 会按字符集"字体链接"分段换字体
            //（中文交给宋体、数字/字母留给 Tahoma），中英混排的字符串就会渲染得发虚发花
            ui_font.SetEncoding(wxFONTENCODING_GB2312);
            SetFont(ui_font);
        }
    }
    //分栏结构（类似 Logisim）：
    //  splitter(外层竖分) = left_panel(调色板+资源树) | work_splitter
    //  work_splitter(中层横分) = right_splitter | agent_area（AI面板，默认隐藏）
    //  right_splitter(内层竖分) = canvas | attribute_bar
    splitter = new wxSplitterWindow(this, wxID_ANY);
    work_splitter = new wxSplitterWindow(splitter, wxID_ANY);
    right_splitter = new wxSplitterWindow(work_splitter, wxID_ANY);
    left_panel    = new wxPanel(splitter);
    canvas        = new Canvas(right_splitter);
    attribute_bar = new AttributeBar(right_splitter);
    //左侧栏：元件调色板在上、资源树在下（调色板需要canvas指针，所以在canvas之后创建）
    palette       = new ToolBar(left_panel, canvas);
    explorer      = new ExplorerPane(left_panel);
    {
        wxBoxSizer* left_sizer = new wxBoxSizer(wxVERTICAL);
        left_sizer->Add(palette,  0, wxEXPAND);   //自然高度：一列按钮
        left_sizer->Add(explorer, 1, wxEXPAND);   //占满剩余高度
        left_panel->SetSizer(left_sizer);
    }
    canvas->set_toolbar(palette);   //画布只借它的指针清按钮高亮（工具栏不再压在画布上）
    agent_area    = new Agent::AgentArea(work_splitter, canvas);

    splitter->SetMinimumPaneSize(150);
    splitter->SplitVertically(left_panel, work_splitter, 250);

    right_splitter->SetMinimumPaneSize(120);
    right_splitter->SplitHorizontally(canvas, attribute_bar, 560);  //画布在上，属性栏在下

    work_splitter->SetMinimumPaneSize(120);
    //AI 面板：建立右分栏布局并默认隐藏（视图菜单可切换）
    show = std::make_unique<Agent::Show>(work_splitter, right_splitter, agent_area);

    //联动：画布选中/放置/清空元件时，属性栏显示对应元件的元数据
    canvas->set_current_item_callback(
        [this](CanvasItem* item){ attribute_bar->set_item(item); });

    //联动：资源树点击元件 -> 画布放置（与工具栏门类一致：虚影跟随、点击放置）
    explorer->SetComponentSelectedCallback(
        [this](const std::string& type){ canvas->select_tool_by_name(type); });

    //菜单栏：File/Edit/Simulate/View（文件菜单的打开/新建/保存由FileActions实现）
    SetMenuBar(new MenuBar());
    file_actions = std::make_unique<FileActions>(this, canvas);

    //视图菜单：切换 AI 助手面板显隐（勾选状态与面板实际状态同步）
    Bind(wxEVT_MENU, &MainFrame::OnToggleAgent, this, ID_SHOW_AGENT);

    //联动：属性栏里改过的属性也算改项目 -> 标记“未保存”
    attribute_bar->set_edited_callback([this]{ file_actions->mark_modified(); });

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(splitter, 1, wxEXPAND);
    SetSizer(sizer);

    //状态栏三个字段：① 状态/提示（FileActions 也往这里写）② 光标坐标 ③ 缩放/网格。
    //坐标是「画布逻辑坐标且已吸附网格」，与放置元件、画导线、写进 XML 用的是同一个值
    CreateStatusBar(3);
    //第三个字段给足宽度（全角文字约150px）：让它远离右端的尺寸手柄
    const int status_widths[3] = {-1, 150, 240};
    SetStatusWidths(3, status_widths);
    //状态栏是原生控件，不会继承框架字体，必须显式再设一次（中文发虚就是这么来的）
    if(GetStatusBar() && ui_font.IsOk()) GetStatusBar()->SetFont(ui_font);

    canvas->set_status_callback([this](const wxString& text){ SetStatusText(text, 0); });
    canvas->set_cursor_callback([this](const wxPoint& coords, bool valid){
        const wxString text = valid ? wxString::Format("X: %d   Y: %d", coords.x, coords.y)
                                    : wxString();
        //只在文本真的变了才写状态栏，避免鼠标一动就刷
        if(GetStatusBar() && GetStatusBar()->GetStatusText(1) != text) SetStatusText(text, 1);
    });
    canvas->set_view_callback([this](float sc, int grid){
        //数字用全角（整串都在中文字符集里，避免状态栏的字体链接把中文渲染坏）
        const wxString text =
            wxString::FromUTF8("缩放 ") + full_width_int((int)(sc * 100.0f + 0.5f)) +
            wxString::FromUTF8("％　网格 ") + full_width_int(grid);
        if(GetStatusBar() && GetStatusBar()->GetStatusText(2) != text) SetStatusText(text, 2);
    });

    Center();
}

//视图菜单：切换 AI 助手面板，并把菜单勾选同步为面板实际状态
void MainFrame::OnToggleAgent(wxCommandEvent&){
    if(show) show->Toggle();
    if(wxMenuBar* bar = GetMenuBar()){
        bar->Check(ID_SHOW_AGENT, show && show->Visible());
    }
}

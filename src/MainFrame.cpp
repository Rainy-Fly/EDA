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
#include"./VerilogPanel/VerilogPanel.hpp"
#include"./DealProjectXML/DealProjectXML.hpp"
#include"./Canva/SimulationBridge.hpp"
#include"./Simulation/SimulationProject.hpp"
#include"./Simulation/SimulationSession.hpp"
#include"./Theme/Theme.hpp"
#include"./Theme/ThemedSplitter.hpp"

//先摘掉属性栏的回调：它指向下面的FileActions，必须在成员析构前断开
MainFrame::~MainFrame(){
    //先停仿真定时器、断开画布上的回调：它们都指向本对象，成员析构后就会悬空
    simulation_timer.Stop();
    if(canvas){
        canvas->set_simulation(nullptr);
        canvas->set_simulation_dirty_callback(nullptr);
    }
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
              menu_bar(nullptr),
      splitter(nullptr),
      work_splitter(nullptr),
      right_splitter(nullptr),
      canvas_splitter(nullptr),
      left_panel(nullptr),
      palette(nullptr),
      explorer(nullptr),
      canvas(nullptr),
      verilog_panel(nullptr),
      attribute_bar(nullptr),
      agent_area(nullptr){
        SetBackgroundColour(UiTheme::Page);
    //（代码面板默认隐藏，见下方 canvas_splitter 初始化）
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
    //  right_splitter(内层竖分) = canvas_splitter | attribute_bar
    //  canvas_splitter(内层竖分) = canvas | verilog_panel（Verilog代码面板，默认隐藏）
    //统一带 wxSP_LIVE_UPDATE：不加这个样式时，拖动分栏画的是一条 2px 纯黑的"跟踪线"
    //（wxSplitterWindow 默认的 m_sashTrackerPen 就是 wxBLACK）；加上后窗格实时缩放、没有黑线。
    //不再用 wxSP_3D：原生 3D 边框太粗、颜色也不受主题控制；ThemedSplitter 自己画抓手。
    const long split_style = wxSP_LIVE_UPDATE;
    splitter = new ThemedSplitter(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, split_style);
    work_splitter = new ThemedSplitter(splitter, wxID_ANY, wxDefaultPosition, wxDefaultSize, split_style);
    right_splitter = new ThemedSplitter(work_splitter, wxID_ANY, wxDefaultPosition, wxDefaultSize, split_style);
    canvas_splitter = new ThemedSplitter(right_splitter, wxID_ANY, wxDefaultPosition, wxDefaultSize, split_style);
    left_panel    = new wxPanel(splitter);
    canvas        = new Canvas(canvas_splitter);
    verilog_panel = new VerilogPanelUI::VerilogPanel(canvas_splitter, canvas);
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
    left_panel->SetBackgroundColour(UiTheme::Page);
    //分隔条区域用页面底色；卡片自己的外边距再形成留白，避免整块灰边
    splitter->SetBackgroundColour(UiTheme::Page);
    work_splitter->SetBackgroundColour(UiTheme::Page);
    right_splitter->SetBackgroundColour(UiTheme::Page);
    canvas_splitter->SetBackgroundColour(UiTheme::Page);

    splitter->SetMinimumPaneSize(150);
    splitter->SplitVertically(left_panel, work_splitter, 250);

    right_splitter->SetMinimumPaneSize(120);
    right_splitter->SplitHorizontally(canvas_splitter, attribute_bar, 560);  //画布区在上，属性栏在下

    //Verilog 代码面板：先建立左画布/右面板的分栏，再摘除面板实现默认隐藏
    //（Unsplit 不销毁窗口，显示时用 SplitVertically 重新接回，可拉伸宽度）
    canvas_splitter->SetMinimumPaneSize(80);
    canvas_splitter->SplitVertically(canvas, verilog_panel, -280);
    canvas_splitter->Unsplit(verilog_panel);
    verilog_panel->Hide();
    verilog_panel->set_applied_callback([this]{ file_actions->mark_modified(); });

    work_splitter->SetMinimumPaneSize(120);
    //AI 面板：建立右分栏布局并默认隐藏（功能面板菜单可切换）
    show = std::make_unique<Agent::Show>(work_splitter, right_splitter, agent_area);

    //面板右上角“×”也走同一套显隐逻辑，菜单勾选由 SyncPanelMenu() 统一回写
    agent_area->SetCloseHandler([this]{ SetAgentVisible(false); });
    verilog_panel->SetCloseHandler([this]{ SetVerilogPanelVisible(false); });

    //联动：画布选中/放置/清空元件时，属性栏显示对应元件的元数据
    canvas->set_current_item_callback(
        [this](CanvasItem* item){ attribute_bar->set_item(item); });

    //联动：资源树点击元件 -> 画布放置（与工具栏门类一致：虚影跟随、点击放置）
    explorer->SetComponentSelectedCallback(
        [this](const std::string& type){ canvas->select_tool_by_name(type); });

    //菜单栏恢复为 wxWidgets 原生菜单，快捷键由菜单标签/系统菜单机制处理。
    menu_bar = new MenuBar();
    SetMenuBar(menu_bar);
    file_actions = std::make_unique<FileActions>(this, canvas);

    //功能面板菜单：切换 AI 助手面板显隐（勾选状态与面板实际状态同步）
    Bind(wxEVT_MENU, &MainFrame::OnToggleAgent, this, ID_SHOW_AGENT);
    //功能面板菜单：切换代码面板显隐（勾选状态与面板实际状态同步）
    Bind(wxEVT_MENU, &MainFrame::OnToggleVerilogPanel, this, ID_SHOW_VERILOG_PANEL);
    //菜单展开前再同步一次：面板可能被关闭按钮或快捷键改过
    Bind(wxEVT_MENU_OPEN, &MainFrame::OnMenuOpen, this);

    //联动：属性栏里改过的属性也算改项目 -> 标记“未保存”
    attribute_bar->set_edited_callback([this]{ file_actions->mark_modified(); });

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(splitter, 1, wxEXPAND);
    SetSizer(sizer);

    //状态栏四个字段：① 状态/提示（FileActions 也往这里写）② 光标坐标 ③ 缩放/网格 ④ 仿真摘要。
    //坐标是「画布逻辑坐标且已吸附网格」，与放置元件、画导线、写进 XML 用的是同一个值
    CreateStatusBar(4);
    if(GetStatusBar()){
        GetStatusBar()->SetBackgroundColour(UiTheme::Subtle);
        GetStatusBar()->SetForegroundColour(UiTheme::TextSecondary);
    }
    //第三个字段给足宽度（全角文字约150px）：让它远离右端的尺寸手柄
    const int status_widths[4] = {-1, 150, 240, 175};
    SetStatusWidths(4, status_widths);
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

    //---- 仿真（模拟菜单）：会话 + 内容变化重建 + 状态栏第4段 ----
    simulation = std::make_unique<Simulation::SimulationSession>();
    canvas->set_simulation(simulation.get());
    //画布内容变化（放置/删除/拖动/连线/加载）后去抖重建，批量加载只算一次
    canvas->set_simulation_dirty_callback([this]{ ScheduleSimulationRebuild(); });
    //模拟菜单：运行/暂停、单步时钟
    Bind(wxEVT_MENU, &MainFrame::OnRunPause, this, ID_RUN_PAUSE);
    Bind(wxEVT_MENU, &MainFrame::OnTick, this, ID_TICK);
    Bind(wxEVT_MENU, &MainFrame::OnSimulationDiagnostics, this, ID_SIM_DIAGNOSTICS);
    simulation_timer.SetOwner(this, 1);
    Bind(wxEVT_TIMER, &MainFrame::OnSimulationTimer, this, 1);
    RebuildSimulation();   //启动时先算一次（空画布会显示"无可仿真元件"）
    SyncPanelMenu();       //初始化菜单勾选（AI/Verilog 默认都隐藏）

    //属性栏内部改成滚动容器后，首次布局时其 best size 可能让分隔器重新分配高度；
    //等首轮 Layout 之后再钉一次默认位置，保证画布/属性栏的初始比例稳定。
    CallAfter([this]{
        if(right_splitter) right_splitter->SetSashPosition(560);
    });

    Center();
}

//---- 仿真（模拟菜单）----

//内容变化后延迟 80ms 重建：加载/批量操作时多次通知只合并成一次
void MainFrame::ScheduleSimulationRebuild(){
    simulation_timer.StartOnce(80);
}

void MainFrame::OnSimulationTimer(wxTimerEvent&){
    RebuildSimulation();
}

//采集画布 → 适配成仿真电路 → 求值 → 刷新画布与状态栏。
//用的采集入口与"保存项目/导出 KiCad 网表"完全一致（CollectFromCanvas），
//所以仿真看到的连接关系和盘上文件是一致的
void MainFrame::RebuildSimulation(){
    if(!canvas || !simulation) return;
    DealProjectXML::ProjectInfo info;
    const DealProjectXML::ProjectData project = DealProjectXML::CollectFromCanvas(canvas, info);
    const Simulation::SimProject sim_project = SimulationBridge::BuildSimulationProject(project);
    simulation->rebuild(sim_project);
    canvas->refresh_simulation_display();
    UpdateSimulationStatus();
}

void MainFrame::UpdateSimulationStatus(){
    if(!simulation) return;
    wxString text;
    if(!simulation->hasModel()){
        text = wxString::FromUTF8("仿真：无可仿真元件");
    }else if(!simulation->enabled()){
        text = wxString::FromUTF8("仿真：已暂停");
    }else{
        text = wxString::FromUTF8("仿真：运行　网络 ") + full_width_int(simulation->netCount()) +
               wxString::FromUTF8("　错误 ") + full_width_int(simulation->errorCount());
    }
    if(GetStatusBar() && GetStatusBar()->GetStatusText(3) != text) SetStatusText(text, 3);
    //有错误时在状态字段提示去「仿真诊断」看细节（会被下一次画布提示覆盖，属一次性提醒）
    if(GetStatusBar() && simulation->hasModel() && simulation->errorCount() > 0){
        const wxString hint = wxString::FromUTF8("仿真：有 ") +
                              full_width_int(simulation->errorCount()) +
                              wxString::FromUTF8(" 个错误，看 模拟 → 仿真诊断");
        if(GetStatusBar()->GetStatusText(0) != hint) SetStatusText(hint, 0);
    }
}

//把仿真会话的诊断报告显示成一个只读多行窗口
void MainFrame::OnSimulationDiagnostics(wxCommandEvent&){
    if(!simulation) return;
    wxDialog dialog(this, wxID_ANY, wxString::FromUTF8("仿真诊断"),
                    wxDefaultPosition, wxSize(720, 560),
                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    wxTextCtrl* text = new wxTextCtrl(&dialog, wxID_ANY,
        wxString::FromUTF8(simulation->report_text().c_str()),
        wxDefaultPosition, wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    wxFont mono = text->GetFont();
    mono.SetFaceName(wxT("Consolas"));   //找不到时 wx 自动回退，不影响显示
    text->SetFont(mono);
    sizer->Add(text, 1, wxEXPAND | wxALL, 8);
    wxSizer* buttons = dialog.CreateButtonSizer(wxOK);
    if(buttons) sizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    dialog.SetSizer(sizer);
    dialog.ShowModal();
}

//运行/暂停：切换后立刻重算一次，并把菜单项文字改成当前可做的动作
void MainFrame::OnRunPause(wxCommandEvent&){
    if(!simulation) return;
    simulation->setEnabled(!simulation->enabled());
    if(simulation->enabled()) simulation->evaluate();   //恢复运行时立刻出值
    if(canvas) canvas->refresh_simulation_display();
    UpdateSimulationStatus();
    if(menu_bar){
        menu_bar->SetLabel(ID_RUN_PAUSE, simulation->enabled()
            ? wxString::FromUTF8("暂停仿真")
            : wxString::FromUTF8("继续运行"));
    }
}

//单步时钟：暂停状态下也能推进一次（本阶段只有组合逻辑，等价于"重新计算"）
void MainFrame::OnTick(wxCommandEvent&){
    if(!simulation) return;
    simulation->evaluate();
    if(canvas) canvas->refresh_simulation_display();
    UpdateSimulationStatus();
}

//功能面板菜单：切换 AI 助手面板
void MainFrame::OnToggleAgent(wxCommandEvent&){
    if(show) SetAgentVisible(!show->Visible());
}

void MainFrame::SetAgentVisible(bool visible){
    if(!show) return;
    show->SetVisible(visible);
    SyncPanelMenu();
}

//功能面板菜单：切换代码面板
void MainFrame::OnToggleVerilogPanel(wxCommandEvent&){
    SetVerilogPanelVisible(!verilog_visible);
}

void MainFrame::SyncPanelMenu(){
    if(menu_bar){
        menu_bar->SyncPanelStates(show && show->Visible(), verilog_visible);
    }
}

void MainFrame::OnMenuOpen(wxMenuEvent& event){
    SyncPanelMenu();
    event.Skip();
}

void MainFrame::SetVerilogPanelVisible(bool visible){
    if(!canvas_splitter || !verilog_panel) return;
    if(visible == verilog_visible) return;
    verilog_visible = visible;
    if(visible){
        //显示：画布在左、代码面板在右，可拉伸宽度
        canvas_splitter->SplitVertically(canvas, verilog_panel, -280);
        verilog_panel->Show();
        //每次打开都从当前画布重新生成代码（画布可能已变化）
        verilog_panel->refresh_from_canvas();
    }else{
        //隐藏：从分隔器摘除（不销毁窗口），画布占满
        canvas_splitter->Unsplit(verilog_panel);
        verilog_panel->Hide();
    }
    SyncPanelMenu();
}

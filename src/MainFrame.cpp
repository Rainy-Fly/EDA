#include"./MainFrame.hpp"
#include"./Canva/Canvas.hpp"
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

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, wxString::FromUTF8("TinyEDA "),
              wxDefaultPosition, wxSize(1280, 800)),
      splitter(nullptr),
      work_splitter(nullptr),
      right_splitter(nullptr),
      explorer(nullptr),
      canvas(nullptr),
      attribute_bar(nullptr),
      agent_area(nullptr){
    //分栏结构：
    //  splitter(外层竖分) = explorer | work_splitter
    //  work_splitter(中层横分) = right_splitter | agent_area（AI面板，默认隐藏）
    //  right_splitter(内层竖分) = canvas | attribute_bar
    splitter = new wxSplitterWindow(this, wxID_ANY);
    work_splitter = new wxSplitterWindow(splitter, wxID_ANY);
    right_splitter = new wxSplitterWindow(work_splitter, wxID_ANY);
    explorer      = new ExplorerPane(splitter);
    canvas        = new Canvas(right_splitter);
    attribute_bar = new AttributeBar(right_splitter);
    agent_area    = new Agent::AgentArea(work_splitter, canvas);

    splitter->SetMinimumPaneSize(150);
    splitter->SplitVertically(explorer, work_splitter, 240);

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
    CreateStatusBar();
    Center();
}

//视图菜单：切换 AI 助手面板，并把菜单勾选同步为面板实际状态
void MainFrame::OnToggleAgent(wxCommandEvent&){
    if(show) show->Toggle();
    if(wxMenuBar* bar = GetMenuBar()){
        bar->Check(ID_SHOW_AGENT, show && show->Visible());
    }
}

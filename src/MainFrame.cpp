#include "MainFrame.hpp"
#include"./utils/SVGPool.hpp"
#include<wx/simplebook.h>
#include <wx/wx.h>
#include "./MainPages/MainPageSizer.hpp"
#include "./MainPages/MainPagesExport.hpp"
// #include<iostream>
// using std::cout;
MainFrame::MainFrame()
    : wxFrame(
        nullptr,
        wxID_ANY,
        wxString::FromUTF8("数字电路 EDA"),
        wxDefaultPosition,
        wxSize(1200, 800)
    )
{   
    SetBackgroundColour(wxColour(240, 240, 240));
    InitChildren(); 
}


void MainFrame::InitChildren(){
    this->svgPool=new SVGPool("./src/assets/electronic-symbols/manifest.json");
    InitMenuBar();
    InitMainPage();
}

// =========================
// 菜单栏
// =========================
void MainFrame::InitMenuBar()
{
    this->menuBar = new wxMenuBar();

    auto* fileMenu = new wxMenu();

    fileMenu->Append(
        wxID_NEW,
        wxString::FromUTF8("新建")
    );

    fileMenu->Append(
        wxID_OPEN,
        wxString::FromUTF8("打开")
    );

    fileMenu->AppendSeparator();

    fileMenu->Append(
        wxID_EXIT,
        wxString::FromUTF8("退出")
    );

    menuBar->Append(
        fileMenu,
        wxString::FromUTF8("文件")
    );

    SetMenuBar(menuBar);

    // =========================
    // 菜单事件
    // =========================

    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&)
        {
            
            wxLogMessage(
                wxString::FromUTF8("点击了新建")
            );
        },
        wxID_NEW
    );

    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&)
        {
            Close();
        },
        wxID_EXIT
    );
}


// =========================
// 主页面
// =========================
void MainFrame::InitMainPage()
{
    this->mainPage = new wxSimplebook(
        this,
        wxID_ANY,
        wxDefaultPosition,
        wxSize(1200, 800),
        wxBORDER_NONE
    );
    MainPageBase* editPage = new EditPage(mainPage,MainPageSizer::GetInstance(1200,800));
    mainPage->AddPage(
        editPage,
        wxString::FromUTF8("编辑页面")
    );
}
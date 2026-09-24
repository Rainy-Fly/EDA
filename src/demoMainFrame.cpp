#include"./demoMainFrame.hpp"
#include"./Canva/Canvas.hpp"
#include"./Explorer/ExplorerPane.h"
#include"./AttributeBar/AttributeBar.hpp"

demoMainFrame::demoMainFrame()
    : wxFrame(nullptr, wxID_ANY, wxString::FromUTF8("TinyEDA Demo"),
              wxDefaultPosition, wxSize(1280, 800)),
      splitter(nullptr),
      right_splitter(nullptr),
      explorer(nullptr),
      canvas(nullptr),
      attribute_bar(nullptr){
    //外层分栏：左侧资源树(ExplorerPane)；右侧为上下分栏（画布 + 属性栏）
    splitter = new wxSplitterWindow(this, wxID_ANY);
    right_splitter = new wxSplitterWindow(splitter, wxID_ANY);
    explorer      = new ExplorerPane(splitter);
    canvas        = new Canvas(right_splitter);
    attribute_bar = new AttributeBar(right_splitter);

    splitter->SetMinimumPaneSize(150);
    splitter->SplitVertically(explorer, right_splitter, 240);

    right_splitter->SetMinimumPaneSize(120);
    right_splitter->SplitHorizontally(canvas, attribute_bar, 560);  //画布在上，属性栏在下

    //联动：画布选中/放置/清空元件时，属性栏显示对应元件的元数据
    canvas->set_current_item_callback(
        [this](CanvasItem* item){ attribute_bar->set_item(item); });

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(splitter, 1, wxEXPAND);
    SetSizer(sizer);
    CreateStatusBar();
    Center();
}

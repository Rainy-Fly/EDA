#include"./demoMainFrame.hpp"
#include"./Canva/Canvas.hpp"
#include"./Explorer/ExplorerPane.h"

demoMainFrame::demoMainFrame()
    : wxFrame(nullptr, wxID_ANY, wxString::FromUTF8("TinyEDA Demo"),
              wxDefaultPosition, wxSize(1280, 800)),
      splitter(nullptr),
      explorer(nullptr),
      canvas(nullptr){
    //左右分栏：左侧资源树(ExplorerPane)，右侧画布(Canvas)
    splitter = new wxSplitterWindow(this, wxID_ANY);
    explorer = new ExplorerPane(splitter);
    canvas   = new Canvas(splitter);
    splitter->SetMinimumPaneSize(150);          //分栏最小宽度，防止拖没
    splitter->SplitVertically(explorer, canvas, 240);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(splitter, 1, wxEXPAND);
    SetSizer(sizer);
    CreateStatusBar();
    Center();
}

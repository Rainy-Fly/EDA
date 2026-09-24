#include"./demoMainFrame.hpp"
#include"./Canva/Canvas.hpp"

demoMainFrame::demoMainFrame()
    : wxFrame(nullptr, wxID_ANY, wxString::FromUTF8("TinyEDA Demo"),
              wxDefaultPosition, wxSize(1280, 800)),
      canvas(nullptr){
    canvas = new Canvas(this);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(canvas, 1, wxEXPAND);
    SetSizer(sizer);
    CreateStatusBar();
    Center();
}

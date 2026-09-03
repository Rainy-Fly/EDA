#pragma once
#include <wx/wx.h>
#include<wx/simplebook.h>

class MainFrame : public wxFrame
{
public:
    MainFrame();
private:
    void InitChildren();
    void InitMenuBar();
    void InitMainPage();
    wxMenuBar* menuBar;
    wxSimplebook* mainPage;
    
};
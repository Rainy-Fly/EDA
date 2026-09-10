#pragma once
#include <wx/wx.h>
#include<wx/simplebook.h>
#include"./utils/SVGPool.hpp"


using std::vector;
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
    SVGPool* svgPool;
};
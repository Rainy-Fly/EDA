#include<wx/wx.h>
#include<wx/sizer.h>
#pragma once
class MainPageSizer : public wxFlexGridSizer
{
public:
    static MainPageSizer* GetInstance(int x=0,int y=0);
private:
    MainPageSizer(int x,int y);
    static MainPageSizer* Sigleon;
};
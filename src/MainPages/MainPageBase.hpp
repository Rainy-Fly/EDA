#pragma once
#include<wx/wx.h>
#include<wx/panel.h>
#include"./MainPageSizer.hpp"

class MainPageBase : public wxPanel
{
public:
    MainPageBase(wxWindow* parent, MainPageSizer* sizer);

protected:
    MainPageSizer* sizer;
private:
    virtual void apply_sizer()=0;
};
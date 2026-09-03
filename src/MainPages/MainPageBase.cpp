#include"./MainPageBase.hpp"
#include "./MainPageSizer.hpp"
MainPageBase::MainPageBase(wxWindow* parent, MainPageSizer* sizer)
    : wxPanel(parent,
            wxID_ANY,
            wxDefaultPosition,
            wxSize(1200,800),
            wxBORDER_NONE)
{   
    this->sizer = sizer;
}
 


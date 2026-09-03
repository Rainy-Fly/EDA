#pragma once
#include"./MainPageBase.hpp"

class EditPage : public MainPageBase
{
private:
    wxButton* buttonA;
    wxButton* buttonB;

public:
    EditPage(wxWindow* parent,MainPageSizer* sizer);
    void apply_sizer() override;
};
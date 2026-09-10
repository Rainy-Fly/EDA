#pragma once
#include"./MainPageBase.hpp"
#include"../utils/SymbolSVG.hpp"
#include<vector>
using std::vector;
class EditPage : public MainPageBase
{
private:
    wxButton* buttonA;
    wxButton* buttonB;
    vector<SymbolSVG*> symbols;

public:
    EditPage(wxWindow* parent,MainPageSizer* sizer);
    void apply_sizer() override;
};
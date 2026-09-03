#include "./EditPage.hpp"
EditPage::EditPage(wxWindow* parent, MainPageSizer* sizer):MainPageBase(parent, sizer)
{
    buttonA = new wxButton(this, wxID_ANY, "Button A");
    buttonB = new wxButton(this, wxID_ANY, "Button B");

    this->apply_sizer();
}

void EditPage::apply_sizer()
{
    this->sizer->Add(buttonA, 0, wxALL | wxEXPAND, 5);
    this->sizer->Add(buttonB, 0, wxALL | wxEXPAND, 5);
    this->SetSizer(this->sizer);
}
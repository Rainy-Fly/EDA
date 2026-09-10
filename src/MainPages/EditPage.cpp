#include "./EditPage.hpp"
// #include<iostream>  用来调试
// using std::cout;
EditPage::EditPage(wxWindow* parent, MainPageSizer* sizer):MainPageBase(parent, sizer)
{
    buttonA = new wxButton(this, wxID_ANY, "Button A");
    buttonB = new wxButton(this, wxID_ANY, "Button B");

    this->apply_sizer();
    buttonA->Bind(
        wxEVT_BUTTON,
        [this](wxCommandEvent&){
            auto* image=new SymbolSVG(this,"antenna-com-dipole",wxSize(40,50),wxPoint(150,150));
            this->symbols.push_back(image);
        }
    );

    Bind(
        wxEVT_MOTION,
        [this](wxMouseEvent& event)
        {
            auto pos = event.GetPosition();

            if (!symbols.empty())
                symbols.back()->Move(pos);
        }
    );
}

void EditPage::apply_sizer()
{
    this->sizer->Add(buttonA, 0, wxALL | wxEXPAND, 5);
    this->sizer->Add(buttonB, 0, wxALL | wxEXPAND, 5);
    this->SetSizer(this->sizer);
}




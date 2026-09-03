#include"./MainPageSizer.hpp"

MainPageSizer* MainPageSizer::Sigleon = nullptr;

MainPageSizer::MainPageSizer(int x,int y): wxFlexGridSizer(2,wxSize(0,0))
{
    if(Sigleon==nullptr)
    {
        Sigleon = this;
        this->SetMinSize(wxSize(x,y));
    }
    else
    {
        wxLogError("MainPageSize is a singleton class, only one instance can be created.");
    }
}

MainPageSizer* MainPageSizer::GetInstance(int x,int y)
{
    if(Sigleon==nullptr)
    {
        if(x==0 && y==0)
        {
            wxLogError("MainPageSize is a singleton class, please create an instance first.");
            return nullptr;
        }
        Sigleon = new MainPageSizer(x,y);
    }
    return Sigleon;
}
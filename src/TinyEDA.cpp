// 目前是随便写的demo,用于学习尝试
#include <wx/wx.h>
#include "./MainFrame.hpp"
class TinyEDA : public wxApp
{
public:
    bool OnInit() override
    {
        MainFrame* frame = new MainFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(TinyEDA);
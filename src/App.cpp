#include"./App.hpp"
#include"./MainFrame.hpp"

bool App::OnInit(){
    //注册PNG等图片格式处理器（wxWidgets默认不注册，否则LoadFile会静默失败）
    wxInitAllImageHandlers();
    MainFrame* frame = new MainFrame();
    frame->Show(true);
    return true;
}

wxIMPLEMENT_APP(App);

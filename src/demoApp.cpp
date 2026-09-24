#include"./demoApp.hpp"
#include"./demoMainFrame.hpp"

bool demoApp::OnInit(){
    //注册PNG等图片格式处理器（wxWidgets默认不注册，否则LoadFile会静默失败）
    wxInitAllImageHandlers();
    demoMainFrame* frame = new demoMainFrame();
    frame->Show(true);
    return true;
}

wxIMPLEMENT_APP(demoApp);

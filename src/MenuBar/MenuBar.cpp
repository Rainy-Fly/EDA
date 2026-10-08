#include"./MenuBar.hpp"

MenuBar::MenuBar(){
    //---- File（文件）----
    wxMenu* file_menu = new wxMenu;
    file_menu->Append(wxID_NEW,    wxString::FromUTF8("新建"));
    file_menu->Append(wxID_OPEN,   wxString::FromUTF8("打开"));
    file_menu->Append(wxID_SAVE,   wxString::FromUTF8("保存"));
    file_menu->AppendSeparator();
    file_menu->Append(wxID_EXIT,   wxString::FromUTF8("退出"));

    //---- Edit（编辑）----
    wxMenu* edit_menu = new wxMenu;
    edit_menu->Append(wxID_UNDO,   wxString::FromUTF8("撤销"));
    edit_menu->Append(wxID_REDO,   wxString::FromUTF8("重做"));
    edit_menu->AppendSeparator();
    edit_menu->Append(wxID_DELETE, wxString::FromUTF8("删除"));

    //---- Simulate（模拟）----
    wxMenu* simulate_menu = new wxMenu;
    simulate_menu->Append(ID_RUN_PAUSE, wxString::FromUTF8("运行 / 暂停"));
    simulate_menu->Append(ID_TICK,      wxString::FromUTF8("单步时钟"));

    Append(file_menu,     wxString::FromUTF8("文件"));
    Append(edit_menu,     wxString::FromUTF8("编辑"));
    Append(simulate_menu, wxString::FromUTF8("模拟"));
}

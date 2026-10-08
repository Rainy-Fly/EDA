#include"./MenuBar.hpp"

MenuBar::MenuBar(){
    //---- File（文件）----
    // 标签里\t后面是快捷键，wxWidgets 会把它解析成真正的加速键（MSW下由菜单加速表实现）。
    // 这三个功能的实际逻辑在 src/MenuBar/FileActions.cpp（挂在主窗口的 wxID_NEW/OPEN/SAVE/SAVEAS 上）
    wxMenu* file_menu = new wxMenu;
    file_menu->Append(wxID_NEW,    wxString::FromUTF8("新建(&N)\tCtrl+N"));
    file_menu->Append(wxID_OPEN,   wxString::FromUTF8("打开(&O)...\tCtrl+O"));
    file_menu->Append(wxID_SAVE,   wxString::FromUTF8("保存(&S)\tCtrl+S"));
    file_menu->Append(wxID_SAVEAS, wxString::FromUTF8("另存为(&A)...\tCtrl+Shift+S"));
    file_menu->AppendSeparator();
    file_menu->Append(wxID_EXIT,   wxString::FromUTF8("退出(&X)\tCtrl+Q"));

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

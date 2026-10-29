#include "./MenuBar.hpp"

MenuBar::MenuBar(){
    wxMenu* file_menu = new wxMenu;
    file_menu->Append(wxID_NEW, wxString::FromUTF8("新建(&N)\tCtrl+N"));
    file_menu->Append(wxID_OPEN, wxString::FromUTF8("打开(&O)...\tCtrl+O"));
    file_menu->Append(wxID_SAVE, wxString::FromUTF8("保存(&S)\tCtrl+S"));
    file_menu->Append(wxID_SAVEAS, wxString::FromUTF8("另存为(&A)...\tCtrl+Shift+S"));
    file_menu->Append(ID_EXPORT_KICAD_NETLIST, wxString::FromUTF8("导出 KiCad 网表(&K)..."));
    file_menu->Append(ID_EXPORT_VERILOG, wxString::FromUTF8("导出 Verilog(&V)..."));
    file_menu->AppendSeparator();
    file_menu->Append(ID_SET_DEFAULT_DIR, wxString::FromUTF8("设置默认目录(&D)..."));
    file_menu->AppendSeparator();
    file_menu->Append(wxID_EXIT, wxString::FromUTF8("退出(&X)\tCtrl+Q"));

    wxMenu* edit_menu = new wxMenu;
    edit_menu->Append(wxID_UNDO, wxString::FromUTF8("撤销"));
    edit_menu->Append(wxID_REDO, wxString::FromUTF8("重做"));
    edit_menu->AppendSeparator();
    edit_menu->Append(wxID_DELETE, wxString::FromUTF8("删除"));

    wxMenu* simulate_menu = new wxMenu;
    simulate_menu->Append(ID_RUN_PAUSE, wxString::FromUTF8("运行 / 暂停"));
    simulate_menu->Append(ID_TICK, wxString::FromUTF8("单步时钟"));
    simulate_menu->AppendSeparator();
    simulate_menu->Append(ID_SIM_DIAGNOSTICS, wxString::FromUTF8("仿真诊断(&D)..."));

    wxMenu* panels_menu = new wxMenu;
    panels_menu->AppendCheckItem(ID_SHOW_AGENT,
                                 wxString::FromUTF8("AI 助手面板(&A)\tF12"));
    panels_menu->AppendCheckItem(ID_SHOW_VERILOG_PANEL,
                                 wxString::FromUTF8("Verilog 代码面板(&V)\tF11"));

    Append(file_menu, wxString::FromUTF8("文件"));
    Append(edit_menu, wxString::FromUTF8("编辑"));
    Append(simulate_menu, wxString::FromUTF8("模拟"));
    Append(panels_menu, wxString::FromUTF8("功能面板"));
}

void MenuBar::SyncPanelStates(bool agent_visible, bool verilog_visible){
    Check(ID_SHOW_AGENT, agent_visible);
    Check(ID_SHOW_VERILOG_PANEL, verilog_visible);
}

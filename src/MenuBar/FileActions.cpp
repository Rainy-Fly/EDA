#include"./FileActions.hpp"
#include"./OpenLocationDialog.hpp"
#include"./MenuBar.hpp"          //ID_SET_DEFAULT_DIR
#include"../Canva/Canvas.hpp"
#include"../Netlist/PcbProjectAdapter.hpp"
#include"../Netlist/PcbNetlistBuilder.hpp"
#include"../Netlist/KiCadNetlistExporter.hpp"
#include"../VerilogBridge/ProjectToModule.hpp"
#include"../verilator/VerilatorApi.hpp"

#include<algorithm>
#include<fstream>
#include<sstream>
#include<vector>

#include<wx/filedlg.h>      //wxFileDialog
#include<wx/dirdlg.h>       //wxDirDialog
#include<wx/filename.h>
#include<wx/msgdlg.h>       //wxMessageBox
#include<wx/stdpaths.h>
#include<wx/filefn.h>       //wxDirExists
#include<wx/utils.h>        //wxGetUserName
#include<wx/log.h>          //wxLogNull

namespace{

// wxString -> std::string 必须显式走 UTF-8：
// ToStdString() 按当前区域设置转换（中文Windows上是GBK），而 DealProjectXML 的接口
// 与项目文件里都是 UTF-8，混用会让中文路径/元件名乱码
std::string to_utf8(const wxString& s){
    const wxScopedCharBuffer buf = s.utf8_str();
    return std::string(buf.data(), buf.length());
}

// 文件对话框的过滤器：只认 .xml，同时留一个“所有文件”兜底
wxString project_wildcard(){
    return wxString::FromUTF8("TinyEDA 项目文件 (*.xml)|*.xml|所有文件 (*.*)|*.*");
}

// 把网表校验/写入诊断整理成可读文本。
wxString format_diagnostics(const std::vector<NetlistData::Diagnostic>& diagnostics){
    std::ostringstream out;
    for(const NetlistData::Diagnostic& diagnostic : diagnostics){
        out << (diagnostic.severity == "error" ? "[错误] " : "[警告] ")
            << diagnostic.code << ": " << diagnostic.message;
        if(!diagnostic.objectIds.empty()){
            out << "（对象: ";
            for(std::size_t i = 0; i < diagnostic.objectIds.size(); ++i){
                if(i) out << ", ";
                out << diagnostic.objectIds[i];
            }
            out << "）";
        }
        if(!diagnostic.pinId.empty()) out << "（引脚: " << diagnostic.pinId << "）";
        out << "\n";
    }
    return wxString::FromUTF8(out.str());
}

//把 Verilator 的诊断（语法/语义/模型）整理成可读文本。
//行号只在解析源码时才有意义，项目侧生成的模型诊断不带位置。
wxString format_verilog_diagnostics(const std::vector<Verilator::Diagnostic>& diagnostics){
    std::ostringstream out;
    for(const Verilator::Diagnostic& diagnostic : diagnostics){
        out << (diagnostic.severity == Verilator::DiagnosticSeverity::Error
                    ? "[错误] "
                    : "[警告] ")
            << diagnostic.code << ": " << diagnostic.message;
        if(diagnostic.location.line > 0){
            out << "（行 " << diagnostic.location.line
                << ", 列 " << diagnostic.location.column << "）";
        }
        out << "\n";
    }
    return wxString::FromUTF8(out.str());
}

} // namespace

FileActions::FileActions(wxFrame* frame_, Canvas* canvas_)
    : frame(frame_), canvas(canvas_), modified(false){
    //主程序启动时加载用户设置（配置文件）
    settings = AppSettings::Load();

    //菜单事件挂在宿主窗口上（本类不是wxWindow，只借用frame的事件分发）
    frame->Bind(wxEVT_MENU, &FileActions::on_new,     this, wxID_NEW);
    frame->Bind(wxEVT_MENU, &FileActions::on_open,    this, wxID_OPEN);
    frame->Bind(wxEVT_MENU, &FileActions::on_save,    this, wxID_SAVE);
    frame->Bind(wxEVT_MENU, &FileActions::on_save_as, this, wxID_SAVEAS);
    frame->Bind(wxEVT_MENU, &FileActions::on_export_kicad_netlist, this, ID_EXPORT_KICAD_NETLIST);
    frame->Bind(wxEVT_MENU, &FileActions::on_export_verilog, this, ID_EXPORT_VERILOG);
    frame->Bind(wxEVT_MENU, &FileActions::on_set_default_dir, this, ID_SET_DEFAULT_DIR);
    frame->Bind(wxEVT_MENU, &FileActions::on_exit,    this, wxID_EXIT);
    //关闭窗口也要走“是否保存”的确认，否则点右上角的×会绕过未保存检查
    frame->Bind(wxEVT_CLOSE_WINDOW, &FileActions::on_close, this);

    //画布内容变化（放置/删除/移动元件、画完导线）-> 标记“有未保存的修改”
    canvas->set_changed_callback([this]{ mark_modified(); });

    //新项目的 <info>：作者取当前系统用户名（项目名在第一次保存时按文件名补）
    info = DealProjectXML::ProjectInfo{};
    info.author = to_utf8(wxGetUserName());

    update_title();
}

FileActions::~FileActions(){
    if(!frame) return;
    //画布比本对象活得久（是frame的子窗口），先摘掉回调，之后再触发变化也不会调用到这里
    if(canvas) canvas->set_changed_callback(nullptr);
    frame->Unbind(wxEVT_MENU, &FileActions::on_new,     this, wxID_NEW);
    frame->Unbind(wxEVT_MENU, &FileActions::on_open,    this, wxID_OPEN);
    frame->Unbind(wxEVT_MENU, &FileActions::on_save,    this, wxID_SAVE);
    frame->Unbind(wxEVT_MENU, &FileActions::on_save_as, this, wxID_SAVEAS);
    frame->Unbind(wxEVT_MENU, &FileActions::on_export_kicad_netlist, this, ID_EXPORT_KICAD_NETLIST);
    frame->Unbind(wxEVT_MENU, &FileActions::on_set_default_dir, this, ID_SET_DEFAULT_DIR);
    frame->Unbind(wxEVT_MENU, &FileActions::on_exit,    this, wxID_EXIT);
    frame->Unbind(wxEVT_CLOSE_WINDOW, &FileActions::on_close, this);
}

void FileActions::mark_modified(){
    set_modified(true);
}

//================ 菜单动作 ================

void FileActions::on_new(wxCommandEvent& event){
    (void)event;
    //先处理未保存的修改：用户取消则什么都不做
    if(!confirm_discard_or_save()) return;
    new_project();
}

void FileActions::on_open(wxCommandEvent& event){
    (void)event;
    if(!confirm_discard_or_save()) return;
    open_project();
}

void FileActions::on_save(wxCommandEvent& event){
    (void)event;
    save_project();
}

void FileActions::on_save_as(wxCommandEvent& event){
    (void)event;
    save_project_as();
}

//导出 KiCad PCB 网表：画布 -> 只读快照 -> PcbNetlist -> .net 文件。
//网表生成与序列化都不在本函数内实现，失败时把具体诊断显示给用户。
void FileActions::on_export_kicad_netlist(wxCommandEvent& event){
    (void)event;
    if(!canvas) return;

    const DealProjectXML::ProjectData project = DealProjectXML::CollectFromCanvas(canvas, info);
    const NetlistData::PcbProjectSnapshot snapshot =
        NetlistData::BuildPcbSnapshotFromProject(project);
    const NetlistData::PcbNetlistBuildResult build =
        NetlistData::PcbNetlistBuilder::build(snapshot);

    if(!build.ok() || !build.value){
        wxMessageBox(wxString::FromUTF8("无法导出 KiCad 网表：\n\n") +
                         format_diagnostics(build.diagnostics),
                     wxString::FromUTF8("导出 KiCad 网表"), wxOK | wxICON_ERROR, frame);
        return;
    }

    const bool hasWarnings = std::any_of(
        build.diagnostics.begin(), build.diagnostics.end(),
        [](const NetlistData::Diagnostic& diagnostic){
            return diagnostic.severity == "warning";
        });
    if(hasWarnings){
        const int answer = wxMessageBox(
            wxString::FromUTF8("导出前检查发现以下警告：\n\n") +
                format_diagnostics(build.diagnostics) +
                wxString::FromUTF8("\n是否继续导出？"),
            wxString::FromUTF8("导出 KiCad 网表"), wxYES_NO | wxICON_WARNING, frame);
        if(answer != wxYES) return;
    }

    //默认文件名：当前项目名 + .net
    const wxString suggested = current_path.empty()
        ? wxString(wxT("project.net"))
        : wxFileName(current_path).GetName() + wxT(".net");
    wxFileDialog dlg(frame, wxString::FromUTF8("导出 KiCad 网表"), start_dir(), suggested,
                     wxString::FromUTF8("KiCad 网表 (*.net)|*.net|所有文件 (*.*)|*.*"),
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if(dlg.ShowModal() != wxID_OK) return;

    wxString path = dlg.GetPath();
    if(!path.Lower().EndsWith(wxT(".net"))) path += wxT(".net");

    NetlistData::KiCadNetlistExportOptions options;
    if(!current_path.empty()) options.sourcePath = to_utf8(current_path);

    NetlistData::KiCadNetlistWriteResult written;
    {
        //底层用 std::ofstream；wxLogNull 避免 wxWidgets 额外弹原始错误框
        wxLogNull no_log;
        written = NetlistData::KiCadNetlistExporter::write(*build.value, to_utf8(path), options);
    }
    if(!written.ok()){
        wxMessageBox(wxString::FromUTF8("写入网表文件失败：\n\n") +
                         format_diagnostics(written.diagnostics),
                     wxString::FromUTF8("导出失败"), wxOK | wxICON_ERROR, frame);
        return;
    }
    if(frame) frame->SetStatusText(wxString::FromUTF8("KiCad 网表已导出"));
}

//导出 Verilog：画布 -> ProjectData -> Verilator 电路模型 -> .v 文件。
//适配与校验都在 VerilogBridge/ProjectToModule 里完成，本函数只负责交互与写盘。
void FileActions::on_export_verilog(wxCommandEvent& event){
    (void)event;
    if(!canvas) return;

    const DealProjectXML::ProjectData project = DealProjectXML::CollectFromCanvas(canvas, info);
    const std::string top_name = info.name.empty() ? "top" : info.name;
    const VerilogBridge::ProjectToModuleResult built =
        VerilogBridge::BuildModuleFromProject(project, top_name);

    if(built.ok && built.module.ports.empty() && built.module.gates.empty()){
        wxMessageBox(wxString::FromUTF8("当前画布没有可导出的逻辑元件（门或输入/输出端子）。"),
                     wxString::FromUTF8("导出 Verilog"), wxOK | wxICON_INFORMATION, frame);
        return;
    }
    if(!built.ok){
        wxMessageBox(wxString::FromUTF8("无法导出 Verilog：\n\n") +
                         format_verilog_diagnostics(built.diagnostics),
                     wxString::FromUTF8("导出 Verilog"), wxOK | wxICON_ERROR, frame);
        return;
    }

    const bool hasWarnings = std::any_of(
        built.diagnostics.begin(), built.diagnostics.end(),
        [](const Verilator::Diagnostic& diagnostic){
            return diagnostic.severity == Verilator::DiagnosticSeverity::Warning;
        });
    if(hasWarnings){
        const int answer = wxMessageBox(
            wxString::FromUTF8("导出前检查发现以下警告：\n\n") +
                format_verilog_diagnostics(built.diagnostics) +
                wxString::FromUTF8("\n是否继续导出？"),
            wxString::FromUTF8("导出 Verilog"), wxYES_NO | wxICON_WARNING, frame);
        if(answer != wxYES) return;
    }

    const Verilator::GenerateResult generated = Verilator::generate_verilog(built.module);
    if(!generated.ok){
        wxMessageBox(wxString::FromUTF8("生成 Verilog 源码失败：\n\n") +
                         format_verilog_diagnostics(generated.diagnostics),
                     wxString::FromUTF8("导出 Verilog"), wxOK | wxICON_ERROR, frame);
        return;
    }

    const wxString suggested = current_path.empty()
        ? wxString::FromUTF8(built.module.name.c_str()) + wxT(".v")
        : wxFileName(current_path).GetName() + wxT(".v");
    wxFileDialog dlg(frame, wxString::FromUTF8("导出 Verilog"), start_dir(), suggested,
                     wxString::FromUTF8("Verilog 源文件 (*.v)|*.v|所有文件 (*.*)|*.*"),
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if(dlg.ShowModal() != wxID_OK) return;

    wxString path = dlg.GetPath();
    if(!path.Lower().EndsWith(wxT(".v"))) path += wxT(".v");

    std::ofstream out(to_utf8(path), std::ios::binary | std::ios::trunc);
    if(!out){
        wxMessageBox(wxString::FromUTF8("无法写入 Verilog 文件：\n") + path,
                     wxString::FromUTF8("导出失败"), wxOK | wxICON_ERROR, frame);
        return;
    }
    out << generated.verilog;
    out.close();
    if(!out){
        wxMessageBox(wxString::FromUTF8("写入 Verilog 文件时出错：\n") + path,
                     wxString::FromUTF8("导出失败"), wxOK | wxICON_ERROR, frame);
        return;
    }
    if(frame) frame->SetStatusText(wxString::FromUTF8("Verilog 已导出"));
}

//设置默认目录：选一个目录存进配置文件，“打开项目”里的“从默认目录打开”就用它
void FileActions::on_set_default_dir(wxCommandEvent& event){
    (void)event;
    const wxString initial = settings.default_dir.empty()
        ? start_dir()
        : wxString::FromUTF8(settings.default_dir.c_str());
    wxDirDialog dlg(frame, wxString::FromUTF8("选择默认目录"), initial,
                    wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if(dlg.ShowModal() != wxID_OK) return;

    settings.default_dir = to_utf8(dlg.GetPath());

    wxString written;
    if(!persist_settings(&written)){
        wxMessageBox(wxString::FromUTF8("配置保存失败（没有写入权限）：") +
                         AppSettings::ConfigPath(),
                     wxString::FromUTF8("保存设置失败"), wxOK | wxICON_WARNING, frame);
        return;
    }
    if(frame){
        //状态栏是原生控件：中文里混半角路径/字符会被 GDI 的字体链接渲染得发虚发花，
        //所以这里只报纯中文的状态，具体路径由标题栏/对话框体现
        frame->SetStatusText(wxString::FromUTF8("默认目录已设置"));
    }
}

void FileActions::on_exit(wxCommandEvent& event){
    (void)event;
    //不直接Destroy：统一走窗口关闭流程，未保存确认只有一份逻辑（见on_close）
    if(frame) frame->Close(false);
}

void FileActions::on_close(wxCloseEvent& event){
    if(event.CanVeto() && !confirm_discard_or_save()){
        event.Veto();   //用户取消：窗口保持打开
        return;
    }
    persist_settings();            //退出前把设置落盘（默认目录/最近位置）
    event.Skip();                  //交给默认处理：真正销毁窗口
}

//================ 具体实现 ================

bool FileActions::confirm_discard_or_save(){
    if(!modified) return true;   //没有未保存的修改，直接放行
    const int answer = wxMessageBox(
        wxString::FromUTF8("当前项目尚未保存，是否保存？"),
        wxString::FromUTF8("TinyEDA"),
        wxYES_NO | wxCANCEL | wxICON_QUESTION, frame);
    if(answer == wxCANCEL) return false;   //取消：调用方中止当前操作
    if(answer == wxNO)     return true;    //不保存：丢弃修改继续
    return save_project();                 //保存失败或用户取消了另存为 -> 中止
}

void FileActions::new_project(){
    canvas->clear_all_items();
    //新项目从默认视角开始（一张空白的画布），不沿用上一个项目的缩放和平移
    canvas->set_view(wxPoint(0, 0), 1.0f);
    current_path.clear();
    info = DealProjectXML::ProjectInfo{};
    info.author = to_utf8(wxGetUserName());
    set_modified(false);
    update_title();   //路径变了，标题要刷新（set_modified在值没变时不会刷新）
    if(frame) frame->SetStatusText(wxString::FromUTF8("新建项目"));
}

bool FileActions::open_project(){
    //第一步：先选“从哪里开始找”（最近位置 / 根目录 / 默认目录）
    const wxString dir = AskOpenLocation(frame,
                                        wxString::FromUTF8(settings.last_dir.c_str()),
                                        AppSettings::ProjectRootDir(),
                                        wxString::FromUTF8(settings.default_dir.c_str()));
    if(dir.empty()) return false;   //用户取消

    wxFileDialog dlg(frame, wxString::FromUTF8("打开项目"), dir, wxString(),
                     project_wildcard(), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if(dlg.ShowModal() != wxID_OK) return false;
    const wxString path = dlg.GetPath();

    //第二步：交给 DealProjectXML 读。它内部先完整解析、成功后才动画布，
    //所以文件损坏/格式不对时画布保持原样（不会“打开失败还把画布清空了”）。
    //wxLogNull：否则 XML 解析失败时 wxWidgets 会先弹它自己的原始错误框，
    //和我们下面这条友好提示重复
    DealProjectXML::ProjectInfo loaded;
    bool ok = false;
    {
        wxLogNull no_log;
        ok = DealProjectXML::LoadProject(canvas, to_utf8(path), &loaded);
    }
    if(!ok){
        wxMessageBox(wxString::FromUTF8("无法读取该文件（不是有效的 TinyEDA 项目文件，或文件已损坏）：") + path,
                     wxString::FromUTF8("打开失败"), wxOK | wxICON_ERROR, frame);
        return false;
    }
    info = loaded;

    remember_path(path);
    //刚打开的文件视为“未修改”，所以标题上没有*
    mark_saved(path, wxString::FromUTF8("已打开项目"));
    return true;
}

bool FileActions::save_project(){
    if(current_path.empty()) return save_project_as();   //第一次保存：先问存到哪
    return write_to(current_path);
}

bool FileActions::save_project_as(){
    //两个分支都显式构造成wxString：否则字面量(wchar_t[])与wxString混在?:里会有二义性
    const wxString suggested = current_path.empty()
        ? wxString(wxT("project.xml"))
        : wxFileName(current_path).GetFullName();
    wxFileDialog dlg(frame, wxString::FromUTF8("另存为"), start_dir(), suggested,
                     project_wildcard(), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if(dlg.ShowModal() != wxID_OK) return false;

    wxString path = dlg.GetPath();
    if(!path.Lower().EndsWith(wxT(".xml"))) path += wxT(".xml");   //用户没写扩展名时补上
    return write_to(path);
}

bool FileActions::write_to(const wxString& path){
    //项目信息：README 的 <info>（created/modified 由 DealProjectXML 在保存时生成）
    if(info.name.empty() || info.name == "Untitled") info.name = to_utf8(wxFileName(path).GetName());
    if(info.author.empty()) info.author = to_utf8(wxGetUserName());

    //wxLogNull：文件写不进去时 wxWidgets 会先弹它自己的原始错误框，和我们下面的提示重复
    bool ok = false;
    {
        wxLogNull no_log;
        ok = DealProjectXML::SaveProject(canvas, to_utf8(path), info);
    }
    if(!ok){
        wxMessageBox(wxString::FromUTF8("写入文件失败（路径不存在、文件被占用或没有写权限）：") + path,
                     wxString::FromUTF8("保存失败"), wxOK | wxICON_ERROR, frame);
        return false;   //保存失败：modified保持true，用户不会以为已经存好了
    }
    remember_path(path);
    mark_saved(path, wxString::FromUTF8("项目已保存"));
    return true;
}

//================ 状态维护 ================

void FileActions::set_modified(bool value){
    if(modified == value) return;
    modified = value;
    update_title();
}

void FileActions::mark_saved(const wxString& path, const wxString& status_message){
    current_path = path;
    modified = false;
    update_title();   //路径变了，标题必须刷新（set_modified在值没变时不会刷新标题）
    if(frame) frame->SetStatusText(status_message);
}

void FileActions::update_title(){
    if(!frame) return;
    const wxString file = current_path.empty()
        ? wxString::FromUTF8("未命名")
        : wxFileName(current_path).GetFullName();
    //未保存时标题前面加*（与常见编辑器一致）
    wxString title = modified ? wxString(wxT("*")) : wxString();
    title += wxString::FromUTF8("TinyEDA - ") + file;
    frame->SetTitle(title);
}

//记住这次打开/保存的位置（“从最近位置打开”用），并立即写回配置文件
void FileActions::remember_path(const wxString& path){
    settings.last_file = to_utf8(path);
    settings.last_dir  = to_utf8(wxFileName(path).GetPath());
    persist_settings();
}

bool FileActions::persist_settings(wxString* written){
    if(written) written->clear();

    //先读磁盘上的最新配置，只覆盖本类负责的三个字段：
    //别处（AI 助手设置面板）可能刚写过 apis/active_api，整体 Save 会把它们清空。
    AppSettings::Settings disk = AppSettings::Load();
    disk.last_file   = settings.last_file;
    disk.last_dir    = settings.last_dir;
    disk.default_dir = settings.default_dir;

    const bool ok = AppSettings::Save(disk, written);
    if(ok) settings = disk;   //本地副本同步为“磁盘上的完整内容”，后续保存不会再把 apis 丢掉
    return ok;
}

wxString FileActions::start_dir() const{
    if(!settings.last_dir.empty() && wxDirExists(wxString::FromUTF8(settings.last_dir.c_str()))){
        return wxString::FromUTF8(settings.last_dir.c_str());   //最近打开的位置
    }
    if(!settings.default_dir.empty() && wxDirExists(wxString::FromUTF8(settings.default_dir.c_str()))){
        return wxString::FromUTF8(settings.default_dir.c_str());   //用户设置的默认目录
    }
    return wxStandardPaths::Get().GetDocumentsDir();
}

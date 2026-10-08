#include"./FileActions.hpp"
#include"../io/CircuitFile.hpp"
#include"../Canva/Canvas.hpp"

#include<wx/filedlg.h>      //wxFileDialog
#include<wx/filename.h>
#include<wx/msgdlg.h>       //wxMessageBox
#include<wx/stdpaths.h>
#include<wx/filefn.h>       //wxDirExists
#include<wx/utils.h>        //wxGetUserName

namespace{

// wxString -> std::string 必须显式走 UTF-8：
// ToStdString() 按当前区域设置转换（中文Windows上是GBK），而项目文件里存的是UTF-8
std::string to_utf8(const wxString& s){
    const wxScopedCharBuffer buf = s.utf8_str();
    return std::string(buf.data(), buf.length());
}

// 文件对话框的过滤器：只认 .xml，同时留一个“所有文件”兜底
wxString xml_wildcard(){
    return wxString::FromUTF8("TinyEDA 项目文件 (*.xml)|*.xml|所有文件 (*.*)|*.*");
}

// 当前时间转 ISO 8601（README 示例为 2025-10-08T15:30:00+08:00）。
// wxDateTime 的 %z 给出 "+0800"，这里补一个冒号变成 "+08:00"
wxString iso8601_now(){
    wxString s = wxDateTime::Now().Format(wxT("%Y-%m-%dT%H:%M:%S%z"));
    if(s.length() >= 5){
        const wxString tail = s.Right(5);                 //如 "+0800"
        const bool sign  = (tail[0] == wxT('+') || tail[0] == wxT('-'));
        const bool digit = tail[1] >= wxT('0') && tail[1] <= wxT('9') &&
                           tail[2] >= wxT('0') && tail[2] <= wxT('9') &&
                           tail[3] >= wxT('0') && tail[3] <= wxT('9') &&
                           tail[4] >= wxT('0') && tail[4] <= wxT('9');
        if(sign && digit){
            s = s.Left(s.length() - 2) + wxT(":") + s.Right(2);
        }
    }
    return s;
}

} // namespace

FileActions::FileActions(wxFrame* frame_, Canvas* canvas_)
    : frame(frame_), canvas(canvas_), modified(false){
    //菜单事件挂在宿主窗口上（本类不是wxWindow，只借用frame的事件分发）
    frame->Bind(wxEVT_MENU, &FileActions::on_new,     this, wxID_NEW);
    frame->Bind(wxEVT_MENU, &FileActions::on_open,    this, wxID_OPEN);
    frame->Bind(wxEVT_MENU, &FileActions::on_save,    this, wxID_SAVE);
    frame->Bind(wxEVT_MENU, &FileActions::on_save_as, this, wxID_SAVEAS);
    frame->Bind(wxEVT_MENU, &FileActions::on_exit,    this, wxID_EXIT);
    //关闭窗口也要走“是否保存”的确认，否则点右上角的×会绕过未保存检查
    frame->Bind(wxEVT_CLOSE_WINDOW, &FileActions::on_close, this);

    //画布内容变化（放置/删除/移动元件、画完导线）-> 标记“有未保存的修改”
    canvas->set_changed_callback([this]{ mark_modified(); });

    //新项目的 <info>：作者取当前系统用户名，创建时间取此刻
    project_author = wxGetUserName();
    project_created = iso8601_now();
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
    event.Skip();       //交给默认处理：真正销毁窗口
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
    canvas->clear_items();
    //新项目从默认视角开始（一张空白的画布），不沿用上一个项目的缩放和平移
    canvas->set_offset_coords(wxPoint(0, 0));
    canvas->set_scale(1.0f);
    //项目信息重置
    current_path.clear();
    project_name.clear();
    project_description.clear();
    project_author = wxGetUserName();
    project_created = iso8601_now();
    set_modified(false);
    update_title();   //路径变了，标题要刷新（set_modified在值没变时不会刷新）
    if(frame) frame->SetStatusText(wxString::FromUTF8("新建项目"));
}

bool FileActions::open_project(){
    wxFileDialog dlg(frame, wxString::FromUTF8("打开项目"), default_dir(), wxString(),
                     xml_wildcard(), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if(dlg.ShowModal() != wxID_OK) return false;
    const wxString path = dlg.GetPath();

    //关键：先把整个文件读完并校验，确认没问题再动画布。
    //这样文件损坏/格式不对时原项目保持原样，不会“打开失败还把画布清空了”
    CircuitDocument doc;
    wxString error, warning;
    if(!circuit_read(path, doc, error, warning)){
        wxMessageBox(error, wxString::FromUTF8("打开失败"), wxOK | wxICON_ERROR, frame);
        return false;
    }

    warning += circuit_import(doc, *canvas);   //把画布内容换成文件里的

    //项目信息沿用文件里的（保存时原样写回，只有 modified 会在保存时更新）
    project_name        = wxString::FromUTF8(doc.name.c_str());
    project_author      = wxString::FromUTF8(doc.author.c_str());
    project_created     = wxString::FromUTF8(doc.created.c_str());
    project_description = wxString::FromUTF8(doc.description.c_str());
    if(project_created.empty()) project_created = iso8601_now();

    if(!warning.empty()){
        wxMessageBox(warning, wxString::FromUTF8("已打开，但有需要注意的地方"),
                     wxOK | wxICON_WARNING, frame);
    }
    //刚打开的文件视为“未修改”，所以标题上没有*
    mark_saved(path, wxString::FromUTF8("已打开: ") + path);
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
    wxFileDialog dlg(frame, wxString::FromUTF8("另存为"), default_dir(), suggested,
                     xml_wildcard(), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if(dlg.ShowModal() != wxID_OK) return false;

    wxString path = dlg.GetPath();
    if(!path.Lower().EndsWith(wxT(".xml"))) path += wxT(".xml");   //用户没写扩展名时补上
    return write_to(path);
}

bool FileActions::write_to(const wxString& path){
    CircuitDocument doc;
    circuit_export(*canvas, doc);   //元件/导线/视口

    //项目信息：README 的 <info>。
    //项目名留空（新建后还没起名）时用文件名兜底；否则沿用已记录的项目名
    if(project_name.empty()) project_name = wxFileName(path).GetName();
    doc.name        = to_utf8(project_name);
    doc.author      = to_utf8(project_author);
    doc.created     = project_created.empty() ? to_utf8(iso8601_now()) : to_utf8(project_created);
    doc.modified    = to_utf8(iso8601_now());
    doc.description = to_utf8(project_description);

    wxString error;
    if(!circuit_write(doc, path, error)){
        wxMessageBox(error, wxString::FromUTF8("保存失败"), wxOK | wxICON_ERROR, frame);
        return false;   //保存失败：modified保持true，用户不会以为已经存好了
    }
    mark_saved(path, wxString::FromUTF8("已保存: ") + path);
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

wxString FileActions::default_dir() const{
    if(!current_path.empty()){
        const wxString dir = wxFileName(current_path).GetPath();
        if(!dir.empty() && wxDirExists(dir)) return dir;
    }
    return wxStandardPaths::Get().GetDocumentsDir();
}

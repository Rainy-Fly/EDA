#include"./FileActions.hpp"
#include"./OpenLocationDialog.hpp"
#include"./MenuBar.hpp"          //ID_SET_DEFAULT_DIR
#include"../Canva/Canvas.hpp"

#include<wx/filedlg.h>      //wxFileDialog
#include<wx/dirdlg.h>       //wxDirDialog
#include<wx/filename.h>
#include<wx/msgdlg.h>       //wxMessageBox
#include<wx/stdpaths.h>
#include<wx/filefn.h>       //wxDirExists
#include<wx/utils.h>        //wxGetUserName

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
    if(!AppSettings::Save(settings)){
        wxMessageBox(wxString::FromUTF8("配置保存失败，默认目录只在本次运行内有效：") +
                         AppSettings::ConfigPath(),
                     wxString::FromUTF8("保存设置失败"), wxOK | wxICON_WARNING, frame);
        return;
    }
    if(frame){
        frame->SetStatusText(wxString::FromUTF8("默认目录已设为: ") +
                             wxString::FromUTF8(settings.default_dir.c_str()));
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
    AppSettings::Save(settings);   //退出前把设置落盘（默认目录/最近位置）
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
    //所以文件损坏/格式不对时画布保持原样（不会“打开失败还把画布清空了”）
    DealProjectXML::ProjectInfo loaded;
    if(!DealProjectXML::LoadProject(canvas, to_utf8(path), &loaded)){
        wxMessageBox(wxString::FromUTF8("无法读取该文件（不是有效的 TinyEDA 项目文件，或文件已损坏）：") + path,
                     wxString::FromUTF8("打开失败"), wxOK | wxICON_ERROR, frame);
        return false;
    }
    info = loaded;

    remember_path(path);
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

    if(!DealProjectXML::SaveProject(canvas, to_utf8(path), info)){
        wxMessageBox(wxString::FromUTF8("写入文件失败（路径不存在、文件被占用或没有写权限）：") + path,
                     wxString::FromUTF8("保存失败"), wxOK | wxICON_ERROR, frame);
        return false;   //保存失败：modified保持true，用户不会以为已经存好了
    }
    remember_path(path);
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

//记住这次打开/保存的位置（“从最近位置打开”用），并立即写回配置文件
void FileActions::remember_path(const wxString& path){
    settings.last_file = to_utf8(path);
    settings.last_dir  = to_utf8(wxFileName(path).GetPath());
    AppSettings::Save(settings);
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

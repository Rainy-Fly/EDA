#include"./AppSettings.hpp"

#include<wx/xml/xml.h>
#include<wx/filename.h>
#include<wx/stdpaths.h>
#include<wx/filefn.h>
#include<wx/log.h>       //wxLogNull：屏蔽 wxWidgets 自带的模态错误弹窗

namespace{

//wxString <-> std::string 一律显式走 UTF-8（ToStdString 会按本地代码页转，中文路径会坏）
std::string to_utf8(const wxString& s){
    const wxScopedCharBuffer buf = s.utf8_str();
    return std::string(buf.data(), buf.length());
}

wxString from_utf8(const std::string& s){
    return wxString::FromUTF8(s.c_str(), s.length());
}

//追加一个元素子节点。
//wxWidgets 的坑：带 parent 的 wxXmlNode 构造函数会把节点插到子列表最前面，AddChild 才是追加，
//所以统一“先建无父节点、再 AddChild”，保证文件里的元素顺序与代码一致
wxXmlNode* append_element(wxXmlNode* parent, const wxString& tag){
    wxXmlNode* node = new wxXmlNode(wxXML_ELEMENT_NODE, tag);
    parent->AddChild(node);
    return node;
}

} // namespace

namespace AppSettings{

wxString ConfigPath(){
    //用户配置目录：%APPDATA%（Roaming）。特意不依赖 GetUserDataDir()，
    //因为它取的是应用名（可执行文件名），改名就会换位置
    return wxStandardPaths::Get().GetUserConfigDir() + wxFILE_SEP_PATH +
           wxT("TinyEDA") + wxFILE_SEP_PATH + wxT("settings.xml");
}

namespace{

// %APPDATA% 不可写时的退路：写到可执行文件旁边（受限环境/便携部署也能存设置）
wxString fallback_path(){
    return wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath() +
           wxFILE_SEP_PATH + wxT("tinyeda-settings.xml");
}

// 从指定文件读设置；成功返回true
bool load_from(const wxString& path, Settings& s){
    if(!wxFileExists(path)) return false;
    wxXmlDocument doc;
    if(!doc.Load(path)) return false;   //文件损坏：按默认值处理
    wxXmlNode* root = doc.GetRoot();
    if(!root || root->GetName() != wxT("tinyeda-settings")) return false;

    for(wxXmlNode* n = root->GetChildren(); n; n = n->GetNext()){
        if(n->GetType() != wxXML_ELEMENT_NODE) continue;   //跳过注释等
        const wxString tag = n->GetName();
        if(tag == wxT("recent")){
            s.last_file = to_utf8(n->GetAttribute(wxT("file"), wxString()));
            s.last_dir  = to_utf8(n->GetAttribute(wxT("dir"),  wxString()));
        }else if(tag == wxT("default-dir")){
            s.default_dir = to_utf8(n->GetAttribute(wxT("path"), wxString()));
        }
        //其它元素忽略（以后加设置项时老版本也能读）
    }
    return true;
}

// 把设置写到指定文件（目录不存在则创建）；成功返回true
bool save_to(const Settings& s, const wxString& path){
    wxFileName fn(path);
    if(!fn.DirExists()){
        if(!fn.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) return false;   //配置目录不存在则创建
    }

    wxXmlDocument doc;
    doc.SetVersion(wxT("1.0"));
    doc.SetFileEncoding(wxT("UTF-8"));   //有中文路径，必须显式UTF-8
    wxXmlNode* root = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("tinyeda-settings"));
    root->AddAttribute(wxT("version"), wxT("1"));
    doc.SetRoot(root);

    if(!s.last_file.empty() || !s.last_dir.empty()){
        wxXmlNode* recent = append_element(root, wxT("recent"));
        recent->AddAttribute(wxT("file"), from_utf8(s.last_file));
        recent->AddAttribute(wxT("dir"),  from_utf8(s.last_dir));
    }
    if(!s.default_dir.empty()){
        wxXmlNode* dd = append_element(root, wxT("default-dir"));
        dd->AddAttribute(wxT("path"), from_utf8(s.default_dir));
    }

    return doc.Save(path, 2);
}

} // namespace

Settings Load(){
    Settings s;
    //wxWidgets 在文件读不了/XML解析失败时会自己弹一个模态错误框（标题是“Tinyeda Error”）。
    //配置文件读不了不该打断用户，这里屏蔽掉，按默认值处理
    wxLogNull no_log;

    if(!load_from(ConfigPath(), s)){
        load_from(fallback_path(), s);   //首选位置没有/坏了，看退路位置
    }

    //目录已经不存在了就当没设置过（例如U盘/网络盘拔掉了，或目录被删）
    if(!s.last_dir.empty() && !wxDirExists(from_utf8(s.last_dir))) s.last_dir.clear();
    if(!s.default_dir.empty() && !wxDirExists(from_utf8(s.default_dir))) s.default_dir.clear();
    return s;
}

bool Save(const Settings& s, wxString* written_path){
    if(written_path) written_path->clear();

    //同上：Mkdir/Save 失败时 wxWidgets 默认会弹“Tinyeda Error”框。
    //配置文件写不进去（受限环境、没权限）只是设置没保存，不该在退出时弹框打断用户，
    //这里屏蔽掉，由调用方决定是否提示
    wxLogNull no_log;

    const wxString primary = ConfigPath();
    if(save_to(s, primary)){
        if(written_path) *written_path = primary;
        return true;
    }

    //首选位置不可写：退回可执行文件旁边，保证设置仍然能保存
    const wxString fallback = fallback_path();
    if(save_to(s, fallback)){
        if(written_path) *written_path = fallback;
        return true;
    }
    return false;
}

wxString ProjectRootDir(){
    //当前工作目录就是仓库根（命令行/IDE从仓库根启动的情况）
    if(wxDirExists(wxT("src"))) return wxFileName::GetCwd();

    //从可执行文件所在目录向上找含 src/ 的目录（build/Debug -> build -> 仓库根）
    const wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    wxString dir = exe.GetPath();
    for(int i = 0; i < 4 && !dir.empty(); ++i){
        if(wxDirExists(dir + wxFILE_SEP_PATH + wxT("src"))) return dir;
        const wxString up = wxFileName(dir).GetPath();   //上一级目录
        if(up == dir) break;                             //已到盘根
        dir = up;
    }
    return exe.GetPath();   //都没找到：退回可执行文件所在目录
}

} // namespace AppSettings

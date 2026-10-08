#include"./AppSettings.hpp"

#include<wx/xml/xml.h>
#include<wx/filename.h>
#include<wx/stdpaths.h>
#include<wx/utils.h>   //wxGetEnv
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

// 用户配置目录，按平台惯例显式指定（不依赖 wxStandardPaths 的默认布局，
// 它的默认 Classic 布局在 Linux 上会落在 $HOME 下，不符合 XDG 惯例）：
//   Windows: %APPDATA%          -> %APPDATA%\TinyEDA\settings.xml
//   macOS  : ~/Library/Preferences
//   Linux  : XDG 配置目录       -> ~/.config/TinyEDA/settings.xml
//            （尊重 XDG_CONFIG_HOME 环境变量；不依赖 wxApp，可被测试直接调用）
wxString UserConfigDir(){
#if defined(_WIN32)
    // Windows：%APPDATA%（Roaming），与 wx msw 的 CSIDL_APPDATA 一致
    return wxStandardPaths::Get().GetUserConfigDir();
#elif defined(__WXMAC__)
    // macOS 惯例：~/Library/Preferences
    return wxStandardPaths::Get().GetUserConfigDir();
#else
    // Linux：XDG 约定（~/.config，或用户自定义的 XDG_CONFIG_HOME）
    wxString dir;
    if(!wxGetEnv("XDG_CONFIG_HOME", &dir) || dir.empty()){
        dir = wxFileName::GetHomeDir() + wxFILE_SEP_PATH + wxT(".config");
    }
    return dir + wxFILE_SEP_PATH + wxT("TinyEDA");
#endif
}

// 老版本（改用 XDG 之前）Linux 上配置落在 $HOME/TinyEDA/settings.xml。
// 读取时作为兜底迁移源：新位置没有才去老位置读，保证切目录不丢设置；
// 写入始终写到新位置。Windows/macOS 从未换过位置，返回空。
wxString legacy_config_path(){
#ifndef _WIN32
    return wxFileName::GetHomeDir() + wxFILE_SEP_PATH + wxT("TinyEDA") +
           wxFILE_SEP_PATH + wxT("settings.xml");
#else
    return wxString();
#endif
}

wxString ConfigPath(){
    return UserConfigDir() + wxFILE_SEP_PATH + wxT("settings.xml");
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
        }else if(tag == wxT("apis")){
            for(wxXmlNode* a = n->GetChildren(); a; a = a->GetNext()){
                if(a->GetType() != wxXML_ELEMENT_NODE || a->GetName() != wxT("api")) continue;
                ApiProfile p;
                p.name     = to_utf8(a->GetAttribute(wxT("name"),     wxString()));
                p.base_url = to_utf8(a->GetAttribute(wxT("base-url"), wxString()));
                p.model    = to_utf8(a->GetAttribute(wxT("model"),    wxString()));
                p.api_key  = to_utf8(a->GetAttribute(wxT("key"),      wxString()));
                if(!p.name.empty()) s.apis.push_back(std::move(p));   //无名的配置忽略
            }
        }else if(tag == wxT("active-api")){
            s.active_api = to_utf8(n->GetAttribute(wxT("name"), wxString()));
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
    if(!s.apis.empty()){
        wxXmlNode* apis = append_element(root, wxT("apis"));
        for(const ApiProfile& p : s.apis){
            wxXmlNode* api = append_element(apis, wxT("api"));
            api->AddAttribute(wxT("name"),     from_utf8(p.name));
            api->AddAttribute(wxT("base-url"), from_utf8(p.base_url));
            api->AddAttribute(wxT("model"),    from_utf8(p.model));
            api->AddAttribute(wxT("key"),      from_utf8(p.api_key));
        }
    }
    if(!s.active_api.empty()){
        wxXmlNode* act = append_element(root, wxT("active-api"));
        act->AddAttribute(wxT("name"), from_utf8(s.active_api));
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
        if(!load_from(legacy_config_path(), s)){   //老位置（Linux Classic布局）兜底迁移
            load_from(fallback_path(), s);          //再退回可执行文件旁边
        }
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

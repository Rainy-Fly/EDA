#pragma once
#include<string>
#include<vector>
#include<wx/string.h>

// 用户设置（配置文件），xml 格式，存在各平台的“用户配置目录”下（跨平台通用）：
//   Windows: %APPDATA%\TinyEDA\settings.xml          （Roaming）
//   macOS  : ~/Library/Preferences/TinyEDA/settings.xml
//   Linux  : ~/.config/TinyEDA/settings.xml           （尊重 XDG_CONFIG_HOME）
// 主程序启动时（FileActions 构造时）加载，设置改变或打开/保存项目后立即写回。
// 内容示例：
//   <?xml version="1.0" encoding="UTF-8"?>
//   <tinyeda-settings version="1">
//     <recent file="C:\work\a.xml" dir="C:\work"/>
//     <default-dir path="D:\EDA\projects"/>
//     <apis>
//       <api name="DeepSeek" base-url="https://api.deepseek.com"
//            model="deepseek-chat" key="sk-xxx"/>
//       <api name="本地" base-url="http://127.0.0.1:11434" model="llama3"/>
//     </apis>
//     <active-api name="DeepSeek"/>
//   </tinyeda-settings>
// 读取容错：文件不存在/损坏/元素缺失一律按默认值处理，不报错。
namespace AppSettings{

// 一个可用的 AI API 配置（用户可添加多个，选择其中一个作为当前使用的）
struct ApiProfile{
    std::string name;      //配置名（用户自定义，如 "DeepSeek"）
    std::string base_url;  //接口地址，如 https://api.deepseek.com
    std::string model;     //模型名，如 deepseek-chat
    std::string api_key;   //API Key（可为空，例如本地 ollama 不需要）
};

struct Settings{
    std::string last_file;    //上次打开/保存的项目文件（完整路径）
    std::string last_dir;     //上次打开/保存所在目录（“从最近位置打开”用）
    std::string default_dir;  //用户设置的默认目录（菜单“设置默认目录”）
    std::vector<ApiProfile> apis;  //用户添加的 AI API 配置（多API）
    std::string active_api;        //当前选中的 API 配置名（apis 中某个 name）
};

// 配置文件完整路径（不保证文件已存在）
wxString ConfigPath();

// 读取配置：文件不存在/损坏时返回全空设置（不报错）。
// 目录已不存在时对应字段会被清空，避免“最近位置”指向已删除的目录。
// 读取过程不会弹任何对话框（内部屏蔽了 wxWidgets 自带的日志弹窗）
Settings Load();

// 写入配置（目录不存在会自动创建）；失败返回false。
// 首选写到 ConfigPath()；如果那个位置不可写（受限环境/无权限），自动退回写到
// 可执行文件旁边的 tinyeda-settings.xml。
// written_path 非空时返回实际写入的路径，便于调用方提示用户。
// 本函数不会弹任何对话框（wxWidgets 自带的“Tinyeda Error”弹窗被屏蔽），
// 失败由调用方决定怎么提示
bool Save(const Settings& s, wxString* written_path = nullptr);

// 项目根目录：优先“当前工作目录含 src/”，否则从可执行文件所在目录向上找含 src/ 的目录
// （build/Debug -> build -> 仓库根），最后退回可执行文件所在目录
wxString ProjectRootDir();

} // namespace AppSettings

#pragma once
#include<string>
#include<wx/string.h>

// 用户设置（配置文件），xml 格式，存在用户配置目录下：
//   Windows: %APPDATA%\TinyEDA\settings.xml
// 主程序启动时（FileActions 构造时）加载，设置改变或打开/保存项目后立即写回。
// 内容示例：
//   <?xml version="1.0" encoding="UTF-8"?>
//   <tinyeda-settings version="1">
//     <recent file="C:\work\a.xml" dir="C:\work"/>
//     <default-dir path="D:\EDA\projects"/>
//   </tinyeda-settings>
// 读取容错：文件不存在/损坏/元素缺失一律按默认值处理，不报错。
namespace AppSettings{

struct Settings{
    std::string last_file;    //上次打开/保存的项目文件（完整路径）
    std::string last_dir;     //上次打开/保存所在目录（“从最近位置打开”用）
    std::string default_dir;  //用户设置的默认目录（菜单“设置默认目录”）
};

// 配置文件完整路径（不保证文件已存在）
wxString ConfigPath();

// 读取配置：文件不存在/损坏时返回全空设置（不报错）。
// 目录已不存在时对应字段会被清空，避免“最近位置”指向已删除的目录
Settings Load();

// 写入配置（目录不存在会自动创建）；失败返回false
bool Save(const Settings& s);

// 项目根目录：优先“当前工作目录含 src/”，否则从可执行文件所在目录向上找含 src/ 的目录
// （build/Debug -> build -> 仓库根），最后退回可执行文件所在目录
wxString ProjectRootDir();

} // namespace AppSettings

#pragma once
#include<wx/wx.h>
#include"./../DealProjectXML/DealProjectXML.hpp"
#include"./../Settings/AppSettings.hpp"

class Canvas;

// 文件菜单的实际功能：新建 / 打开 / 保存 / 另存为 / 设置默认目录 / 退出，
// 以及关闭窗口时的未保存确认。
//
// 菜单结构由 MenuBar（src/MenuBar/MenuBar.cpp）负责，本类不自己建菜单栏，只往宿主窗口上
// 挂事件处理函数，因此换成别的主窗口也能一行接入。
// 项目文件的读写调用伙伴的 src/DealProjectXML 模块（不再自己实现xml读写）。
// 用户设置（默认目录、上次打开的项目）存在 src/Settings/AppSettings 的配置文件里，
// 本对象构造时加载、改动后立即写回，所以“主程序启动即加载配置”。
//
// 生命周期：本对象必须在宿主窗口之前销毁（demoMainFrame 里用 unique_ptr 成员，
// 成员先于基类 wxFrame 析构）；析构时会解绑自己注册的处理函数与画布回调，避免悬空调用。
class FileActions{
public:
    FileActions(wxFrame* frame, Canvas* canvas);
    ~FileActions();

    //当前项目文件路径（空 = 还没保存过的新项目）
    const wxString& get_current_path() const { return current_path; }
    //项目是否有未保存的修改
    bool is_modified() const { return modified; }
    //外部（如属性栏编辑属性）想标记“有未保存的修改”时调用
    void mark_modified();

private:
    wxFrame* frame;                   //宿主窗口（不拥有）
    Canvas* canvas;                   //画布（不拥有，由宿主窗口管理）
    wxString current_path;            //当前项目文件路径；空表示尚未保存过
    AppSettings::Settings settings;   //用户设置（配置文件内容，构造时加载）
    DealProjectXML::ProjectInfo info; //当前项目信息（README 的 <info>）
    bool modified;                    //是否有未保存的修改

    //菜单动作
    void on_new(wxCommandEvent& event);
    void on_open(wxCommandEvent& event);
    void on_save(wxCommandEvent& event);
    void on_save_as(wxCommandEvent& event);
    void on_set_default_dir(wxCommandEvent& event);
    void on_exit(wxCommandEvent& event);
    void on_close(wxCloseEvent& event);

    //“当前项目尚未保存，是否保存？”三态确认。
    //返回false表示用户取消（选了取消，或选了保存但保存/另存为没成功），调用方应中止当前操作
    bool confirm_discard_or_save();

    void new_project();                  //新建空项目（不弹确认，调用方已确认过）
    bool open_project();                 //打开项目；false=用户取消或读取失败（画布保持原样）
    bool save_project();                 //保存；没有路径时转“另存为”
    bool save_project_as();              //另存为
    bool write_to(const wxString& path); //按指定路径写出文件（不弹对话框）

    void set_modified(bool value);
    //写完/读完文件后统一更新：当前路径、标题栏（*标记）、状态栏提示
    void mark_saved(const wxString& path, const wxString& status_message);
    void update_title();
    //记住这次打开/保存的位置，并写回配置文件
    void remember_path(const wxString& path);
    //“另存为/打开”文件对话框的起始目录：最近位置 > 默认目录 > 文档目录
    wxString start_dir() const;
};

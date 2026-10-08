#pragma once
#include<wx/wx.h>
#include<wx/listbox.h>
#include<functional>
#include<string>
#include<vector>
#include"../../Settings/AppSettings.hpp"

namespace Agent{

// 设置面板：管理多个 AI API 配置（名称/接口地址/模型/Key），
// 可添加/删除/选择，改动保存进用户配置文件（AppSettings::settings.xml）。
class SettingPanel : public wxPanel{
public:
    SettingPanel(wxWindow* parent);

    //把当前选中的配置推给 Controller（聊天立即用新配置）
    void SetProfileConsumer(std::function<void(const AppSettings::ApiProfile&)> consumer);

private:
    void ReloadList();               //按 apis_ 刷新列表
    void LoadFieldsToUi();           //把选中配置填进输入框
    void UiToSelected();             //输入框写回选中的配置
    void OnSelect(wxCommandEvent& e);
    void OnAdd(wxCommandEvent& e);
    void OnDelete(wxCommandEvent& e);
    void OnSave(wxCommandEvent& e);

    std::vector<AppSettings::ApiProfile> apis_;
    std::string active_name_;
    std::function<void(const AppSettings::ApiProfile&)> consumer_;

    wxListBox* list_;
    wxTextCtrl* name_ctrl_;
    wxTextCtrl* base_ctrl_;
    wxTextCtrl* model_ctrl_;
    wxTextCtrl* key_ctrl_;
    wxStaticText* status_;

    wxDECLARE_EVENT_TABLE();
};

} // namespace Agent

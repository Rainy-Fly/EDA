#include"./Setting.hpp"

#include<wx/stattext.h>
#include<wx/button.h>
#include<wx/sizer.h>
#include<wx/msgdlg.h>

namespace Agent{

BEGIN_EVENT_TABLE(SettingPanel, wxPanel)
    EVT_LISTBOX(wxID_ANY, SettingPanel::OnSelect)
    EVT_BUTTON(wxID_ADD, SettingPanel::OnAdd)
    EVT_BUTTON(wxID_DELETE, SettingPanel::OnDelete)
    EVT_BUTTON(wxID_SAVE, SettingPanel::OnSave)
END_EVENT_TABLE()

SettingPanel::SettingPanel(wxWindow* parent) : wxPanel(parent){
    //从用户配置文件读入多API配置
    const AppSettings::Settings s = AppSettings::Load();
    apis_ = s.apis;
    active_name_ = s.active_api;
    if(apis_.empty()){
        //默认给一个 DeepSeek 配置（key 留空，用户填）
        AppSettings::ApiProfile p;
        p.name = "DeepSeek";
        p.base_url = "https://api.deepseek.com";
        p.model = "deepseek-chat";
        p.api_key = "";
        apis_.push_back(p);
        active_name_ = p.name;
    }

    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

    list_ = new wxListBox(this, wxID_ANY);
    root->Add(list_, 1, wxEXPAND | wxALL, 6);

    //输入区：名称 / 接口地址 / 模型 / Key
    wxFlexGridSizer* grid = new wxFlexGridSizer(4, 2, 6, 10);
    grid->AddGrowableCol(1, 1);
    grid->Add(new wxStaticText(this, wxID_ANY, wxString::FromUTF8("名称:")), 0, wxALIGN_CENTER_VERTICAL);
    name_ctrl_ = new wxTextCtrl(this, wxID_ANY);
    grid->Add(name_ctrl_, 0, wxEXPAND);

    grid->Add(new wxStaticText(this, wxID_ANY, wxString::FromUTF8("接口地址:")), 0, wxALIGN_CENTER_VERTICAL);
    base_ctrl_ = new wxTextCtrl(this, wxID_ANY);
    grid->Add(base_ctrl_, 0, wxEXPAND);

    grid->Add(new wxStaticText(this, wxID_ANY, wxString::FromUTF8("模型:")), 0, wxALIGN_CENTER_VERTICAL);
    model_ctrl_ = new wxTextCtrl(this, wxID_ANY);
    grid->Add(model_ctrl_, 0, wxEXPAND);

    grid->Add(new wxStaticText(this, wxID_ANY, wxString::FromUTF8("API Key:")), 0, wxALIGN_CENTER_VERTICAL);
    key_ctrl_ = new wxTextCtrl(this, wxID_ANY);
    grid->Add(key_ctrl_, 0, wxEXPAND);
    root->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);

    //按钮行
    wxBoxSizer* btns = new wxBoxSizer(wxHORIZONTAL);
    btns->Add(new wxButton(this, wxID_ADD,    wxString::FromUTF8("添加")), 0, wxRIGHT, 6);
    btns->Add(new wxButton(this, wxID_DELETE, wxString::FromUTF8("删除")), 0, wxRIGHT, 6);
    btns->Add(new wxButton(this, wxID_SAVE,   wxString::FromUTF8("保存并设为当前")), 0);
    root->Add(btns, 0, wxALL, 6);

    status_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    root->Add(status_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

    SetSizer(root);
    ReloadList();
    LoadFieldsToUi();
}

void SettingPanel::SetProfileConsumer(std::function<void(const AppSettings::ApiProfile&)> consumer){
    consumer_ = std::move(consumer);
}

void SettingPanel::ReloadList(){
    list_->Clear();
    for(const auto& p : apis_) list_->Append(wxString::FromUTF8(p.name));
    //选中当前 active 的配置（列表第一项作为兜底）
    int sel = 0;
    for(int i = 0; i < (int)apis_.size(); ++i){
        if(apis_[i].name == active_name_){ sel = i; break; }
    }
    if(sel < (int)apis_.size()) list_->SetSelection(sel);
}

void SettingPanel::LoadFieldsToUi(){
    const int sel = list_->GetSelection();
    if(sel < 0 || sel >= (int)apis_.size()) return;
    const AppSettings::ApiProfile& p = apis_[sel];
    name_ctrl_->SetValue(wxString::FromUTF8(p.name));
    base_ctrl_->SetValue(wxString::FromUTF8(p.base_url));
    model_ctrl_->SetValue(wxString::FromUTF8(p.model));
    key_ctrl_->SetValue(wxString::FromUTF8(p.api_key));
    status_->SetLabel(active_name_ == p.name
                          ? wxString::FromUTF8("当前使用：") + wxString::FromUTF8(p.name)
                          : wxString::FromUTF8("（未设为当前）"));
}

void SettingPanel::UiToSelected(){
    const int sel = list_->GetSelection();
    if(sel < 0 || sel >= (int)apis_.size()) return;
    AppSettings::ApiProfile& p = apis_[sel];
    p.name     = std::string(name_ctrl_->GetValue().utf8_str().data());
    p.base_url = std::string(base_ctrl_->GetValue().utf8_str().data());
    p.model    = std::string(model_ctrl_->GetValue().utf8_str().data());
    p.api_key  = std::string(key_ctrl_->GetValue().utf8_str().data());
}

void SettingPanel::OnSelect(wxCommandEvent&){
    UiToSelected();        //先把输入框内容留在旧选中项上
    LoadFieldsToUi();
}

void SettingPanel::OnAdd(wxCommandEvent&){
    UiToSelected();
    //新配置：DeepSeek 默认值，名字不重名
    AppSettings::ApiProfile p;
    int n = 1;
    while(true){
        p.name = "API " + std::to_string(n);
        bool dup = false;
        for(const auto& a : apis_) if(a.name == p.name){ dup = true; break; }
        if(!dup) break;
        ++n;
    }
    p.base_url = "https://api.deepseek.com";
    p.model = "deepseek-chat";
    apis_.push_back(p);
    ReloadList();
    list_->SetSelection((int)apis_.size() - 1);
    LoadFieldsToUi();
}

void SettingPanel::OnDelete(wxCommandEvent&){
    const int sel = list_->GetSelection();
    if(sel < 0 || sel >= (int)apis_.size()) return;
    const std::string name = apis_[sel].name;
    apis_.erase(apis_.begin() + sel);
    if(active_name_ == name){
        active_name_ = apis_.empty() ? "" : apis_.front().name;
    }
    //持久化
    AppSettings::Settings s = AppSettings::Load();
    s.apis = apis_;
    s.active_api = active_name_;
    AppSettings::Save(s);
    ReloadList();
    LoadFieldsToUi();
}

void SettingPanel::OnSave(wxCommandEvent&){
    if(list_->GetSelection() < 0 || apis_.empty()){
        wxMessageBox(wxString::FromUTF8("没有可保存的配置"), wxString::FromUTF8("提示"),
                     wxOK | wxICON_INFORMATION, this);
        return;
    }
    UiToSelected();
    AppSettings::ApiProfile& p = apis_[list_->GetSelection()];
    if(p.name.empty()){
        wxMessageBox(wxString::FromUTF8("名称不能为空"), wxString::FromUTF8("提示"),
                     wxOK | wxICON_WARNING, this);
        return;
    }
    active_name_ = p.name;
    ReloadList();
    LoadFieldsToUi();

    //持久化到用户配置文件
    AppSettings::Settings s = AppSettings::Load();
    s.apis = apis_;
    s.active_api = active_name_;
    wxString written;
    const bool ok = AppSettings::Save(s, &written);
    status_->SetLabel(ok
        ? wxString::FromUTF8("已保存，当前使用：") + wxString::FromUTF8(p.name)
        : wxString::FromUTF8("保存失败（配置目录不可写）"));

    //通知聊天侧立即切换配置
    if(consumer_) consumer_(p);
}

} // namespace Agent

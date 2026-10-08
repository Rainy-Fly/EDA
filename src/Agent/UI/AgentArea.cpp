#include"./AgentArea.hpp"

namespace Agent{

AgentArea::AgentArea(wxWindow* parent, Canvas* canvas)
    : wxPanel(parent){
    notebook_ = new wxNotebook(this, wxID_ANY);

    chat_ = new ChatPanel(notebook_);
    setting_ = new SettingPanel(notebook_);
    notebook_->AddPage(chat_, wxString::FromUTF8("对话"));
    notebook_->AddPage(setting_, wxString::FromUTF8("设置"));

    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
    root->Add(notebook_, 1, wxEXPAND);
    SetSizer(root);

    //Controller：事件发给 ChatPanel；设置面板改配置后推给 Controller
    ctl_ = std::make_unique<Controller>(chat_, canvas);
    chat_->SetController(ctl_.get());
    setting_->SetProfileConsumer([this](const AppSettings::ApiProfile& p){
        if(ctl_) ctl_->SetProfile(p);
    });
}

AgentArea::~AgentArea(){
    //析构顺序：先停 Controller（join工作线程），再销毁子窗口
    ctl_.reset();
}

} // namespace Agent

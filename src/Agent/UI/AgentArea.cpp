#include"./AgentArea.hpp"
#include"../../Theme/Theme.hpp"

#include<wx/simplebook.h>
#include<wx/sizer.h>

namespace Agent{

AgentArea::AgentArea(wxWindow* parent, Canvas* canvas)
    : CardPanel(parent, wxID_ANY, wxString::FromUTF8("AI 助手"), true,
                parent->FromDIP(UiTheme::SpaceXs)),
      book_(nullptr),
      chat_(nullptr),
      setting_(nullptr),
      tab_chat_(nullptr),
      tab_setting_(nullptr){
    wxBoxSizer* body = Content();

    //分段页签：用扁平 ThemedButton 代替原生 notebook 标签，和整体圆角/动效一致
    wxBoxSizer* tabs = new wxBoxSizer(wxHORIZONTAL);
    tab_chat_ = new ThemedButton(this, wxID_ANY, wxString::FromUTF8("对话"), false);
    tab_chat_->SetFlat(true);
    tab_chat_->SetMinSize(wxSize(FromDIP(64), FromDIP(30)));
    tab_chat_->SetClickHandler([this]{ SelectTab(0); });
    tabs->Add(tab_chat_, 0, wxRIGHT, FromDIP(UiTheme::SpaceXs));

    tab_setting_ = new ThemedButton(this, wxID_ANY, wxString::FromUTF8("设置"), false);
    tab_setting_->SetFlat(true);
    tab_setting_->SetMinSize(wxSize(FromDIP(64), FromDIP(30)));
    tab_setting_->SetClickHandler([this]{ SelectTab(1); });
    tabs->Add(tab_setting_, 0);
    body->Add(tabs, 0, wxBOTTOM, FromDIP(UiTheme::SpaceSm));

    book_ = new wxSimplebook(this, wxID_ANY);
    book_->SetBackgroundColour(UiTheme::Surface);
    chat_ = new ChatPanel(book_);
    setting_ = new SettingPanel(book_);
    book_->AddPage(chat_, wxString::FromUTF8("对话"));
    book_->AddPage(setting_, wxString::FromUTF8("设置"));
    body->Add(book_, 1, wxEXPAND);

    SelectTab(0);

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

void AgentArea::SelectTab(int index){
    if(!book_) return;
    if(index < 0 || index > 1) return;
    book_->SetSelection(index);
    if(tab_chat_) tab_chat_->SetSelected(index == 0);
    if(tab_setting_) tab_setting_->SetSelected(index == 1);
}

} // namespace Agent

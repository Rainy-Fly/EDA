#pragma once
#include<wx/wx.h>
#include<memory>
#include"../Controller.hpp"
#include"../../Theme/CardPanel.hpp"
#include"../../Theme/ThemedButton.hpp"
#include"./ChatPanel.hpp"
#include"./Setting.hpp"

class Canvas;
class wxSimplebook;

namespace Agent{

// AI 窗口整体：一个带“对话/设置”两个分段页签的卡片面板，
// 持有 ChatPanel、SettingPanel 与 Controller，并在销毁时安全回收 Controller。
class AgentArea : public CardPanel{
public:
    AgentArea(wxWindow* parent, Canvas* canvas);
    ~AgentArea() override;

    ChatPanel* Chat(){ return chat_; }
    SettingPanel* Setting(){ return setting_; }
    Controller* Ctl(){ return ctl_.get(); }

private:
    void SelectTab(int index);

    wxSimplebook* book_;
    ChatPanel* chat_;
    SettingPanel* setting_;
    ThemedButton* tab_chat_;
    ThemedButton* tab_setting_;
    std::unique_ptr<Controller> ctl_;
};

} // namespace Agent

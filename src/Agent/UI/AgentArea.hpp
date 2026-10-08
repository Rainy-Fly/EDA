#pragma once
#include<wx/wx.h>
#include<wx/notebook.h>
#include<memory>
#include"../Controller.hpp"
#include"./ChatPanel.hpp"
#include"./Setting.hpp"

class Canvas;

namespace Agent{

// AI 窗口整体：一个带“对话/设置”两个标签页的面板，
// 持有 ChatPanel、SettingPanel 与 Controller，并在销毁时安全回收 Controller。
class AgentArea : public wxPanel{
public:
    AgentArea(wxWindow* parent, Canvas* canvas);
    ~AgentArea() override;

    ChatPanel* Chat(){ return chat_; }
    SettingPanel* Setting(){ return setting_; }
    Controller* Ctl(){ return ctl_.get(); }

private:
    wxNotebook* notebook_;
    ChatPanel* chat_;
    SettingPanel* setting_;
    std::unique_ptr<Controller> ctl_;
};

} // namespace Agent

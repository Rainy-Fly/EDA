#include"./Show.hpp"

namespace Agent{

Show::Show(wxSplitterWindow* host, wxWindow* work, wxWindow* agent)
    : host_(host), work_(work), agent_(agent){
    //建立初始布局：work 左、agent 右（负值=按比例），随后默认隐藏
    host_->SplitVertically(work_, agent_, -180);
    visible_ = true;
    SetVisible(false);
}

void Show::Toggle(){
    SetVisible(!visible_);
}

void Show::SetVisible(bool on){
    if(on == visible_) return;
    visible_ = on;
    if(visible_){
        //显示：工作区在左、Agent 在右，可拉伸（DoSplit 会自动 Show 两个窗格）
        host_->SplitVertically(work_, agent_, -180);
    }else{
        //隐藏：从分隔器摘除（不销毁窗口），工作区占满；被摘除的窗口不会自动隐藏
        host_->Unsplit(agent_);
        agent_->Hide();
    }
}

} // namespace Agent

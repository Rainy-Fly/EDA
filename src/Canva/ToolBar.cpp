#include"./ToolBar.hpp"
#include"./Canvas.hpp"
#include"./ItemSVG.hpp"
#include<wx/statline.h>

//Windows 的 winnt.h 把 DELETE 定义成访问权限宏 (0x00010000L)，
//与 ToolAction::DELETE 冲突（MSVC下会报语法错误）。本文件用不到该宏，这里直接取消。
//头文件里的枚举声明另有 push_macro/undef/pop_macro 保护
#ifdef DELETE
    #undef DELETE
#endif

ToolBar::ToolBar(wxWindow* parent, Canvas* cvs)
    : wxPanel(parent, wxID_ANY), canvas(cvs), selected(nullptr){
    SetBackgroundColour(wxColour(235, 235, 235));
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    add_tool(sizer, "与非门", ItemType::NAND);
    add_tool(sizer, "与门",   ItemType::AND);
    add_tool(sizer, "或门",   ItemType::OR);
    add_tool(sizer, "或非门", ItemType::NOR);
    add_tool(sizer, "异或门", ItemType::XOR);
    add_tool(sizer, "非门",   ItemType::NOT);
    add_tool(sizer, "导线",   ItemType::WIRE);
    //动作工具与元件工具用竖线分隔
    sizer->Add(new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxSize(1, 24),
                                wxLI_VERTICAL),
               0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, 4);
    add_action_button(sizer, "选择", ToolAction::SELECT);
    add_action_button(sizer, "删除", ToolAction::ERASE);
    add_action_button(sizer, "克隆", ToolAction::CLONE);
    SetSizer(sizer);
    Layout();
    Fit();
    Move(8, 8);    //放在画布左上角
    Raise();       //始终在元件之上
    //滚轮/移动事件不会自动传播到父窗口，转发给Canvas（保证虚影跟随、缩放不因悬停工具栏而中断）
    Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& e){ canvas->on_mouse_scroll(e); });
    Bind(wxEVT_MOTION,     [this](wxMouseEvent& e){ canvas->on_mouse_move(e); });
}

void ToolBar::add_tool(wxSizer* sizer, const std::string& label, ItemType type){
    wxButton* btn = new wxButton(this, wxID_ANY, wxString::FromUTF8(label));
    //用元件SVG做按钮图标（来自共享缓存，不释放）；render_svg去掉scale factor，
    //否则GetBitmap(24)会按逻辑尺寸150绘制，图标又大又糊
    const wxBitmapBundle* bundle = ItemSVG_bundle(label);
    if(bundle && bundle->IsOk()){
        btn->SetBitmap(wxBitmapBundle::FromBitmap(render_svg(*bundle, 24, 24)));
    }
    btn->Bind(wxEVT_BUTTON, [this, label, type, btn](wxCommandEvent&){
        on_tool_click(label, type, btn);
    });
    //按钮上的滚轮/移动事件也转发给Canvas（鼠标悬停在按钮上时虚影仍能跟随）
    btn->Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& e){ canvas->on_mouse_scroll(e); });
    btn->Bind(wxEVT_MOTION,     [this](wxMouseEvent& e){ canvas->on_mouse_move(e); });
    sizer->Add(btn, 0, wxALL, 2);
}

void ToolBar::on_tool_click(const std::string& name, ItemType type, wxButton* btn){
    //高亮当前选中的按钮
    clear_selection();
    selected = btn;
    wxFont f = selected->GetFont();
    f.SetWeight(wxFONTWEIGHT_BOLD);
    selected->SetFont(f);
    //通知画布进入放置模式（门类元件产生虚影，导线暂不处理）
    canvas->select_tool(name, type);
}

//动作工具按钮：无图标（没有对应SVG），纯文字
void ToolBar::add_action_button(wxSizer* sizer, const std::string& label,
                                ToolAction action){
    wxButton* btn = new wxButton(this, wxID_ANY, wxString::FromUTF8(label));
    btn->Bind(wxEVT_BUTTON, [this, label, action, btn](wxCommandEvent&){
        on_action_click(label, action, btn);
    });
    //按钮上的滚轮/移动事件也转发给Canvas（鼠标悬停在按钮上时画布仍能缩放/跟随）
    btn->Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& e){ canvas->on_mouse_scroll(e); });
    btn->Bind(wxEVT_MOTION,     [this](wxMouseEvent& e){ canvas->on_mouse_move(e); });
    sizer->Add(btn, 0, wxALL, 2);
}

void ToolBar::on_action_click(const std::string& label, ToolAction action, wxButton* btn){
    clear_selection();
    selected = btn;
    wxFont f = selected->GetFont();
    f.SetWeight(wxFONTWEIGHT_BOLD);
    selected->SetFont(f);
    canvas->select_action(action);
}

void ToolBar::clear_selection(){
    if(selected){
        wxFont f = selected->GetFont();
        f.SetWeight(wxFONTWEIGHT_NORMAL);
        selected->SetFont(f);
        selected = nullptr;
    }
}

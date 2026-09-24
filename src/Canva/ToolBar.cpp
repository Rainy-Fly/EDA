#include"./ToolBar.hpp"
#include"./Canvas.hpp"
#include"./ItemPNG.hpp"

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
    //用元件图片做按钮图标（来自共享缓存，不释放）
    const wxBitmap* icon = ItemPNG_image(label);
    if(icon){
        wxImage scaled = icon->ConvertToImage().Scale(24, 24, wxIMAGE_QUALITY_HIGH);
        btn->SetBitmap(wxBitmapBundle::FromBitmap(wxBitmap(scaled)));
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

void ToolBar::clear_selection(){
    if(selected){
        wxFont f = selected->GetFont();
        f.SetWeight(wxFONTWEIGHT_NORMAL);
        selected->SetFont(f);
        selected = nullptr;
    }
}

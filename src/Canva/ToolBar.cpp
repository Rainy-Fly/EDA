#include"./ToolBar.hpp"
#include"./Canvas.hpp"
#include"./ItemSVG.hpp"
#include<wx/statline.h>
#include<wx/filename.h>
#include<wx/stdpaths.h>

namespace{

//工具按钮的小图标（选择/删除/克隆/导线）：src/Canva/assets/Tools/<文件名>.svg。
//查找方式与元件SVG一致（先按当前目录，再按 exe 的上一级目录=仓库根），找不到返回空bundle不报错
wxBitmapBundle tool_icon(const wxString& file, const wxSize size){
    const wxString rel = wxString::FromUTF8("src/Canva/assets/Tools/") + file;
    const wxString root = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath()
                          + wxFILE_SEP_PATH + wxT("..");
    const wxString candidates[] = { rel, root + wxFILE_SEP_PATH + rel };
    for(const wxString& path : candidates){
        if(!wxFileExists(path)) continue;
        wxBitmapBundle bundle = wxBitmapBundle::FromSVGFile(path, size);
        if(bundle.IsOk()) return bundle;
    }
    return wxBitmapBundle();
}

} // namespace

ToolBar::ToolBar(wxWindow* parent, Canvas* cvs)
    : wxPanel(parent, wxID_ANY), canvas(cvs), selected(nullptr){
    //元件调色板（类似 Logisim 顶部的图标工具栏）：**界面上只有 16px 图标**，
    //名字放进悬停提示（鼠标停住才出现），所以 10 个按钮两三行就排完，垂直空间留给资源树。
    //它不再压在画布上，所以也不用再把滚轮/移动事件转发给画布了。
    SetBackgroundColour(wxColour(240, 240, 240));
    wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);

    //元件：7 个，2 行 5 列
    wxGridSizer* tools = new wxGridSizer(2, 5, 3, 3);
    add_tool(tools, "与非门", ItemType::NAND);
    add_tool(tools, "与门",   ItemType::AND);
    add_tool(tools, "或门",   ItemType::OR);
    add_tool(tools, "或非门", ItemType::NOR);
    add_tool(tools, "异或门", ItemType::XOR);
    add_tool(tools, "非门",   ItemType::NOT);
    add_tool(tools, "导线",   ItemType::WIRE);
    //仿真输入/输出端子（小方框）：输入端子单击切换 0/1，输出端子显示所接网络电平。
    //它们是通用元件（类型串 IN_TERMINAL / OUT_TERMINAL 来自 src/metadata/terminals.json）
    add_tool(tools, "输入端子", ItemType::GENERIC);
    add_tool(tools, "输出端子", ItemType::GENERIC);
    outer->Add(tools, 0, wxLEFT | wxRIGHT | wxTOP, 6);

    //动作工具与元件工具用一条横线分隔
    outer->Add(new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                wxLI_HORIZONTAL),
               0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 6);

    //动作工具：选择/删除/克隆，1 行 3 列
    wxGridSizer* actions = new wxGridSizer(1, 3, 3, 3);
    add_action_button(actions, "选择", ToolAction::SELECT);
    add_action_button(actions, "删除", ToolAction::ERASE);
    add_action_button(actions, "克隆", ToolAction::CLONE);
    outer->Add(actions, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

    SetSizer(outer);
    Layout();
}

//建一个 32x30 的图标按钮：界面上只有图标，名字放进悬停提示（鼠标停住才出现）
wxToggleButton* ToolBar::make_button(wxSizer* sizer, const wxString& tooltip,
                                     const wxBitmapBundle& icon){
    wxToggleButton* btn = new wxToggleButton(this, wxID_ANY, wxEmptyString,
                                             wxDefaultPosition, wxSize(32, 30));
    if(icon.IsOk()){
        //render_svg 去掉 scale factor，否则 GetBitmap(16) 会按逻辑尺寸150绘制，图标又大又糊
        btn->SetBitmap(wxBitmapBundle::FromBitmap(render_svg(icon, 16, 16)));
        btn->SetBitmapMargins(2, 2);   //没有文字时让图标在按钮里更居中一点
    }
    btn->SetToolTip(tooltip);          //鼠标在上面停一下才显示名字
    sizer->Add(btn, 0, 0, 0);
    return btn;
}

void ToolBar::add_tool(wxSizer* sizer, const std::string& label, ItemType type){
    //图标：元件用它自己的SVG（共享缓存，不释放）；导线没有元件SVG，用 Tools/wire.svg
    wxBitmapBundle icon;
    const wxBitmapBundle* bundle = ItemSVG_bundle(label);
    if(bundle && bundle->IsOk()) icon = *bundle;
    else if(type == ItemType::WIRE) icon = tool_icon(wxT("wire.svg"), wxSize(16, 16));

    wxToggleButton* btn = make_button(sizer, wxString::FromUTF8(label), icon);
    //按下就进入放置模式（不等松开）：点一下图标虚影立刻跟着鼠标走。
    //必须 event.Skip()：不 Skip 会截断原生按钮自己的按下处理，可能连松开时的
    //点击事件都不再发（松开那次只是把虚影按当前位置重建，无副作用）。
    btn->Bind(wxEVT_LEFT_DOWN, [this, label, type, btn](wxMouseEvent& event){
        on_tool_click(label, type, btn);
        event.Skip();
    });
    //松开时按钮自己会把按下态翻一下，这里再规范化一次选中态
    btn->Bind(wxEVT_TOGGLEBUTTON, [this, label, type, btn](wxCommandEvent&){
        on_tool_click(label, type, btn);
    });
}

void ToolBar::on_tool_click(const std::string& name, ItemType type, wxToggleButton* btn){
    //当前工具高亮：主题按钮不认 SetBackgroundColour，所以用「按下态」表示选中
    clear_selection();
    selected = btn;
    if(!btn->GetValue()) btn->SetValue(true);
    //通知画布进入放置模式（门类元件产生虚影；导线进入画线模式）
    canvas->select_tool(name, type);
}

//动作工具按钮：图标来自 src/Canva/assets/Tools/（select/delete/clone.svg）
void ToolBar::add_action_button(wxSizer* sizer, const std::string& label,
                                ToolAction action){
    const wxString icon_file = (action == ToolAction::SELECT) ? wxT("select.svg")
                             : (action == ToolAction::ERASE)  ? wxT("delete.svg")
                                                              : wxT("clone.svg");
    wxToggleButton* btn = make_button(sizer, wxString::FromUTF8(label),
                                      tool_icon(icon_file, wxSize(16, 16)));
    btn->Bind(wxEVT_TOGGLEBUTTON, [this, label, action, btn](wxCommandEvent&){
        on_action_click(label, action, btn);
    });
}

void ToolBar::on_action_click(const std::string& label, ToolAction action, wxToggleButton* btn){
    clear_selection();
    selected = btn;
    if(!btn->GetValue()) btn->SetValue(true);
    canvas->select_action(action);
}

void ToolBar::clear_selection(){
    if(selected){
        selected->SetValue(false);   //弹起 = 没有选中任何工具
        selected = nullptr;
    }
}

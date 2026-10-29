#include"./ToolBar.hpp"
#include"./Canvas.hpp"
#include"./ItemSVG.hpp"
#include<wx/filename.h>
#include<wx/stdpaths.h>
#include"../Theme/Theme.hpp"

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
    : CardPanel(parent, wxID_ANY, wxString::FromUTF8("元件"), false,
                parent->FromDIP(UiTheme::SpaceXs)),
      canvas(cvs), selected(nullptr){
    //元件调色板（类似 Logisim 顶部的图标工具栏）：**界面上只有 24px 图标**，
    //名字放进悬停提示（鼠标停住才出现），所以 10 个按钮两三行就排完，垂直空间留给资源树。
    //它不再压在画布上，所以也不用再把滚轮/移动事件转发给画布了。
    wxBoxSizer* outer = Content();

    //元件：10 个，2 行 5 列
    wxGridSizer* tools = new wxGridSizer(2, 5, FromDIP(3), FromDIP(3));
    add_tool(tools, "与非门", ItemType::NAND);
    add_tool(tools, "与门",   ItemType::AND);
    add_tool(tools, "或门",   ItemType::OR);
    add_tool(tools, "或非门", ItemType::NOR);
    add_tool(tools, "异或门", ItemType::XOR);
    add_tool(tools, "同或门", ItemType::XNOR);
    add_tool(tools, "非门",   ItemType::NOT);
    add_tool(tools, "导线",   ItemType::WIRE);
    //仿真输入/输出端子（小方框）：输入端子单击切换 0/1，输出端子显示所接网络电平。
    //它们是通用元件（类型串 IN_TERMINAL / OUT_TERMINAL 来自 src/metadata/terminals.json）
    add_tool(tools, "输入端子", ItemType::GENERIC);
    add_tool(tools, "输出端子", ItemType::GENERIC);
    outer->Add(tools, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(2));

    //动作工具与元件工具用一条 1px 细线分隔（不用原生 wxStaticLine 的 3D 蚀刻效果）
    wxPanel* rule = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    rule->SetBackgroundColour(UiTheme::BorderSubtle);
    rule->SetMinSize(wxSize(-1, FromDIP(1)));
    outer->Add(rule, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(6));

    //动作工具：选择/激活/删除/克隆，1 行 4 列
    wxGridSizer* actions = new wxGridSizer(1, 4, FromDIP(3), FromDIP(3));
    add_action_button(actions, "选择", ToolAction::SELECT);
    add_action_button(actions, "激活", ToolAction::ACTIVATE);
    add_action_button(actions, "删除", ToolAction::ERASE);
    add_action_button(actions, "克隆", ToolAction::CLONE);
    outer->Add(actions, 0, wxALIGN_CENTER_HORIZONTAL);

    Layout();
}

//建一个图标按钮：界面上只有图标，名字放进悬停提示（鼠标停住才提示）
ThemedButton* ToolBar::make_button(wxSizer* sizer, const wxString& tooltip,
                                   const wxBitmapBundle& icon){
    ThemedButton* btn = new ThemedButton(this, wxID_ANY, wxEmptyString);
    btn->SetFlat(true);
    btn->SetMinSize(wxSize(FromDIP(36), FromDIP(36)));
    if(icon.IsOk()){
        //render_svg 去掉 scale factor，否则 GetBitmap(24) 会按逻辑尺寸150绘制，图标又大又糊。
        //按钮仍是 36×36，24px 图标留 6px 边距；门的 SVG 本身有透明边距，看起来比 16px 饱满得多。
        btn->SetBitmap(render_svg(icon, 24, 24));
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
    else if(type == ItemType::WIRE) icon = tool_icon(wxT("wire.svg"), wxSize(24, 24));

    ThemedButton* btn = make_button(sizer, wxString::FromUTF8(label), icon);
    btn->SetClickHandler([this, label, type, btn]{
        on_tool_click(label, type, btn);
    });
}

void ToolBar::on_tool_click(const std::string& name, ItemType type, ThemedButton* btn){
    clear_selection();
    selected = btn;
    btn->SetSelected(true);
    //通知画布进入放置模式（门类元件产生虚影；导线进入画线模式）
    canvas->select_tool(name, type);
}

//动作工具按钮：图标来自 src/Canva/assets/Tools/（select/activate/delete/clone.svg）
void ToolBar::add_action_button(wxSizer* sizer, const std::string& label,
                                ToolAction action){
    const wxString icon_file = (action == ToolAction::SELECT)   ? wxT("select.svg")
                             : (action == ToolAction::ACTIVATE) ? wxT("activate.svg")
                             : (action == ToolAction::ERASE)    ? wxT("delete.svg")
                                                               : wxT("clone.svg");
    ThemedButton* btn = make_button(sizer, wxString::FromUTF8(label),
                                    tool_icon(icon_file, wxSize(24, 24)));
    btn->SetClickHandler([this, label, action, btn]{
        on_action_click(label, action, btn);
    });
}

void ToolBar::on_action_click(const std::string& label, ToolAction action, ThemedButton* btn){
    clear_selection();
    selected = btn;
    btn->SetSelected(true);
    canvas->select_action(action);
}

void ToolBar::clear_selection(){
    if(selected){
        selected->SetSelected(false);
        selected = nullptr;
    }
}

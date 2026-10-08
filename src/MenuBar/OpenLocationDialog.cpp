#include"./OpenLocationDialog.hpp"

#include<wx/wx.h>
#include<wx/statline.h>
#include<wx/filefn.h>
#include<wx/statbox.h>

namespace{

// 一个单选项：单选框 + 下面一行显示实际路径（目录不存在或为空则禁用）
struct Choice{
    wxRadioButton* radio = nullptr;
    wxString dir;
    bool usable = false;
};

Choice add_choice(wxWindow* parent, wxSizer* sizer,
                  const wxString& label, const wxString& dir,
                  const wxString& empty_hint, bool first){
    Choice c;
    c.dir = dir;
    c.usable = !dir.empty() && wxDirExists(dir);

    c.radio = new wxRadioButton(parent, wxID_ANY, label,
                                wxDefaultPosition, wxDefaultSize,
                                first ? wxRB_GROUP : 0);
    c.radio->Enable(c.usable);
    sizer->Add(c.radio, 0, wxLEFT | wxRIGHT | wxTOP, 12);

    const wxString shown = c.usable ? dir : empty_hint;
    wxStaticText* path = new wxStaticText(parent, wxID_ANY, shown);
    path->SetForegroundColour(c.usable ? wxColour(60, 60, 70) : wxColour(150, 150, 150));
    sizer->Add(path, 0, wxLEFT | wxRIGHT | wxBOTTOM, 34);   //缩进，看起来属于上面那个选项
    return c;
}

} // namespace

wxString AskOpenLocation(wxWindow* parent,
                         const wxString& recent_dir,
                         const wxString& root_dir,
                         const wxString& default_dir){
    wxDialog dlg(parent, wxID_ANY, wxString::FromUTF8("打开项目 - 选择位置"),
                 wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE);

    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

    wxStaticText* title = new wxStaticText(&dlg, wxID_ANY,
        wxString::FromUTF8("从哪里开始查找项目文件？"));
    wxFont title_font = title->GetFont();
    title_font.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(title_font);
    root->Add(title, 0, wxALL, 12);

    Choice recent = add_choice(&dlg, root, wxString::FromUTF8("从最近位置打开"),
                               recent_dir,
                               wxString::FromUTF8("（还没有打开过项目）"), true);
    Choice proj   = add_choice(&dlg, root, wxString::FromUTF8("从根目录打开"),
                               root_dir, wxString::FromUTF8("（未找到项目根目录）"), false);
    Choice deflt  = add_choice(&dlg, root, wxString::FromUTF8("从默认目录打开"),
                               default_dir,
                               wxString::FromUTF8("（尚未设置默认目录，可在“文件”菜单里设置）"), false);

    //默认选中第一个可用的选项（优先“最近位置”）
    if(recent.usable)      recent.radio->SetValue(true);
    else if(proj.usable)   proj.radio->SetValue(true);
    else if(deflt.usable)  deflt.radio->SetValue(true);

    root->Add(new wxStaticLine(&dlg), 0, wxEXPAND | wxALL, 8);

    wxSizer* buttons = dlg.CreateButtonSizer(wxOK | wxCANCEL);
    if(buttons) root->Add(buttons, 0, wxALIGN_RIGHT | wxALL, 8);

    dlg.SetSizerAndFit(root);
    dlg.CentreOnParent();

    if(dlg.ShowModal() != wxID_OK) return wxString();

    if(recent.radio->GetValue() && recent.usable) return recent.dir;
    if(proj.radio->GetValue()   && proj.usable)   return proj.dir;
    if(deflt.radio->GetValue()  && deflt.usable)  return deflt.dir;
    return wxString();   //一个都没选（理论上不会）
}

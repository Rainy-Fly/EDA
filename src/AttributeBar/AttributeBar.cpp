#include"./AttributeBar.hpp"
#include"../Canva/CanvasItem.hpp"
#include"../Canva/MetaData.hpp"

#include<wx/statline.h>

#include<vector>

AttributeBar::AttributeBar(wxWindow* parent)
    : wxPanel(parent, wxID_ANY), current_item(nullptr){
    SetBackgroundColour(wxColour(245, 245, 248));
    SetMinSize(wxSize(200, 140));

    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

    //标题：显示当前元件名（或占位提示）
    title_text = new wxStaticText(this, wxID_ANY, wxString::FromUTF8("属性"));
    wxFont title_font = title_text->GetFont();
    title_font.SetWeight(wxFONTWEIGHT_BOLD);
    title_font.SetPointSize(title_font.GetPointSize() + 1);
    title_text->SetFont(title_font);
    title_text->SetForegroundColour(wxColour(40, 40, 60));
    root->Add(title_text, 0, wxALL, 8);

    //分隔线
    wxStaticLine* line = new wxStaticLine(this);
    root->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    //属性行容器：每次 set_item 时清空重建
    content_sizer = new wxBoxSizer(wxVERTICAL);
    root->Add(content_sizer, 1, wxEXPAND | wxALL, 8);

    SetSizer(root);
    rebuild();
}

void AttributeBar::set_item(CanvasItem* item){
    current_item = item;
    rebuild();
}

//按 current_item 重建属性行：先清空旧行，再写入当前元件的各项信息
void AttributeBar::rebuild(){
    //清空旧的属性行（true=连同子窗口一起销毁）
    content_sizer->Clear(true);

    wxFlexGridSizer* grid = new wxFlexGridSizer(2, 4, 6);

    if(!current_item){
        add_label_value(grid, wxString::FromUTF8("提示"), wxString::FromUTF8("未选择元件\n在画布上点击元件查看属性"));
        content_sizer->Add(grid, 0, wxEXPAND);
        Layout();
        return;
    }

    // 元件基本信息
    add_label_value(grid, wxString::FromUTF8("名称"),
                    wxString::FromUTF8(current_item->name.c_str()));
    add_label_value(grid, wxString::FromUTF8("ID"),
                    wxString::Format(wxT("%d"), current_item->id));
    add_label_value(grid, wxString::FromUTF8("类型"),
                    wxString::FromUTF8(item_type_label(current_item->type)));
    add_label_value(grid, wxString::FromUTF8("坐标"),
                    wxString::Format(wxT("(%d, %d)"),
                                     current_item->coords_x, current_item->coords_y));

    // 元数据信息（DataBits 等；来自 src/metadata/*.json）
    MetaData* md = current_item->metadata;
    if(md){
        add_label_value(grid, wxString::FromUTF8("类别"),
                        wxString::FromUTF8(md->get_category().c_str()));
        add_label_value(grid, wxString::FromUTF8("描述"),
                        wxString::FromUTF8(md->get_description().c_str()));

        // 逻辑门子类：类型化字段（输入数/输出数/数据位宽DataBits）
        const bool is_gate = (dynamic_cast<LogicGateMetaData*>(md) != nullptr);
        if(LogicGateMetaData* gate = dynamic_cast<LogicGateMetaData*>(md)){
            add_label_value(grid, wxString::FromUTF8("输入数"),
                            wxString::Format(wxT("%d"), gate->get_inputs()));
            add_label_value(grid, wxString::FromUTF8("输出数"),
                            wxString::Format(wxT("%d"), gate->get_outputs()));
            add_label_value(grid, wxString::FromUTF8("DataBits"),
                            wxString::Format(wxT("%d"), gate->get_data_bits()));
        }

        // 其余通用参数（如 DataBits/resistance/voltage/waveform 等）。
        // 门类已用类型化字段显示 inputs/outputs/DataBits，避免重复；
        // 其他类别（导线/总线/电源…）的 DataBits 等仍从params中显示
        static const std::vector<std::string> hidden = {
            "inputs", "outputs", "DataBits",
        };
        for(const auto& kv : md->get_params()){
            if(is_gate){
                bool skip = false;
                for(const std::string& h : hidden){
                    if(kv.first == h){ skip = true; break; }
                }
                if(skip) continue;
            }
            add_label_value(grid, wxString::FromUTF8(kv.first.c_str()),
                            wxString::FromUTF8(kv.second.c_str()));
        }
    }else{
        add_label_value(grid, wxString::FromUTF8("元数据"),
                        wxString::FromUTF8("（无）"));
    }

    content_sizer->Add(grid, 0, wxEXPAND);
    Layout();
}

void AttributeBar::add_label_value(wxSizer* sizer, const wxString& label,
                                   const wxString& value){
    wxStaticText* lbl = new wxStaticText(this, wxID_ANY, label);
    lbl->SetForegroundColour(wxColour(90, 90, 100));
    wxStaticText* val = new wxStaticText(this, wxID_ANY, value);
    val->SetForegroundColour(wxColour(20, 20, 30));
    sizer->Add(lbl, 0, wxALIGN_TOP | wxRIGHT, 8);
    sizer->Add(val, 1, wxALIGN_TOP | wxEXPAND);
}

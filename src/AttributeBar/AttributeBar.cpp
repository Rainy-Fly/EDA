#include"./AttributeBar.hpp"
#include"../Canva/CanvasItem.hpp"
#include"../Canva/MetaData.hpp"

#include<wx/statline.h>

#include<vector>

AttributeBar::AttributeBar(wxWindow* parent)
    : wxPanel(parent, wxID_ANY), current_item(nullptr), updating(false){
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

//按 current_item 重建属性行：先清空旧行，再写入当前元件的各项信息。
//重建期间 updating=true，抑制编辑框SetValue触发的写回
void AttributeBar::rebuild(){
    updating = true;
    content_sizer->Clear(true);   //清空旧的属性行（true=连同子窗口一起销毁）

    wxFlexGridSizer* grid = new wxFlexGridSizer(2, 4, 6);
    grid->AddGrowableCol(1, 1);   //值列可伸展（编辑框铺满）

    if(!current_item){
        add_label_value(grid, wxString::FromUTF8("提示"), wxString::FromUTF8("未选择元件\n在画布上点击元件查看属性"));
        content_sizer->Add(grid, 0, wxEXPAND);
        Layout();
        updating = false;
        return;
    }

    // 元件基本信息（只读：名称/ID是元件身份标识，坐标非元数据）
    add_label_value(grid, wxString::FromUTF8("名称"),
                    wxString::FromUTF8(current_item->name.c_str()));
    add_label_value(grid, wxString::FromUTF8("ID"),
                    wxString::Format(wxT("%d"), current_item->id));
    // 类型：通用元件显示元数据类型串（如 "Resistor-IEC-Standard"），门类显示中文名
    wxString type_label = wxString::FromUTF8(item_type_label(current_item->type));
    if(current_item->type == ItemType::GENERIC && current_item->metadata){
        type_label = wxString::FromUTF8(current_item->metadata->get_type().c_str());
    }
    add_label_value(grid, wxString::FromUTF8("类型"), type_label);
    add_label_value(grid, wxString::FromUTF8("坐标"),
                    wxString::Format(wxT("(%d, %d)"),
                                     current_item->coords_x, current_item->coords_y));

    // 元数据信息（可编辑，写回该元件自己的MetaData）
    MetaData* md = current_item->metadata;
    if(md){
        add_label_value(grid, wxString::FromUTF8("类别"),
                        wxString::FromUTF8(md->get_category().c_str()));   //类别只读（结构性）
        add_editable_value(grid, wxString::FromUTF8("描述"),
                           "description", wxString::FromUTF8(md->get_description().c_str()));

        // 逻辑门：输入/输出引脚数由符号类型决定（2输入与门、1输入非门…），
        // 是固定属性而非可配置参数，不显示、不可编辑；
        // DataBits（数据位宽）作为可编辑参数保留
        const bool is_gate = (dynamic_cast<LogicGateMetaData*>(md) != nullptr);
        if(LogicGateMetaData* gate = dynamic_cast<LogicGateMetaData*>(md)){
            add_editable_value(grid, wxString::FromUTF8("DataBits"), "DataBits",
                               wxString::Format(wxT("%d"), gate->get_data_bits()));
        }

        // 其余通用参数可编辑。门类已用类型化字段显示 inputs/outputs/DataBits，避免重复；
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
            add_editable_value(grid, wxString::FromUTF8(kv.first.c_str()),
                               kv.first, wxString::FromUTF8(kv.second.c_str()));
        }
    }else{
        add_label_value(grid, wxString::FromUTF8("元数据"),
                        wxString::FromUTF8("（无）"));
    }

    content_sizer->Add(grid, 0, wxEXPAND);
    Layout();
    updating = false;
}

//把编辑后的新值写入【当前元件的元数据】。
//门类的inputs/outputs/DataBits走类型化setter（同时更新params）；
//description走基类setter；其余写入通用params表
void AttributeBar::set_metadata_value(MetaData* md, const std::string& key,
                                      const std::string& value){
    if(!md) return;
    auto to_int = [&](int def)->int{
        try{ return std::stoi(value); }catch(...){ return def; }
    };
    if(LogicGateMetaData* gate = dynamic_cast<LogicGateMetaData*>(md)){
        if(key == "inputs"){    gate->set_inputs(to_int(gate->get_inputs()));    return; }
        if(key == "outputs"){   gate->set_outputs(to_int(gate->get_outputs()));  return; }
        if(key == "DataBits"){  gate->set_data_bits(to_int(gate->get_data_bits())); return; }
    }
    if(key == "description"){ md->set_description(value); return; }
    md->params[key] = value;
}

//只读行“标签: 值”
void AttributeBar::add_label_value(wxSizer* sizer, const wxString& label,
                                   const wxString& value){
    wxStaticText* lbl = new wxStaticText(this, wxID_ANY, label);
    lbl->SetForegroundColour(wxColour(90, 90, 100));
    wxStaticText* val = new wxStaticText(this, wxID_ANY, value);
    val->SetForegroundColour(wxColour(20, 20, 30));
    sizer->Add(lbl, 0, wxALIGN_TOP | wxRIGHT, 8);
    sizer->Add(val, 1, wxALIGN_TOP | wxEXPAND);
}

//可编辑行“标签: [编辑框]”：内容变化时写回元数据
void AttributeBar::add_editable_value(wxSizer* sizer, const wxString& label,
                                      const std::string& meta_key,
                                      const wxString& value){
    wxStaticText* lbl = new wxStaticText(this, wxID_ANY, label);
    lbl->SetForegroundColour(wxColour(90, 90, 100));
    wxTextCtrl* edit = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition,
                                      wxSize(120, -1));
    edit->SetBackgroundColour(wxColour(255, 255, 255));
    edit->Bind(wxEVT_TEXT, [this, meta_key](wxCommandEvent& e){
        if(updating) return;   //重建期SetValue触发的TEXT事件：忽略
        if(!current_item) return;
        wxTextCtrl* tc = dynamic_cast<wxTextCtrl*>(e.GetEventObject());
        if(tc){
            set_metadata_value(current_item->metadata, meta_key,
                               tc->GetValue().ToStdString());
        }
    });
    sizer->Add(lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    sizer->Add(edit, 1, wxEXPAND);
}

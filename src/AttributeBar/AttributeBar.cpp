#include"./AttributeBar.hpp"
#include"../Canva/CanvasItem.hpp"
#include"../Canva/MetaData.hpp"

#include<wx/statline.h>
#include<wx/scrolwin.h>
#include"../Theme/Theme.hpp"

#include<vector>

AttributeBar::AttributeBar(wxWindow* parent)
    : CardPanel(parent, wxID_ANY, wxString::FromUTF8("属性"), false,
                parent->FromDIP(UiTheme::SpaceXs)),
      current_item(nullptr), updating(false){
    SetMinSize(wxSize(200, 140));

    //属性行容器：参数多时（IC/继电器/端子提示）可以纵向滚动，而不是被卡片底边截断。
    //标题/卡片边框由 CardPanel 统一画。
    scroller_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                     wxDefaultSize, wxVSCROLL);
    scroller_->SetBackgroundColour(UiTheme::Surface);
    scroller_->SetScrollRate(0, FromDIP(8));
    Content()->Add(scroller_, 1, wxEXPAND);
    content_sizer = new wxBoxSizer(wxVERTICAL);
    scroller_->SetSizer(content_sizer);
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
        content_sizer->Layout();
        scroller_->FitInside();
        Layout();
        updating = false;
        return;
    }

    MetaData* md = current_item->metadata;

    // 元件基本信息（只读：名称/ID是元件身份标识，坐标非元数据）
    add_label_value(grid, wxString::FromUTF8("名称"),
                    wxString::FromUTF8(current_item->name.c_str()));

    // 输入/输出端子只是仿真测试点，不是物理元件；提示放在最上面，避免参数多时被滚出视野。
    const std::string meta_type = md ? md->get_type() : std::string();
    const bool simulation_terminal =
        meta_type == "IN_TERMINAL" || meta_type == "OUT_TERMINAL" ||
        (md && md->get_category() == "Simulation");
    if(simulation_terminal){
        add_label_value(grid, wxString::FromUTF8("用途"),
                        wxString::FromUTF8("仅用于仿真测试；封装和导出时不允许出现，请导出前删除。"),
                        UiTheme::Warning);
    }

    // 放置预览：从调色板/资源树点选元件后，ghost 还没有正式实例 ID 和固定坐标
    if(current_item->ghost_mode){
        add_label_value(grid, wxString::FromUTF8("状态"),
                        wxString::FromUTF8("待放置（单击画布放下）"),
                        UiTheme::Brand700);
        add_label_value(grid, wxString::FromUTF8("ID"),
                        wxString::FromUTF8("—（放置后分配）"));
    }else{
        add_label_value(grid, wxString::FromUTF8("ID"),
                        wxString::Format(wxT("%d"), current_item->id));
    }

    // 类型：通用元件显示元数据类型串（如 "Resistor-IEC-Standard"），门类显示中文名
    wxString type_label = wxString::FromUTF8(item_type_label(current_item->type));
    if(current_item->type == ItemType::GENERIC && current_item->metadata){
        type_label = wxString::FromUTF8(current_item->metadata->get_type().c_str());
    }
    add_label_value(grid, wxString::FromUTF8("类型"), type_label);
    if(current_item->ghost_mode){
        add_label_value(grid, wxString::FromUTF8("坐标"),
                        wxString::FromUTF8("跟随鼠标"));
    }else{
        add_label_value(grid, wxString::FromUTF8("坐标"),
                        wxString::Format(wxT("(%d, %d)"),
                                         current_item->coords_x, current_item->coords_y));
    }

    // 元数据信息（可编辑，写回该元件自己的MetaData）
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
    content_sizer->Layout();
    scroller_->FitInside();
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
                                   const wxString& value,
                                   const wxColour& value_colour){
    wxWindow* parent = scroller_ ? static_cast<wxWindow*>(scroller_) : this;
    wxStaticText* lbl = new wxStaticText(parent, wxID_ANY, label);
    lbl->SetForegroundColour(UiTheme::TextSecondary);
    wxStaticText* val = new wxStaticText(parent, wxID_ANY, value);
    val->SetForegroundColour(value_colour.IsOk() ? value_colour : UiTheme::Text);
    sizer->Add(lbl, 0, wxALIGN_TOP | wxRIGHT, 8);
    sizer->Add(val, 1, wxALIGN_TOP | wxEXPAND);
}

//可编辑行“标签: [编辑框]”：内容变化时写回元数据
void AttributeBar::add_editable_value(wxSizer* sizer, const wxString& label,
                                      const std::string& meta_key,
                                      const wxString& value){
    wxWindow* parent = scroller_ ? static_cast<wxWindow*>(scroller_) : this;
    wxStaticText* lbl = new wxStaticText(parent, wxID_ANY, label);
    lbl->SetForegroundColour(UiTheme::TextSecondary);
    wxTextCtrl* edit = new wxTextCtrl(parent, wxID_ANY, value, wxDefaultPosition,
                                      wxSize(120, -1));
    edit->SetBackgroundColour(UiTheme::Surface);
    edit->SetForegroundColour(UiTheme::Text);
    edit->Bind(wxEVT_TEXT, [this, meta_key](wxCommandEvent& e){
        if(updating) return;   //重建期SetValue触发的TEXT事件：忽略
        if(!current_item) return;
        wxTextCtrl* tc = dynamic_cast<wxTextCtrl*>(e.GetEventObject());
        if(tc){
            set_metadata_value(current_item->metadata, meta_key,
                               tc->GetValue().ToStdString());
            //改属性也是改项目内容：通知菜单栏标记“未保存”
            if(edited_callback) edited_callback();
        }
    });
    sizer->Add(lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    sizer->Add(edit, 1, wxEXPAND);
}

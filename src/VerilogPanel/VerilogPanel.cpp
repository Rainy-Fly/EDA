#include "./VerilogPanel.hpp"

#include "../Canva/Canvas.hpp"
#include "../DealProjectXML/DealProjectXML.hpp"
#include "../VerilogBridge/ModuleToProject.hpp"
#include "../VerilogBridge/ProjectToModule.hpp"
#include "../verilator/VerilatorApi.hpp"

#include <wx/button.h>
#include <wx/stattext.h>

namespace{

wxString diagnostics_text(const std::vector<Verilator::Diagnostic>& diagnostics){
    wxString text;
    for(const Verilator::Diagnostic& diagnostic : diagnostics){
        text += diagnostic.severity == Verilator::DiagnosticSeverity::Error
                    ? wxString::FromUTF8("错误 ")
                    : wxString::FromUTF8("警告 ");
        if(diagnostic.location.line > 0){
            text += wxString::Format(wxT("(第 %d 行, 第 %d 列) "),
                                     diagnostic.location.line,
                                     diagnostic.location.column);
        }
        text += wxString::FromUTF8(("[" + diagnostic.code + "] " +
                                    diagnostic.message).c_str());
        text += wxT("\n");
    }
    return text;
}

} // namespace

namespace VerilogPanelUI{

VerilogPanel::VerilogPanel(wxWindow* parent, Canvas* canvas)
    : wxPanel(parent, wxID_ANY),
      canvas_(canvas),
      code_(nullptr),
      diagnostics_(nullptr),
      convert_button_(nullptr){
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    wxStaticText* title =
        new wxStaticText(this, wxID_ANY, wxString::FromUTF8("Verilog 代码"));
    wxFont title_font = title->GetFont();
    title_font.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(title_font);
    sizer->Add(title, 0, wxALL, 6);

    code_ = new wxTextCtrl(this, wxID_ANY, wxString(),
                           wxDefaultPosition, wxDefaultSize,
                           wxTE_MULTILINE | wxTE_DONTWRAP);
    wxFont mono = code_->GetFont();
    mono.SetFaceName(wxT("Consolas"));   //找不到时 wx 自动回退
    code_->SetFont(mono);
    sizer->Add(code_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    diagnostics_ = new wxTextCtrl(this, wxID_ANY, wxString(),
                                  wxDefaultPosition, wxSize(-1, 96),
                                  wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    diagnostics_->SetFont(mono);
    sizer->Add(diagnostics_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer(1);
    convert_button_ = new wxButton(this, wxID_ANY, wxString::FromUTF8("转化"));
    buttons->Add(convert_button_, 0);
    sizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    SetSizer(sizer);
    SetMinSize(wxSize(260, 240));

    convert_button_->Bind(wxEVT_BUTTON, &VerilogPanel::on_convert, this);
}

void VerilogPanel::refresh_from_canvas(){
    if(!canvas_ || !code_) return;

    DealProjectXML::ProjectInfo info;
    info.name = "top";
    const DealProjectXML::ProjectData project =
        DealProjectXML::CollectFromCanvas(canvas_, info);

    const VerilogBridge::ProjectToModuleResult module =
        VerilogBridge::BuildModuleFromProject(project, info.name);
    if(!module.ok){
        show_diagnostics(module.diagnostics);
        set_code("// 当前画布无法转换为 Verilog。\n"
                 "// 请先修正下方诊断中的错误，再重新打开本面板刷新代码。\n");
        return;
    }

    const Verilator::GenerateResult generated =
        Verilator::generate_verilog(module.module);
    if(!generated.ok){
        show_diagnostics(generated.diagnostics);
        set_code("// 当前画布无法生成 Verilog 代码。\n");
        return;
    }

    set_code(generated.verilog);
    show_diagnostics(module.diagnostics);   //仅保留警告
}

void VerilogPanel::convert_to_canvas(){
    if(!canvas_ || !code_) return;

    const std::string source(code_->GetValue().mb_str(wxConvUTF8));
    const Verilator::ParseResult parsed = Verilator::parse_verilog(source);
    if(!parsed.ok){
        show_diagnostics(parsed.diagnostics);
        return;
    }

    //新内容放在当前视口左上角附近，转化后用户能直接看到。
    const wxPoint view = canvas_->get_offset();
    const VerilogBridge::ModuleToProjectResult project =
        VerilogBridge::BuildProjectFromModule(parsed.module,
                                              wxPoint(view.x + 40, view.y + 40));
    if(!project.ok){
        show_diagnostics(project.diagnostics);
        return;
    }

    DealProjectXML::ApplyToCanvas(canvas_, project.project);
    canvas_->Refresh();
    show_diagnostics(project.diagnostics);

    if(applied_callback_) applied_callback_();
}

void VerilogPanel::on_convert(wxCommandEvent&){
    convert_to_canvas();
}

void VerilogPanel::show_diagnostics(
    const std::vector<Verilator::Diagnostic>& diagnostics){
    if(!diagnostics_) return;
    diagnostics_->ChangeValue(diagnostics_text(diagnostics));
}

void VerilogPanel::set_code(const std::string& code){
    if(!code_) return;
    code_->ChangeValue(wxString::FromUTF8(code.c_str()));
}

} // namespace VerilogPanelUI

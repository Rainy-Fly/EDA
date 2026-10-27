#include "./VerilogPanel.hpp"

#include "../Canva/Canvas.hpp"
#include "../DealProjectXML/DealProjectXML.hpp"
#include "../VerilogBridge/ModuleToProject.hpp"
#include "../VerilogBridge/ProjectToModule.hpp"
#include "../verilator/VerilatorApi.hpp"
#include "../verilator/include/VerilogKeywords.hpp"

#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/stc/stc.h>
#include "../Theme/Theme.hpp"

#include <set>

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

// 浅色主题：关键字蓝、声明词紫、注释绿、数字与字符串棕红，贴近常见 IDE。
void setup_editor_styles(wxStyledTextCtrl* stc){
    stc->StyleClearAll();

    const wxColour kFg = UiTheme::Text;
    const wxColour kKeyword = UiTheme::Brand900;
    const wxColour kWord2(0x7F, 0x00, 0x7F);
    const wxColour kComment = UiTheme::Success;
    const wxColour kString(0xA3, 0x15, 0x15);
    const wxColour kOperator(0x33, 0x33, 0x33);
    const wxColour kCaretLine(0xF6, 0xFA, 0xFB);

    stc->StyleSetForeground(wxSTC_V_DEFAULT, kFg);
    stc->StyleSetForeground(wxSTC_V_IDENTIFIER, kFg);
    stc->StyleSetForeground(wxSTC_V_WORD, kKeyword);
    stc->StyleSetBold(wxSTC_V_WORD, true);
    stc->StyleSetForeground(wxSTC_V_WORD2, kWord2);
    stc->StyleSetBold(wxSTC_V_WORD2, true);
    stc->StyleSetForeground(wxSTC_V_WORD3, kWord2);
    stc->StyleSetForeground(wxSTC_V_NUMBER, kString);
    stc->StyleSetForeground(wxSTC_V_STRING, kString);
    stc->StyleSetForeground(wxSTC_V_OPERATOR, kOperator);
    stc->StyleSetForeground(wxSTC_V_COMMENT, kComment);
    stc->StyleSetForeground(wxSTC_V_COMMENTLINE, kComment);
    stc->StyleSetForeground(wxSTC_V_COMMENTLINEBANG, kComment);
    stc->StyleSetForeground(wxSTC_V_COMMENT_WORD, kComment);
    stc->StyleSetItalic(wxSTC_V_COMMENT_WORD, true);
    //端口/信号声明词：紫色（input/output/inout/端口连接符）
    stc->StyleSetForeground(wxSTC_V_INPUT, kWord2);
    stc->StyleSetForeground(wxSTC_V_OUTPUT, kWord2);
    stc->StyleSetForeground(wxSTC_V_INOUT, kWord2);
    stc->StyleSetForeground(wxSTC_V_PORT_CONNECT, kWord2);
    //字符串未闭合的行：红字警示
    stc->StyleSetForeground(wxSTC_V_STRINGEOL, wxColour(0xFF, 0x00, 0x00));
    stc->StyleSetForeground(wxSTC_V_USER, kFg);

    //行号栏：灰字，不抢眼
    stc->StyleSetForeground(wxSTC_STYLE_LINENUMBER,
                            wxColour(0x80, 0x80, 0x80));
    stc->StyleSetBackground(wxSTC_STYLE_LINENUMBER,
                            UiTheme::Subtle);

    stc->StyleSetForeground(wxSTC_STYLE_DEFAULT, kFg);
    stc->StyleSetBackground(wxSTC_STYLE_DEFAULT, UiTheme::Surface);
    stc->StyleSetFont(wxSTC_STYLE_DEFAULT, stc->GetFont());

    //选择与当前行
    stc->SetSelForeground(true, *wxWHITE);
    stc->SetSelBackground(true, UiTheme::Brand900);
    stc->SetCaretLineVisible(true);
    stc->SetCaretLineBackground(kCaretLine);
}

bool is_id_start(wchar_t c){
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_id_char(wchar_t c){
    return is_id_start(c) || (c >= '0' && c <= '9') || c == '$';
}

} // namespace

namespace VerilogPanelUI{

VerilogPanel::VerilogPanel(wxWindow* parent, Canvas* canvas)
    : wxPanel(parent, wxID_ANY),
      canvas_(canvas),
      code_(nullptr),
      diagnostics_(nullptr),
    generate_circuit_button_(nullptr),
    generate_code_button_(nullptr){
        SetBackgroundColour(UiTheme::Page);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    wxStaticText* title =
        new wxStaticText(this, wxID_ANY, wxString::FromUTF8("Verilog 代码"));
    title->SetForegroundColour(UiTheme::Brand900);
    wxFont title_font = title->GetFont();
    title_font.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(title_font);
    sizer->Add(title, 0, wxALL, 6);

    //---- 编辑器（Scintilla）----
    code_ = new wxStyledTextCtrl(this, wxID_ANY);
    code_->SetCodePage(wxSTC_CP_UTF8);   //UTF-8：MSW 下中文/注释不乱码
    code_->SetLexer(wxSTC_LEX_VERILOG);
    //关键字分两组：0=控制/保留字，1=声明/端口词，配色不同
    {
        wxString keywords0;
        wxString keywords1;
        for(const std::string_view keyword : Verilator::kSupportedVerilogKeywords){
            keywords0 += wxString::FromUTF8(std::string(keyword).c_str()) + wxT(" ");
        }
        for(const std::string_view keyword : Verilator::kUnsupportedVerilogKeywords){
            keywords0 += wxString::FromUTF8(std::string(keyword).c_str()) + wxT(" ");
        }
        keywords1 = wxT("input output inout wire reg integer parameter "
                        "localparam signed unsigned tri supply0 supply1");
        code_->SetKeyWords(0, keywords0);
        code_->SetKeyWords(1, keywords1);
    }
    setup_editor_styles(code_);

    //Tab：按一次缩进 4 格（空格缩进），Shift+Tab 反缩进；退格优先退缩进
    code_->SetTabWidth(4);
    code_->SetUseTabs(false);
    code_->SetIndent(4);
    code_->SetTabIndents(true);
    code_->SetBackSpaceUnIndents(true);
    code_->SetIndentationGuides(wxSTC_IV_LOOKBOTH);
    code_->SetEOLMode(wxSTC_EOL_LF);
    code_->SetWrapMode(wxSTC_WRAP_NONE);
    code_->SetCaretWidth(2);

    //行号栏
    code_->SetMarginType(0, wxSTC_MARGIN_NUMBER);
    code_->SetMarginWidth(0, code_->TextWidth(wxSTC_STYLE_LINENUMBER,
                                              wxT("9999")));

    //自动补全：忽略大小写，单个候选也直接弹出（可左右选择）
    code_->AutoCompSetIgnoreCase(true);
    code_->AutoCompSetChooseSingle(false);
    code_->AutoCompSetMaxHeight(12);

    //事件：输入字符时触发补全；光标移动时刷新括号匹配
    code_->Bind(wxEVT_STC_CHARADDED, &VerilogPanel::on_char_added, this);
    code_->Bind(wxEVT_STC_UPDATEUI, &VerilogPanel::on_update_ui, this);

    sizer->Add(code_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    wxFont mono = code_->GetFont();
    mono.SetFaceName(wxT("Consolas"));   //找不到时 wx 自动回退
    code_->SetFont(mono);

    // 两个方向分开操作，按钮显式指定高度。
    wxBoxSizer* buttons = new wxBoxSizer(wxVERTICAL);
    generate_circuit_button_ = new ThemedButton(
        this, wxID_ANY, wxString::FromUTF8("由代码生成电路图"), true);
    generate_circuit_button_->SetMinSize(wxSize(-1, 36));
    buttons->Add(generate_circuit_button_, 0, wxEXPAND | wxBOTTOM, 6);

    generate_code_button_ = new ThemedButton(
        this, wxID_ANY, wxString::FromUTF8("由图生成代码"));
    generate_code_button_->SetMinSize(wxSize(-1, 36));
    buttons->Add(generate_code_button_, 0, wxEXPAND);
    sizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    // 诊断框只读，放在操作按钮下方，避免与可编辑代码区混淆。
    diagnostics_ = new wxTextCtrl(this, wxID_ANY, wxString(),
                                  wxDefaultPosition, wxSize(-1, 96),
                                  wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    diagnostics_->SetBackgroundColour(UiTheme::Subtle);
    diagnostics_->SetForegroundColour(UiTheme::TextSecondary);
    diagnostics_->SetFont(mono);
    sizer->Add(diagnostics_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    SetSizer(sizer);
    SetMinSize(wxSize(260, 240));

    generate_circuit_button_->SetClickHandler([this]{
        wxCommandEvent event;
        on_generate_circuit(event);
    });
    generate_code_button_->SetClickHandler([this]{
        wxCommandEvent event;
        on_generate_code(event);
    });
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

    const std::string source(code_->GetText().mb_str(wxConvUTF8));
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

void VerilogPanel::on_generate_circuit(wxCommandEvent&){
    convert_to_canvas();
}

void VerilogPanel::on_generate_code(wxCommandEvent&){
    refresh_from_canvas();
}

//输入标识符字符后弹出补全列表：候选 = Verilog 关键字 + 文档内已有标识符。
//在注释/字符串里不弹出；补全列表已显示时交给 Scintilla 自行过滤。
void VerilogPanel::on_char_added(wxStyledTextEvent& event){
    event.Skip();
    if(!code_ || code_->AutoCompActive()) return;

    const int pos = code_->GetCurrentPos();
    int start = pos;
    while(start > 0 && is_id_char(static_cast<wchar_t>(code_->GetCharAt(start - 1)))){
        --start;
    }
    const wxString prefix = code_->GetTextRange(start, pos);
    if(prefix.empty() || !is_id_start(prefix[0])) return;

    //仅当光标处于标识符/默认样式时才补全（注释、字符串内不弹）
    const int style = code_->GetStyleAt(pos - 1);
    if(style != wxSTC_V_DEFAULT && style != wxSTC_V_IDENTIFIER &&
       style != wxSTC_V_WORD && style != wxSTC_V_WORD2){
        return;
    }

    const wxString prefix_lower = prefix.Lower();
    wxString choices;
    std::set<wxString> seen;

    //1) 关键字
    const wxString keywords = wxT("module endmodule input output wire assign "
                                  "always initial begin end reg logic if else "
                                  "case casex casez endcase default for while "
                                  "repeat forever function endfunction task "
                                  "endtask parameter localparam generate "
                                  "endgenerate inout tri integer real time "
                                  "signed unsigned posedge negedge and or not "
                                  "xor xnor nand nor buf bufif0 bufif1 notif0 "
                                  "notif1 pmos nmos cmos tran supply0 supply1 "
                                  "specify endspecify primitive endprimitive "
                                  "genvar fork join wait disable event");
    for(const wxString& keyword : wxSplit(keywords, ' ')){
        if(!keyword.empty() && keyword.Lower().StartsWith(prefix_lower)){
            choices += keyword + wxT(" ");
            seen.insert(keyword);
        }
    }

    //2) 文档内标识符（端口/信号/门名等都来自这里）
    const wxString doc = code_->GetText();
    const int length = doc.length();
    int i = 0;
    while(i < length){
        const wchar_t c = doc[i];
        if(is_id_start(c)){
            const int word_start = i;
            ++i;
            while(i < length && is_id_char(doc[i])) ++i;
            const wxString word = doc.Mid(word_start, i - word_start);
            if(word.Lower().StartsWith(prefix_lower) && !seen.count(word)){
                choices += word + wxT(" ");
                seen.insert(word);
            }
        }else{
            ++i;
        }
    }

    if(!choices.empty()){
        code_->AutoCompShow(prefix.length(), choices);
    }
}

//光标移动/内容变化后刷新括号匹配高亮
void VerilogPanel::on_update_ui(wxStyledTextEvent& event){
    event.Skip();
    if(!code_) return;

    const int pos = code_->GetCurrentPos();
    int brace_pos = -1;
    const int ch = pos > 0 ? code_->GetCharAt(pos - 1) : 0;
    if(ch == '(' || ch == '[' || ch == '{'){
        brace_pos = pos - 1;
    }else if(ch == ')' || ch == ']' || ch == '}'){
        brace_pos = code_->BraceMatch(pos - 1);
    }

    if(brace_pos >= 0){
        const int match = code_->BraceMatch(brace_pos);
        if(match >= 0){
            code_->BraceHighlight(brace_pos, match);
        }else{
            code_->BraceBadLight(brace_pos);
        }
    }else{
        code_->BraceHighlight(-1, -1);
    }
}

void VerilogPanel::show_diagnostics(
    const std::vector<Verilator::Diagnostic>& diagnostics){
    if(!diagnostics_) return;
    diagnostics_->ChangeValue(diagnostics_text(diagnostics));
}

void VerilogPanel::set_code(const std::string& code){
    if(!code_) return;
    code_->SetText(wxString::FromUTF8(code.c_str()));
    code_->EmptyUndoBuffer();   //程序写入不算用户编辑，不参与撤销
    code_->SetCurrentPos(0);
    code_->SetSelectionStart(0);
    code_->SetSelectionEnd(0);
}

} // namespace VerilogPanelUI

#pragma once
#include<wx/wx.h>
#include<atomic>
#include<functional>
#include<string>
#include<thread>
#include<utility>
#include<vector>
#include"../Theme/CardPanel.hpp"
#include"../Theme/ThemedButton.hpp"

class Canvas;
class wxStyledTextCtrl;
class wxStyledTextEvent;

namespace Verilator{ struct Diagnostic; }

namespace VerilogPanelUI{

//========================================================
// 工具运行完成事件（工作线程 -> 主线程）
//========================================================
class VerilogToolEvent : public wxEvent{
public:
    VerilogToolEvent(wxEventType type, int winid, const wxString& report,
                     const wxString& err, bool ok, const wxString& vcdPath)
        : wxEvent(winid, type), report_(report), err_(err), ok_(ok), vcd_path_(vcdPath){}
    wxEvent* Clone() const override { return new VerilogToolEvent(*this); }
    const wxString& Report() const { return report_; }
    const wxString& Err() const { return err_; }
    bool Ok() const { return ok_; }
    const wxString& VcdPath() const { return vcd_path_; }
private:
    wxString report_;
    wxString err_;
    bool ok_;
    wxString vcd_path_;
};

wxDECLARE_EVENT(wxEVT_VERILOG_TOOL, VerilogToolEvent);

// 画布右侧的 Verilog 代码面板：
//   - 编辑器基于 wxStyledTextCtrl（Scintilla）：Verilog 语法高亮、Tab 缩进、
//     行号、缩进参考线、括号匹配、输入时自动补全（关键字 + 文档内标识符）
//   - 显示当前画布生成的 Verilog；无法转换时在诊断区显示报错
//   - 代码可编辑：可由代码生成电路图，也可由当前画布重新生成代码
//   - verilator/gtkwave 集成：代码检查（lint）与生成并查看波形（后台线程执行，
//     完成后经 wxEVT_VERILOG_TOOL 事件回主线程更新诊断区，不阻塞 UI）
class VerilogPanel : public CardPanel{
public:
    VerilogPanel(wxWindow* parent, Canvas* canvas);
    ~VerilogPanel() override;

    //从画布重新生成代码（显示面板时调用）
    void refresh_from_canvas();
    //解析编辑器里的代码并应用到画布；失败时显示诊断
    void convert_to_canvas();

    //转化成功后的回调（主窗口用它标记项目已修改）
    void set_applied_callback(std::function<void()> callback){
        applied_callback_ = std::move(callback);
    }

private:
    void on_generate_circuit(wxCommandEvent& event);
    void on_generate_code(wxCommandEvent& event);
    void on_lint(wxCommandEvent& event);
    void on_waveform(wxCommandEvent& event);
    void on_tool_done(VerilogToolEvent& event);
    void on_char_added(wxStyledTextEvent& event);   //输入标识符字符时弹出补全列表
    void on_update_ui(wxStyledTextEvent& event);    //光标移动时更新括号匹配高亮
    void show_diagnostics(const std::vector<Verilator::Diagnostic>& diagnostics);
    void set_code(const std::string& code);

    Canvas* canvas_;
    wxStyledTextCtrl* code_;
    wxTextCtrl* diagnostics_;
    ThemedButton* generate_circuit_button_;
    ThemedButton* generate_code_button_;
    ThemedButton* lint_button_;
    ThemedButton* waveform_button_;
    std::function<void()> applied_callback_;
    std::atomic<bool> tool_busy_{false};
    std::thread tool_worker_;
};

} // namespace VerilogPanelUI

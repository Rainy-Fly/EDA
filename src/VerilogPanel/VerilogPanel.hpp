#pragma once
#include<wx/wx.h>
#include<functional>
#include<string>
#include<utility>
#include<vector>

class Canvas;

namespace Verilator{ struct Diagnostic; }

namespace VerilogPanelUI{

// 画布右侧的 Verilog 代码面板：
//   - 显示当前画布生成的 Verilog；无法转换时在诊断区显示报错
//   - 代码可编辑，点击【转化】后把代码解析并应用到画布
class VerilogPanel : public wxPanel{
public:
    VerilogPanel(wxWindow* parent, Canvas* canvas);

    //从画布重新生成代码（显示面板时调用）
    void refresh_from_canvas();
    //解析编辑器里的代码并应用到画布；失败时显示诊断
    void convert_to_canvas();

    //转化成功后的回调（主窗口用它标记项目已修改）
    void set_applied_callback(std::function<void()> callback){
        applied_callback_ = std::move(callback);
    }

private:
    void on_convert(wxCommandEvent& event);
    void show_diagnostics(const std::vector<Verilator::Diagnostic>& diagnostics);
    void set_code(const std::string& code);

    Canvas* canvas_;
    wxTextCtrl* code_;
    wxTextCtrl* diagnostics_;
    wxButton* convert_button_;
    std::function<void()> applied_callback_;
};

} // namespace VerilogPanelUI

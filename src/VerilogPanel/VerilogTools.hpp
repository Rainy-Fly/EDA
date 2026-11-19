#pragma once
#include<wx/string.h>

// Verilator / GTKWave 外部工具集成：
//   - 探测工具是否安装（运行时检测，缺失时 UI 给出安装指引）
//   - 代码检查：verilator --lint-only（带行号的警告/错误报告）
//   - 波形生成：自动生成 testbench -> verilator --binary 编译仿真 -> VCD 文件
//   - 打开波形：调用系统 gtkwave 查看 VCD
// 全部通过子进程调用外部工具，不链接 verilator 库，跨平台一致。
namespace VerilogTools{

// 在 PATH 中查找可执行工具（verilator / gtkwave）。
// 找到返回 true 并把完整路径写入 found_path。
bool find_tool(const wxString& name, wxString& found_path);

// 对 Verilog 源码做静态检查：写临时文件后调 verilator --lint-only。
// report 收集工具输出（带行号的警告/错误）；返回 0 表示无错误。
// err 为非空表示环境错误（工具缺失/写临时文件失败）。
int lint_verilog(const wxString& source, wxString& report, wxString& err);

// 生成波形：解析源码得到端口 -> 自动生成 testbench（组合逻辑遍历输入组合）
// -> verilator --binary 编译 -> 运行生成 VCD 到 vcd_path。
// report 收集过程输出；成功返回 true。err 为非空表示环境/步骤错误。
bool generate_waveform(const wxString& verilog_source,
                       const wxString& vcd_path,
                       wxString& report,
                       wxString& err);

// 用系统 gtkwave 打开 VCD（异步，不阻塞）。
bool open_waveform(const wxString& vcd_path, wxString& err);

} // namespace VerilogTools

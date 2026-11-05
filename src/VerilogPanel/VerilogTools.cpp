#include "./VerilogTools.hpp"

#include "../verilator/VerilatorApi.hpp"
#include "../verilator/model/CircuitModel.hpp"

#include <wx/arrstr.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <cstdio>

namespace VerilogTools {

namespace {

// 命令参数用双引号包裹（路径可能含空格）
wxString quote(const wxString& s){
    return wxT("\"") + s + wxT("\"");
}

bool write_file(const wxString& path, const wxString& content){
    FILE* file = std::fopen(path.ToUTF8().data(), "wb");
    if(!file) return false;
    const std::string utf8 = content.ToUTF8().data();
    const bool ok = std::fwrite(utf8.data(), 1, utf8.size(), file) == utf8.size();
    std::fclose(file);
    return ok;
}

// 工作目录：系统临时目录下的 tinyeda_verilator（自动创建）
wxString work_dir(){
    wxString dir = wxStandardPaths::Get().GetTempDir();
    dir += wxT("/tinyeda_verilator");
    if(!wxDirExists(dir)){
        wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    }
    return dir;
}

// 执行命令并收集 stdout+stderr，返回退出码
int run_command(const wxString& command, wxString& output){
    wxArrayString out;
    wxArrayString err;
    const int code = wxExecute(command, out, err, wxEXEC_SYNC | wxEXEC_HIDE_CONSOLE);
    wxString combined;
    for(const wxString& line : out) combined += line + wxT("\n");
    for(const wxString& line : err) combined += line + wxT("\n");
    output = combined;
    return code;
}

// 带自定义环境的命令执行（env 为空时环境不变）
int run_command_env(const wxString& command, const wxExecuteEnv& env, wxString& output){
    wxArrayString out;
    wxArrayString err;
    const int code = wxExecute(command, out, err, wxEXEC_SYNC | wxEXEC_HIDE_CONSOLE, &env);
    wxString combined;
    for(const wxString& line : out) combined += line + wxT("\n");
    for(const wxString& line : err) combined += line + wxT("\n");
    output = combined;
    return code;
}

// 根据模块端口自动生成 testbench：
//   组合逻辑：遍历全部输入组合（输入≤4 时 2^n 个；更多时只遍历前 4 个输入，
//   其余固定 0，避免组合爆炸）。输出用 wire 接收，写 VCD。
wxString build_testbench(const Verilator::Module& module, const wxString& vcd_path){
    wxString tb;
    tb += wxT("`timescale 1ns/1ps\n");
    tb += wxT("module tb_top;\n");

    std::vector<wxString> inputs;
    std::vector<wxString> outputs;
    for(const Verilator::Port& port : module.ports){
        const wxString name = wxString::FromUTF8(port.name.c_str(), port.name.size());
        if(port.direction == Verilator::PortDirection::Input){
            inputs.push_back(name);
            tb += wxT("  reg  ") + name + wxT(";\n");
        }else if(port.direction == Verilator::PortDirection::Output){
            outputs.push_back(name);
            tb += wxT("  wire ") + name + wxT(";\n");
        }
    }

    //实例化被测模块
    tb += wxT("\n  ") + wxString::FromUTF8(module.name.c_str(), module.name.size())
        + wxT(" dut(");
    bool first = true;
    for(const wxString& name : inputs){
        tb += first ? wxT("") : wxT(", ");
        tb += wxT(".") + name + wxT("(") + name + wxT(")");
        first = false;
    }
    for(const wxString& name : outputs){
        tb += first ? wxT("") : wxT(", ");
        tb += wxT(".") + name + wxT("(") + name + wxT(")");
        first = false;
    }
    tb += wxT(");\n");

    //激励：遍历输入组合
    tb += wxT("\n  integer t;\n");
    tb += wxT("  initial begin\n");
    tb += wxT("    $dumpfile(") + quote(vcd_path) + wxT(");\n");
    tb += wxT("    $dumpvars(0, tb_top);\n");

    const std::size_t swept = std::min<std::size_t>(inputs.size(), 4);  //最多扫4个输入
    const unsigned long long count = 1ULL << swept;
    for(unsigned long long pattern = 0; pattern < count; ++pattern){
        for(std::size_t i = 0; i < inputs.size(); ++i){
            const int bit = i < swept
                ? (int)((pattern >> i) & 1ULL)
                : 0;
            tb += wxT("    ") + inputs[i] + wxT(" = ") + (bit ? wxT("1") : wxT("0"))
                + wxT("; ");
        }
        tb += wxT("#10;\n");
    }
    tb += wxT("    #10;\n");
    tb += wxT("    $finish;\n");
    tb += wxT("  end\n");
    tb += wxT("endmodule\n");
    return tb;
}

} // namespace

bool find_tool(const wxString& name, wxString& found_path){
    //wxWidgets 3.2 没有 wxFindExecutable，手动遍历 PATH（Windows 分隔符 ';'，其余 ':'）
    const wxString path_env = wxGetenv(wxT("PATH"));
    if(path_env.IsEmpty()) return false;

    wxArrayString dirs;
    if(path_env.Find(wxT(';')) != wxNOT_FOUND) dirs = wxSplit(path_env, wxT(';'));
    else                                         dirs = wxSplit(path_env, wxT(':'));
    for(const wxString& dir : dirs){
        wxString candidate = dir;
        if(!candidate.IsEmpty() && candidate.Last() != wxT('/') && candidate.Last() != wxT('\\')){
            candidate += wxT('/');
        }
        candidate += name;
#ifdef _WIN32
        if(!candidate.Lower().EndsWith(wxT(".exe"))) candidate += wxT(".exe");
#endif
        if(wxFileExists(candidate) && wxIsExecutable(candidate)){
            found_path = candidate;
            return true;
        }
    }
    return false;
}

int lint_verilog(const wxString& source, wxString& report, wxString& err){
    wxString top;
    if(!find_tool(wxT("verilator"), top)){
        err = wxString::FromUTF8("未找到 verilator 可执行程序。请先安装："
                                 "Linux 运行 'sudo apt install verilator'，"
                                 "Windows 使用 MSYS2/MinGW 安装 verilator。");
        return -1;
    }

    const wxString file = work_dir() + wxT("/lint_check.v");
    if(!write_file(file, source)){
        err = wxString::FromUTF8("写临时文件失败：") + file;
        return -1;
    }

    //-Wall 打开全部警告；--quiet 去掉“编译中”噪音，保留问题报告
    const int code = run_command(
        top + wxT(" --lint-only -Wall --quiet --timing -Wno-fatal ") + quote(file),
        report);
    if(report.IsEmpty()){
        report = wxString::FromUTF8(code == 0 ? "（verilator 无输出）"
                                              : "（verilator 无输出，退出码 ");
        if(code != 0) report += wxString::Format(wxT("%d）"), code);
    }
    return code;
}

bool generate_waveform(const wxString& verilog_source,
                       const wxString& vcd_path,
                       wxString& report,
                       wxString& err){
    wxString verilator;
    if(!find_tool(wxT("verilator"), verilator)){
        err = wxString::FromUTF8("未找到 verilator 可执行程序。请先安装："
                                 "Linux 运行 'sudo apt install verilator'，"
                                 "Windows 使用 MSYS2/MinGW 安装 verilator。");
        return false;
    }

    //1) 解析源码拿端口（也是语法检查）
    const std::string source_utf8 = verilog_source.ToUTF8().data();
    const Verilator::ParseResult parsed = Verilator::parse_verilog(source_utf8);
    if(!parsed.ok){
        wxString diag;
        for(const Verilator::Diagnostic& d : parsed.diagnostics){
            diag += wxString::FromUTF8(d.message.c_str(), d.message.size()) + wxT("\n");
        }
        err = wxString::FromUTF8("Verilog 源码无法解析，无法生成波形：\n") + diag;
        return false;
    }
    if(parsed.module.ports.empty()){
        err = wxString::FromUTF8("模块没有输入/输出端口，无法生成激励。");
        return false;
    }

    //2) 生成 testbench 并写盘
    const wxString dir = work_dir();
    const wxString top_file = dir + wxT("/top.v");
    const wxString tb_file  = dir + wxT("/tb_top.v");
    if(!write_file(top_file, verilog_source)){
        err = wxString::FromUTF8("写临时文件失败：") + top_file;
        return false;
    }
    const wxString tb = build_testbench(parsed.module, vcd_path);
    if(!write_file(tb_file, tb)){
        err = wxString::FromUTF8("写 testbench 失败：") + tb_file;
        return false;
    }

    //3) verilator --binary 编译（--timing 支持 #delay；--trace 才能产生 VCD；
    //   --top-module 指定 tb 为顶层）
    //   CCACHE_DISABLE=1：verilator 内部 make 会用 ccache 加速，但缓存目录不可写
    //   时会导致编译失败（只读 HOME/沙箱环境），禁用后一定可用。
    //   --timescale 统一时间单位，避免 TIMESCALEMOD 警告。
    const wxString obj_dir = dir + wxT("/obj_dir");
    wxString compile_cmd = verilator
        + wxT(" --binary --timing --trace --timescale \"1ns/1ps\" -Wno-fatal -Wno-lint "
              "--quiet --top-module tb_top -Mdir ") + quote(obj_dir) + wxT(" ")
        + quote(top_file) + wxT(" ") + quote(tb_file);
    wxExecuteEnv compile_env;
#ifndef _WIN32
    wxGetEnvMap(&compile_env.env);
    compile_env.env[wxT("CCACHE_DISABLE")] = wxT("1");
#endif
    const int compile_code = run_command_env(compile_cmd, compile_env, report);
    if(compile_code != 0){
        report = wxString::FromUTF8("verilator 编译失败（退出码 ")
               + wxString::Format(wxT("%d）：\n"), compile_code) + report;
        return false;
    }

    //4) 运行仿真，生成 VCD
    wxString run_output;
    const int run_code = run_command(quote(obj_dir + wxT("/Vtb_top")), run_output);
    if(run_code != 0){
        report += wxString::FromUTF8("仿真运行失败（退出码 ")
               + wxString::Format(wxT("%d）：\n"), run_code) + run_output;
        return false;
    }

    //5) 确认 VCD 已生成
    if(!wxFileExists(vcd_path)){
        report += wxString::FromUTF8("未生成波形文件：") + vcd_path;
        err = wxString::FromUTF8("仿真未产生 VCD 文件。");
        return false;
    }
    report += wxString::FromUTF8("波形已生成：") + vcd_path + wxT("\n");
    return true;
}

bool open_waveform(const wxString& vcd_path, wxString& err){
    wxString gtkwave;
    if(!find_tool(wxT("gtkwave"), gtkwave)){
        err = wxString::FromUTF8("未找到 gtkwave。请先安装："
                                 "Linux 运行 'sudo apt install gtkwave'，"
                                 "Windows 从官网下载安装包。");
        return false;
    }
    //异步打开，不阻塞本程序
    wxExecute(gtkwave + wxT(" ") + quote(vcd_path), wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE);
    return true;
}

} // namespace VerilogTools

#include "./VerilogTools.hpp"

#include "../verilator/VerilatorApi.hpp"
#include "../verilator/model/CircuitModel.hpp"

#include <wx/arrstr.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace VerilogTools {

namespace {

// 所有会写共享临时文件（lint_check.v / top.v / tb_top.v / obj_dir）并执行
// verilator 的操作都要串行化：并发调用会互相覆盖临时文件导致结果错乱。
std::mutex s_tool_mutex;

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

bool read_file(const wxString& path, std::string& content){
    FILE* file = std::fopen(path.ToUTF8().data(), "rb");
    if(!file) return false;
    std::string out;
    char buf[4096];
    std::size_t n;
    while((n = std::fread(buf, 1, sizeof(buf), file)) > 0){
        out.append(buf, n);
    }
    std::fclose(file);
    content = std::move(out);
    return true;
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

// 通过 /bin/sh（Windows 用 cmd）执行命令并收集 stdout+stderr。
// 用 libc 的 popen 而不是 wxExecute：wxExecute 只能从主线程调用，
// 而本模块会在后台工作线程里执行 verilator 编译/仿真，必须线程安全。
int run_command(const wxString& command, wxString& output){
#ifdef _WIN32
    FILE* pipe = _popen((command + wxT(" 2>&1")).ToUTF8().data(), "r");
#else
    FILE* pipe = popen((command + wxT(" 2>&1")).ToUTF8().data(), "r");
#endif
    if(!pipe) return -1;

    std::string out;
    char buf[4096];
    std::size_t n;
    while((n = std::fread(buf, 1, sizeof(buf), pipe)) > 0){
        out.append(buf, n);
    }
#ifdef _WIN32
    const int status = _pclose(pipe);
    const int code = status;   //Windows 下 _pclose 直接返回退出码
#else
    const int status = pclose(pipe);
    const int code = (status == -1) ? -1 : WEXITSTATUS(status);
#endif
    output = wxString::FromUTF8(out.c_str(), out.size());
    return code;
}

// 生成 GTKWave 保存文件(.gtkw)：解析 VCD 中最外层 scope 的信号名，
// gtkwave 打开时会自动把这些信号加入波形区（否则默认只显示空波形，
// 用户必须手动导入信号）。格式参照 GTKWave 官方示例 save file。
// 返回 true 表示文件写成功。
bool write_gtkw_save(const wxString& vcd_path){
    std::string vcd;
    if(!read_file(vcd_path, vcd)) return false;

    std::vector<std::string> scopes;        //scope 名栈
    std::vector<std::string> top_signals;   //最外层 scope 的信号全名
    int depth = 0;
    std::size_t pos = 0;
    while(pos < vcd.size()){
        const std::size_t nl = vcd.find('\n', pos);
        std::string line = vcd.substr(pos, (nl == std::string::npos ? vcd.size() : nl) - pos);
        pos = (nl == std::string::npos) ? vcd.size() : nl + 1;
        if(!line.empty() && line.back() == '\r') line.pop_back();

        std::vector<std::string> tokens;
        std::size_t p = 0;
        while(p < line.size()){
            while(p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
            if(p >= line.size()) break;
            std::size_t q = p;
            while(q < line.size() && line[q] != ' ' && line[q] != '\t') ++q;
            tokens.push_back(line.substr(p, q - p));
            p = q;
        }
        if(tokens.empty()) continue;

        if(tokens[0] == "$scope"){
            scopes.push_back(tokens.size() >= 3 ? tokens[2] : std::string());
            ++depth;
        }else if(tokens[0] == "$upscope"){
            if(!scopes.empty()) scopes.pop_back();
            if(depth > 0) --depth;
        }else if(tokens[0] == "$var" && depth == 1 && tokens.size() >= 4){
            //$var 行：$var wire 1 " a $end，信号名是倒数第二个 token
            const std::string name = tokens[tokens.size() - 2];
            std::string full;
            for(const std::string& s : scopes){ full += s; full += '.'; }
            full += name;
            top_signals.push_back(full);
        }
    }

    const wxString gtkw_path = vcd_path.BeforeLast(wxT('.')) + wxT(".gtkw");
    wxString content;
    content += wxT("[*]\n[*] GTKWave save file (TinyEDA)\n[*]\n");
    content += wxT("[dumpfile] ") + quote(vcd_path) + wxT("\n");
    content += wxT("[timestart] 0\n");
    content += wxT("[size] 1100 600\n");
    content += wxT("[pos] -1 -1\n");
    content += wxT("*-3.973790 10 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1\n");
    content += wxT("[sst_width] 220\n");
    content += wxT("[signals_width] 107\n");
    content += wxT("[sst_expanded] 1\n");
    content += wxT("[sst_vpaned_height] 152\n");
    for(const std::string& signal : top_signals){
        content += wxT("@28\n");
        content += wxString::FromUTF8(signal.c_str(), signal.size()) + wxT("\n");
    }
    return write_file(gtkw_path, content);
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
    tb += wxT("\n  initial begin\n");
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
    std::lock_guard<std::mutex> lock(s_tool_mutex);
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

    //-Wall 打开全部警告；--quiet 去掉“编译中”噪音，保留问题报告。
    //-Wno-DECLFILENAME：lint 临时文件叫 lint_check.v，与模块名不匹配会刷警告
    const int code = run_command(
        top + wxT(" --lint-only -Wall --quiet --timing -Wno-fatal "
                  "-Wno-DECLFILENAME ") + quote(file),
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
    std::lock_guard<std::mutex> lock(s_tool_mutex);
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
    //   CCACHE_DISABLE=1（shell 前缀）：verilator 内部 make 会用 ccache 加速，
    //   但缓存目录不可写时会导致编译失败（只读 HOME/沙箱环境），禁用后一定可用。
    //   --timescale 统一时间单位，避免 TIMESCALEMOD 警告。
    const wxString obj_dir = dir + wxT("/obj_dir");
    wxString compile_cmd = verilator
        + wxT(" --binary --timing --trace --timescale \"1ns/1ps\" -Wno-fatal -Wno-lint "
              "--quiet --top-module tb_top -Mdir ") + quote(obj_dir) + wxT(" ")
        + quote(top_file) + wxT(" ") + quote(tb_file);
#ifndef _WIN32
    compile_cmd = wxT("CCACHE_DISABLE=1 ") + compile_cmd;
#endif
    const int compile_code = run_command(compile_cmd, report);
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
    //生成保存文件(.gtkw)：gtkwave 打开时自动把信号加载到波形区，
    //否则默认只显示空波形（用户必须手动导入信号）。
    //同时生成 rc 配置文件：do_initial_zoom_fit 让打开时自动缩放铺满整个
    //波形，避免初始缩放把几十 ns 的波形压缩成一条直线。
    //（不用 Tcl 脚本 -S：gtkwave 启动执行脚本出错会直接退出进程。）
    const wxString gtkw_path = vcd_path.BeforeLast(wxT('.')) + wxT(".gtkw");
    const wxString rc_path   = vcd_path.BeforeLast(wxT('.')) + wxT(".rc");
    write_gtkw_save(vcd_path);
    write_file(rc_path, wxT("do_initial_zoom_fit 1\n"));

    //后台启动 gtkwave 并立即返回，不阻塞：Unix 用 shell 的 &，Windows 用 start。
    //不能用 wxExecute(ASYNC)——wxExecute 只能从主线程调用，这里可能在工作线程。
    wxString cmd = gtkwave + wxT(" -r ") + quote(rc_path)
                 + wxT(" ") + quote(vcd_path) + wxT(" ") + quote(gtkw_path);
    char buf[64];
#ifdef _WIN32
    cmd = wxT("start \"\" ") + cmd;
    FILE* pipe = _popen(cmd.ToUTF8().data(), "r");
#else
    cmd += wxT(" >/dev/null 2>&1 &");
    FILE* pipe = popen(cmd.ToUTF8().data(), "r");
#endif
    if(pipe){
        while(std::fread(buf, 1, sizeof(buf), pipe) > 0){}   //读掉输出再关闭管道
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
    }
    return true;
}

} // namespace VerilogTools

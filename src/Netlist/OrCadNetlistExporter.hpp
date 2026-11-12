#pragma once

#include "Netlist.hpp"
#include "PcbNetlist.hpp"

#include <string>
#include <vector>

namespace NetlistData {

struct OrCadNetlistExportOptions {
    std::string sourcePath;              // 原理图/项目文件路径，写入头部注释
    std::string date;                    // 日期字符串；为空时使用当前本地日期
    std::string toolName = "TinyEDA";    // 写入头部注释
};

struct OrCadNetlistWriteResult {
    std::vector<Diagnostic> diagnostics;

    bool ok() const;
};

// 将 PCB 网表序列化为 OrCAD PCB2 网表（KiCad 社区版 S-表达式格式）。
// 格式对照目标：KiCad 官方文档 eeschema_advanced.adoc 的
// "OrcadPCB2 netlist example using XSLT"（SoftPLC Corporation, GPL v2）：
//   ( { Eeschema Netlist Version 1.1  <date>
//   <tool>}
//    ( <tstamp> <footprint|$noname> <ref> <value|"~">
//     (  <pin_number> <net_name> )
//     ...
//    )
//    ...
//   )
//   *
// 单节点网络用 "?" 表示，无名字网络用 "$N-0<序号>" 表示，与参考实现一致。
// 只消费 PcbNetlist，不读取画布、项目 XML 或 GUI 状态。
class OrCadNetlistExporter {
public:
    // 校验并序列化。校验失败时返回空字符串，诊断写入 diagnostics（可为 nullptr）。
    static std::string serialize(const PcbNetlist& netlist,
                                 const OrCadNetlistExportOptions& options = {},
                                 std::vector<Diagnostic>* diagnostics = nullptr);

    // 校验、序列化并写文件。任何校验或写入失败都会返回 error 诊断。
    static OrCadNetlistWriteResult write(const PcbNetlist& netlist,
                                         const std::string& path,
                                         const OrCadNetlistExportOptions& options = {});
};

} // namespace NetlistData

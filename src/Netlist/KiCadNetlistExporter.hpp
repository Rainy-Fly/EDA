#pragma once

#include "Netlist.hpp"
#include "PcbNetlist.hpp"

#include <string>
#include <vector>

namespace NetlistData {

struct KiCadNetlistExportOptions {
    std::string sourcePath;              // 原理图/项目文件路径，写入 design/source
    std::string date;                    // ISO 8601 日期；为空时使用当前本地日期
    std::string toolName = "TinyEDA";    // 写入 design/tool
};

struct KiCadNetlistWriteResult {
    std::vector<Diagnostic> diagnostics;

    bool ok() const;
};

// 将 PCB 网表序列化为 KiCad S-expression 网表（export version "E"）。
// 格式对照目标：KiCad 6+ 通用 "E" 格式；结构与字段已用 KiCad 10.0.6
// (kicad-cli sch export netlist) 生成的参考网表及 kicad_netlist_reader.cpp 核对。
// 只消费 PcbNetlist，不读取画布、项目 XML 或 GUI 状态。
class KiCadNetlistExporter {
public:
    // 校验并序列化。校验失败时返回空字符串，诊断写入 diagnostics（可为 nullptr）。
    static std::string serialize(const PcbNetlist& netlist,
                                 const KiCadNetlistExportOptions& options = {},
                                 std::vector<Diagnostic>* diagnostics = nullptr);

    // 校验、序列化并写文件。任何校验或写入失败都会返回 error 诊断。
    static KiCadNetlistWriteResult write(const PcbNetlist& netlist,
                                         const std::string& path,
                                         const KiCadNetlistExportOptions& options = {});
};

} // namespace NetlistData

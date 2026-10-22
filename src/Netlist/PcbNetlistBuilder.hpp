#pragma once

#include "Netlist.hpp"
#include "PcbNetlist.hpp"
#include "PcbProjectSnapshot.hpp"

#include <optional>
#include <vector>

namespace NetlistData {

struct PcbNetlistBuildResult {
    std::optional<PcbNetlist> value;
    std::vector<Diagnostic> diagnostics;

    bool ok() const;
};

// 从只读项目快照构建 PCB 网表：
//   - 不要求元件具备仿真/综合模型；
//   - 只按导线首尾端点与引脚坐标的精确重合建立连接；
//   - 线段交叉/中段碰触不建立连接，并产生诊断；
//   - 不修改输入，不依赖 GUI 或项目 XML。
class PcbNetlistBuilder {
public:
    static PcbNetlistBuildResult build(const PcbProjectSnapshot& project);
};

} // namespace NetlistData

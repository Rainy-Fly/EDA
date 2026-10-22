#pragma once

#include "Netlist.hpp"
#include "PcbNetlist.hpp"

#include <string>
#include <vector>

namespace NetlistData {

struct PcbPoint {
    int x = 0;
    int y = 0;
};

bool operator==(const PcbPoint& left, const PcbPoint& right);

// 适配层解析出的物理引脚：坐标是元件锚点平移后的绝对逻辑坐标。
struct PcbSnapshotPin {
    std::string id;
    std::string name;
    std::string number; // 缺失时为空，由 Builder 报告诊断
    PcbPoint position;
};

// 适配层解析出的元件实例。reference/referencePrefix/value/footprint 来自
// 元件元数据或项目实例覆盖参数，不在 Builder 内按类型猜测。
struct PcbSnapshotComponent {
    std::string instanceId;
    std::string type;
    std::string category;
    std::string reference;       // 显式位号；为空时由 referencePrefix 自动编号
    std::string referencePrefix; // 位号前缀（如 "R"）
    std::string value;
    std::string footprint;
    // 电源/地等非物理符号：参与连通性，但不导出为 PCB 元件。
    bool exportable = true;
    std::vector<PcbSnapshotPin> pins;
};

// 导线/总线。points 是完整折线；只有首尾端点参与电气连接，
// 内部折点、线段交叉和线段中段碰触都不产生连接。
struct PcbSnapshotWire {
    std::string id;
    std::vector<PcbPoint> points;
    bool bus = false;
};

// 构建 PCB 网表的只读输入快照。
struct PcbProjectSnapshot {
    std::string projectName;
    std::vector<PcbSnapshotComponent> components;
    std::vector<PcbSnapshotWire> wires;
    // 适配层已发现的诊断（如元数据缺失、总线被忽略），由 Builder 合并。
    std::vector<Diagnostic> diagnostics;
};

} // namespace NetlistData

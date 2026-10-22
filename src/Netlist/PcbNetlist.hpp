#pragma once

#include "Netlist.hpp"

#include <string>
#include <vector>

namespace NetlistData {

// PCB 网表的物理引脚：逻辑引脚 ID 与 PCB 焊盘编号。
struct PcbPin {
    std::string id;     // 原理图/元件库引脚 ID（如 "A"、"Y"）
    std::string name;   // 显示名（可为空）
    std::string number; // PCB 焊盘编号（如 "1"）
};

// PCB 网表中的一个物理元件。
struct PcbComponent {
    std::string instanceId; // 项目内实例 ID
    std::string type;       // 元件库类型键
    std::string reference;  // 位号（如 "R1"）
    std::string value;      // 元件值（如 "10k"）
    std::string footprint;  // KiCad 封装标识
    std::vector<PcbPin> pins;
};

// 网络节点：位号 + 物理焊盘编号。
struct PcbNode {
    std::string reference;
    std::string pinNumber;
    std::string pinType = "passive";
};

// PCB 网络。id 由转换器确定性生成，不参与项目 XML 持久化。
struct PcbNet {
    std::string id;
    std::string name;
    std::vector<PcbNode> nodes;
};

// 面向 PCB 的中间数据模型：不依赖 wxWidgets，也不包含 KiCad 序列化逻辑。
struct PcbNetlist {
    std::string projectName;
    std::vector<PcbComponent> components;
    std::vector<PcbNet> nets;
};

} // namespace NetlistData

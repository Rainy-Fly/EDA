#pragma once

#include "PcbProjectSnapshot.hpp"

#include "../DealProjectXML/DealProjectXML.hpp"

namespace NetlistData {

// 将项目数据（XML 2.0：ComponentData/WireData + junction/label）转换为只读 PCB 快照。
//
// 连通性规则：
//   - 2.0 导线优先使用 start/end 显式引用（pin/junction/label）；
//   - 旧 1.0 导线没有引用时，回退到首尾端点与引脚坐标的精确重合；
//   - 显式引用有效时用引用对象的当前坐标校正几何端点；
//   - 线段交叉、线段中段碰触不建立连接，并产生 warning；
//   - 总线不参与 PCB 网表。
//
// 位号/值/封装/引脚编号只从实例字段、参数或元件定义读取，缺失时由 Builder 诊断，
// 不在此处按元件类型猜测。
PcbProjectSnapshot BuildPcbSnapshotFromProject(const DealProjectXML::ProjectData& project);

} // namespace NetlistData

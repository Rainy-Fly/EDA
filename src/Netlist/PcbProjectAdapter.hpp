#pragma once

#include "PcbProjectSnapshot.hpp"

#include "../DealProjectXML/DealProjectXML.hpp"

namespace NetlistData {

// 将当前项目数据（XML 1.0 模型：ComponentData + WireData）转换为只读 PCB 快照。
//
// 连通性规则（与当前画布模型一致）：
//   - 只有导线首尾端点与元件引脚坐标精确重合才算连接；
//   - 共享同一坐标的导线端点相互连接；
//   - 线段交叉、线段中段碰触不建立连接，并产生 warning；
//   - 总线不参与 PCB 网表。
//
// 位号/值/封装/引脚编号只从实例参数（params）读取，缺失时由 Builder 诊断，
// 不在此处按元件类型猜测。
PcbProjectSnapshot BuildPcbSnapshotFromProject(const DealProjectXML::ProjectData& project);

} // namespace NetlistData

#pragma once
#include"../Simulation/SimulationProject.hpp"

namespace DealProjectXML{ struct ProjectData; }

// 项目数据（XML 2.0 模型）→ 仿真输入（Simulation::SimProject）的桥接层。
//
// 放在 GUI 层（Canva 目录）是因为它要用元件元数据解析引脚并按实例旋转/镜像变换；
// 仿真核心（Simulation::SimulationAdapter/Session）保持与 wx 无关，可以独立自检。
namespace SimulationBridge{

// 只读转换：不改动项目数据，也不依赖画布窗口。
// 引脚坐标 = 元件锚点 + 元数据引脚偏移（已按 rotation/mirror 变换）。
Simulation::SimProject BuildSimulationProject(const DealProjectXML::ProjectData& project);

} // namespace SimulationBridge

#pragma once
#include<string>
#include<vector>

#include"SimulationCircuit.hpp"
#include"SimulationResult.hpp"

// 仿真电路的结构校验（只检查“电路描述是否自洽”，不做求值）。
// 与求值分开，便于 GUI/适配器在仿真前先给出明确的错误定位。
namespace Simulation{

// 校验内容：
//   - 元件 ID 非空且唯一；探针 ID 非空且唯一
//   - 元件类型受支持（GateKind::Unsupported 明确报错）
//   - 门的输入/输出引脚数量符合要求（NOT=1，AND/OR/XOR/NAND/NOR>=2，Input=0）
//   - 元件/探针/网络初值引用的网络都已声明
//   - 一个网络最多一个驱动源（本阶段不做多驱动解析，直接报错）
std::vector<SimulationDiagnostic> validate_circuit(const SimulationCircuit& circuit);

// 校验结果里是否有 error 级诊断
bool has_error(const std::vector<SimulationDiagnostic>& diagnostics);

} // namespace Simulation

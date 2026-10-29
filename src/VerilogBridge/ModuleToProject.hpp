#pragma once

#include "../DealProjectXML/DealProjectXML.hpp"
#include "../verilator/model/CircuitModel.hpp"

#include <wx/gdicmn.h>

#include <string>
#include <vector>

namespace VerilogBridge{

// 单模块 Verilog 电路模型 -> 扁平项目数据（端口端子 + 门 + 电源/地 + 导线）。
// 元件坐标由拓扑分层自动布局生成，origin 是布局区域左上角。
struct ModuleToProjectResult{
    bool ok = false;
    DealProjectXML::ProjectData project;
    std::vector<Verilator::Diagnostic> diagnostics;
};

ModuleToProjectResult BuildProjectFromModule(const Verilator::Module& module,
                                             wxPoint origin);

} // namespace VerilogBridge

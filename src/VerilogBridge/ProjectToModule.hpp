#pragma once

#include "../DealProjectXML/DealProjectXML.hpp"
#include "../verilator/model/CircuitModel.hpp"
#include "../verilator/include/VerilatorTypes.hpp"

#include <string>
#include <vector>

namespace VerilogBridge{

// 画布/项目数据 -> Verilator 电路模型。
// 只处理扁平项目：门元件、输入/输出端子、电源/地、导线/junction/label。
// 不支持的元件、多重驱动、悬空门输入等都会写入 diagnostics，不静默丢弃。
struct ProjectToModuleResult{
    bool ok = false;
    Verilator::Module module;
    std::vector<Verilator::Diagnostic> diagnostics;
};

// topName 为空或非法时回退为 "top"。
ProjectToModuleResult BuildModuleFromProject(const DealProjectXML::ProjectData& project,
                                             const std::string& topName);

} // namespace VerilogBridge

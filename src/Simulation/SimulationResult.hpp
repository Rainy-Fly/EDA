#pragma once
#include<map>
#include<string>
#include<vector>

#include"LogicValue.hpp"

// 一次组合逻辑求值的结果与诊断（与 GUI 无关）。
namespace Simulation{

// 一条诊断：稳定 code 供测试/UI 定位；
// severity = "error"（求值失败）或 "warning"（不影响结果）
struct SimulationDiagnostic{
    std::string severity;                //"error"/"warning"
    std::string code;                    //如 SIMULATION_COMBINATIONAL_LOOP
    std::string message;                 //给人看的说明（含 ID/网络名）
    std::vector<std::string> objectIds;  //相关元件/网络/探针 ID
};

inline SimulationDiagnostic make_diagnostic(const std::string& severity,
                                            const std::string& code,
                                            const std::string& message,
                                            std::vector<std::string> objectIds = {}){
    SimulationDiagnostic d;
    d.severity = severity;
    d.code = code;
    d.message = message;
    d.objectIds = std::move(objectIds);
    return d;
}

inline SimulationDiagnostic make_error(const std::string& code, const std::string& message,
                                       std::vector<std::string> objectIds = {}){
    return make_diagnostic("error", code, message, std::move(objectIds));
}

inline SimulationDiagnostic make_warning(const std::string& code, const std::string& message,
                                         std::vector<std::string> objectIds = {}){
    return make_diagnostic("warning", code, message, std::move(objectIds));
}

// 跑一次组合逻辑后的快照：每个网络的值 + 每个输出探针看到的网络值。
struct SimulationResult{
    bool ok = false;                               //没有任何 error 级诊断
    std::map<std::string, LogicValue> netValues;   //网络 -> 值
    std::map<std::string, LogicValue> probeValues; //探针ID -> 观察到的值
    std::vector<SimulationDiagnostic> diagnostics;
    int iterations = 0;                            //实际迭代轮数（调试/测试用）

    bool has_error() const{
        for(const auto& diagnostic : diagnostics)
            if(diagnostic.severity == "error") return true;
        return false;
    }

    bool has_diagnostic(const std::string& code) const{
        for(const auto& diagnostic : diagnostics)
            if(diagnostic.code == code) return true;
        return false;
    }

    //查不到时返回 Unknown（不抛异常，符合“缺失即未知”的语义）
    LogicValue net_value(const std::string& net) const{
        const auto it = netValues.find(net);
        return (it == netValues.end()) ? LogicValue::Unknown : it->second;
    }

    LogicValue probe_value(const std::string& probeId) const{
        const auto it = probeValues.find(probeId);
        return (it == probeValues.end()) ? LogicValue::Unknown : it->second;
    }
};

} // namespace Simulation

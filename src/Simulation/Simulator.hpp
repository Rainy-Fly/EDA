#pragma once
#include<map>
#include<string>
#include<vector>

#include"SimulationCircuit.hpp"
#include"SimulationResult.hpp"

// 最小组合逻辑仿真器（与 GUI 无关）。
//
// 用法：
//   Simulation::Simulator sim(circuit);            // 构造时做一次结构校验
//   sim.set_input("sw1", Simulation::LogicValue::One);
//   Simulation::SimulationResult r = sim.evaluate();   // 迭代求值到不动点
//   r.probe_value("lamp1") / r.net_value("n1")
//
// 行为约定（见《仿真任务规划.md》3.3）：
//   - 网络初值为 X，可用 SimulationCircuit::initialNetValues 覆盖；
//   - 迭代求值，上限 2*元件数+16：超过仍不稳定就报 SIMULATION_COMBINATIONAL_LOOP，
//     不会无限循环；
//   - 只读自己的 SimulationCircuit，不读 GUI 控件、画布像素或 KiCad 网表文件。
namespace Simulation{

class Simulator{
public:
    explicit Simulator(SimulationCircuit circuit);

    //结构诊断（构造时算一次；有 error 级诊断时 evaluate() 直接返回失败）
    const std::vector<SimulationDiagnostic>& diagnostics() const{ return diagnostics_; }
    bool valid() const;

    //设置输入开关的值。返回 false 表示没有这个元件、或它不是 Input 类型
    bool set_input(const std::string& componentId, LogicValue value);
    bool set_input(const std::string& componentId, bool value);

    //跑一次组合逻辑求值；可以反复调用（改了输入再求值即可拿到新结果）
    SimulationResult evaluate();

    //最近一次 evaluate() 得到的值（还没求值时返回 Unknown）
    LogicValue net_value(const std::string& net) const;
    LogicValue probe_value(const std::string& probeId) const;

private:
    //按门类型对一组输入求值
    static LogicValue compute_gate(GateKind kind, const std::vector<LogicValue>& inputs);

    SimulationCircuit circuit_;
    std::vector<SimulationDiagnostic> diagnostics_;
    std::map<std::string, LogicValue> lastNetValues_;
    std::map<std::string, LogicValue> lastProbeValues_;
};

} // namespace Simulation

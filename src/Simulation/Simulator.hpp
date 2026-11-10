#pragma once
#include<map>
#include<string>
#include<vector>

#include"SimulationCircuit.hpp"
#include"SimulationResult.hpp"

// 组合 + 时序仿真器（与 GUI 无关）。
//
// 用法：
//   Simulation::Simulator sim(circuit);            // 构造时做一次结构校验
//   sim.set_input("sw1", Simulation::LogicValue::One);
//   Simulation::SimulationResult r = sim.evaluate();   // 迭代求值到不动点
//   r.probe_value("lamp1") / r.net_value("n1")
//
// 时序行为约定（D触发器 + 时钟源）：
//   - evaluate() 是“一个时间步”：先按当前状态定值（输入开关、时钟源、触发器 Q），
//     组合求值到不动点，再对每个 D触发器做 CLK 上升沿检测（上次 0 → 本次 1），
//     沿上把 D 采入 Q；若发生了采样，再迭代求值一轮让新 Q 传播。
//   - clock_tick()：把全部时钟源的输出翻转一次（0↔1），然后照常 evaluate()，
//     产生一个时钟沿。单步时钟按钮就调它。
//   - D触发器 Q 初值为 0；时钟源输出初值为 0。
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

    //跑一次求值（一个时间步）；可以反复调用（改了输入再求值即可拿到新结果）
    SimulationResult evaluate();

    //推进一个时钟周期：翻转全部时钟源输出并求值（产生一个 CLK 上升沿）
    SimulationResult clock_tick();

    //最近一次求值得到的值（还没求值时返回 Unknown）
    LogicValue net_value(const std::string& net) const;
    LogicValue probe_value(const std::string& probeId) const;

private:
    //按门类型对一组输入求值
    static LogicValue compute_gate(GateKind kind, const std::vector<LogicValue>& inputs);

    //把“当前状态”（输入开关 / 时钟源 / 触发器 Q）写进网络值表
    void apply_state(std::map<std::string, LogicValue>& values) const;
    //对所有 D触发器做 CLK 上升沿检测并采样 D；返回是否有任一触发器更新
    bool sample_dffs(const std::map<std::string, LogicValue>& values);
    //组合迭代求值（DFF/时钟由状态表定值，不参与迭代）；返回是否收敛
    bool iterate_gates(std::map<std::string, LogicValue>& values);

    SimulationCircuit circuit_;
    std::vector<SimulationDiagnostic> diagnostics_;
    std::map<std::string, LogicValue> lastNetValues_;
    std::map<std::string, LogicValue> lastProbeValues_;

    //时序状态：时钟源输出网络 → 当前电平；D触发器输出网络 → 当前 Q；
    //D触发器 CLK 输入网络 → 上次求值时的电平（上升沿检测用）
    std::map<std::string, LogicValue> clockValues_;
    std::map<std::string, LogicValue> dffQ_;
    std::map<std::string, LogicValue> lastClk_;
};

} // namespace Simulation

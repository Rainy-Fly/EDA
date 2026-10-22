#pragma once
#include<map>
#include<memory>
#include<string>
#include<vector>

#include"SimulationAdapter.hpp"
#include"SimulationCircuit.hpp"
#include"SimulationProject.hpp"
#include"SimulationResult.hpp"

namespace Simulation{

class Simulator;

// 一次"仿真会话"：持有适配结果 + 求值器 + 最近一次求值结果。
//
// GUI 侧（画布/主窗口）只通过它取颜色与状态，不直接接触 Simulator：
//   session.rebuild(sim_project);   // 内容变了就重建（内部自动求值）
//   session.wireValue(wireId)       // 1/0/X（未参与仿真时 has=false）
//   session.componentValue(itemId)  // 门=输出、开关=状态、灯/探针=观察值
class SimulationSession{
public:
    SimulationSession();
    ~SimulationSession();

    //仿真开关（菜单 模拟→运行/暂停）。暂停时保留上一次结果，单步时钟才重算
    void setEnabled(bool on){ enabled_ = on; }
    bool enabled() const{ return enabled_; }

    //重建：适配 + 构造求值器 +（运行中时）求值。返回是否有可仿真内容
    bool rebuild(const SimProject& project);
    void clear();

    //强制重算一次（菜单"单步时钟"）
    bool evaluate();

    bool hasModel() const{ return hasModel_; }
    int errorCount() const;
    int warningCount() const;
    int netCount() const;
    int componentCount() const;
    const std::vector<SimulationDiagnostic>& diagnostics() const{ return diagnostics_; }

    //角色查询（画布决定可点击/是否打"不支持"标记）
    bool isSwitch(int componentId) const;
    bool isLamp(int componentId) const;
    bool isSupported(int componentId) const;

    //值查询：has* 用来区分"未参与仿真"和"X"
    bool wireHasValue(int wireId) const;
    LogicValue wireValue(int wireId) const;
    bool pinHasValue(int componentId, const std::string& pinId) const;
    LogicValue pinValue(int componentId, const std::string& pinId) const;
    bool componentHasValue(int componentId) const;
    LogicValue componentValue(int componentId) const;

    //诊断报告（UTF-8 多行文本）：运行状态、每个元件的引脚→网络→电平（含"无驱动/悬空"）、
    //全部诊断码。供「模拟 → 仿真诊断...」窗口直接显示。
    std::string report_text() const;

private:
    LogicValue value_of_net(const std::string& net) const;
    bool net_has_value(const std::string& net) const;

    std::unique_ptr<Simulator> simulator_;
    SimulationModel model_;
    SimulationResult last_;
    std::vector<SimulationDiagnostic> diagnostics_;
    std::map<int, std::string> names_;   //元件ID → 显示名（报告用）
    std::map<int, std::string> types_;   //元件ID → 类型串（报告用）
    bool enabled_ = true;
    bool hasModel_ = false;
};

} // namespace Simulation

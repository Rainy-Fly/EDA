// 仿真适配层 + 会话 自检（不依赖 GUI / 不依赖 wx / 不依赖网表模块）。
// 覆盖：开关→AND→灯、多级组合、junction 合并、同名 label 合并、free 端点不连、
//       坐标兜底（旧 1.0 与旋转后的引脚）、不支持元件、多驱动降级、no_connect、
//       空项目、暂停/单步语义。
#include"../src/Simulation/SimulationAdapter.hpp"
#include"../src/Simulation/SimulationSession.hpp"

#include<iostream>
#include<map>
#include<string>
#include<utility>
#include<vector>

namespace{

int failures = 0;
int checks = 0;

void check(bool condition, const std::string& label){
    ++checks;
    if(!condition){
        std::cerr << "FAIL: " << label << '\n';
        ++failures;
    }
}

using namespace Simulation;

SimPoint P(int x, int y){ return SimPoint(x, y); }

SimPinRef pin(const std::string& id, const std::string& direction, int x, int y){
    SimPinRef ref;
    ref.id = id;
    ref.direction = direction;
    ref.position = P(x, y);
    return ref;
}

SimComponent comp(int id, const std::string& type, const std::string& category,
                  std::vector<SimPinRef> pins,
                  std::map<std::string,std::string> params = {}){
    SimComponent component;
    component.id = id;
    component.type = type;
    component.category = category;
    component.name = type;
    component.pins = std::move(pins);
    component.params = std::move(params);
    return component;
}

SimEndpoint pin_end(int componentId, const std::string& pinId){
    SimEndpoint endpoint;
    endpoint.kind = "pin";
    endpoint.componentId = componentId;
    endpoint.pinId = pinId;
    return endpoint;
}
SimEndpoint junction_end(const std::string& id){
    SimEndpoint endpoint;
    endpoint.kind = "junction";
    endpoint.junctionId = id;
    return endpoint;
}
SimEndpoint label_end(const std::string& id){
    SimEndpoint endpoint;
    endpoint.kind = "label";
    endpoint.labelId = id;
    return endpoint;
}

SimWire wire(int id, SimPoint from, SimPoint to,
             SimEndpoint start = SimEndpoint(), SimEndpoint end = SimEndpoint()){
    SimWire w;
    w.id = id;
    w.type = "WIRE";
    w.points = {from, to};
    w.start = std::move(start);
    w.start.position = from;
    w.end = std::move(end);
    w.end.position = to;
    return w;
}

// ---- 输入/输出端子 & 用例1/2 用到的电路 ----

SimComponent in_terminal(int id, int x, int y, const std::string& state = "0"){
    return comp(id, "IN_TERMINAL", "Simulation", {pin("A", "output", x, y)},
                {{"State", state}});
}
SimComponent out_terminal(int id, int x, int y){
    return comp(id, "OUT_TERMINAL", "Simulation", {pin("A", "input", x, y)});
}

// 输入端子(1) --线--> AND(2) <-VCC(3)；AND --> 灯(4)
SimProject switch_and_lamp(){
    SimProject project;
    project.components.push_back(in_terminal(1, 0, 0, "0"));
    project.components.push_back(comp(2, "AND", "LogicGate",
        {pin("IN1", "input", 60, 0), pin("IN2", "input", 60, 20),
         pin("OUT", "output", 120, 10)}));
    project.components.push_back(comp(3, "POWER", "Source",
        {pin("+", "power", 60, 20), pin("-", "power", 60, 40)}));
    project.components.push_back(comp(4, "Miscellaneous-COM-Lamp-Indicator", "Miscellaneous",
        {pin("A", "passive", 140, 10), pin("B", "passive", 160, 10)}));
    project.wires.push_back(wire(10, P(0, 0), P(60, 0), pin_end(1, "A"), pin_end(2, "IN1")));
    project.wires.push_back(wire(11, P(120, 10), P(140, 10), pin_end(2, "OUT"), pin_end(4, "A")));
    return project;
}

// 输入端子(1) --> AND(2) --> NOT(5) --> 探针(6)
SimProject multilevel(){
    SimProject project;
    project.components.push_back(in_terminal(1, 0, 0, "1"));
    project.components.push_back(comp(2, "AND", "LogicGate",
        {pin("IN1", "input", 60, 0), pin("IN2", "input", 60, 20),
         pin("OUT", "output", 120, 10)}));
    project.components.push_back(comp(3, "POWER", "Source",
        {pin("+", "power", 60, 20), pin("-", "power", 60, 40)}));
    project.components.push_back(comp(5, "NOT", "LogicGate",
        {pin("IN1", "input", 180, 10), pin("OUT", "output", 240, 10)}));
    project.components.push_back(comp(6, "Miscellaneous-COM-Probe_Point", "Miscellaneous",
        {pin("A", "passive", 300, 10)}));
    project.wires.push_back(wire(10, P(0, 0), P(60, 0), pin_end(1, "A"), pin_end(2, "IN1")));
    project.wires.push_back(wire(11, P(120, 10), P(180, 10), pin_end(2, "OUT"), pin_end(5, "IN1")));
    project.wires.push_back(wire(12, P(240, 10), P(300, 10), pin_end(5, "OUT"), pin_end(6, "A")));
    return project;
}

// ---- 用例1：开关 → AND → 灯（点击开关切换 0/1） ----
void test_switch_and_lamp(){
    SimProject project = switch_and_lamp();
    SimulationSession session;
    check(session.rebuild(project), "开关+AND+灯：有可仿真内容");
    check(session.errorCount() == 0, "开关+AND+灯：没有 error 级诊断");
    check(session.hasModel(), "会话有模型");
    check(session.componentValue(2) == LogicValue::Zero, "State=0：AND 输出 0");
    check(session.componentValue(4) == LogicValue::Zero, "State=0：灯灭");
    check(session.wireHasValue(10) && session.wireHasValue(11), "两根导线都有值");
    check(session.wireValue(10) == LogicValue::Zero, "输入导线 = 0");
    check(session.wireValue(11) == LogicValue::Zero, "输出导线 = 0");
    check(session.isSwitch(1), "开关角色可查");
    check(session.isLamp(4), "灯角色可查");
    check(session.isSupported(2), "AND 参与仿真");

    //模拟"单击开关"：改 State 后重建
    project.components[0].params["State"] = "1";
    check(session.rebuild(project), "改 State 后重建成功");
    check(session.componentValue(2) == LogicValue::One, "State=1：AND 输出 1");
    check(session.componentValue(4) == LogicValue::One, "State=1：灯亮");
    check(session.wireValue(10) == LogicValue::One, "输入导线 = 1");
    check(session.wireValue(11) == LogicValue::One, "输出导线 = 1");
}

// ---- 用例2：多级组合（改输入→输出跟着变） ----
void test_multilevel(){
    SimProject project = multilevel();
    SimulationSession session;
    check(session.rebuild(project), "多级电路重建成功");
    // 1 AND 1 = 1，NOT 1 = 0
    check(session.componentValue(5) == LogicValue::Zero, "多级：NOT 输出 0");
    // 探针观察的是 NOT 的输出网络
    check(session.componentHasValue(6) && session.componentValue(6) == LogicValue::Zero,
          "多级：探针读到 0");

    project.components[0].params["State"] = "0";
    check(session.rebuild(project), "多级：改输入后重建成功");
    check(session.componentValue(5) == LogicValue::One, "多级：NOT 输出变 1");
    check(session.componentValue(6) == LogicValue::One, "多级：探针跟着变 1");
}

// ---- 用例3：junction 合并三条支路 ----
void test_junction(){
    SimProject project;
    project.components.push_back(in_terminal(1, -20, 0, "1"));
    project.components.push_back(comp(2, "Miscellaneous-COM-Probe_Point", "Miscellaneous",
        {pin("A", "passive", 80, 0)}));
    project.junctionPositions.push_back(P(30, 0));
    project.wires.push_back(wire(10, P(-20, 0), P(30, 0), pin_end(1, "A"), junction_end("J1")));
    project.wires.push_back(wire(11, P(30, 0), P(80, 0), junction_end("J1"), pin_end(2, "A")));
    project.wires.push_back(wire(12, P(30, 0), P(30, -40), junction_end("J1")));

    SimulationSession session;
    check(session.rebuild(project), "junction 电路重建成功");
    const SimulationModel model = build_simulation_model(project);
    const std::string netSwitch = model.binding.pinNet.at({1, "A"});
    const std::string netProbe  = model.binding.pinNet.at({2, "A"});
    check(netSwitch == netProbe, "junction：端子与探针在同一网络");
    check(model.binding.wireNet.at(10) == netSwitch, "junction：导线 10 同网");
    check(model.binding.wireNet.at(11) == netSwitch, "junction：导线 11 同网");
    check(model.binding.wireNet.at(12) == netSwitch, "junction：导线 12 同网");
    check(session.componentValue(2) == LogicValue::One, "junction：探针读到开关的 1");
}

// ---- 用例4：同名 label 合并；free 端点互不相连 ----
void test_labels_and_free(){
    {   //两个不同 id、同名 CLK 的标签 => 同一网络
        SimProject project;
        project.components.push_back(comp(1, "Miscellaneous-COM-Probe_Point", "Miscellaneous",
            {pin("A", "passive", 120, 0)}));
        project.labels.push_back({"L1", "CLK", "local", P(0, 0)});
        project.labels.push_back({"L2", "CLK", "local", P(100, 0)});
        project.wires.push_back(wire(10, P(0, 0), P(40, 0), label_end("L1")));
        project.wires.push_back(wire(11, P(100, 0), P(120, 0), label_end("L2"), pin_end(1, "A")));
        SimulationModel model = build_simulation_model(project);
        check(model.binding.wireNet.at(10) == model.binding.wireNet.at(11),
              "同名 label => 同一网络");
    }
    {   //两个自由端点，坐标不同 => 不同网络
        SimProject project;
        project.wires.push_back(wire(10, P(0, 0), P(20, 0)));
        project.wires.push_back(wire(11, P(100, 0), P(120, 0)));
        SimulationModel model = build_simulation_model(project);
        check(model.binding.wireNet.at(10) != model.binding.wireNet.at(11),
              "自由端点不自动相连");
    }
}

// ---- 用例5：坐标兜底（旧 1.0 / 旋转后的引脚位置） ----
void test_geometry_fallback(){
    SimProject project;
    //假设元件被旋转 90°：引脚落在 (0,-50)
    project.components.push_back(comp(1, "Miscellaneous-COM-Probe_Point", "Miscellaneous",
        {pin("A", "passive", 0, -50)}));
    //端点 kind 为空（旧 1.0），仅靠坐标重合
    project.wires.push_back(wire(10, P(0, -50), P(0, -90)));
    const SimulationModel model = build_simulation_model(project);
    check(model.binding.pinNet.at({1, "A"}) == model.binding.wireNet.at(10),
          "端点按坐标重合也能连到引脚（旧 1.0 / 旋转后）");
}

// ---- 用例6：不支持的元件 ----
void test_unsupported(){
    SimProject project;
    project.components.push_back(comp(1, "Miscellaneous-COM-Probe_Point", "Miscellaneous",
        {pin("A", "passive", 0, 0)}));
    project.components.push_back(comp(2, "RESISTOR", "Passive",
        {pin("1", "passive", 40, 0), pin("2", "passive", 80, 0)}));
    project.components.push_back(comp(3, "Miscellaneous-COM-Probe_Point", "Miscellaneous",
        {pin("A", "passive", 80, 0)}));
    project.wires.push_back(wire(10, P(0, 0), P(40, 0), pin_end(1, "A"), pin_end(2, "1")));
    project.wires.push_back(wire(11, P(80, 0), P(120, 0), pin_end(2, "2"), pin_end(3, "A")));

    const SimulationModel model = build_simulation_model(project);
    check(model.binding.role.at(2) == SimRole::Unsupported, "电阻 => Unsupported 角色");
    bool warned = false;
    for(const SimulationDiagnostic& diagnostic : model.diagnostics)
        if(diagnostic.code == "SIMULATION_COMPONENT_UNSUPPORTED") warned = true;
    check(warned, "不支持的元件给出 warning");
    check(model.binding.wireNet.at(10) != model.binding.wireNet.at(11),
          "不支持的元件不会把两侧网络合并");
}

// ---- 用例7：多驱动降级 + no_connect ----
void test_multiple_drivers_and_noconnect(){
    {   //两个输入端子驱动同一网络（坐标重合）
        SimProject project;
        project.components.push_back(in_terminal(1, 0, 0, "1"));
        project.components.push_back(in_terminal(2, 0, 0, "0"));
        project.wires.push_back(wire(10, P(0, 0), P(40, 0)));
        const SimulationModel model = build_simulation_model(project);
        bool warned = false;
        for(const SimulationDiagnostic& diagnostic : model.diagnostics)
            if(diagnostic.code == "SIMULATION_NET_MULTIPLE_DRIVERS") warned = true;
        check(warned, "多驱动 => warning");
        check(model.circuit.components.size() == 1, "多驱动时只保留一个驱动源");
    }
    {   //no_connect 引脚却接了线
        SimProject project;
        project.components.push_back(comp(1, "AND", "LogicGate",
            {pin("IN1", "input", 0, 0), pin("IN2", "input", 0, 20),
             pin("OUT", "output", 60, 10)}));
        project.wires.push_back(wire(10, P(0, 0), P(-40, 0), pin_end(1, "IN1")));
        project.noConnects.push_back({1, "IN1"});
        const SimulationModel model = build_simulation_model(project);
        bool warned = false;
        for(const SimulationDiagnostic& diagnostic : model.diagnostics)
            if(diagnostic.code == "SIMULATION_NOCONNECT_WIRED") warned = true;
        check(warned, "no_connect 引脚接线 => warning");
    }
}

// ---- 用例8：空项目 + 暂停/单步语义 ----
void test_empty_and_pause(){
    {   //空项目
        SimProject project;
        SimulationSession session;
        check(!session.rebuild(project), "空项目：没有可仿真内容");
        check(session.errorCount() == 0, "空项目：没有诊断");
        check(!session.evaluate(), "空项目：求值返回 false");
        check(!session.hasModel(), "空项目：没有模型");
    }
    {   //暂停时不求值，单步后才有值
        SimProject project = switch_and_lamp();
        SimulationSession session;
        session.setEnabled(false);
        check(session.rebuild(project), "暂停状态下也能建模型");
        check(!session.wireHasValue(11), "暂停且未求值：导线没有值");
        check(session.evaluate(), "单步时钟：求值成功");
        check(session.wireHasValue(11), "单步后导线有值");
        check(session.componentValue(2) == LogicValue::Zero, "单步后 AND 输出 0");
        session.setEnabled(true);
        check(session.evaluate(), "恢复运行后求值成功");
    }
}

// ---- 用例9：输入端子 / 输出端子 ----

// 两个输入端子 → AND → 输出端子
SimProject terminals_and_gate(const std::string& a, const std::string& b){
    SimProject project;
    project.components.push_back(in_terminal(1, 0, 0, a));
    project.components.push_back(in_terminal(2, 0, 80, b));
    project.components.push_back(comp(3, "AND", "LogicGate",
        {pin("IN1", "input", 60, 0), pin("IN2", "input", 60, 80),
         pin("OUT", "output", 120, 40)}));
    project.components.push_back(out_terminal(4, 140, 40));
    project.wires.push_back(wire(10, P(0, 0), P(60, 0), pin_end(1, "A"), pin_end(3, "IN1")));
    project.wires.push_back(wire(11, P(0, 80), P(60, 80), pin_end(2, "A"), pin_end(3, "IN2")));
    project.wires.push_back(wire(12, P(120, 40), P(140, 40), pin_end(3, "OUT"), pin_end(4, "A")));
    return project;
}

void test_terminals(){
    {   // 1 AND 1 = 1
        SimProject project = terminals_and_gate("1", "1");
        SimulationSession session;
        check(session.rebuild(project), "端子+AND：有可仿真内容");
        check(session.errorCount() == 0, "端子+AND：没有 error 诊断");
        check(session.isSwitch(1), "输入端子按可点击开关处理");
        check(session.componentValue(1) == LogicValue::One, "输入端子1 = 1");
        check(session.componentValue(3) == LogicValue::One, "1 AND 1 => 门输出 1");
        check(session.componentValue(4) == LogicValue::One, "输出端子读到 1");
        check(session.wireValue(12) == LogicValue::One, "输出导线 = 1");
        // 改第一个端子为 0：输出跟着变 0
        project.components[0].params["State"] = "0";
        check(session.rebuild(project), "改输入端子后重建成功");
        check(session.componentValue(3) == LogicValue::Zero, "0 AND 1 => 门输出 0");
        check(session.componentValue(4) == LogicValue::Zero, "输出端子读到 0");
        check(session.wireValue(12) == LogicValue::Zero, "输出导线 = 0");
    }
    {   // 门输入悬空 => X（不是 0）：明确固化语义
        SimProject project;
        project.components.push_back(in_terminal(1, 0, 0, "1"));
        project.components.push_back(comp(3, "AND", "LogicGate",
            {pin("IN1", "input", 60, 0), pin("IN2", "input", 60, 80),
             pin("OUT", "output", 120, 40)}));
        project.components.push_back(out_terminal(4, 140, 40));
        project.wires.push_back(wire(10, P(0, 0), P(60, 0), pin_end(1, "A"), pin_end(3, "IN1")));
        project.wires.push_back(wire(12, P(120, 40), P(140, 40), pin_end(3, "OUT"), pin_end(4, "A")));

        SimulationSession session;
        check(session.rebuild(project), "单输入 AND：建模型成功");
        check(session.componentValue(3) == LogicValue::Unknown,
              "另一个输入悬空 => 门输出 X（不是 0）");
        check(session.componentValue(4) == LogicValue::Unknown, "输出端子读到 X");
        const std::string report = session.report_text();
        check(report.find("无驱动") != std::string::npos, "诊断报告标出无驱动");
        check(report.find("仿真诊断") != std::string::npos, "诊断报告有标题");
    }
    {   // 输入/输出端子都没接线
        SimProject project;
        project.components.push_back(in_terminal(1, 0, 0, "1"));
        project.components.push_back(out_terminal(2, 200, 0));
        const SimulationModel model = build_simulation_model(project);
        int terminalWarnings = 0;
        for(const SimulationDiagnostic& diagnostic : model.diagnostics)
            if(diagnostic.code == "SIMULATION_TERMINAL_UNCONNECTED") ++terminalWarnings;
        check(terminalWarnings >= 2, "未接线的输入/输出端子各给一条 warning");
    }
}

// ---- 用例10：单刀开关是"两端导通"器件（POWER -> 开关 -> 门 才是对的） ----
void test_switch_passthrough(){
    // POWER(3) 的 "+" -> 开关(1).A；开关(1).B -> AND(2).IN1；输入端子(5) -> AND(2).IN2；
    // AND.OUT -> 输出端子(4)
    const auto build = [](const std::string& state){
        SimProject project;
        project.components.push_back(comp(1, "Switch-COM-SPST", "Switch",
            {pin("A", "passive", 0, 0), pin("B", "passive", 40, 0)}, {{"State", state}}));
        project.components.push_back(comp(2, "AND", "LogicGate",
            {pin("IN1", "input", 120, 0), pin("IN2", "input", 120, 80),
             pin("OUT", "output", 180, 40)}));
        project.components.push_back(comp(3, "POWER", "Source",
            {pin("+", "power", 0, 0), pin("-", "power", 0, -40)}));
        project.components.push_back(out_terminal(4, 220, 40));
        project.components.push_back(in_terminal(5, 60, 80, "1"));
        //A ↔ POWER+，B ↔ AND.IN1
        project.wires.push_back(wire(10, P(0, 0), P(0, 0), pin_end(3, "+"), pin_end(1, "A")));
        project.wires.push_back(wire(11, P(40, 0), P(120, 0), pin_end(1, "B"), pin_end(2, "IN1")));
        project.wires.push_back(wire(12, P(60, 80), P(120, 80), pin_end(5, "A"), pin_end(2, "IN2")));
        project.wires.push_back(wire(13, P(180, 40), P(220, 40), pin_end(2, "OUT"), pin_end(4, "A")));
        return project;
    };

    {   // 断开：IN1 没有驱动源（X），IN2 = 1 => X
        SimProject project = build("0");
        SimulationSession session;
        check(session.rebuild(project), "开关断开：建模型成功");
        check(session.componentValue(1) == LogicValue::Zero, "开关状态徽标 = 0（断开）");
        check(session.componentValue(2) == LogicValue::Unknown,
              "断开时 AND.IN1 悬空 => 输出 X");
        check(session.wireValue(11) == LogicValue::Unknown, "开关负载侧导线 = X");
    }
    {   // 闭合：POWER+(1) 经开关送到 IN1 => 1 AND 1 = 1
        SimProject project = build("1");
        SimulationSession session;
        check(session.rebuild(project), "开关闭合：建模型成功");
        check(session.componentValue(1) == LogicValue::One, "开关状态徽标 = 1（闭合）");
        check(session.wireValue(11) == LogicValue::One, "闭合后负载侧导线 = 1（导通）");
        check(session.componentValue(2) == LogicValue::One, "1 AND 1 => 门输出 1");
        check(session.componentValue(4) == LogicValue::One, "输出端子读到 1");
    }
    {   // 源接在 B 侧、负载在 A 侧：方向要能自动反过来
        SimProject project;
        project.components.push_back(comp(1, "Switch-COM-SPST", "Switch",
            {pin("A", "passive", 0, 0), pin("B", "passive", 40, 0)}, {{"State", "1"}}));
        project.components.push_back(out_terminal(4, -40, 0));
        project.components.push_back(comp(3, "POWER", "Source",
            {pin("+", "power", 40, 0), pin("-", "power", 40, -40)}));
        project.wires.push_back(wire(10, P(0, 0), P(40, 0), pin_end(1, "A"), pin_end(1, "B")));
        project.wires.push_back(wire(11, P(0, 0), P(-40, 0), pin_end(1, "A"), pin_end(4, "A")));
        project.wires.push_back(wire(12, P(40, 0), P(40, 0), pin_end(3, "+"), pin_end(1, "B")));
        SimulationSession session;
        check(session.rebuild(project), "反向接法：建模型成功");
        check(session.wireValue(11) == LogicValue::One, "源在 B 侧时也能把 1 送到 A 侧");
    }
}

} // namespace

int main(){
    test_switch_and_lamp();
    test_multilevel();
    test_junction();
    test_labels_and_free();
    test_geometry_fallback();
    test_unsupported();
    test_multiple_drivers_and_noconnect();
    test_empty_and_pause();
    test_terminals();
    test_switch_passthrough();

    if(failures == 0){
        std::cout << "Simulation adapter self-test passed (" << checks << " checks).\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " checks FAILED.\n";
    return 1;
}

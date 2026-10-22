// 仿真核心自检（不依赖 GUI，也不依赖网表模块）。
// 覆盖《仿真任务规划.md》第 5 节要求的 10 项最小测试，外加少量边界用例。
#include"../src/Simulation/SimulationValidator.hpp"
#include"../src/Simulation/Simulator.hpp"

#include<iostream>
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

// ---- 造电路的小工具 ----

SimulationComponent input(const std::string& id, const std::string& net,
                          LogicValue value = LogicValue::Unknown){
    return SimulationComponent(id, GateKind::Input, {}, net, value, "INPUT");
}

SimulationComponent gate(const std::string& id, GateKind kind,
                         std::vector<std::string> inputs, const std::string& output){
    return SimulationComponent(id, kind, std::move(inputs), output,
                               LogicValue::Unknown, gate_kind_name(kind));
}

SimulationProbe probe(const std::string& id, const std::string& net){
    return SimulationProbe(id, net);
}

bool has_diagnostic(const std::vector<SimulationDiagnostic>& diagnostics, const std::string& code){
    for(const auto& diagnostic : diagnostics)
        if(diagnostic.code == code) return true;
    return false;
}

//两个输入 a/b、一个输出 y 的门电路（探针 lamp 看 y）
SimulationCircuit two_input_circuit(GateKind kind){
    SimulationCircuit circuit;
    circuit.nets = {"a", "b", "y"};
    circuit.components = {input("sw_a", "a"), input("sw_b", "b"),
                          gate("g", kind, {"a", "b"}, "y")};
    circuit.probes = {probe("lamp", "y")};
    return circuit;
}

//用给定输入跑一次两输入门，返回探针值
LogicValue eval_two_input(GateKind kind, LogicValue a, LogicValue b, bool* ok = nullptr){
    Simulator simulator(two_input_circuit(kind));
    simulator.set_input("sw_a", a);
    simulator.set_input("sw_b", b);
    const SimulationResult result = simulator.evaluate();
    if(ok) *ok = result.ok;
    return result.probe_value("lamp");
}

// ---- 1) NOT ----
void test_not(){
    SimulationCircuit circuit;
    circuit.nets = {"a", "y"};
    circuit.components = {input("sw", "a"), gate("g", GateKind::Not, {"a"}, "y")};
    circuit.probes = {probe("lamp", "y")};

    Simulator simulator(circuit);
    check(simulator.valid(), "NOT 电路结构有效");
    check(simulator.set_input("sw", LogicValue::Zero), "能设置输入开关的值");
    check(!simulator.set_input("g", LogicValue::One), "对非输入元件 set_input 返回 false");

    SimulationResult result = simulator.evaluate();
    check(result.ok, "NOT 求值成功");
    check(result.probe_value("lamp") == LogicValue::One, "NOT 0 = 1");
    check(result.iterations > 0, "迭代轮数 > 0");

    simulator.set_input("sw", LogicValue::One);
    result = simulator.evaluate();
    check(result.ok && result.probe_value("lamp") == LogicValue::Zero, "NOT 1 = 0");
}

// ---- 2) AND ----
void test_and(){
    check(eval_two_input(GateKind::And, LogicValue::Zero, LogicValue::Zero) == LogicValue::Zero,
          "0 AND 0 = 0");
    check(eval_two_input(GateKind::And, LogicValue::Zero, LogicValue::One) == LogicValue::Zero,
          "0 AND 1 = 0");
    check(eval_two_input(GateKind::And, LogicValue::One, LogicValue::Zero) == LogicValue::Zero,
          "1 AND 0 = 0");
    check(eval_two_input(GateKind::And, LogicValue::One, LogicValue::One) == LogicValue::One,
          "1 AND 1 = 1");
}

// ---- 3) OR ----
void test_or(){
    check(eval_two_input(GateKind::Or, LogicValue::Zero, LogicValue::Zero) == LogicValue::Zero,
          "0 OR 0 = 0");
    check(eval_two_input(GateKind::Or, LogicValue::Zero, LogicValue::One) == LogicValue::One,
          "0 OR 1 = 1");
    check(eval_two_input(GateKind::Or, LogicValue::One, LogicValue::Zero) == LogicValue::One,
          "1 OR 0 = 1");
    check(eval_two_input(GateKind::Or, LogicValue::One, LogicValue::One) == LogicValue::One,
          "1 OR 1 = 1");
}

// ---- 4) 未知值传播 ----
void test_unknown(){
    check(eval_two_input(GateKind::And, LogicValue::Zero, LogicValue::Unknown) == LogicValue::Zero,
          "0 AND X = 0");
    check(eval_two_input(GateKind::And, LogicValue::One, LogicValue::Unknown) == LogicValue::Unknown,
          "1 AND X = X");
    check(eval_two_input(GateKind::Or, LogicValue::One, LogicValue::Unknown) == LogicValue::One,
          "1 OR X = 1");
    check(eval_two_input(GateKind::Or, LogicValue::Zero, LogicValue::Unknown) == LogicValue::Unknown,
          "0 OR X = X");

    //NOT X = X
    SimulationCircuit circuit;
    circuit.nets = {"a", "y"};
    circuit.components = {input("sw", "a"), gate("g", GateKind::Not, {"a"}, "y")};
    circuit.probes = {probe("lamp", "y")};
    Simulator simulator(circuit);   //输入不设置 => 初值 X
    const SimulationResult result = simulator.evaluate();
    check(result.ok && result.probe_value("lamp") == LogicValue::Unknown, "NOT X = X");
}

// ---- 5) 多级组合逻辑 ----
void test_multi_level(){
    SimulationCircuit circuit;
    circuit.nets = {"a", "b", "n1", "n2"};
    circuit.components = {input("sw_a", "a"), input("sw_b", "b"),
                          gate("and1", GateKind::And, {"a", "b"}, "n1"),
                          gate("not1", GateKind::Not, {"n1"}, "n2")};
    circuit.probes = {probe("lamp", "n2")};

    Simulator simulator(circuit);
    simulator.set_input("sw_a", true);
    simulator.set_input("sw_b", true);
    SimulationResult result = simulator.evaluate();
    check(result.ok && result.probe_value("lamp") == LogicValue::Zero,
          "多级：(1 AND 1) -> NOT = 0");
    check(result.net_value("n1") == LogicValue::One, "中间网络 n1 = 1");

    simulator.set_input("sw_b", false);
    result = simulator.evaluate();
    check(result.ok && result.probe_value("lamp") == LogicValue::One,
          "多级：(1 AND 0) -> NOT = 1");
    check(result.net_value("n1") == LogicValue::Zero, "中间网络 n1 = 0");
}

// ---- 6) 改输入后重新求值 ----
void test_reevaluate(){
    Simulator simulator(two_input_circuit(GateKind::Or));
    simulator.set_input("sw_a", false);
    simulator.set_input("sw_b", false);
    check(simulator.evaluate().probe_value("lamp") == LogicValue::Zero, "重新求值：0 OR 0 = 0");

    simulator.set_input("sw_b", true);
    check(simulator.evaluate().probe_value("lamp") == LogicValue::One, "改输入后：0 OR 1 = 1");

    simulator.set_input("sw_b", false);
    check(simulator.evaluate().probe_value("lamp") == LogicValue::Zero, "再改回去：回到 0");
}

// ---- 7) 多个输出探针 ----
void test_multiple_probes(){
    SimulationCircuit circuit;
    circuit.nets = {"a", "n1", "n2"};
    circuit.components = {input("sw", "a"),
                          gate("not1", GateKind::Not, {"a"}, "n1"),
                          gate("not2", GateKind::Not, {"n1"}, "n2")};
    circuit.probes = {probe("p1", "n1"), probe("p2", "n2")};

    Simulator simulator(circuit);
    simulator.set_input("sw", true);
    const SimulationResult result = simulator.evaluate();
    check(result.ok, "多探针电路求值成功");
    check(result.probe_value("p1") == LogicValue::Zero, "探针 p1 读到 n1 = 0");
    check(result.probe_value("p2") == LogicValue::One, "探针 p2 读到 n2 = 1");
}

// ---- 8) 无效网络引用 / 错误的引脚数量 / 不支持的类型 / 多驱动 ----
void test_invalid_circuits(){
    {   //输出网络没声明
        SimulationCircuit circuit;
        circuit.nets = {"a"};
        circuit.components = {input("sw", "a"), gate("g", GateKind::Not, {"a"}, "y")};
        Simulator simulator(circuit);
        check(!simulator.valid(), "输出网络未声明 -> 结构无效");
        check(has_diagnostic(simulator.diagnostics(), "SIMULATION_NET_UNKNOWN"),
              "诊断码 SIMULATION_NET_UNKNOWN");
        check(!simulator.evaluate().ok, "结构无效时不求值、结果为失败");
    }
    {   //输入网络没声明
        SimulationCircuit circuit;
        circuit.nets = {"y"};
        circuit.components = {gate("g", GateKind::Not, {"missing"}, "y")};
        Simulator simulator(circuit);
        check(has_diagnostic(simulator.diagnostics(), "SIMULATION_NET_UNKNOWN"),
              "输入网络未声明 -> SIMULATION_NET_UNKNOWN");
    }
    {   //NOT 给了两个输入
        SimulationCircuit circuit;
        circuit.nets = {"a", "b", "y"};
        circuit.components = {input("sw_a", "a"), input("sw_b", "b"),
                              gate("g", GateKind::Not, {"a", "b"}, "y")};
        Simulator simulator(circuit);
        check(has_diagnostic(simulator.diagnostics(), "SIMULATION_GATE_PIN_COUNT_INVALID"),
              "NOT 两个输入 -> SIMULATION_GATE_PIN_COUNT_INVALID");
    }
    {   //AND 只给一个输入
        SimulationCircuit circuit;
        circuit.nets = {"a", "y"};
        circuit.components = {input("sw_a", "a"), gate("g", GateKind::And, {"a"}, "y")};
        Simulator simulator(circuit);
        check(has_diagnostic(simulator.diagnostics(), "SIMULATION_GATE_PIN_COUNT_INVALID"),
              "AND 一个输入 -> SIMULATION_GATE_PIN_COUNT_INVALID");
    }
    {   //不支持的元件类型
        SimulationCircuit circuit;
        circuit.nets = {"a", "y"};
        circuit.components = {input("sw", "a"),
                              SimulationComponent("u1", GateKind::Unsupported, {"a"}, "y",
                                                  LogicValue::Unknown, "Resistor")};
        Simulator simulator(circuit);
        check(has_diagnostic(simulator.diagnostics(), "SIMULATION_COMPONENT_TYPE_UNSUPPORTED"),
              "不支持的元件类型 -> SIMULATION_COMPONENT_TYPE_UNSUPPORTED");
    }
    {   //一个网络两个驱动源
        SimulationCircuit circuit;
        circuit.nets = {"a", "b", "y"};
        circuit.components = {input("sw_a", "a"), input("sw_b", "b"),
                              gate("g1", GateKind::Not, {"a"}, "y"),
                              gate("g2", GateKind::Not, {"b"}, "y")};
        Simulator simulator(circuit);
        check(has_diagnostic(simulator.diagnostics(), "SIMULATION_NET_MULTIPLE_DRIVERS"),
              "网络多驱动 -> SIMULATION_NET_MULTIPLE_DRIVERS");
    }
    {   //元件 ID 重复 + 探针 ID 重复
        SimulationCircuit circuit;
        circuit.nets = {"a", "y"};
        circuit.components = {input("dup", "a"), gate("dup", GateKind::Not, {"a"}, "y")};
        circuit.probes = {probe("p", "y"), probe("p", "y")};
        Simulator simulator(circuit);
        check(has_diagnostic(simulator.diagnostics(), "SIMULATION_COMPONENT_ID_DUPLICATE"),
              "元件 ID 重复 -> SIMULATION_COMPONENT_ID_DUPLICATE");
        check(has_diagnostic(simulator.diagnostics(), "SIMULATION_PROBE_ID_DUPLICATE"),
              "探针 ID 重复 -> SIMULATION_PROBE_ID_DUPLICATE");
    }
}

// ---- 9) 组合环：有限次迭代 + 明确诊断 ----
void test_combinational_loop(){
    {   //一个 NOT 把自己的输出接回输入：奇数反相环，永不稳定
        SimulationCircuit circuit;
        circuit.nets = {"n1"};
        circuit.initialNetValues["n1"] = LogicValue::Zero;   //给一个已知初值才会震荡
        circuit.components = {gate("g", GateKind::Not, {"n1"}, "n1")};
        circuit.probes = {probe("lamp", "n1")};

        Simulator simulator(circuit);
        check(simulator.valid(), "自反馈 NOT 的结构本身有效");
        const SimulationResult result = simulator.evaluate();
        check(!result.ok, "不收敛的组合环 -> 求值失败");
        check(result.has_diagnostic("SIMULATION_COMBINATIONAL_LOOP"),
              "诊断码 SIMULATION_COMBINATIONAL_LOOP");
        check(result.iterations > 0, "有迭代上限保护（迭代轮数有限）");
    }
    {   //两个 NOT 对头接成环：有稳定解，不应误报
        SimulationCircuit circuit;
        circuit.nets = {"n1", "n2"};
        circuit.initialNetValues["n1"] = LogicValue::One;
        circuit.components = {gate("g1", GateKind::Not, {"n1"}, "n2"),
                              gate("g2", GateKind::Not, {"n2"}, "n1")};
        circuit.probes = {probe("lamp", "n2")};

        Simulator simulator(circuit);
        const SimulationResult result = simulator.evaluate();
        check(result.ok, "稳定的反馈环不报错");
        check(!result.has_diagnostic("SIMULATION_COMBINATIONAL_LOOP"), "稳定反馈环不误报环诊断");
        check(result.probe_value("lamp") == LogicValue::Zero, "稳定反馈环收敛到 n2 = 0");
    }
}

// ---- 10) 空电路 / 单输入单输出 ----
void test_empty_and_minimal(){
    {   //空电路
        SimulationCircuit circuit;
        Simulator simulator(circuit);
        check(simulator.valid(), "空电路结构有效");
        const SimulationResult result = simulator.evaluate();
        check(result.ok, "空电路求值成功");
        check(result.netValues.empty(), "空电路没有网络值");
        check(result.probeValues.empty(), "空电路没有探针值");
    }
    {   //输入直接接到探针
        SimulationCircuit circuit;
        circuit.nets = {"a"};
        circuit.components = {input("sw", "a")};
        circuit.probes = {probe("lamp", "a")};

        Simulator simulator(circuit);
        simulator.set_input("sw", true);
        check(simulator.evaluate().probe_value("lamp") == LogicValue::One,
              "单输入直连探针 = 1");
        simulator.set_input("sw", false);
        check(simulator.evaluate().probe_value("lamp") == LogicValue::Zero,
              "单输入直连探针 = 0");
    }
}

// ---- 附加：XOR/NAND/NOR、多输入、无驱动网络 ----
void test_extras(){
    check(eval_two_input(GateKind::Nand, LogicValue::One, LogicValue::One) == LogicValue::Zero,
          "1 NAND 1 = 0");
    check(eval_two_input(GateKind::Nor, LogicValue::Zero, LogicValue::Zero) == LogicValue::One,
          "0 NOR 0 = 1");
    check(eval_two_input(GateKind::Xor, LogicValue::One, LogicValue::Zero) == LogicValue::One,
          "1 XOR 0 = 1");
    check(eval_two_input(GateKind::Xor, LogicValue::Unknown, LogicValue::One) == LogicValue::Unknown,
          "X XOR 1 = X");

    {   //没被任何元件驱动的网络保持 X，不算错误
        SimulationCircuit circuit;
        circuit.nets = {"lonely"};
        circuit.probes = {probe("p", "lonely")};
        Simulator simulator(circuit);
        const SimulationResult result = simulator.evaluate();
        check(result.ok, "无驱动网络不算错误");
        check(result.probe_value("p") == LogicValue::Unknown, "无驱动网络的探针值 = X");
    }
    {   //三输入 AND
        SimulationCircuit circuit;
        circuit.nets = {"a", "b", "c", "y"};
        circuit.components = {input("sa", "a"), input("sb", "b"), input("sc", "c"),
                              gate("g", GateKind::And, {"a", "b", "c"}, "y")};
        circuit.probes = {probe("lamp", "y")};

        Simulator simulator(circuit);
        simulator.set_input("sa", true);
        simulator.set_input("sb", true);
        simulator.set_input("sc", true);
        check(simulator.evaluate().probe_value("lamp") == LogicValue::One,
              "三输入 AND 全 1 = 1");
        simulator.set_input("sc", false);
        check(simulator.evaluate().probe_value("lamp") == LogicValue::Zero,
              "三输入 AND 有一个 0 = 0");
    }
    {   //导通/缓冲（单刀开关闭合时用）：输出 = 输入
        SimulationCircuit circuit;
        circuit.nets = {"a", "y"};
        circuit.components = {input("sw", "a"), gate("buf", GateKind::Buffer, {"a"}, "y")};
        circuit.probes = {probe("lamp", "y")};

        Simulator simulator(circuit);
        simulator.set_input("sw", true);
        check(simulator.evaluate().probe_value("lamp") == LogicValue::One, "BUFFER：1 通过 = 1");
        simulator.set_input("sw", false);
        check(simulator.evaluate().probe_value("lamp") == LogicValue::Zero, "BUFFER：0 通过 = 0");
    }
}

} // namespace

int main(){
    test_not();
    test_and();
    test_or();
    test_unknown();
    test_multi_level();
    test_reevaluate();
    test_multiple_probes();
    test_invalid_circuits();
    test_combinational_loop();
    test_empty_and_minimal();
    test_extras();

    if(failures == 0){
        std::cout << "Simulation self-test passed (" << checks << " checks).\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " checks FAILED.\n";
    return 1;
}

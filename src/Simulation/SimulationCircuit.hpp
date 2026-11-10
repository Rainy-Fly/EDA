#pragma once
#include<cstddef>
#include<map>
#include<string>
#include<vector>

#include"LogicValue.hpp"

// 与 GUI 无关的仿真输入模型：
// 只描述“有哪些网络、哪些元件、引脚接到哪个网络、输入开关的初值、哪些探针看哪个网络”。
// 不保存任何 wxWindow / 画布对象 / UI 裸指针，也不读取画布像素或 KiCad .net 文件。
namespace Simulation{

// 仿真支持的门类型。MVP 只要求 NOT/AND/OR + 输入开关 + 输出探针；
// XOR/NAND/NOR 成本很低，顺便支持。
// Unsupported 用于让“暂不支持的元件类型”得到明确诊断（不静默当成某种门）。
enum class GateKind{
    Not,        //非门：1 输入
    And,        //与门：>=2 输入
    Or,         //或门：>=2 输入
    Xor,        //异或门：>=2 输入（附加）
    Xnor,       //同或门：>=2 输入（附加）
    Nand,       //与非门：>=2 输入（附加）
    Nor,        //或门非：>=2 输入（附加）
    Buffer,     //缓冲/导通：1 输入，输出=输入（单刀开关闭合时用它把一侧的电平送到另一侧）
    Input,      //输入端子/输入开关：0 输入，1 输出；值由调用方设置（0/1，未设置为 X）
    Dff,        //D触发器：2 输入（顺序固定 [D, CLK]），1 输出；CLK 0→1 上升沿时 Q←D，否则保持
    Clock,      //时钟源：0 输入，1 输出；值由仿真器状态控制（clock_tick 时翻转 0/1）
    Unsupported //未知/暂不支持
};

// 一个元件实例。
//   门：inputNets 是输入网络（顺序即引脚顺序），outputNet 是输出网络。
//   Input：inputNets 为空，值写到 outputNet；当前值见 inputValue。
struct SimulationComponent{
    std::string id;                                 //实例ID（电路内唯一）
    GateKind kind = GateKind::Unsupported;
    std::vector<std::string> inputNets;             //输入网络
    std::string outputNet;                          //输出网络
    LogicValue inputValue = LogicValue::Unknown;    //仅 Input 元件有效
    std::string typeName;                           //原始类型串（只用于诊断信息）

    SimulationComponent() = default;
    SimulationComponent(std::string componentId, GateKind gateKind,
                        std::vector<std::string> inputs, std::string output,
                        LogicValue value = LogicValue::Unknown,
                        std::string type = std::string())
        : id(std::move(componentId)),
          kind(gateKind),
          inputNets(std::move(inputs)),
          outputNet(std::move(output)),
          inputValue(value),
          typeName(std::move(type)) {}
};

// 输出灯/输出探针：观察某个网络当前逻辑值。
struct SimulationProbe{
    std::string id;
    std::string net;

    SimulationProbe() = default;
    SimulationProbe(std::string probeId, std::string netName)
        : id(std::move(probeId)), net(std::move(netName)) {}
};

// 仿真电路（与 GUI 无关的输入模型）。
struct SimulationCircuit{
    std::vector<std::string> nets;                       //声明的网络
    std::vector<SimulationComponent> components;
    std::vector<SimulationProbe> probes;
    std::map<std::string, LogicValue> initialNetValues;  //可选：网络初值；缺省为 X

    bool empty() const{
        return components.empty() && probes.empty() && nets.empty();
    }
};

//门类型名称（诊断信息用）
//注意：新增 GateKind 枚举值时，这个 switch 和下面的 gate_input_count_range 都必须同步加 case，
//否则 -Wall 的 -Wswitch 会警告，而且 gate_input_count_range 返回 false 会让校验器把该门
//判成"不支持的类型"（error），导致整张电路都不求值（同或门曾经就是这样）
inline const char* gate_kind_name(GateKind kind){
    switch(kind){
        case GateKind::Not:  return "NOT";
        case GateKind::And:  return "AND";
        case GateKind::Or:   return "OR";
        case GateKind::Xor:  return "XOR";
        case GateKind::Xnor: return "XNOR";
        case GateKind::Nand: return "NAND";
        case GateKind::Nor:  return "NOR";
        case GateKind::Buffer: return "BUF";   //单刀开关闭合时把一侧电平送到另一侧
        case GateKind::Input: return "INPUT";
        case GateKind::Dff:  return "D_FF";    //D触发器：CLK 上升沿采样
        case GateKind::Clock: return "CLK";    //时钟源：单步时钟时翻转
        case GateKind::Unsupported: break;
    }
    return "UNSUPPORTED";
}

//该类型需要的输入引脚数范围：minInputs <= 实际 <= maxInputs 才合法。
//Unsupported 返回 false（由 SimulationValidator 单独报“不支持的类型”）。
inline bool gate_input_count_range(GateKind kind, std::size_t& minInputs, std::size_t& maxInputs){
    switch(kind){
        case GateKind::Not:   minInputs = 1; maxInputs = 1; return true;
        case GateKind::And:
        case GateKind::Or:
        case GateKind::Xor:
        case GateKind::Xnor:
        case GateKind::Nand:
        case GateKind::Nor:   minInputs = 2; maxInputs = static_cast<std::size_t>(-1); return true;
        case GateKind::Buffer: minInputs = 1; maxInputs = 1; return true;
        case GateKind::Input: minInputs = 0; maxInputs = 0; return true;
        case GateKind::Dff:   minInputs = 2; maxInputs = 2; return true;   //[D, CLK]
        case GateKind::Clock: minInputs = 0; maxInputs = 0; return true;
        case GateKind::Unsupported: break;
    }
    return false;
}

//是不是“门”（有输入网络、要按真值表求值；Buffer 也算，它只是 output=input）
inline bool is_gate(GateKind kind){
    return kind == GateKind::Not || kind == GateKind::And || kind == GateKind::Or ||
           kind == GateKind::Xor || kind == GateKind::Xnor || kind == GateKind::Nand ||
           kind == GateKind::Nor ||
           kind == GateKind::Buffer;
}

//把电路里所有声明过的网络置为初值（未给初值的网络为 X）
inline std::map<std::string, LogicValue> initial_net_values(const SimulationCircuit& circuit){
    std::map<std::string, LogicValue> values;
    for(const std::string& net : circuit.nets) values[net] = LogicValue::Unknown;
    for(const auto& entry : circuit.initialNetValues) values[entry.first] = entry.second;
    return values;
}

} // namespace Simulation

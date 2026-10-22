#pragma once
#include<map>
#include<string>
#include<utility>
#include<vector>

#include"SimulationCircuit.hpp"
#include"SimulationProject.hpp"
#include"SimulationResult.hpp"

// 项目数据 → 仿真电路 的适配层（与 GUI 无关）。
//
// 职责：
//   1) 连通性：用并查集把导线端点按「同一引脚 / 同一 junction / 同一 label(scope+text)
//      / 坐标精确重合」合并成网络；
//   2) 角色：按元件元数据的 type/category 判断它对应哪种仿真元件；
//   3) 产出：SimulationCircuit（门/输入开关/常量源）+ 探针（灯/探针点）+ 一张
//      "画布对象 → 网络/角色" 映射表，供画布着色使用。
namespace Simulation{

// 元件在仿真里的角色
enum class SimRole{
    Gate,         //逻辑门（AND/OR/NOT/NAND/NOR/XOR）
    InputSwitch,  //可点击的输入开关：0/1 由用户点击切换
    Constant,     //常量源：POWER 的 "+"=1、"-"=0，GND 的 "GND"=0
    Probe,        //输出探针（Miscellaneous-COM-Probe_Point）
    Lamp,         //输出灯（白炽灯/指示灯/LED）：值=1 时点亮
    Unsupported   //不参与仿真（电阻/电容/晶体管/芯片…）
};

const char* sim_role_name(SimRole role);

// 画布对象 → 仿真对象的映射（供画布着色/点击）
struct SimulationBinding{
    std::map<std::pair<int,std::string>, std::string> pinNet;   //(元件ID, 引脚ID) → 网络
    std::map<int, std::string> wireNet;                         //导线ID → 网络（两端同网）
    std::map<int, SimRole> role;                                //元件ID → 角色
    //该元件"状态所对应的网络"：门=输出网络；开关/常量=驱动网络；灯/探针=观察网络
    std::map<int, std::string> deviceNet;
    //网络 → 驱动它的元件ID（诊断用：区分"有驱动"和"悬空没驱动"）
    std::map<std::string, int> netDriver;
    //开关当前值（来自实例参数 State，缺省 0）
    std::map<int, LogicValue> switchValue;
};

struct SimulationModel{
    SimulationCircuit circuit;
    SimulationBinding binding;
    std::vector<SimulationDiagnostic> diagnostics;

    bool has_simulatable_content() const{
        return !circuit.components.empty() || !circuit.probes.empty();
    }
};

// 构建仿真模型。同一条网络出现多个驱动源时：保留元件ID最小的那个并给出 warning，
// 保证界面仍然能显示值（不因为一处冲突整张图都不出值）。
SimulationModel build_simulation_model(const SimProject& project);

} // namespace Simulation

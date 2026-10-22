#pragma once
#include<map>
#include<string>
#include<utility>
#include<vector>

// 仿真的"项目输入"模型（与 wx/GUI/XML 完全无关）。
//
// 由 GUI 侧的 SimulationBridge（src/Canva/SimulationBridge.*）从
// DealProjectXML::ProjectData + 元件元数据填充，再交给 SimulationAdapter
// 做连通性合并与"元件→仿真角色"映射。
// 因此仿真核心可以脱离 wxWidgets 单独编译和自检。
namespace Simulation{

//逻辑坐标（与画布网格同一坐标系）
struct SimPoint{
    int x = 0;
    int y = 0;
    SimPoint() = default;
    SimPoint(int px, int py) : x(px), y(py) {}
    bool operator==(const SimPoint& o) const{ return x == o.x && y == o.y; }
    bool operator!=(const SimPoint& o) const{ return !(*this == o); }
};

//引脚：position 已应用实例的旋转/镜像，是绝对逻辑坐标
struct SimPinRef{
    std::string id;          //稳定引脚ID（IN1/OUT/A/+/GND/...）
    std::string direction;   //input/output/passive/power/inout
    SimPoint position;
    unsigned int width = 1;
};

struct SimComponent{
    int id = 0;
    std::string type;        //元数据 type，如 "AND"/"Switch-COM-SPST"/"POWER"/"GND"
    std::string category;    //元数据 category，如 "LogicGate"/"Switch"/"Source"
    std::string name;        //中文显示名（诊断信息用）
    std::map<std::string,std::string> params;  //实例参数（开关状态存在 "State"）
    std::vector<SimPinRef> pins;
};

//导线端点：kind = pin / junction / label / free；空串表示旧版 1.0（只按坐标连接）。
//position 是该端点在画布上的绝对逻辑坐标（由 GUI 侧填好，供几何兜底连接）。
struct SimEndpoint{
    std::string kind;
    int componentId = 0;
    std::string pinId;
    std::string junctionId;
    std::string labelId;
    SimPoint position;
};

struct SimWire{
    int id = 0;
    std::string type;                 //"WIRE"/"BUS"
    std::vector<SimPoint> points;     //完整折线（≥2）
    SimEndpoint start;
    SimEndpoint end;
};

//网络标签（本地/电源/层次）
struct SimLabel{
    std::string id;
    std::string text;
    std::string scope = "local";      //local/power/hier
    SimPoint position;
};

struct SimProject{
    std::vector<SimComponent> components;
    std::vector<SimWire> wires;
    std::vector<SimPoint> junctionPositions;                 //junction 坐标（几何兜底）
    std::vector<SimLabel> labels;
    std::vector<std::pair<int,std::string>> noConnects;      //(元件ID, 引脚ID)

    bool empty() const{ return components.empty() && wires.empty(); }
};

} // namespace Simulation

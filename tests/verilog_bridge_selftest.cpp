#include "../src/DealProjectXML/DealProjectXML.hpp"
#include "../src/VerilogBridge/ModuleToProject.hpp"
#include "../src/VerilogBridge/ProjectToModule.hpp"
#include "../src/verilator/VerilatorApi.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include <wx/init.h>

namespace{

int checks = 0;
int failures = 0;

void check(bool condition, const std::string& message){
    ++checks;
    if(condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << "\n";
}

DealProjectXML::ComponentData component(int id, const std::string& type,
                                        int x, int y, const std::string& reference){
    DealProjectXML::ComponentData data;
    data.id = id;
    data.type = type;
    data.x = x;
    data.y = y;
    data.reference = reference;
    return data;
}

DealProjectXML::WireData pin_wire(int id, const wxPoint& start_point,
                                  const wxPoint& end_point,
                                  int start_component, const std::string& start_pin,
                                  int end_component, const std::string& end_pin){
    DealProjectXML::WireData wire;
    wire.id = id;
    wire.type = "WIRE";
    wire.points = {start_point, end_point};
    wire.start.kind = "pin";
    wire.start.component_id = start_component;
    wire.start.pin_id = start_pin;
    wire.end.kind = "pin";
    wire.end.component_id = end_component;
    wire.end.pin_id = end_pin;
    return wire;
}

bool has_code(const std::vector<Verilator::Diagnostic>& diagnostics,
              const std::string& code){
    for(const Verilator::Diagnostic& diagnostic : diagnostics){
        if(diagnostic.code == code) return true;
    }
    return false;
}

// 两个输入端子 + 一个二输入门 + 一个输出端子。
// 引脚坐标来自元数据：IN_TERMINAL A=(+50,0)、OUT_TERMINAL A=(-50,0)、
// 二输入门 IN1/IN2=(-50,±10)、OUT=(+50,0)。
DealProjectXML::ProjectData two_input_gate_project(const std::string& gate_type){
    DealProjectXML::ProjectData project;
    project.info.name = "top";
    project.components.push_back(component(1, "IN_TERMINAL", 0, 0, "A"));
    project.components.push_back(component(2, "IN_TERMINAL", 0, 100, "B"));
    project.components.push_back(component(3, gate_type, 100, 50, "U1"));
    project.components.push_back(component(4, "OUT_TERMINAL", 250, 50, "Y"));
    project.wires.push_back(pin_wire(1, wxPoint(50, 0), wxPoint(50, 40), 1, "A", 3, "IN1"));
    project.wires.push_back(pin_wire(2, wxPoint(50, 100), wxPoint(50, 60), 2, "A", 3, "IN2"));
    project.wires.push_back(pin_wire(3, wxPoint(150, 50), wxPoint(200, 50), 3, "OUT", 4, "A"));
    return project;
}

void test_and_gate_export(){
    const auto result = VerilogBridge::BuildModuleFromProject(
        two_input_gate_project("AND"), "top");
    check(result.ok, "AND project builds a module");
    check(result.module.name == "top", "module name is top");
    check(result.module.ports.size() == 3, "module has three ports");
    check(result.module.gates.size() == 1, "module has one gate");

    if(result.module.gates.size() == 1){
        check(result.module.gates[0].kind == Verilator::GateKind::And,
              "gate kind is AND");
        check(result.module.gates[0].inputs.size() == 2,
              "AND gate has two inputs");
    }

    const auto generated = Verilator::generate_verilog(result.module);
    check(generated.ok, "generated source is valid");
    check(generated.verilog.find("module top") != std::string::npos,
          "generated source contains module top");
    check(generated.verilog.find("input A") != std::string::npos,
          "generated source contains input A");
    check(generated.verilog.find("output Y") != std::string::npos,
          "generated source contains output Y");

    if(generated.ok){
        const auto reparsed = Verilator::parse_verilog(generated.verilog);
        check(reparsed.ok, "generated source can be parsed back");
    }
}

void test_xnor_gate_export(){
    const auto result = VerilogBridge::BuildModuleFromProject(
        two_input_gate_project("XNOR"), "top");
    check(result.ok, "XNOR project builds a module");
    check(result.module.gates.size() == 1, "XNOR module has one gate");
    if(result.module.gates.size() == 1){
        check(result.module.gates[0].kind == Verilator::GateKind::Xnor,
              "gate kind is XNOR");
    }
    const auto generated = Verilator::generate_verilog(result.module);
    check(generated.ok, "XNOR source is generated");
}

void test_constant_input(){
    DealProjectXML::ProjectData project;
    project.info.name = "top";
    project.components.push_back(component(1, "POWER", 0, 0, "V1"));
    project.components.push_back(component(2, "IN_TERMINAL", 0, 100, "B"));
    project.components.push_back(component(3, "AND", 100, 50, "U1"));
    project.components.push_back(component(4, "OUT_TERMINAL", 250, 50, "Y"));
    // POWER 的 "+" 引脚在 (0,-50)，接 AND 的 IN1。
    project.wires.push_back(pin_wire(1, wxPoint(0, -50), wxPoint(50, 40), 1, "+", 3, "IN1"));
    project.wires.push_back(pin_wire(2, wxPoint(50, 100), wxPoint(50, 60), 2, "A", 3, "IN2"));
    project.wires.push_back(pin_wire(3, wxPoint(150, 50), wxPoint(200, 50), 3, "OUT", 4, "A"));

    const auto result = VerilogBridge::BuildModuleFromProject(project, "top");
    check(result.ok, "constant input project builds a module");
    const auto generated = Verilator::generate_verilog(result.module);
    check(generated.ok, "constant input source is generated");
    check(generated.verilog.find("1'b1") != std::string::npos,
          "generated source contains constant 1'b1");
}

void test_unconnected_gate_input_rejected(){
    DealProjectXML::ProjectData project;
    project.info.name = "top";
    project.components.push_back(component(1, "IN_TERMINAL", 0, 0, "A"));
    project.components.push_back(component(2, "AND", 100, 50, "U1"));
    project.components.push_back(component(3, "OUT_TERMINAL", 250, 50, "Y"));
    project.wires.push_back(pin_wire(1, wxPoint(50, 0), wxPoint(50, 40), 1, "A", 2, "IN1"));
    project.wires.push_back(pin_wire(2, wxPoint(150, 50), wxPoint(200, 50), 2, "OUT", 3, "A"));

    const auto result = VerilogBridge::BuildModuleFromProject(project, "top");
    check(!result.ok, "unconnected gate input is rejected");
    check(has_code(result.diagnostics, "CIRCUIT_SOURCE_UNRIVEN"),
          "unconnected gate input reports SOURCE_UNRIVEN");
}

void test_unsupported_component_rejected(){
    DealProjectXML::ProjectData project;
    project.info.name = "top";
    project.components.push_back(component(1, "RESISTOR", 0, 0, "R1"));
    project.components.push_back(component(2, "OUT_TERMINAL", 250, 50, "Y"));

    const auto result = VerilogBridge::BuildModuleFromProject(project, "top");
    check(!result.ok, "unsupported component is rejected");
    check(has_code(result.diagnostics, "PROJECT_VERILOG_UNSUPPORTED_COMPONENT"),
          "unsupported component reports its diagnostic");
}

// Verilog -> 项目 -> Verilog 往返：生成的项目能再被转回模型并生成源码。
void test_round_trip_from_verilog(){
    const std::string source =
        "module top(\n"
        "    input A,\n"
        "    input B,\n"
        "    output Y\n"
        ");\n"
        "    wire n1;\n"
        "    assign n1 = A & B;\n"
        "    assign Y = n1;\n"
        "endmodule\n";

    const Verilator::ParseResult parsed = Verilator::parse_verilog(source);
    check(parsed.ok, "sample source parses");
    if(!parsed.ok) return;

    const VerilogBridge::ModuleToProjectResult project =
        VerilogBridge::BuildProjectFromModule(parsed.module, wxPoint(0, 0));
    check(project.ok, "module builds a project");
    if(!project.ok) return;

    // 两个输入端子 + 一个与门 + 一个输出端子；常量网络无（纯逻辑），无电源元件。
    check(project.project.components.size() == 4,
          "project has two terminals plus one gate plus one output terminal");
    bool found_gate = false;
    for(const DealProjectXML::ComponentData& cd : project.project.components){
        if(cd.type == "AND") found_gate = true;
    }
    check(found_gate, "project contains an AND gate");
    check(!project.project.wires.empty(), "project contains wires");

    const VerilogBridge::ProjectToModuleResult back =
        VerilogBridge::BuildModuleFromProject(project.project, "top");
    check(back.ok, "project converts back to a module");
    if(!back.ok) return;
    check(back.module.gates.size() == 1, "round trip keeps one gate");
    check(back.module.gates[0].kind == Verilator::GateKind::And,
          "round trip keeps gate kind");

    const Verilator::GenerateResult generated = Verilator::generate_verilog(back.module);
    check(generated.ok, "round trip source is valid");
    check(generated.verilog.find("module top") != std::string::npos,
          "round trip source contains module top");
}

// 常量输入网络会生成电源元件。
void test_round_trip_constant(){
    const std::string source =
        "module top(\n"
        "    input B,\n"
        "    output Y\n"
        ");\n"
        "    wire n1;\n"
        "    assign n1 = B & 1'b1;\n"
        "    assign Y = n1;\n"
        "endmodule\n";

    const Verilator::ParseResult parsed = Verilator::parse_verilog(source);
    check(parsed.ok, "constant source parses");
    if(!parsed.ok) return;

    const VerilogBridge::ModuleToProjectResult project =
        VerilogBridge::BuildProjectFromModule(parsed.module, wxPoint(0, 0));
    check(project.ok, "constant module builds a project");
    if(!project.ok) return;

    bool found_power = false;
    for(const DealProjectXML::ComponentData& cd : project.project.components){
        if(cd.type == "POWER") found_power = true;
    }
    check(found_power, "project contains a POWER component");

    const VerilogBridge::ProjectToModuleResult back =
        VerilogBridge::BuildModuleFromProject(project.project, "top");
    check(back.ok, "constant project converts back to a module");
}

// 空模块（无端口无门）应被拒绝。
void test_empty_module_rejected(){
    const std::string source = "module empty();\nendmodule\n";
    const Verilator::ParseResult parsed = Verilator::parse_verilog(source);
    check(parsed.ok, "empty module parses");
    if(!parsed.ok) return;

    const VerilogBridge::ModuleToProjectResult project =
        VerilogBridge::BuildProjectFromModule(parsed.module, wxPoint(0, 0));
    check(!project.ok, "empty module is rejected");
    check(has_code(project.diagnostics, "PROJECT_VERILOG_EMPTY_MODULE"),
          "empty module reports its diagnostic");
}

//两条正交线段是否重合（共线且区间重叠长度为正值）。
bool segments_overlap(const wxPoint& a, const wxPoint& b,
                      const wxPoint& c, const wxPoint& d){
    const long abx = b.x - a.x, aby = b.y - a.y;
    if(abx * (d.y - c.y) - aby * (d.x - c.x) != 0) return false;   //不平行
    if((c.x - a.x) * aby - (c.y - a.y) * abx != 0) return false;   //不共线
    const int lo = std::max(std::min(a.x, b.x), std::min(c.x, d.x));
    const int hi = std::min(std::max(a.x, b.x), std::max(c.x, d.x));
    const int lo_y = std::max(std::min(a.y, b.y), std::min(c.y, d.y));
    const int hi_y = std::min(std::max(a.y, b.y), std::max(c.y, d.y));
    return (hi - lo) + (hi_y - lo_y) > 0;   //任一轴上区间重叠即重合
}

// 复刻 Canvas.cpp 的线段内部交叉求交：返回 true 并给出交点（t/u 严格在 (0,1) 内）
bool seg_intersection(const wxPoint& a, const wxPoint& b,
                      const wxPoint& c, const wxPoint& d, wxPoint* out){
    const long abx = b.x - a.x, aby = b.y - a.y;
    const long cdx = d.x - c.x, cdy = d.y - c.y;
    const long denom = abx * cdy - aby * cdx;
    if(denom == 0) return false;
    const long acx = c.x - a.x, acy = c.y - a.y;
    const double t = (double)(acx * cdy - acy * cdx) / denom;
    const double u = (double)(acx * aby - acy * abx) / denom;
    if(t <= 0.0 || t >= 1.0 || u <= 0.0 || u >= 1.0) return false;
    if(out){
        out->x = a.x + (int)std::lround(t * abx);
        out->y = a.y + (int)std::lround(t * aby);
    }
    return true;
}

// 点是否位于正交线段内部/端点（返回 0 表示在线上）
bool point_on_segment(const wxPoint& p, const wxPoint& a, const wxPoint& b){
    if(p.x == a.x && p.y == a.y) return true;
    if(p.x == b.x && p.y == b.y) return true;
    if(a.x == b.x) return (p.x == a.x && p.y >= std::min(a.y, b.y) && p.y <= std::max(a.y, b.y));
    if(a.y == b.y) return (p.y == a.y && p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x));
    return false;
}

//生成电路时导线不允许线段重合（点相交允许）。用扇出场景验证：
//同一输入端子驱动门的两路输入、同一门输出驱动两个输出端子。
void test_wires_do_not_overlap(){
    const std::string source =
        "module top(\n"
        "    input A,\n"
        "    input B,\n"
        "    output Y1,\n"
        "    output Y2\n"
        ");\n"
        "    wire n1;\n"
        "    wire n2;\n"
        "    assign n1 = A & A;\n"    //A 同一网络两路：输入端子出两条线
        "    assign n2 = n1 & B;\n"
        "    assign Y1 = n2;\n"
        "    assign Y2 = n2;\n"       //n2 扇出两个输出端子：门输出两条线
        "endmodule\n";

    const Verilator::ParseResult parsed = Verilator::parse_verilog(source);
    check(parsed.ok, "fanout source parses");
    if(!parsed.ok) return;

    const VerilogBridge::ModuleToProjectResult project =
        VerilogBridge::BuildProjectFromModule(parsed.module, wxPoint(0, 0));
    check(project.ok, "fanout module builds a project");
    if(!project.ok) return;

    bool overlap = false;
    const auto& wires = project.project.wires;
    for(size_t i = 0; i < wires.size() && !overlap; ++i){
        for(size_t j = i + 1; j < wires.size() && !overlap; ++j){
            const auto& pi = wires[i].points;
            const auto& pj = wires[j].points;
            for(size_t si = 1; si < pi.size(); ++si){
                for(size_t sj = 1; sj < pj.size(); ++sj){
                    if(segments_overlap(pi[si - 1], pi[si], pj[sj - 1], pj[sj])){
                        overlap = true;
                        break;
                    }
                }
            }
        }
    }
    check(!overlap, "generated wires do not overlap");
}

//生成电路时导线必须全程正交、无重复折点；且所有交叉点必须真实落在两条导线段上
//（曾因 try_detour 垂直段绕行点计算错误产生斜线段与重复点，弧画在斜线上"乱飞"）。
//用真实用户模块（4 输入 XOR）验证。
void test_wires_are_orthogonal(){
    const std::string source =
        "module top(\n"
        "    input in1,\n"
        "    input in2,\n"
        "    input in3,\n"
        "    input in4,\n"
        "    output out1\n"
        ");\n"
        "    wire net5;\n"
        "    wire net6;\n"
        "    assign net5 = ~(in1 & in2);\n"
        "    assign net6 = ~(in3 & in4);\n"
        "    assign out1 = (net5 ^ net6);\n"
        "endmodule\n";

    const Verilator::ParseResult parsed = Verilator::parse_verilog(source);
    check(parsed.ok, "orthogonal source parses");
    if(!parsed.ok) return;

    //origin 故意用非网格坐标：布局起点必须被吸附到网格
    const VerilogBridge::ModuleToProjectResult project =
        VerilogBridge::BuildProjectFromModule(parsed.module, wxPoint(37, 53));
    check(project.ok, "module builds a project");
    if(!project.ok) return;

    const auto& wires = project.project.wires;
    check(!wires.empty(), "project has wires");

    bool diagonal = false;
    bool duplicate = false;
    bool off_grid = false;
    bool overlap = false;
    for(size_t i = 0; i < wires.size(); ++i){
        const auto& pts = wires[i].points;
        for(const auto& p : pts){
            if(p.x % 10 != 0 || p.y % 10 != 0) off_grid = true;
        }
        for(size_t s = 1; s < pts.size(); ++s){
            if(pts[s].x != pts[s - 1].x && pts[s].y != pts[s - 1].y){
                diagonal = true;
            }
            if(pts[s] == pts[s - 1]) duplicate = true;
        }
        for(size_t j = i + 1; j < wires.size(); ++j){
            const auto& pj = wires[j].points;
            for(size_t si = 1; si < pts.size(); ++si){
                for(size_t sj = 1; sj < pj.size(); ++sj){
                    if(segments_overlap(pts[si - 1], pts[si], pj[sj - 1], pj[sj])){
                        overlap = true;
                    }
                }
            }
        }
    }
    check(!diagonal, "generated wires are all orthogonal (no diagonal segments)");
    check(!duplicate, "generated wires have no duplicate bend points");
    check(!off_grid, "generated wire points all lie on the grid");
    check(!overlap, "generated wires have no overlapping segments (point crossings allowed)");

    //交叉点必须真实落在两条导线段上（否则弧会画到导线外面）
    bool bad_cross = false;
    for(size_t i = 0; i < wires.size(); ++i){
        const auto& pi = wires[i].points;
        for(size_t j = i + 1; j < wires.size(); ++j){
            const auto& pj = wires[j].points;
            for(size_t si = 1; si < pi.size(); ++si){
                for(size_t sj = 1; sj < pj.size(); ++sj){
                    wxPoint p;
                    if(!seg_intersection(pi[si - 1], pi[si], pj[sj - 1], pj[sj], &p)) continue;
                    if(!point_on_segment(p, pi[si - 1], pi[si]) ||
                       !point_on_segment(p, pj[sj - 1], pj[sj])){
                        bad_cross = true;
                    }
                }
            }
        }
    }
    check(!bad_cross, "every wire crossing lies on both wire segments");
}

//更复杂模块：8 输入、3 层逻辑、11 条导线，验证 lane 循环与绕行兜底下
//仍然全程正交、网格对齐、无重复点、无线段重合（允许点相交）。
void test_complex_module_clean(){
    const std::string source =
        "module top(\n"
        "    input a1, input a2, input a3, input a4,\n"
        "    input b1, input b2, input b3, input b4,\n"
        "    output y\n"
        ");\n"
        "    wire n1, n2, n3, n4, m1, m2;\n"
        "    assign n1 = ~(a1 & a2);\n"
        "    assign n2 = ~(a3 & a4);\n"
        "    assign n3 = ~(b1 & b2);\n"
        "    assign n4 = ~(b3 & b4);\n"
        "    assign m1 = (n1 ^ n2);\n"
        "    assign m2 = (n3 ^ n4);\n"
        "    assign y = (m1 & m2);\n"
        "endmodule\n";

    const Verilator::ParseResult parsed = Verilator::parse_verilog(source);
    check(parsed.ok, "complex source parses");
    if(!parsed.ok) return;

    const VerilogBridge::ModuleToProjectResult project =
        VerilogBridge::BuildProjectFromModule(parsed.module, wxPoint(27, 63));
    check(project.ok, "complex module builds a project");
    if(!project.ok) return;

    const auto& wires = project.project.wires;
    //NAND 展开为 AND+NOT 两门，故实际 19 条导线
    check(wires.size() == 19, "complex module has 19 wires");

    bool diagonal = false;
    bool duplicate = false;
    bool off_grid = false;
    bool overlap = false;
    bool bad_cross = false;
    for(size_t i = 0; i < wires.size(); ++i){
        const auto& pts = wires[i].points;
        for(const auto& p : pts){
            if(p.x % 10 != 0 || p.y % 10 != 0) off_grid = true;
        }
        for(size_t s = 1; s < pts.size(); ++s){
            if(pts[s].x != pts[s - 1].x && pts[s].y != pts[s - 1].y) diagonal = true;
            if(pts[s] == pts[s - 1]) duplicate = true;
        }
        for(size_t j = i + 1; j < wires.size(); ++j){
            const auto& pj = wires[j].points;
            for(size_t si = 1; si < pts.size(); ++si){
                for(size_t sj = 1; sj < pj.size(); ++sj){
                    if(segments_overlap(pts[si - 1], pts[si], pj[sj - 1], pj[sj])){
                        overlap = true;
                    }
                    wxPoint p;
                    if(seg_intersection(pts[si - 1], pts[si], pj[sj - 1], pj[sj], &p) &&
                       (!point_on_segment(p, pts[si - 1], pts[si]) ||
                        !point_on_segment(p, pj[sj - 1], pj[sj]))){
                        bad_cross = true;
                    }
                }
            }
        }
    }
    check(!diagonal, "complex wires are all orthogonal");
    check(!duplicate, "complex wires have no duplicate bend points");
    check(!off_grid, "complex wire points all lie on the grid");
    check(!overlap, "complex wires have no overlapping segments");
    check(!bad_cross, "complex wire crossings all lie on both segments");
}

} // namespace

int main(int argc, char** argv){
    // 元数据加载会用到 wxStandardPaths，先初始化 wx 基础运行时以避免断言噪声。
    wxInitializer initializer(argc, argv);

    test_and_gate_export();
    test_xnor_gate_export();
    test_constant_input();
    test_unconnected_gate_input_rejected();
    test_unsupported_component_rejected();
    test_round_trip_from_verilog();
    test_round_trip_constant();
    test_empty_module_rejected();
    test_wires_do_not_overlap();
    test_wires_are_orthogonal();
    test_complex_module_clean();

    std::cout << "verilog_bridge_selftest: " << checks << " checks, "
              << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}

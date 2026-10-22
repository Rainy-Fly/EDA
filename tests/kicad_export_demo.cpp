// KiCad 网表导出演示/自检：
// 用一个内存中的最小项目走完整链路
//   ProjectData -> PcbProjectAdapter -> PcbNetlistBuilder -> KiCadNetlistExporter
// 并把结果写到 tests/kicad_export_demo.net，便于直接用 KiCad 打开检查。
//
// 从源码根目录运行：
//   ./build/kicad_export_demo
// 或在源码根目录执行 ctest 时会自动生成该文件。

#include "../src/DealProjectXML/DealProjectXML.hpp"
#include "../src/Netlist/KiCadNetlistExporter.hpp"
#include "../src/Netlist/PcbNetlistBuilder.hpp"
#include "../src/Netlist/PcbProjectAdapter.hpp"

#include <wx/init.h>

#include <cstdio>
#include <iostream>
#include <string>

namespace {

DealProjectXML::ComponentData make_resistor(int id, const std::string& reference,
                                            const std::string& value, int x, int y) {
    DealProjectXML::ComponentData component;
    component.id = id;
    component.type = "RESISTOR";
    component.name = "电阻";
    component.category = "Resistor";
    component.x = x;
    component.y = y;
    component.reference = reference;
    component.value = value;
    // footprint 留空，由 RESISTOR 定义里的默认封装补齐。
    return component;
}

DealProjectXML::WireData make_wire(int id, const wxPoint& start, const wxPoint& end,
                                   DealProjectXML::WireEndpointData startEndpoint,
                                   DealProjectXML::WireEndpointData endEndpoint) {
    DealProjectXML::WireData wire;
    wire.id = id;
    wire.type = "WIRE";
    wire.name = "导线";
    wire.points = {start, end};
    wire.start = std::move(startEndpoint);
    wire.end = std::move(endEndpoint);
    return wire;
}

DealProjectXML::WireEndpointData pin_endpoint(int componentId, const std::string& pinId) {
    DealProjectXML::WireEndpointData endpoint;
    endpoint.kind = "pin";
    endpoint.component_id = componentId;
    endpoint.pin_id = pinId;
    return endpoint;
}

DealProjectXML::WireEndpointData junction_endpoint(const std::string& junctionId) {
    DealProjectXML::WireEndpointData endpoint;
    endpoint.kind = "junction";
    endpoint.junction_id = junctionId;
    return endpoint;
}

DealProjectXML::WireEndpointData label_endpoint(const std::string& labelId) {
    DealProjectXML::WireEndpointData endpoint;
    endpoint.kind = "label";
    endpoint.label_id = labelId;
    return endpoint;
}

} // namespace

int main(int argc, char** argv) {
    // 元数据加载会通过 wxStandardPaths 查找库目录；控制台程序需要先初始化 wx。
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "wxWidgets initialization failed\n";
        return 1;
    }

    const std::string output = argc > 1 ? argv[1] : "tests/kicad_export_demo.net";

    DealProjectXML::ProjectData project;
    project.schema_version = "2.0";
    project.info.name = "kicad-export-demo";

    // R1.B(50,0) -- junction(100,0) -- R2.A(150,0)
    // R2.B(250,0) -- label OUT -- label OUT -- R3.A(350,0)
    project.components = {
        make_resistor(1, "R1", "10k", 0, 0),
        make_resistor(2, "R2", "1k", 200, 0),
        make_resistor(3, "R3", "220", 400, 0),
    };
    project.junctions = {{"j1", 100, 0}};
    project.labels = {
        {"out1", "OUT", "local", 300, 0},
        {"out2", "OUT", "local", 340, 0},
    };
    project.wires = {
        make_wire(10, {50, 0}, {100, 0}, pin_endpoint(1, "B"), junction_endpoint("j1")),
        make_wire(11, {100, 0}, {150, 0}, junction_endpoint("j1"), pin_endpoint(2, "A")),
        make_wire(12, {250, 0}, {300, 0}, pin_endpoint(2, "B"), label_endpoint("out1")),
        make_wire(13, {340, 0}, {350, 0}, label_endpoint("out2"), pin_endpoint(3, "A")),
    };

    const NetlistData::PcbProjectSnapshot snapshot =
        NetlistData::BuildPcbSnapshotFromProject(project);
    const NetlistData::PcbNetlistBuildResult built =
        NetlistData::PcbNetlistBuilder::build(snapshot);

    for (const auto& diagnostic : built.diagnostics) {
        std::cerr << (diagnostic.severity == "error" ? "[error] " : "[warning] ")
                  << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    if (!built.ok() || !built.value) {
        std::cerr << "netlist build failed\n";
        return 1;
    }

    NetlistData::KiCadNetlistExportOptions options;
    options.sourcePath = "tests/kicad_export_demo.xml";
    options.date = "2026-10-22";
    const NetlistData::KiCadNetlistWriteResult written =
        NetlistData::KiCadNetlistExporter::write(*built.value, output, options);
    for (const auto& diagnostic : written.diagnostics) {
        std::cerr << "[error] " << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    if (!written.ok()) {
        std::cerr << "netlist write failed\n";
        return 1;
    }

    std::cout << "wrote " << output << "\n";
    std::cout << "components: " << built.value->components.size()
              << ", nets: " << built.value->nets.size() << "\n";
    for (const auto& net : built.value->nets) {
        std::cout << "  net " << net.name << " (" << net.nodes.size() << " nodes)\n";
    }
    return 0;
}

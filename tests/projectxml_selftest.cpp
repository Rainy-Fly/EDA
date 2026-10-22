#include "../src/DealProjectXML/DealProjectXML.hpp"

#include <wx/filename.h>
#include <wx/init.h>
#include <wx/xml/xml.h>

#include <cstdio>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace{

int failures = 0;

void check(bool condition, const std::string& message){
    if(!condition){
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

std::string temp_path(const char* name){
    return wxFileName(wxFileName::GetTempDir(), wxString::FromUTF8(name)).GetFullPath().ToUTF8().data();
}

void test_roundtrip_2_0(){
    DealProjectXML::ProjectData data;
    data.schema_version = "2.0";
    data.info.name = "测试项目";
    data.info.author = "selftest";
    data.info.description = "roundtrip";
    data.offset = wxPoint(12, 34);
    data.scale = 1.5f;
    data.grid_step = 10;
    data.libraries.push_back({"builtin", "1.0"});

    DealProjectXML::ComponentData c;
    c.id = 7;
    c.type = "NAND";
    c.library_id = "builtin";
    c.definition_version = "3";
    c.name = "与非门";
    c.category = "LogicGate";
    c.x = 100;
    c.y = 200;
    c.rotation = 90;
    c.mirror = "x";
    c.reference = "U7";
    c.value = "74HC00";
    c.footprint = "Package_SO:SOIC-14";
    c.description = "实例描述";
    c.params = {{"DataBits", "1"}, {"extra", "value"}};
    c.pin_overrides = {{"OUT", "7"}};
    data.components.push_back(c);

    DealProjectXML::WireData w;
    w.id = 8;
    w.type = "WIRE";
    w.name = "导线";
    w.points = {wxPoint(50, 200), wxPoint(100, 200), wxPoint(100, 250)};
    w.start.kind = "pin";
    w.start.component_id = 7;
    w.start.pin_id = "OUT";
    w.end.kind = "junction";
    w.end.junction_id = "j1";
    data.wires.push_back(w);

    data.junctions.push_back({"j1", 100, 250});
    data.labels.push_back({"l1", "VCC", "power", 100, 300});
    data.no_connects.push_back({7, "IN2"});

    const std::string path = temp_path("tinyeda_projectxml_roundtrip.xml");
    check(DealProjectXML::WriteXml(data, path), "WriteXml 2.0");

    DealProjectXML::ProjectData loaded;
    check(DealProjectXML::ReadXml(path, &loaded), "ReadXml 2.0");
    check(loaded.schema_version == "2.0", "schema version = 2.0");
    check(loaded.info.name == "测试项目", "info.name");
    check(loaded.offset == wxPoint(12, 34), "view offset");
    check(loaded.scale > 1.49f && loaded.scale < 1.51f, "view scale");
    check(loaded.libraries.size() == 1 && loaded.libraries[0].version == "1.0", "libraries");

    check(loaded.components.size() == 1, "component count");
    const auto& lc = loaded.components[0];
    check(lc.id == 7, "component id");
    check(lc.type == "NAND", "component type");
    check(lc.library_id == "builtin", "component library");
    check(lc.definition_version == "3", "component definition version");
    check(lc.rotation == 90, "component rotation");
    check(lc.mirror == "x", "component mirror");
    check(lc.reference == "U7", "component reference");
    check(lc.value == "74HC00", "component value");
    check(lc.footprint == "Package_SO:SOIC-14", "component footprint");
    check(lc.params.count("extra") && lc.params.at("extra") == "value", "component param");
    check(lc.pin_overrides.size() == 1 && lc.pin_overrides[0].id == "OUT" &&
          lc.pin_overrides[0].number == "7", "component pin override");

    check(loaded.wires.size() == 1, "wire count");
    const auto& lw = loaded.wires[0];
    check(lw.points.size() == 3, "wire points");
    check(lw.start.kind == "pin" && lw.start.component_id == 7 && lw.start.pin_id == "OUT",
          "wire start endpoint");
    check(lw.end.kind == "junction" && lw.end.junction_id == "j1", "wire end endpoint");
    check(loaded.junctions.size() == 1 && loaded.junctions[0].x == 100, "junction");
    check(loaded.labels.size() == 1 && loaded.labels[0].text == "VCC" &&
          loaded.labels[0].scope == "power", "label");
    check(loaded.no_connects.size() == 1 && loaded.no_connects[0].component_id == 7 &&
          loaded.no_connects[0].pin_id == "IN2", "no-connect");

    wxRemoveFile(wxString::FromUTF8(path));
}

void test_1_0_compatibility(){
    const std::string path = temp_path("tinyeda_projectxml_legacy.xml");
    wxXmlDocument doc;
    wxXmlNode* root = new wxXmlNode(wxXML_ELEMENT_NODE, "teda-project");
    root->AddAttribute("schema-version", "1.0");
    doc.SetRoot(root);

    wxXmlNode* info = new wxXmlNode(wxXML_ELEMENT_NODE, "info");
    wxXmlNode* name = new wxXmlNode(wxXML_ELEMENT_NODE, "name");
    name->AddChild(new wxXmlNode(wxXML_TEXT_NODE, "", "旧项目"));
    info->AddChild(name);
    wxXmlNode* view = new wxXmlNode(wxXML_ELEMENT_NODE, "view");
    view->AddAttribute("offset-x", "5");
    view->AddAttribute("offset-y", "6");
    view->AddAttribute("scale", "2.0");
    info->AddChild(view);
    root->AddChild(info);

    wxXmlNode* circuit = new wxXmlNode(wxXML_ELEMENT_NODE, "circuit");
    wxXmlNode* comp = new wxXmlNode(wxXML_ELEMENT_NODE, "component");
    comp->AddAttribute("id", "1");
    comp->AddAttribute("type", "RESISTOR");
    comp->AddAttribute("name", "电阻");
    comp->AddAttribute("category", "Passive");
    comp->AddAttribute("x", "10");
    comp->AddAttribute("y", "20");
    for(const auto& kv : std::vector<std::pair<const char*, const char*>>{
            {"reference", "R1"}, {"value", "4.7k"}, {"footprint", "R_0603"},
            {"rotation", "270"}, {"mirror", "y"}, {"pin_numbers", "A=1;B=2"},
            {"resistance", "4.7k"}}){
        wxXmlNode* p = new wxXmlNode(wxXML_ELEMENT_NODE, "param");
        p->AddAttribute("key", kv.first);
        p->AddChild(new wxXmlNode(wxXML_TEXT_NODE, "", kv.second));
        comp->AddChild(p);
    }
    circuit->AddChild(comp);

    wxXmlNode* wire = new wxXmlNode(wxXML_ELEMENT_NODE, "wire");
    wire->AddAttribute("id", "2");
    wire->AddAttribute("type", "WIRE");
    for(const auto& pt : std::vector<std::pair<const char*, const char*>>{
            {"-40", "20"}, {"50", "20"}}){
        wxXmlNode* p = new wxXmlNode(wxXML_ELEMENT_NODE, "point");
        p->AddAttribute("x", pt.first);
        p->AddAttribute("y", pt.second);
        wire->AddChild(p);
    }
    circuit->AddChild(wire);
    root->AddChild(circuit);
    check(doc.Save(wxString::FromUTF8(path), 2), "write legacy 1.0");

    DealProjectXML::ProjectData loaded;
    check(DealProjectXML::ReadXml(path, &loaded), "ReadXml 1.0");
    check(loaded.schema_version == "1.0", "legacy schema version retained");
    check(loaded.components.size() == 1, "legacy component count");
    const auto& c = loaded.components[0];
    check(c.reference == "R1", "legacy reference migrated");
    check(c.value == "4.7k", "legacy value migrated");
    check(c.footprint == "R_0603", "legacy footprint migrated");
    check(c.rotation == 270, "legacy rotation migrated");
    check(c.mirror == "y", "legacy mirror migrated");
    check(c.params.count("reference") == 0 && c.params.count("value") == 0 &&
          c.params.count("footprint") == 0 && c.params.count("pin_numbers") == 0,
          "legacy typed params removed");
    check(c.params.count("resistance") && c.params.at("resistance") == "4.7k",
          "legacy ordinary param kept");
    check(c.pin_overrides.size() == 2 && c.pin_overrides[0].id == "A" &&
          c.pin_overrides[0].number == "1" && c.pin_overrides[1].number == "2",
          "legacy pin numbers migrated");
    check(loaded.wires.size() == 1 && loaded.wires[0].points.size() == 2, "legacy wire");
    check(loaded.wires[0].start.kind.empty() && loaded.wires[0].end.kind.empty(),
          "legacy wire has no explicit endpoints");

    // 保存为 2.0 后再读：迁移结果不丢失。
    const std::string upgraded = temp_path("tinyeda_projectxml_upgraded.xml");
    check(DealProjectXML::WriteXml(loaded, upgraded), "write upgraded 2.0");
    DealProjectXML::ProjectData again;
    check(DealProjectXML::ReadXml(upgraded, &again), "read upgraded 2.0");
    check(again.schema_version == "2.0", "upgraded schema version");
    check(again.components.size() == 1 && again.components[0].reference == "R1" &&
          again.components[0].rotation == 270 && again.components[0].mirror == "y",
          "upgraded component attributes");
    check(again.components[0].pin_overrides.size() == 2, "upgraded pin overrides");

    wxRemoveFile(wxString::FromUTF8(path));
    wxRemoveFile(wxString::FromUTF8(upgraded));
}

void test_errors(){
    const std::string unknown = temp_path("tinyeda_projectxml_unknown.xml");
    wxXmlDocument doc;
    wxXmlNode* root = new wxXmlNode(wxXML_ELEMENT_NODE, "teda-project");
    root->AddAttribute("schema-version", "9.0");
    doc.SetRoot(root);
    check(doc.Save(wxString::FromUTF8(unknown), 2), "write unknown version");
    DealProjectXML::ProjectData data;
    check(!DealProjectXML::ReadXml(unknown, &data), "unknown version rejected");

    const std::string duplicate = temp_path("tinyeda_projectxml_duplicate.xml");
    wxXmlDocument dupDoc;
    wxXmlNode* dupRoot = new wxXmlNode(wxXML_ELEMENT_NODE, "teda-project");
    dupRoot->AddAttribute("schema-version", "2.0");
    dupDoc.SetRoot(dupRoot);
    wxXmlNode* circuit = new wxXmlNode(wxXML_ELEMENT_NODE, "circuit");
    for(int i = 0; i < 2; ++i){
        wxXmlNode* c = new wxXmlNode(wxXML_ELEMENT_NODE, "component");
        c->AddAttribute("id", "1");
        c->AddAttribute("type", "RESISTOR");
        circuit->AddChild(c);
    }
    dupRoot->AddChild(circuit);
    check(dupDoc.Save(wxString::FromUTF8(duplicate), 2), "write duplicate ids");
    check(!DealProjectXML::ReadXml(duplicate, &data), "duplicate instance id rejected");

    check(!DealProjectXML::WriteXml(data, ""), "empty path rejected");
    check(!DealProjectXML::ReadXml("/definitely/not/a/file.xml", &data), "missing file rejected");

    wxRemoveFile(wxString::FromUTF8(unknown));
    wxRemoveFile(wxString::FromUTF8(duplicate));
}

} // namespace

int main(){
    wxInitializer initializer;
    if(!initializer.IsOk()){
        std::cerr << "wxWidgets initialization failed\n";
        return 1;
    }
    test_roundtrip_2_0();
    test_1_0_compatibility();
    test_errors();
    if(failures == 0){
        std::cout << "projectxml_selftest: all checks passed\n";
        return 0;
    }
    std::cerr << "projectxml_selftest: " << failures << " check(s) failed\n";
    return 1;
}

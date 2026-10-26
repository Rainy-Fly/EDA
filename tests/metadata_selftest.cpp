#include "../src/Canva/MetaData.hpp"
#include "../src/metadata/MetaDataJson.hpp"

#include <iostream>
#include <memory>
#include <set>
#include <string>

namespace{

int failures = 0;

void check(bool condition, const std::string& message){
    if(!condition){
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

void test_resistor(){
    std::unique_ptr<MetaData> md(load_metadata("RESISTOR"));
    check(md != nullptr, "RESISTOR exists");
    if(!md) return;
    check(md->pins.size() == 2, "RESISTOR has 2 explicit pins");
    check(md->pins[0].id == "A" && md->pins[0].number == "1", "RESISTOR pin A=1");
    check(md->pins[1].id == "B" && md->pins[1].number == "2", "RESISTOR pin B=2");
    check(md->reference_prefix == "R", "RESISTOR reference prefix");
    check(!md->footprint.empty(), "RESISTOR footprint");
    check(md->value_parameter == "resistance", "RESISTOR value parameter");
    check(md->library_id == "builtin" && md->library_version == "1.0", "RESISTOR library");
    check(md->definition_version == "1", "RESISTOR definition version");
    check(md->params.count("resistance") && md->params["resistance"] == "10k",
          "RESISTOR structured parameter default");
    check(md->parameter_defs.count("resistance") &&
          md->parameter_defs.at("resistance").type == "string",
          "RESISTOR parameter definition");
    check(md->pins[0].direction == "passive" && md->pins[0].electrical_type == "passive",
          "RESISTOR pin electrical metadata");
}

void test_gate(){
    std::unique_ptr<MetaData> md(load_metadata("NAND"));
    check(md != nullptr, "NAND exists");
    if(!md) return;
    check(md->pins.size() == 3, "NAND has 2 inputs + 1 output");
    check(md->pins[0].id == "IN1" && md->pins[1].id == "IN2" && md->pins[2].id == "OUT",
          "NAND pin ids");
    check(md->pins[0].number == "1" && md->pins[2].number == "3", "NAND pad numbers");
    check(md->pins[0].direction == "input" && md->pins[2].direction == "output",
          "NAND pin directions");
    const auto* gate = dynamic_cast<const LogicGateMetaData*>(md.get());
    check(gate && gate->get_inputs() == 2 && gate->get_outputs() == 1, "NAND gate typing");
}

void test_connector_and_ic(){
    std::unique_ptr<MetaData> header(load_metadata("HEADER"));
    check(header != nullptr, "HEADER exists");
    if(header){
        check(header->pins.size() == 8, "HEADER has 8 explicit pins");
        std::set<std::string> ids, numbers;
        for(const MetaDataPin& p : header->pins){
            ids.insert(p.id);
            numbers.insert(p.number);
        }
        check(ids.size() == header->pins.size(), "HEADER pin ids unique");
        check(numbers.size() == header->pins.size(), "HEADER pad numbers unique");
    }

    std::unique_ptr<MetaData> opamp(load_metadata("IC-COM-Comparator"));
    check(opamp != nullptr, "comparator exists");
    if(opamp){
        check(opamp->pins.size() == 5, "comparator has 5 explicit pins");
        check(opamp->find_pin("V+") && opamp->find_pin("V-"), "comparator power pins");
    }
}

void test_all_definitions(){
    std::vector<MetaData*> all = load_all_metadata();
    check(!all.empty(), "metadata registry is not empty");
    int withPins = 0;
    for(MetaData* raw : all){
        std::unique_ptr<MetaData> md(raw);
        if(md->get_category() == "Wire") continue;
        check(!md->pins.empty(), "non-wire definition has explicit pins: " + md->get_type());
        if(md->pins.empty()) continue;
        ++withPins;
        std::set<std::string> ids;
        for(const MetaDataPin& pin : md->pins){
            check(!pin.id.empty(), "pin id not empty: " + md->get_type());
            check(ids.insert(pin.id).second,
                  "pin id unique: " + md->get_type() + "/" + pin.id);
            check(!pin.number.empty(), "pin number not empty: " + md->get_type() + "/" + pin.id);
        }
        check(!md->library_id.empty() && !md->library_version.empty() &&
              !md->definition_version.empty(),
              "library and definition versions present: " + md->get_type());
        const std::string category = md->get_category();
        // 排除仿真用端子和层级化模块实例，它们不参与 PCB 封装
        if(category != "Ground" && category != "Source" &&
           category != "Simulation" && category != "Hierarchy")
            check(!md->footprint.empty(),
                  "physical definition has a default footprint: " + md->get_type());
    }
    check(withPins >= 100, "most definitions carry explicit pins");
}

} // namespace

int main(){
    test_resistor();
    test_gate();
    test_connector_and_ic();
    test_all_definitions();
    if(failures == 0){
        std::cout << "metadata_selftest: all checks passed\n";
        return 0;
    }
    std::cerr << "metadata_selftest: " << failures << " check(s) failed\n";
    return 1;
}

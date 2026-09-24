#pragma once
#include<string>
#include<utility>
const std::string MetaDataJsonPath="" ;
class MetaData{
public:
    MetaData() = default;
    MetaData(std::string name, int id) : name(std::move(name)), id(id) {}
    const std::string& get_name() const { return name; }
    int get_id() const { return id; }
private:
    std::string name;
    int id;
};

//与非门、电阻、电容、芯片、模块、子图、连接器、电源/地符号、激励源  等等 ，每个都是一个类，继承自MetaData

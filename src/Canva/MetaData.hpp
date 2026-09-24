#pragma once
#include<string>
#include<map>
#include<utility>

// 元件元数据文件所在目录（相对仓库根；运行时同时支持“当前目录”与“可执行文件相对”两种基准）
const std::string MetaDataJsonPath = "src/metadata/";

// 元件元数据基类：每个元件在 src/metadata/ 下的多个JSON文件中有一条定义，
// 运行时由 MetaDataJson（load_metadata）读取为 MetaData（或其子类）实例。
// 公共字段：中文名/英文名/ID/类型字符串/类别/描述；其余键值对统一放入 params，
// 特定类别可继承本类并增加类型化字段（如逻辑门 LogicGateMetaData 的输入/输出/位宽）。
class MetaData{
public:
    MetaData() = default;
    MetaData(std::string name, int id) : name(std::move(name)), id(id) {}
    MetaData(std::string name, std::string name_en, int id,
             std::string type, std::string category, std::string description)
        : name(std::move(name)), name_en(std::move(name_en)), id(id),
          type(std::move(type)), category(std::move(category)),
          description(std::move(description)) {}
    virtual ~MetaData() = default;

    const std::string& get_name() const        { return name; }
    const std::string& get_name_en() const     { return name_en; }
    int get_id() const                         { return id; }
    const std::string& get_type() const        { return type; }
    const std::string& get_category() const    { return category; }
    const std::string& get_description() const { return description; }

    // 通用参数（JSON中除标准字段外的其余键值对，如 DataBits/resistance/voltage 等）
    const std::map<std::string, std::string>& get_params() const { return params; }
    // 取某个参数；不存在返回 def
    std::string get_param(const std::string& key, const std::string& def = "") const{
        auto it = params.find(key);
        return it == params.end() ? def : it->second;
    }

    // 通用参数表（子类/属性栏可直接读写）
    std::map<std::string, std::string> params;

private:
    std::string name;
    std::string name_en;
    int id = 0;
    std::string type;      //类型字符串，如 "NAND"（与 ItemType 对应）
    std::string category;  //类别，如 "LogicGate"/"Passive"/"Source"/"Connector"/"Wire"
    std::string description;
};

// 逻辑门元数据：与非门、与门、或门、或非门、异或门、非门。
// 增加类型化的输入数/输出数/数据位宽(DataBits)；
// 其余类别（电阻、电容、芯片、模块、子图、连接器、电源/地、激励源 等）可按同样方式继承MetaData。
class LogicGateMetaData : public MetaData{
public:
    LogicGateMetaData() = default;
    LogicGateMetaData(std::string name, std::string name_en, int id,
                      std::string type, std::string category, std::string description)
        : MetaData(std::move(name), std::move(name_en), id,
                   std::move(type), std::move(category), std::move(description)){}

    int get_inputs() const   { return inputs; }
    int get_outputs() const  { return outputs; }
    int get_data_bits() const{ return data_bits; }   //DataBits：数据位宽（门为1）

    void set_inputs(int v)   { inputs = v;    params["inputs"]   = std::to_string(v); }
    void set_outputs(int v)  { outputs = v;   params["outputs"]  = std::to_string(v); }
    void set_data_bits(int v){ data_bits = v; params["DataBits"] = std::to_string(v); }

private:
    int inputs = 2;    //输入引脚数（非门为1）
    int outputs = 1;   //输出引脚数
    int data_bits = 1; //数据位宽
};

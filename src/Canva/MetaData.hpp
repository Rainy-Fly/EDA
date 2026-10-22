#pragma once
#include<string>
#include<map>
#include<vector>
#include<utility>

// 元件元数据文件所在目录（相对仓库根；运行时同时支持“当前目录”与“可执行文件相对”两种基准）
const std::string MetaDataJsonPath = "src/metadata/";

// 元件定义中的显式引脚。
// id 是元件定义内的稳定逻辑引脚标识（如 "IN1"/"A"/"COIL1"），
// number 是对应的物理焊盘号（如 KiCad 封装的 "1"/"2"；未知时为空）。
// direction/electrical_type 供仿真与 ERC 使用；x/y 是相对元件锚点的网格偏移。
struct MetaDataPin{
    std::string id;
    std::string name;
    std::string number;
    std::string direction = "passive";         // input/output/inout/passive/power
    std::string electrical_type = "passive";   // input/output/bidirectional/passive/power/ground/no_connect
    int x = 0;
    int y = 0;
    unsigned int width = 1;
};

// 结构化参数定义：类型、默认值、是否允许实例覆盖。
struct MetaDataParameter{
    std::string type = "string";   // string/int/float/bool/enum
    std::string default_value;
    bool editable = true;
    std::vector<std::string> choices;
};

// 元件元数据基类：每个元件在 src/metadata/ 下的多个JSON文件中有一条定义，
// 运行时由 MetaDataJson（load_metadata）读取为 MetaData（或其子类）实例。
// 公共字段：中文名/英文名/ID/类型字符串/类别/描述；显式引脚、库版本与结构化参数也在此层。
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
    void set_description(const std::string& d) { description = d; }   //属性栏可编辑描述

    // 通用参数（JSON中除标准字段外的其余键值对，如 DataBits/resistance/voltage 等）
    const std::map<std::string, std::string>& get_params() const { return params; }
    // 取某个参数；不存在返回 def
    std::string get_param(const std::string& key, const std::string& def = "") const{
        auto it = params.find(key);
        return it == params.end() ? def : it->second;
    }

    // 通用参数表（子类/属性栏可直接读写）。值是实例当前生效值，
    // 初始值来自元数据参数定义的 default_value。
    std::map<std::string, std::string> params;

    //---- 元件定义（库级）----
    // 库版本与元件定义版本职责分离：
    //   library_id/library_version 标识“这套元件定义来自哪个库版本”；
    //   definition_version 标识单个元件定义自身的修订版本。
    // 项目实例只引用这三者，不复制定义内容。
    std::string library_id = "builtin";
    std::string library_version = "1.0";
    std::string definition_version = "1";

    // PCB 相关默认属性；实例可在项目 XML 中覆盖。
    std::string reference_prefix;
    std::string footprint;
    std::string value;
    std::string value_parameter;   //值来自哪个参数（如 "resistance"）

    // 显式引脚定义；为空时画布/网表层才回退到旧启发式。
    std::vector<MetaDataPin> pins;

    // 结构化参数定义（默认值同时写入 params，供属性栏显示与编辑）。
    std::map<std::string, MetaDataParameter> parameter_defs;

    // 旧版 pin_numbers 字符串解析结果（"A=1;B=2"），仅用于兼容旧元数据。
    std::map<std::string, std::string> legacy_pin_numbers;

    // 取显式引脚；没有显式定义时返回 nullptr。
    const MetaDataPin* find_pin(const std::string& pin_id) const{
        for(const MetaDataPin& pin : pins)
            if(pin.id == pin_id) return &pin;
        return nullptr;
    }

    // 取引脚焊盘号：显式 pins[].number 优先，其次旧版 pin_numbers。
    std::string pin_number(const std::string& pin_id) const{
        if(const MetaDataPin* pin = find_pin(pin_id); pin && !pin->number.empty())
            return pin->number;
        auto it = legacy_pin_numbers.find(pin_id);
        return it == legacy_pin_numbers.end() ? std::string() : it->second;
    }

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

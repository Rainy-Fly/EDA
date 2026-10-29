#include"./MetaDataJson.hpp"
#include"../Canva/MetaData.hpp"

#include<json/json.h>
#include<wx/dir.h>
#include<wx/stdpaths.h>
#include<wx/filename.h>

#include<algorithm>
#include<fstream>
#include<set>
#include<map>
#include<unordered_map>
#include<vector>

namespace{

// 一条元件定义（JSON中 components 数组的一个元素解析结果）
struct Definition{
    std::string name;       //中文名
    std::string name_en;    //英文名
    std::string type;       //类型字符串（与 ItemType 对应，如 "NAND"/"WIRE"）
    std::string category;   //类别（"LogicGate"/"Passive"/"Source"/"Connector"/"Wire"）
    std::string description;
    int id = 0;
    int inputs = 2, outputs = 1, data_bits = 1;
    std::map<std::string, std::string> params;   //其余键值对（通用参数）

    //---- 2.0 元数据模型 ----
    std::string library_id = "builtin";
    std::string library_version = "1.0";
    std::string definition_version = "1";
    std::string reference_prefix;
    std::string footprint;
    std::string value;
    std::string value_parameter;
    std::vector<MetaDataPin> pins;
    std::map<std::string, MetaDataParameter> parameter_defs;
    std::map<std::string, std::string> legacy_pin_numbers;
};

std::string scalar_to_string(const Json::Value& v){
    if(v.isString()) return v.asString();
    if(v.isBool()) return v.asBool() ? "true" : "false";
    if(v.isInt() || v.isUInt() || v.isInt64() || v.isUInt64())
        return std::to_string(v.asInt64());
    if(v.isDouble()) return std::to_string(v.asDouble());
    return {};
}

// 解析旧版 "A=1;B=2" 或 "1,2" 形式的引脚号映射。
void parse_legacy_pin_numbers(const std::string& text,
                              std::map<std::string, std::string>& out){
    std::vector<std::string> positional;
    std::string item;
    auto flush = [&](){
        while(!item.empty() && (item.back() == ' ' || item.back() == '\t')) item.pop_back();
        std::size_t begin = 0;
        while(begin < item.size() && (item[begin] == ' ' || item[begin] == '\t')) ++begin;
        const std::string token = item.substr(begin);
        if(!token.empty()){
            const std::size_t eq = token.find('=');
            if(eq != std::string::npos){
                const std::string key = token.substr(0, eq);
                const std::string value = token.substr(eq + 1);
                if(!key.empty() && !value.empty()) out[key] = value;
            }else{
                positional.push_back(token);
            }
        }
        item.clear();
    };
    for(char ch : text){
        if(ch == ';' || ch == ',') flush();
        else item.push_back(ch);
    }
    flush();
    // 纯数字列表（"1,2"）按出现顺序映射到 A/B（兼容旧约定）。
    if(out.empty()){
        for(std::size_t i = 0; i < positional.size(); ++i)
            out[std::string(1, (char)('A' + i))] = positional[i];
    }
}

// 把 parameters/params 块解析为“结构化定义 + 默认值”：
// 参数值同时写入 params，保证属性栏与旧代码无需改动即可看到/编辑。
void parse_parameter_block(const Json::Value& block, Definition& d){
    if(!block.isObject()) return;
    for(const std::string& key : block.getMemberNames()){
        const Json::Value& v = block[key];
        MetaDataParameter def;
        if(v.isObject()){
            def.type = v.get("type", "string").asString();
            if(v.isMember("default")) def.default_value = scalar_to_string(v["default"]);
            if(v.isMember("editable")) def.editable = v["editable"].asBool();
            if(v["choices"].isArray()){
                for(const Json::Value& c : v["choices"])
                    def.choices.push_back(scalar_to_string(c));
            }
        }else{
            def.default_value = scalar_to_string(v);
            if(v.isBool()) def.type = "bool";
            else if(v.isInt() || v.isUInt() || v.isInt64() || v.isUInt64()) def.type = "int";
            else if(v.isDouble()) def.type = "float";
        }
        d.parameter_defs[key] = def;
        if(!def.default_value.empty() || d.params.find(key) == d.params.end())
            d.params[key] = def.default_value;
    }
}

// 解析显式引脚数组。未知/非法项跳过；id 为空时用 name，二者都为空则跳过。
void parse_pins(const Json::Value& pins, Definition& d){
    if(!pins.isArray()) return;
    for(const Json::Value& p : pins){
        if(!p.isObject()) continue;
        MetaDataPin pin;
        pin.id = scalar_to_string(p.get("id", Json::Value()));
        pin.name = scalar_to_string(p.get("name", Json::Value()));
        if(pin.id.empty()) pin.id = pin.name;
        if(pin.name.empty()) pin.name = pin.id;
        if(pin.id.empty()) continue;
        if(p.isMember("number")) pin.number = scalar_to_string(p["number"]);
        pin.direction = p.get("direction", "passive").asString();
        if(p.isMember("electrical_type")) pin.electrical_type = scalar_to_string(p["electrical_type"]);
        else if(p.isMember("electricalType")) pin.electrical_type = scalar_to_string(p["electricalType"]);
        if(p.isMember("x")) pin.x = p["x"].asInt();
        if(p.isMember("y")) pin.y = p["y"].asInt();
        if(p.isMember("width")) pin.width = (unsigned int)p["width"].asUInt();
        d.pins.push_back(std::move(pin));
    }
}

// 元数据目录候选：优先“当前目录”，兜底“可执行文件所在目录/..”（build/.. => 仓库根）。
// 与 ItemSVG 的路径解析策略一致，保证从任意目录运行都能找到元数据文件。
std::vector<wxString> metadata_dirs(){
    std::set<wxString> dirs;
    const wxString rel = wxString::FromUTF8(MetaDataJsonPath);   //"src/metadata/"
    dirs.insert(rel);
    const wxString exe = wxStandardPaths::Get().GetExecutablePath();
    const wxString root = wxFileName(exe).GetPath() + wxFILE_SEP_PATH + wxT("..");
    dirs.insert(root + wxFILE_SEP_PATH + rel);
    return std::vector<wxString>(dirs.begin(), dirs.end());
}

// 解析一个JSON文件，把其中所有元件定义写入索引（name/name_en/type 三个键都指向同一定义）
void load_json_file(const wxString& path,
                    std::unordered_map<std::string, Definition>& index){
    std::ifstream ifs(path.ToStdString());
    if(!ifs) return;
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    if(!Json::parseFromStream(builder, ifs, &root, &errs)) return;
    const Json::Value& comps = root["components"];
    if(!comps.isArray()) return;

    // 文件级库版本：所有定义默认继承；单个元件可覆盖。
    std::string library_id = root.get("library_id", "builtin").asString();
    std::string library_version = root.get("library_version", "1.0").asString();
    if(const Json::Value& lib = root["library"]; lib.isObject()){
        library_id = lib.get("id", library_id).asString();
        library_version = lib.get("version", library_version).asString();
    }
    const std::string file_schema = root.get("schema_version", "2.0").asString();

    for(const Json::Value& c : comps){
        if(!c.isObject()) continue;
        Definition d;
        d.name        = c.get("name", "").asString();
        d.name_en     = c.get("name_en", "").asString();
        d.type        = c.get("type", "").asString();
        d.category    = c.get("category", "").asString();
        d.description = c.get("description", "").asString();
        if(c.isMember("id")) d.id = c["id"].asInt();

        d.library_id = c.get("library_id", library_id).asString();
        d.library_version = c.get("library_version", library_version).asString();
        if(const Json::Value& lib = c["library"]; lib.isObject()){
            d.library_id = lib.get("id", d.library_id).asString();
            d.library_version = lib.get("version", d.library_version).asString();
        }
        d.definition_version = c.get("definition_version", "1").isString()
                                   ? c["definition_version"].asString()
                                   : scalar_to_string(c.get("definition_version", Json::Value(1)));
        if(d.definition_version.empty()) d.definition_version = "1";

        // 结构化定义字段
        d.reference_prefix = c.get("reference_prefix", "").asString();
        d.footprint = c.get("footprint", "").asString();
        d.value = c.get("value", "").asString();
        d.value_parameter = c.get("value_parameter", "").asString();
        parse_pins(c["pins"], d);
        parse_parameter_block(c["parameters"], d);
        // 兼容旧元数据的嵌套 params 块（此前被解析器忽略）
        parse_parameter_block(c["params"], d);
        if(c.isMember("pin_numbers"))
            parse_legacy_pin_numbers(scalar_to_string(c["pin_numbers"]), d.legacy_pin_numbers);
        // 值/封装是实例可覆盖属性；保留在 params 中以便属性栏继续显示与编辑。
        if(!d.value.empty()) d.params["value"] = d.value;
        if(!d.footprint.empty()) d.params["footprint"] = d.footprint;

        // 标准字段与结构化字段之外的所有标量键值对 -> 通用参数表。
        // 这样旧元数据仍能工作，同时不再让结构字段污染实例参数。
        const std::set<std::string> structured = {
            "name", "name_en", "type", "category", "description", "id",
            "library", "library_id", "library_version", "definition_version",
            "reference_prefix", "footprint", "value", "value_parameter",
            "pins", "parameters", "params", "pin_numbers", "schema_version"
        };
        for(const std::string& k : c.getMemberNames()){
            if(structured.find(k) != structured.end()) continue;
            const std::string text = scalar_to_string(c[k]);
            if(!text.empty()) d.params[k] = text;
        }

        // 门类专用字段（同时保留在params中，便于通用显示与子类类型化访问）
        auto take_int = [&](const char* key, int def)->int{
            auto it = d.params.find(key);
            if(it == d.params.end()) return def;
            try{ return std::stoi(it->second); }catch(...){ return def; }
        };
        d.inputs    = take_int("inputs", 2);
        d.outputs   = take_int("outputs", 1);
        d.data_bits = take_int("DataBits", 1);

        // 没有显式引脚时，按旧规则补出引脚结构（保留旧元数据的可用性）。
        if(d.pins.empty()){
            if(d.category == "LogicGate"){
                for(int i = 0; i < std::max(1, d.inputs); ++i){
                    MetaDataPin pin;
                    pin.id = pin.name = "IN" + std::to_string(i + 1);
                    pin.number = std::to_string(i + 1);
                    pin.direction = "input";
                    pin.electrical_type = "input";
                    pin.x = -40;
                    pin.y = (2 * i - (d.inputs - 1)) * 10;
                    d.pins.push_back(pin);
                }
                MetaDataPin out;
                out.id = out.name = "OUT";
                out.number = std::to_string(d.inputs + 1);
                out.direction = "output";
                out.electrical_type = "output";
                out.x = 40;
                out.y = 0;
                d.pins.push_back(out);
            }
        }

        // 空名字的定义忽略
        if(d.name.empty()) continue;
        if(!d.name_en.empty())   index[d.name_en] = d;
        if(!d.type.empty())      index[d.type]    = d;
        index[d.name] = d;
    }
    (void)file_schema;
}

// 全局注册表：首次调用时扫描并加载 src/metadata/ 下所有 *.json
const std::unordered_map<std::string, Definition>& registry(){
    static const std::unordered_map<std::string, Definition> reg = [](){
        std::unordered_map<std::string, Definition> r;
        for(const wxString& dir : metadata_dirs()){
            wxArrayString files;
            if(!wxDir::Exists(dir)) continue;
            wxDir::GetAllFiles(dir, &files, wxT("*.json"), wxDIR_FILES);
            for(const wxString& f : files){
                load_json_file(f, r);
            }
        }
        return r;
    }();
    return reg;
}

// 依据定义构造对应类别的 MetaData 实例（当前：逻辑门用 LogicGateMetaData，其余用基类）
void copy_definition(const Definition& d, MetaData* m){
    m->params = d.params;
    m->library_id = d.library_id;
    m->library_version = d.library_version;
    m->definition_version = d.definition_version;
    m->reference_prefix = d.reference_prefix;
    m->footprint = d.footprint;
    m->value = d.value;
    m->value_parameter = d.value_parameter;
    m->pins = d.pins;
    m->parameter_defs = d.parameter_defs;
    m->legacy_pin_numbers = d.legacy_pin_numbers;
}

MetaData* build_metadata(const Definition& d){
    if(d.category == "LogicGate"){
        auto* g = new LogicGateMetaData(d.name, d.name_en, d.id,
                                        d.type, d.category, d.description);
        g->set_inputs(d.inputs);
        g->set_outputs(d.outputs);
        g->set_data_bits(d.data_bits);
        copy_definition(d, g);
        return g;
    }
    auto* m = new MetaData(d.name, d.name_en, d.id,
                           d.type, d.category, d.description);
    copy_definition(d, m);
    return m;
}

} // namespace

MetaData* load_metadata(const std::string& key){
    if(key.empty()) return nullptr;
    const auto& reg = registry();
    auto it = reg.find(key);
    if(it == reg.end()) return nullptr;   //找不到：返回nullptr，不报错
    return build_metadata(it->second);    //每次返回独立副本，调用者拥有
}

//枚举所有元件定义：索引中同一元件被 name/name_en/type 三个键重复指向，
//按id去重后每个元件只返回一份（按id排序保证稳定顺序）
std::vector<MetaData*> load_all_metadata(){
    std::vector<MetaData*> out;
    const auto& reg = registry();
    std::map<int, const Definition*> unique;   //id -> 定义（id唯一且已分配）
    for(const auto& kv : reg){
        if(unique.find(kv.second.id) == unique.end()){
            unique[kv.second.id] = &kv.second;
        }
    }
    for(const auto& kv : unique){
        out.push_back(build_metadata(*kv.second));
    }
    return out;
}

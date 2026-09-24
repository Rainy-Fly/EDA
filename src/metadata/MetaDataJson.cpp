#include"./MetaDataJson.hpp"
#include"../Canva/MetaData.hpp"

#include<json/json.h>
#include<wx/dir.h>
#include<wx/stdpaths.h>
#include<wx/filename.h>

#include<fstream>
#include<set>
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
};

// 元数据目录候选：优先“当前目录”，兜底“可执行文件所在目录/..”（build/.. => 仓库根）。
// 与 ItemPNG 的图片路径解析策略一致，保证从任意目录运行都能找到元数据文件。
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

    for(const Json::Value& c : comps){
        if(!c.isObject()) continue;
        Definition d;
        d.name        = c.get("name", "").asString();
        d.name_en     = c.get("name_en", "").asString();
        d.type        = c.get("type", "").asString();
        d.category    = c.get("category", "").asString();
        d.description = c.get("description", "").asString();
        if(c.isMember("id")) d.id = c["id"].asInt();

        // 标准字段之外的所有键值对 -> 通用参数表
        for(const std::string& k : c.getMemberNames()){
            if(k == "name" || k == "name_en" || k == "type" ||
               k == "category" || k == "description" || k == "id"){
                continue;
            }
            const Json::Value& v = c[k];
            if(v.isString()){
                d.params[k] = v.asString();
            }else if(v.isBool()){
                d.params[k] = v.asBool() ? "true" : "false";
            }else if(v.isInt() || v.isUInt() || v.isInt64() || v.isUInt64()){
                d.params[k] = std::to_string(v.asInt64());
            }else if(v.isDouble()){
                d.params[k] = std::to_string(v.asDouble());
            }
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

        // 空名字的定义忽略
        if(d.name.empty()) continue;
        if(!d.name_en.empty())   index[d.name_en] = d;
        if(!d.type.empty())      index[d.type]    = d;
        index[d.name] = d;
    }
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
MetaData* build_metadata(const Definition& d){
    if(d.category == "LogicGate"){
        auto* g = new LogicGateMetaData(d.name, d.name_en, d.id,
                                        d.type, d.category, d.description);
        g->set_inputs(d.inputs);
        g->set_outputs(d.outputs);
        g->set_data_bits(d.data_bits);
        g->params = d.params;
        return g;
    }
    auto* m = new MetaData(d.name, d.name_en, d.id,
                           d.type, d.category, d.description);
    m->params = d.params;
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

#include"./ItemSVG.hpp"
#include"../metadata/MetaDataJson.hpp"
#include<unordered_map>
#include<vector>
#include<wx/stdpaths.h>
#include<wx/filename.h>

namespace{

// 元件名字 -> 类型。已知门类返回对应类型；导线/总线返回WIRE（Linking）；
// 其余（电子符号库通用元件）返回GENERIC，具体类型串由元数据给出（= SVG文件名）
ItemType type_from_name(const std::string& name){
    if(name == "与非门" || name == "NAND") return ItemType::NAND;
    if(name == "与门"   || name == "AND")  return ItemType::AND;
    if(name == "或门"   || name == "OR")   return ItemType::OR;
    if(name == "或非门" || name == "NOR")  return ItemType::NOR;
    if(name == "异或门" || name == "XOR")  return ItemType::XOR;
    if(name == "非门"   || name == "NOT")  return ItemType::NOT;
    if(name == "导线"   || name == "WIRE" ||
       name == "总线"   || name == "BUS")  return ItemType::WIRE;
    return ItemType::GENERIC;
}

// 类型 -> src/Canva/assets/Symbols/ 下的简单文件名
std::string simple_name_of(ItemType type){
    switch(type){
        case ItemType::NAND: return "NAND.svg";
        case ItemType::AND:  return "AND.svg";
        case ItemType::OR:   return "OR.svg";
        case ItemType::NOR:  return "NOR.svg";
        case ItemType::XOR:  return "XOR.svg";
        case ItemType::NOT:  return "NOT.svg";
        default: return "";
    }
}

// 类型 -> src/assets/electronic-symbols/SVG/ 下的原始文件名（兜底）
std::string source_name_of(ItemType type){
    switch(type){
        case ItemType::NAND: return "IC-COM-Logic-NAND.svg";
        case ItemType::AND:  return "IC-COM-Logic-AND.svg";
        case ItemType::OR:   return "IC-COM-Logic-OR.svg";
        case ItemType::NOR:  return "IC-COM-Logic-NOR.svg";
        case ItemType::XOR:  return "IC-COM-Logic-XOR.svg";
        case ItemType::NOT:  return "IC-COM-Logic-Inverter.svg";
        default: return "";
    }
}

// 通用元件：通过元数据拿到类型串（= SVG文件名去掉.svg），如 "Resistor-IEC-Standard"；
// 找不到元数据时把名字本身当文件名兜底
std::string generic_svg_file(const std::string& name){
    MetaData* md = CanvasItem::load_config(ItemType::GENERIC, name);
    if(md){
        const std::string t = md->get_type();
        delete md;
        if(!t.empty()) return t;
    }
    return name;
}

//生成候选路径：
//  门类优先 src/Canva/assets/Symbols/<简单名>.svg，兜底 src/assets/electronic-symbols/SVG/<原始名>.svg；
//  通用元件直接按元数据类型串找 src/assets/electronic-symbols/SVG/<类型>.svg；
//  每种都有“当前目录相对”与“可执行文件所在目录的相对（build/..）”两种基准，
//  这样无论从仓库根目录运行还是从其他目录运行都能找到SVG。
std::vector<wxString> candidate_paths(ItemType type, const std::string& generic_file){
    std::vector<wxString> paths;
    const wxString rel_electro = wxString::FromUTF8("src/assets/electronic-symbols/SVG/");

    wxString root;
    {
        const wxString exe = wxStandardPaths::Get().GetExecutablePath();
        root = wxFileName(exe).GetPath() + wxFILE_SEP_PATH + wxT("..");  //build/.. => 仓库根
    }

    if(type == ItemType::GENERIC){
        if(generic_file.empty()) return paths;
        const wxString file = wxString::FromUTF8(generic_file.c_str()) + wxT(".svg");
        paths.push_back(rel_electro + file);
        paths.push_back(root + wxFILE_SEP_PATH + rel_electro + file);
        return paths;
    }

    const std::string simple = simple_name_of(type);
    const std::string source = source_name_of(type);
    if(simple.empty()) return paths;   //WIRE等无SVG类型

    const wxString rel_symbols = wxString::FromUTF8("src/Canva/assets/Symbols/");
    paths.push_back(rel_symbols + wxString::FromUTF8(simple.c_str()));
    paths.push_back(root + wxFILE_SEP_PATH + rel_symbols + wxString::FromUTF8(simple.c_str()));
    paths.push_back(rel_electro + wxString::FromUTF8(source.c_str()));
    paths.push_back(root + wxFILE_SEP_PATH + rel_electro + wxString::FromUTF8(source.c_str()));
    return paths;
}

//按候选路径提取SVG为wxBitmapBundle；全部缺失返回空bundle（不报错）。
//SVG默认尺寸取150×150（与assets里电子符号的viewBox一致），
//矢量重绘时按该默认尺寸×画布缩放得到任意清晰度
const wxBitmapBundle extract_svg(ItemType type, const std::string& generic_file){
    const wxSize default_size(150, 150);
    for(const wxString& path : candidate_paths(type, generic_file)){
        if(!wxFileExists(path)) continue;
        wxBitmapBundle bundle = wxBitmapBundle::FromSVGFile(path, default_size);
        if(bundle.IsOk()) return bundle;   //解析失败返回空bundle，继续尝试下一候选
    }
    return wxBitmapBundle();   //SVG未准备好：空bundle，不报错
}

//共享SVG缓存：每类元件只有一份wxBitmapBundle，所有实例/虚影/按钮图标都引用它。
//key：门类用类型串（"NAND"…）；通用元件用 "G:"+类型串（"G:Resistor-IEC-Standard"）。
//unordered_map的引用/指针在rehash后依然有效，且这里插入后不再修改，指针稳定
std::unordered_map<std::string, wxBitmapBundle>& bundle_cache(){
    static std::unordered_map<std::string, wxBitmapBundle> cache;
    return cache;
}

} // namespace

const wxBitmapBundle* ItemSVG_bundle(const std::string& name){
    const ItemType type = type_from_name(name);
    if(type == ItemType::WIRE) return nullptr;

    std::string cache_key;
    std::string generic_file;
    if(type == ItemType::GENERIC){
        generic_file = generic_svg_file(name);
        cache_key = std::string("G:") + generic_file;
    }else{
        cache_key = item_type_name(type);
    }

    auto& cache = bundle_cache();
    auto it = cache.find(cache_key);
    if(it != cache.end()) return &it->second;   //已提取过（含提取失败缓存的空bundle）

    wxBitmapBundle bundle = extract_svg(type, generic_file);
    auto result = cache.emplace(cache_key, std::move(bundle));
    return &result.first->second;
}

CanvasItem* ItemSVG(const std::string& name){
    const ItemType type = type_from_name(name);
    //调用读取配置的函数（从src/metadata/*.json读取；找不到返回nullptr不报错）
    MetaData* metadata = CanvasItem::load_config(type, name);
    //SVG引用来自共享缓存（不拥有、不释放）
    const wxBitmapBundle* bundle = ItemSVG_bundle(name);
    if(type == ItemType::WIRE){
        return new Linking(name, type, nullptr, metadata);
    }
    return new Symbol(name, type, bundle, metadata);
}

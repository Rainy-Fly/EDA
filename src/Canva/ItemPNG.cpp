#include"./ItemPNG.hpp"
#include<unordered_map>
#include<vector>
#include<wx/stdpaths.h>
#include<wx/filename.h>

namespace{

// 元件名字 -> 类型
ItemType type_from_name(const std::string& name){
    if(name == "与非门" || name == "NAND") return ItemType::NAND;
    if(name == "与门"   || name == "AND")  return ItemType::AND;
    if(name == "或门"   || name == "OR")   return ItemType::OR;
    if(name == "或非门" || name == "NOR")  return ItemType::NOR;
    if(name == "异或门" || name == "XOR")  return ItemType::XOR;
    if(name == "非门"   || name == "NOT")  return ItemType::NOT;
    if(name == "导线"   || name == "WIRE") return ItemType::WIRE;
    //未知名字：按导线处理，不报错
    return ItemType::WIRE;
}

//确保图片格式处理器已注册（wxWidgets默认不注册，LoadFile会静默失败；只需一次）
void ensure_image_handlers(){
    static const bool inited = [](){
        wxInitAllImageHandlers();
        return true;
    }();
    (void)inited;
}

// 类型 -> src/Canva/assets/Symbols/ 下的简单文件名
std::string simple_name_of(ItemType type){
    switch(type){
        case ItemType::NAND: return "NAND.png";
        case ItemType::AND:  return "AND.png";
        case ItemType::OR:   return "OR.png";
        case ItemType::NOR:  return "NOR.png";
        case ItemType::XOR:  return "XOR.png";
        case ItemType::NOT:  return "NOT.png";
        case ItemType::WIRE: return "";
    }
    return "";
}

// 类型 -> src/assets/electronic-symbols/PNG/ 下的原始文件名（兜底）
std::string source_name_of(ItemType type){
    switch(type){
        case ItemType::NAND: return "IC-COM-Logic-NAND.png";
        case ItemType::AND:  return "IC-COM-Logic-AND.png";
        case ItemType::OR:   return "IC-COM-Logic-OR.png";
        case ItemType::NOR:  return "IC-COM-Logic-NOR.png";
        case ItemType::XOR:  return "IC-COM-Logic-XOR.png";
        case ItemType::NOT:  return "IC-COM-Logic-Inverter.png";
        case ItemType::WIRE: return "";
    }
    return "";
}

//生成候选路径：
//  优先 src/Canva/assets/Symbols/<简单名>.png，兜底 src/assets/electronic-symbols/PNG/<原始名>.png；
//  每种都有“当前目录相对”与“可执行文件所在目录的相对（build/../src）”两种基准，
//  这样无论从仓库根目录运行还是从其他目录运行都能找到图片。
std::vector<wxString> candidate_paths(ItemType type){
    std::vector<wxString> paths;
    const std::string simple = simple_name_of(type);
    const std::string source = source_name_of(type);
    if(simple.empty()) return paths;   //WIRE等无图片类型

    const wxString rel_symbols = wxString::FromUTF8("src/Canva/assets/Symbols/");
    const wxString rel_electro = wxString::FromUTF8("src/assets/electronic-symbols/PNG/");

    wxString root;
    {
        const wxString exe = wxStandardPaths::Get().GetExecutablePath();
        root = wxFileName(exe).GetPath() + wxFILE_SEP_PATH + wxT("..");  //build/.. => 仓库根
    }

    paths.push_back(rel_symbols + wxString::FromUTF8(simple.c_str()));
    paths.push_back(root + wxFILE_SEP_PATH + rel_symbols + wxString::FromUTF8(simple.c_str()));
    paths.push_back(rel_electro + wxString::FromUTF8(source.c_str()));
    paths.push_back(root + wxFILE_SEP_PATH + rel_electro + wxString::FromUTF8(source.c_str()));
    return paths;
}

//按候选路径提取PNG图片；全部缺失返回false（不报错）
bool extract_png(ItemType type, wxImage& out){
    ensure_image_handlers();
    for(const wxString& path : candidate_paths(type)){
        if(wxFileExists(path) && out.LoadFile(path)) return true;
    }
    return false;
}

//共享图片缓存：每类元件只有一份源图片，所有实例/虚影/按钮图标都引用它（图片引用不变）
std::unordered_map<ItemType, wxBitmap*>& image_cache(){
    static std::unordered_map<ItemType, wxBitmap*> cache;
    return cache;
}

} // namespace

const wxBitmap* ItemPNG_image(const std::string& name){
    const ItemType type = type_from_name(name);
    if(type == ItemType::WIRE) return nullptr;

    auto& cache = image_cache();
    auto it = cache.find(type);
    if(it != cache.end()) return it->second;   //已提取过（含提取失败缓存的nullptr）

    wxBitmap* bmp = nullptr;
    wxImage img;
    if(extract_png(type, img)){
        bmp = new wxBitmap(img);
    }
    cache[type] = bmp;   //图片未准备好时缓存nullptr，避免反复尝试
    return bmp;
}

CanvasItem* ItemPNG(const std::string& name){
    const ItemType type = type_from_name(name);
    //调用读取配置的函数（具体怎么读暂未实现，返回nullptr不报错）
    MetaData* metadata = CanvasItem::load_config(type, name);
    //图片引用来自共享缓存（不拥有、不释放）
    const wxBitmap* image = ItemPNG_image(name);
    if(type == ItemType::WIRE){
        return new Linking(name, type, nullptr, metadata);
    }
    return new Symbol(name, type, image, metadata);
}

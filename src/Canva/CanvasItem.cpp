#include"./CanvasItem.hpp"
#include"../metadata/MetaDataJson.hpp"
#include<algorithm>
#include<cmath>

int CanvasItem::next_id = 1;

//显示尺寸上限（像素），避免极端缩放下把图片缩得过大
static const int MaxDisplaySize = 2048;

const char* item_type_name(ItemType type){
    switch(type){
        case ItemType::NAND: return "NAND";
        case ItemType::AND:  return "AND";
        case ItemType::OR:   return "OR";
        case ItemType::NOR:  return "NOR";
        case ItemType::XOR:  return "XOR";
        case ItemType::NOT:  return "NOT";
        case ItemType::WIRE: return "WIRE";
    }
    return "";
}

const char* item_type_label(ItemType type){
    switch(type){
        case ItemType::NAND: return "与非门";
        case ItemType::AND:  return "与门";
        case ItemType::OR:   return "或门";
        case ItemType::NOR:  return "或非门";
        case ItemType::XOR:  return "异或门";
        case ItemType::NOT:  return "非门";
        case ItemType::WIRE: return "导线";
    }
    return "";
}

//构造函数：持有图片引用（来自ItemPNG共享缓存，不拥有）与元数据
CanvasItem::CanvasItem(const std::string& name, ItemType type,
                       const wxBitmap* image, MetaData* metadata)
    : name(name),
      id(next_id++),
      coords_x(0),
      coords_y(0),
      type(type),
      image(image),
      ui_node(nullptr),
      metadata(metadata),
      ghost_mode(false){
    //图片引用构造后不再改变；放大缩小时由update_ui从这张源图重新缩放
    rendered_width = 0;
    rendered_height = 0;
}

CanvasItem::~CanvasItem(){
    //image来自ItemPNG的共享缓存，由缓存统一管理，这里不释放
    delete metadata;
    //ui_node是画布面板的子窗口，由wxWidgets窗口树统一释放，这里不删除
}

//读取配置：从 src/metadata/ 下的JSON文件（见MetaDataJson）读取元件元数据。
//优先按类型字符串（"NAND"/"WIRE"）查找，再按名字（中文/英文）兜底；
//找不到返回nullptr（不报错，与图片缺失不报错一致）
MetaData* CanvasItem::load_config(ItemType type, const std::string& name){
    if(MetaData* m = load_metadata(item_type_name(type))) return m;
    return load_metadata(name);
}

void CanvasItem::create_ui(wxWindow* parent){
    //基类：无UI节点（如Linking导线）
    ui_node = nullptr;
}

void CanvasItem::update_ui(float scale, const wxPoint& offset_coords){
    if(!ui_node || !image) return;
    //大小：源图片尺寸 × 画布缩放（限制在[1, MaxDisplaySize]内，极端缩放不至于过大）
    const int width  = std::clamp((int)std::lround(image->GetWidth()  * scale), 1, MaxDisplaySize);
    const int height = std::clamp((int)std::lround(image->GetHeight() * scale), 1, MaxDisplaySize);

    //性能关键：仅当目标尺寸变化（滚轮缩放）时才从源图重渲染位图；
    //鼠标移动（尺寸不变）时跳过重渲染，只更新位置，保证虚影/元件移动流畅
    if(width != rendered_width || height != rendered_height){
        rendered_bitmap = scale_image(scale, width, height);
        rendered_width  = width;
        rendered_height = height;
        if(wxStaticBitmap* bmp = dynamic_cast<wxStaticBitmap*>(ui_node)){
            bmp->SetBitmap(wxBitmapBundle::FromBitmap(rendered_bitmap));
        }
        ui_node->SetSize(width, height);   //节点尺寸与位图尺寸一致，保证命中检测与显示一致
    }

    //位置：coords（锚点）对应图片中心 => 窗口坐标pos（恒更新，代价极低）
    const float pos_x = (coords_x - offset_coords.x) * scale;
    const float pos_y = (coords_y - offset_coords.y) * scale;
    ui_node->Move((int)std::lround(pos_x - rendered_width  / 2.0f),
                  (int)std::lround(pos_y - rendered_height / 2.0f));
}

wxBitmap CanvasItem::scale_image(float scale, int width, int height) const{
    if(!image) return wxBitmap();
    wxImage img = image->ConvertToImage();
    if(img.GetWidth() != width || img.GetHeight() != height){
        //交互缩放用NORMAL质量：明显快于HIGH，视觉上可接受
        img = img.Scale(width, height, wxIMAGE_QUALITY_NORMAL);
    }
    //虚影：叠加半透明（保留原图透明区域，仅降低不透明像素的alpha）
    if(ghost_mode){
        if(!img.HasAlpha()) img.InitAlpha();
        unsigned char* alpha = img.GetAlpha();
        if(alpha){
            const size_t n = (size_t)img.GetWidth() * img.GetHeight();
            for(size_t i = 0; i < n; ++i){
                alpha[i] = (unsigned char)(alpha[i] * 0.5f);
            }
        }
    }
    return wxBitmap(img);
}

void Symbol::create_ui(wxWindow* parent){
    ui_node = new wxStaticBitmap(parent, wxID_ANY, wxBitmap());
}

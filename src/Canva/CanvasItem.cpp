#include"./CanvasItem.hpp"
#include"../metadata/MetaDataJson.hpp"
#include<algorithm>
#include<cmath>

int CanvasItem::next_id = 1;

//显示尺寸上限（像素），避免极端缩放下把元件画得过大
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
        case ItemType::GENERIC: return "GENERIC";
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
        case ItemType::GENERIC: return "通用元件";
    }
    return "";
}

//从SVG bundle按指定像素尺寸重绘，去掉scale factor（见头文件注释）
wxBitmap render_svg(const wxBitmapBundle& svg, int width, int height){
    if(!svg.IsOk()) return wxBitmap();
    wxBitmap bmp = svg.GetBitmap(wxSize(width, height));
    //wxImage没有scale factor，转wxImage再转回wxBitmap即归一化为物理像素显示
    return wxBitmap(bmp.ConvertToImage());
}

//构造函数：持有SVG引用（来自ItemSVG共享缓存，不拥有）与元数据。
//SVG是矢量图，放大缩小不会失真，由update_ui按目标尺寸直接重绘
CanvasItem::CanvasItem(const std::string& name, ItemType type,
                       const wxBitmapBundle* svg, MetaData* metadata)
    : name(name),
      id(next_id++),
      coords_x(0),
      coords_y(0),
      type(type),
      svg(svg),
      ui_node(nullptr),
      metadata(metadata),
      ghost_mode(false){
    //SVG引用构造后不再改变；缩放时由update_ui用wxBitmapBundle按目标尺寸重绘
    rendered_width = 0;
    rendered_height = 0;
}

CanvasItem::~CanvasItem(){
    //svg来自ItemSVG的共享缓存，由缓存统一管理，这里不释放
    delete metadata;
    //ui_node是画布面板的子窗口，由wxWidgets窗口树统一释放，这里不删除
}

//读取配置：从 src/metadata/ 下的JSON文件（见MetaDataJson）读取元件元数据。
//优先按类型字符串（"NAND"/"WIRE"）查找，再按名字（中文/英文）兜底；
//找不到返回nullptr（不报错，与SVG缺失不报错一致）
MetaData* CanvasItem::load_config(ItemType type, const std::string& name){
    if(MetaData* m = load_metadata(item_type_name(type))) return m;
    return load_metadata(name);
}

void CanvasItem::create_ui(wxWindow* parent){
    //基类：无UI节点（如Linking导线）
    ui_node = nullptr;
}

//虚影半透明：把渲染出的位图alpha整体减半（保留SVG自身的透明区域）
static wxBitmap apply_ghost_alpha(const wxBitmap& bmp){
    wxImage img = bmp.ConvertToImage();
    if(!img.HasAlpha()) img.InitAlpha();
    unsigned char* alpha = img.GetAlpha();
    if(alpha){
        const size_t n = (size_t)img.GetWidth() * img.GetHeight();
        for(size_t i = 0; i < n; ++i){
            alpha[i] = (unsigned char)(alpha[i] * 0.5f);
        }
    }
    return wxBitmap(img);
}

void CanvasItem::update_ui(float scale, const wxPoint& offset_coords){
    if(!ui_node || !svg || !svg->IsOk()) return;
    //大小：SVG按“默认尺寸(150) × 画布缩放”重绘（矢量，任意缩放清晰）。
    //wxBitmapBundle内部按尺寸缓存重绘结果，重复调用同尺寸时零成本
    const wxSize pref = svg->GetPreferredBitmapSizeAtScale(scale);
    const int width  = std::clamp(pref.x, 1, MaxDisplaySize);
    const int height = std::clamp(pref.y, 1, MaxDisplaySize);

    //性能关键：仅当目标尺寸变化（滚轮缩放）时才重绘位图；
    //鼠标移动（尺寸不变）时跳过重绘，只更新位置，保证虚影/元件移动流畅
    if(width != rendered_width || height != rendered_height){
        //render_svg统一去掉scale factor，否则GTK按逻辑尺寸绘制会抵消尺寸变化（图片不随缩放变化）
        wxBitmap bmp = render_svg(*svg, width, height);
        if(ghost_mode) bmp = apply_ghost_alpha(bmp);
        rendered_bitmap = bmp;
        rendered_width  = width;
        rendered_height = height;
        if(wxStaticBitmap* node = dynamic_cast<wxStaticBitmap*>(ui_node)){
            node->SetBitmap(wxBitmapBundle::FromBitmap(rendered_bitmap));
        }
        ui_node->SetSize(width, height);   //节点尺寸与位图尺寸一致，保证命中检测与显示一致
    }

    //位置：coords（锚点）对应图片中心 => 窗口坐标pos（恒更新，代价极低）
    const float pos_x = (coords_x - offset_coords.x) * scale;
    const float pos_y = (coords_y - offset_coords.y) * scale;
    ui_node->Move((int)std::lround(pos_x - rendered_width  / 2.0f),
                  (int)std::lround(pos_y - rendered_height / 2.0f));
}

void Symbol::create_ui(wxWindow* parent){
    ui_node = new wxStaticBitmap(parent, wxID_ANY, wxBitmap());
}

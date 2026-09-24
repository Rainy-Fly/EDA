#include"./CanvasItem.hpp"
#include<algorithm>
#include<cmath>

int CanvasItem::next_id = 1;

//显示尺寸上限（像素），避免极端缩放下把图片缩得过大
static const int MaxDisplaySize = 2048;

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
}

CanvasItem::~CanvasItem(){
    //image来自ItemPNG的共享缓存，由缓存统一管理，这里不释放
    delete metadata;
    //ui_node是画布面板的子窗口，由wxWidgets窗口树统一释放，这里不删除
}

MetaData* CanvasItem::load_config(ItemType type, const std::string& name){
    //TODO:具体怎么读暂未实现：根据type去assets下的元件子文件夹，根据name从json读取数据并返回MetaData*
    //暂返回nullptr，不报错
    return nullptr;
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
    wxBitmap scaled = scale_image(scale, width, height);
    if(wxStaticBitmap* bmp = dynamic_cast<wxStaticBitmap*>(ui_node)){
        bmp->SetBitmap(wxBitmapBundle::FromBitmap(scaled));
    }
    ui_node->SetSize(width, height);   //节点尺寸与位图尺寸一致，保证命中检测与显示一致
    //位置：coords（锚点）对应图片中心 => 窗口坐标pos
    const float pos_x = (coords_x - offset_coords.x) * scale;
    const float pos_y = (coords_y - offset_coords.y) * scale;
    ui_node->Move((int)std::lround(pos_x - width  / 2.0f),
                  (int)std::lround(pos_y - height / 2.0f));
}

wxBitmap CanvasItem::scale_image(float scale, int width, int height) const{
    if(!image) return wxBitmap();
    wxImage img = image->ConvertToImage();
    if(img.GetWidth() != width || img.GetHeight() != height){
        img = img.Scale(width, height, wxIMAGE_QUALITY_HIGH);
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

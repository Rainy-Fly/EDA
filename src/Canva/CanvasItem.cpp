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

void CanvasItem::reserve_id(int id){
    //下一枚待分配编号至少要比已恢复的最大编号大1，否则新元件会与文件里的元件撞编号
    if(id >= next_id) next_id = id + 1;
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

// 引脚布局（见头文件注释；偏移均为GridStep的倍数，保证引脚在网格上）
std::vector<Pin> item_pins(const CanvasItem& item){
    std::vector<Pin> pins;
    constexpr int PIN_X = 50;   //左右引脚距中心5格（逻辑坐标）

    // 逻辑门：左输入/右输出
    if(const LogicGateMetaData* g = dynamic_cast<const LogicGateMetaData*>(item.metadata)){
        const int n = std::max(1, g->get_inputs());
        for(int i = 0; i < n; ++i){
            const int y = (2 * i - (n - 1)) * 10;
            pins.push_back({"IN" + std::to_string(i + 1), wxPoint(-PIN_X, y)});
        }
        const int o = std::max(1, g->get_outputs());
        for(int i = 0; i < o; ++i){
            const int y = (o == 1) ? 0 : (2 * i - (o - 1)) * 10;
            pins.push_back({(o == 1) ? "OUT" : "OUT" + std::to_string(i + 1),
                            wxPoint(PIN_X, y)});
        }
        return pins;
    }

    const std::string type = item.metadata ? item.metadata->get_type() : item.name;
    const std::string cat  = item.metadata ? item.metadata->get_category() : "";

    // 晶体管：基极左，集电极/发射极右
    if(type.find("Transistor") != std::string::npos ||
       type.find("BJT") != std::string::npos ||
       type.find("MOSFET") != std::string::npos ||
       type.find("JFET") != std::string::npos ||
       type.find("Darlington") != std::string::npos ||
       type.find("Phototrans") != std::string::npos){
        pins.push_back({"B", wxPoint(-PIN_X, 0)});
        pins.push_back({"C", wxPoint(PIN_X, -20)});
        pins.push_back({"E", wxPoint(PIN_X, 20)});
        return pins;
    }
    // 运放/比较器：反相/同相输入 + 输出
    if(type.find("OpAmp") != std::string::npos ||
       type.find("Comparator") != std::string::npos){
        pins.push_back({"IN-", wxPoint(-PIN_X, -20)});
        pins.push_back({"IN+", wxPoint(-PIN_X, 20)});
        pins.push_back({"OUT", wxPoint(PIN_X, 0)});
        return pins;
    }
    // 地：上方引脚；电源：上下引脚
    if(cat == "Ground"){
        pins.push_back({"GND", wxPoint(0, -PIN_X)});
        return pins;
    }
    if(cat == "Source"){
        pins.push_back({"+", wxPoint(0, -PIN_X)});
        pins.push_back({"-", wxPoint(0, PIN_X)});
        return pins;
    }
    // 继电器：线圈(左) + 触点(右)
    if(cat == "Relay"){
        pins.push_back({"COIL", wxPoint(-PIN_X, -20)});
        pins.push_back({"COIL", wxPoint(-PIN_X, 20)});
        pins.push_back({"COM",  wxPoint(PIN_X, 0)});
        pins.push_back({"NC",   wxPoint(PIN_X, 20)});
        return pins;
    }
    // 默认：左右两引脚
    pins.push_back({"A", wxPoint(-PIN_X, 0)});
    pins.push_back({"B", wxPoint(PIN_X, 0)});
    return pins;
}

// 正交展开：相邻锚点间插入拐角点，保证只有水平/垂直段
std::vector<wxPoint> orthogonal_expand(const std::vector<wxPoint>& anchors){
    std::vector<wxPoint> out;
    if(anchors.empty()) return out;
    out.push_back(anchors[0]);
    for(size_t i = 1; i < anchors.size(); ++i){
        const wxPoint& a = anchors[i - 1];
        const wxPoint& b = anchors[i];
        // 先走较长轴：|dx|>=|dy| 先横后竖，否则先竖后横
        const wxPoint corner = (std::abs(b.x - a.x) >= std::abs(b.y - a.y))
                                   ? wxPoint(b.x, a.y)
                                   : wxPoint(a.x, b.y);
        if(corner != a) out.push_back(corner);
        if(b != corner) out.push_back(b);
    }
    return out;
}

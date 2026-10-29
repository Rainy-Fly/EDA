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
        case ItemType::XNOR: return "同或门";
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
//SVG是矢量图，放大缩小不会失真，由画布按scale调用update_bitmap重绘位图
CanvasItem::CanvasItem(const std::string& name, ItemType type,
                       const wxBitmapBundle* svg, MetaData* metadata)
    : name(name),
      id(next_id++),
      coords_x(0),
      coords_y(0),
      type(type),
      svg(svg),
      metadata(metadata),
      ghost_mode(false){
    //SVG引用构造后不再改变；缩放时由update_bitmap用wxBitmapBundle按目标尺寸重绘
    rendered_width = 0;
    rendered_height = 0;
    rendered_source_width = 0;
    rendered_source_height = 0;
    rendered_ghost = false;
    rendered_rotation = 0;
    rendered_mirror = "none";
}

CanvasItem::~CanvasItem(){
    //svg来自ItemSVG的共享缓存，由缓存统一管理，这里不释放
    delete metadata;
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

//按画布scale重绘位图（矢量→目标像素）。
//性能关键：尺寸与虚影状态都没变时直接复用缓存——鼠标移动（只有位置变）时这里是零成本
void CanvasItem::update_bitmap(float scale){
    if(!svg || !svg->IsOk()) return;
    //大小：SVG按“默认尺寸(150) × 画布缩放”重绘（矢量，任意缩放清晰）。
    //wxBitmapBundle内部按尺寸缓存重绘结果，重复调用同尺寸时零成本
    const wxSize pref = svg->GetPreferredBitmapSizeAtScale(scale);
    const int width  = std::clamp(pref.x, 1, MaxDisplaySize);
    const int height = std::clamp(pref.y, 1, MaxDisplaySize);
    if(width == rendered_source_width && height == rendered_source_height &&
       ghost_mode == rendered_ghost && rotation == rendered_rotation &&
       mirror == rendered_mirror){
        return;   //没变化：复用缓存
    }
    //render_svg统一去掉scale factor，否则GTK按逻辑尺寸绘制会抵消尺寸变化（图片不随缩放变化）
    wxBitmap bmp = render_svg(*svg, width, height);
    if(ghost_mode) bmp = apply_ghost_alpha(bmp);
    //实例变换：先镜像，再按顺时针旋转。wxImage的操作会产生新位图。
    wxImage img = bmp.ConvertToImage();
    if(mirror == "x") img = img.Mirror(true);      //水平翻转
    else if(mirror == "y") img = img.Mirror(false); //垂直翻转
    const int normalized = ((rotation % 360) + 360) % 360;
    if(normalized == 90) img = img.Rotate90(true);
    else if(normalized == 180) img = img.Rotate180();
    else if(normalized == 270) img = img.Rotate90(false);
    rendered_bitmap = wxBitmap(img);
    rendered_width  = rendered_bitmap.GetWidth();
    rendered_height = rendered_bitmap.GetHeight();
    rendered_source_width = width;
    rendered_source_height = height;
    rendered_ghost  = ghost_mode;
    rendered_rotation = rotation;
    rendered_mirror = mirror;
}

//元件在画布客户区里的矩形：图片中心对准 (coords - offset_coords) × scale。
//画布就是按这个矩形把位图画上去的，命中检测也用它 —— 显示与点击范围完全一致
wxRect CanvasItem::display_rect(float scale, const wxPoint& offset_coords) const{
    const float pos_x = (coords_x - offset_coords.x) * scale;
    const float pos_y = (coords_y - offset_coords.y) * scale;
    return wxRect((int)std::lround(pos_x - rendered_width  / 2.0f),
                  (int)std::lround(pos_y - rendered_height / 2.0f),
                  rendered_width, rendered_height);
}

bool CanvasItem::hit_test(const wxPoint& pos, float scale, const wxPoint& offset_coords) const{
    if(!rendered_bitmap.IsOk()) return false;   //还没渲染出来：不参与命中
    return display_rect(scale, offset_coords).Contains(pos);
}

// 对引脚偏移应用实例的旋转/镜像（先镜像，再顺时针旋转）。
wxPoint transform_pin_offset(wxPoint offset, int rotation, const std::string& mirror){
    int x = offset.x;
    int y = offset.y;
    if(mirror == "x") x = -x;        //水平翻转
    else if(mirror == "y") y = -y;   //垂直翻转
    switch(((rotation % 360) + 360) % 360){
        case 90:  return wxPoint(-y, x);
        case 180: return wxPoint(-x, -y);
        case 270: return wxPoint(y, -x);
        default:  return wxPoint(x, y);
    }
}

// 引脚布局：优先使用元数据中的显式 pins；没有显式定义时才回退到旧启发式。
// 返回的 offset 已经是实例变换后的结果。
std::vector<Pin> item_pins(const CanvasItem& item){
    std::vector<Pin> pins = item_pins_for(item.metadata, item.name);
    for(Pin& pin : pins)
        pin.offset = transform_pin_offset(pin.offset, item.rotation, item.mirror);
    return pins;
}

std::vector<Pin> item_pins_for(const MetaData* metadata, const std::string& name){
    std::vector<Pin> pins;
    constexpr int PIN_X = 50;   //左右引脚距中心5格（逻辑坐标）

    // 1) 显式引脚定义：这是 XML 2.0 / 元数据 2.0 的主路径。
    if(metadata && !metadata->pins.empty()){
        for(const MetaDataPin& def : metadata->pins){
            Pin pin;
            pin.id = def.id;
            pin.name = def.name.empty() ? def.id : def.name;
            pin.number = def.number.empty() ? metadata->pin_number(def.id) : def.number;
            pin.offset = wxPoint(def.x, def.y);
            pins.push_back(std::move(pin));
        }
        return pins;
    }

    // 2) 兼容旧元数据：按类型/类别生成启发式引脚。
    // 逻辑门：左输入/右输出
    if(const LogicGateMetaData* g = dynamic_cast<const LogicGateMetaData*>(metadata)){
        // 门的 SVG 在 80px 显示下正好占满 ±40；引脚放在符号边缘，才能贴在引脚线上。
        // 其他通用元件仍沿用 ±50 的旧启发式，避免影响电阻/晶体管等非门符号。
        constexpr int GATE_PIN_X = 40;
        const int n = std::max(1, g->get_inputs());
        for(int i = 0; i < n; ++i){
            const int y = (2 * i - (n - 1)) * 10;
            const std::string id = "IN" + std::to_string(i + 1);
            pins.push_back({id, id, std::to_string(i + 1), wxPoint(-GATE_PIN_X, y)});
        }
        const int o = std::max(1, g->get_outputs());
        for(int i = 0; i < o; ++i){
            const int y = (o == 1) ? 0 : (2 * i - (o - 1)) * 10;
            const std::string id = (o == 1) ? "OUT" : "OUT" + std::to_string(i + 1);
            pins.push_back({id, id, std::to_string(n + i + 1), wxPoint(GATE_PIN_X, y)});
        }
        return pins;
    }

    const std::string type = metadata ? metadata->get_type() : name;
    const std::string cat  = metadata ? metadata->get_category() : "";

    // 晶体管：基极左，集电极/发射极右
    if(type.find("Transistor") != std::string::npos ||
       type.find("BJT") != std::string::npos ||
       type.find("MOSFET") != std::string::npos ||
       type.find("JFET") != std::string::npos ||
       type.find("Darlington") != std::string::npos ||
       type.find("Phototrans") != std::string::npos){
        pins.push_back({"B", "B", "1", wxPoint(-PIN_X, 0)});
        pins.push_back({"C", "C", "2", wxPoint(PIN_X, -20)});
        pins.push_back({"E", "E", "3", wxPoint(PIN_X, 20)});
        return pins;
    }
    // 运放/比较器：反相/同相输入 + 输出
    if(type.find("OpAmp") != std::string::npos ||
       type.find("Comparator") != std::string::npos){
        pins.push_back({"IN-", "IN-", "1", wxPoint(-PIN_X, -20)});
        pins.push_back({"IN+", "IN+", "2", wxPoint(-PIN_X, 20)});
        pins.push_back({"OUT", "OUT", "3", wxPoint(PIN_X, 0)});
        return pins;
    }
    // 地：上方引脚；电源：上下引脚
    if(cat == "Ground"){
        pins.push_back({"GND", "GND", "1", wxPoint(0, -PIN_X)});
        return pins;
    }
    if(cat == "Source"){
        pins.push_back({"+", "+", "1", wxPoint(0, -PIN_X)});
        pins.push_back({"-", "-", "2", wxPoint(0, PIN_X)});
        return pins;
    }
    // 继电器：线圈(左) + 触点(右)
    if(cat == "Relay"){
        pins.push_back({"COIL1", "COIL1", "1", wxPoint(-PIN_X, -20)});
        pins.push_back({"COIL2", "COIL2", "2", wxPoint(-PIN_X, 20)});
        pins.push_back({"COM", "COM", "3", wxPoint(PIN_X, 0)});
        pins.push_back({"NC", "NC", "4", wxPoint(PIN_X, 20)});
        return pins;
    }
    // 默认：左右两引脚
    pins.push_back({"A", "A", "1", wxPoint(-PIN_X, 0)});
    pins.push_back({"B", "B", "2", wxPoint(PIN_X, 0)});
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

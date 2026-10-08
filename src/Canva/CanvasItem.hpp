#pragma once
#include<string>
#include<vector>
#include<wx/wx.h>
#include"./MetaData.hpp"

// 元件类型：每类元器件都是一个type。
// load_config 根据 type/name 从 src/metadata/*.json 中读取元数据并返回 MetaData*
enum class ItemType{
    NAND,   // 与非门
    AND,    // 与门
    OR,     // 或门
    NOR,    // 或非门
    XOR,    // 异或门
    NOT,    // 非门
    WIRE,   // 导线
    GENERIC,// 通用元件（电阻/电容/二极管/…来自电子符号库，类型串由元数据决定）
};

// ItemType -> 类型字符串（与 src/metadata/*.json 中元数据的 type 字段对应，如 "NAND"/"WIRE"）
const char* item_type_name(ItemType type);
// ItemType -> 中文显示名（如 "与非门"），供属性栏等界面显示
const char* item_type_label(ItemType type);

// 从SVG bundle按指定像素尺寸重绘，并去掉wxBitmapBundle::GetBitmap()为HiDPI设置的scale factor。
// 否则GTK按“逻辑尺寸”（物理宽÷scale factor）绘制，会把尺寸变化抵消：
// 例如GetBitmap(24)的scale factor=24/150，实际会按150绘制，图标又大又糊。
// 返回的位图物理尺寸=width×height、scale factor=1（bundle无效时返回空位图）
wxBitmap render_svg(const wxBitmapBundle& svg, int width, int height);

// 可放置元件的父类
class CanvasItem{
public:
    std::string name;   //元件名字，如 "与非门"
    int id;
    int coords_x;  //网格坐标x，用于吸附（锚点：图片中心对应的逻辑坐标）
    int coords_y;  // 网格坐标y,用于吸附
    ItemType type;
    const wxBitmapBundle* svg; //源SVG引用：由ItemSVG从assets提取并共享缓存，构造后不变；放大缩小时由wxBitmapBundle按目标尺寸矢量重绘更新UI节点（本类不拥有，不释放）
    wxWindow* ui_node;       //UI节点引用：画布上显示该元件的子窗口（如 wxStaticBitmap）
    MetaData* metadata;      //元数据：构造时经 load_config 从 src/metadata/*.json 读取（可能为空）
    bool ghost_mode;         //是否为虚影（放置时跟随鼠标的半透明元件）

    //构造函数：持有SVG引用（来自ItemSVG共享缓存，不拥有）与元数据
    CanvasItem(const std::string& name, ItemType type,
               const wxBitmapBundle* svg, MetaData* metadata = nullptr);
    virtual ~CanvasItem();

    //读取配置：从 src/metadata/ 下的JSON文件读取元件元数据（找不到返回nullptr不报错）
    static MetaData* load_config(ItemType type, const std::string& name);

    //恢复元件编号后调用（打开项目文件时用），保证后续新建元件的编号不会与文件中的重复
    static void reserve_id(int id);

    //创建UI节点（需要父窗口；Linking等无UI节点的类型保持空指针）
    virtual void create_ui(wxWindow* parent);

    //依据画布scale与offset_coords，把SVG矢量重绘到目标大小，并把UI节点放在coords（锚点）对应位置：
    //  大小 = SVG默认尺寸(150) × scale（限制在[1, MaxDisplaySize]内）
    //  位置 = (coords - offset_coords) × scale，图片中心对准该点
    //性能拆分：仅在目标尺寸变化（缩放）时才重绘位图；单纯移动只更新位置（Move），
    //避免鼠标移动时反复重绘导致的卡顿
    virtual void update_ui(float scale, const wxPoint& offset_coords);

private:
    static int next_id;
    int rendered_width;         //当前已渲染位图的尺寸（=0表示尚未渲染）
    int rendered_height;
    wxBitmap rendered_bitmap;   //当前显示的位图（缓存，避免重复重绘）
};

// 器件实例类	: 与非门、电阻、电容、芯片、模块、子图、连接器、电源/地符号、激励源
class Symbol:public CanvasItem{
public:
    using CanvasItem::CanvasItem;
    //UI节点为 wxStaticBitmap，显示缩放后的图片
    void create_ui(wxWindow* parent) override;
};

// 连线/网络类	导线、总线、节点、网络标签、电源/地端口、页连接符
class Linking : public CanvasItem{
public:
    using CanvasItem::CanvasItem;
    //连线端点/折点（逻辑坐标coords，已吸附网格）。
    //导线不创建UI节点（ui_node=nullptr），由画布onPaint直接绘制
    std::vector<wxPoint> points;
};

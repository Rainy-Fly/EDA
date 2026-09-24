#pragma once
#include<string>
#include<wx/wx.h>
#include"./MetaData.hpp"

// 元件类型：每类元器件都是一个type。
// load_config 根据 type 去 assets 下的元件文件夹下找到相应的子文件夹，根据 name 从 json 中读取数据并返回 MetaData*
enum class ItemType{
    NAND,   // 与非门
    AND,    // 与门
    OR,     // 或门
    NOR,    // 或非门
    XOR,    // 异或门
    NOT,    // 非门
    WIRE,   // 导线
};

// 可放置元件的父类
class CanvasItem{
public:
    std::string name;   //元件名字，如 "与非门"
    int id;
    int coords_x;  //网格坐标x，用于吸附（锚点：图片中心对应的逻辑坐标）
    int coords_y;  // 网格坐标y,用于吸附
    ItemType type;
    const wxBitmap* image;   //源图片引用：由ItemPNG从assets提取并共享缓存，构造后不变；放大缩小时从这张图片重新缩放更新UI节点（本类不拥有，不释放）
    wxWindow* ui_node;       //UI节点引用：画布上显示该元件的子窗口（如 wxStaticBitmap）
    MetaData* metadata;      //元数据：构造时经 load_config 读取（具体怎么读暂未实现，可能为空）
    bool ghost_mode;         //是否为虚影（放置时跟随鼠标的半透明元件）

    //构造函数：持有图片引用（来自ItemPNG共享缓存，不拥有）与元数据
    CanvasItem(const std::string& name, ItemType type,
               const wxBitmap* image, MetaData* metadata = nullptr);
    virtual ~CanvasItem();

    //读取配置（具体怎么读暂未实现，返回nullptr不报错）
    static MetaData* load_config(ItemType type, const std::string& name);

    //创建UI节点（需要父窗口；Linking等无UI节点的类型保持空指针）
    virtual void create_ui(wxWindow* parent);

    //依据画布scale与offset_coords，从image重新缩放得到目标大小，并把UI节点放在coords（锚点）对应位置：
    //  大小 = 源图片尺寸 × scale（限制在[1, MaxDisplaySize]内）
    //  位置 = (coords - offset_coords) × scale，图片中心对准该点
    //性能拆分：仅在目标尺寸变化（缩放）时才从源图重渲染位图；单纯移动只更新位置（Move），
    //避免鼠标移动时反复缩放图片导致的卡顿
    virtual void update_ui(float scale, const wxPoint& offset_coords);

protected:
    //从源图片缩放（或旋转）出目标尺寸的位图；虚影会叠加半透明
    virtual wxBitmap scale_image(float scale, int width, int height) const;

private:
    static int next_id;
    int rendered_width;         //当前已渲染位图的尺寸（=0表示尚未渲染）
    int rendered_height;
    wxBitmap rendered_bitmap;   //当前显示的位图（缓存，避免重复缩放）
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
    //暂不创建UI节点
};

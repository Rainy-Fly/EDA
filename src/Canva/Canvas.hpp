#pragma once
#include<wx/wx.h>
#include<unordered_set>
#include<functional>
#include<tuple>
#include"./CanvasItem.hpp"
#include"./ToolBar.hpp"
using canvasPos=std::tuple<float,float>;

// 画布：保持内部逻辑坐标系，像摄像机一样在逻辑网格coords上移动。
// 元件附着在逻辑坐标coords上（自身不变），在画布窗口上的视觉位置pos随offset_coords与scale变化。
class Canvas : public wxPanel{
public:
    Canvas(wxWindow* parent, wxWindowID id = wxID_ANY);
    ~Canvas() override;

    //选中工具栏工具：设置放置模式。门类元件产生跟随鼠标的虚影，点击画布后实例化；导线暂不处理
    void select_tool(const std::string& name, ItemType type);
    //结束放置模式：清除虚影与工具栏选中态
    void cancel_tool();

    //注册“当前元件变化”回调（供属性栏等联动）。
    //点击选中/放置元件时回调该元件；点击空白或开始新放置时回调nullptr
    void set_current_item_callback(std::function<void(CanvasItem*)> callback);

    friend class ToolBar;   //工具栏需要转发滚轮事件给画布

private:
    wxPoint offset_coords; //此窗口左上角在画布坐标系的网格位置
    float scale; //窗口缩放比例 单位pos/coords比
    std::unordered_set<CanvasItem*>* canvasItemCollection; //元件集合

    ToolBar* toolbar;      //放置在画布上的工具栏
    bool placing;          //是否处于放置模式
    CanvasItem* ghost;     //跟随鼠标的虚影
    CanvasItem* dragging_item;  //正在拖动的元件
    bool middle_dragging;  //按住中键拖动画布中
    wxPoint middle_last_pos;
    CanvasItem* current_item;                //当前选中的元件（nullptr=未选择）
    std::function<void(CanvasItem*)> current_item_callback;  //当前元件变化回调

    //更新当前元件并触发回调（指针未变化时不触发）
    void set_current_item(CanvasItem* item);

    //事件处理
    void on_mouse_scroll(wxMouseEvent& event);//鼠标中键滚动时触发（缩放）
    void on_middle_down(wxMouseEvent& event); //按住中键开始拖动画布
    void on_middle_up(wxMouseEvent& event);   //松开中键结束拖动
    void on_mouse_move(wxMouseEvent& event);  //鼠标移动：画布拖动/元件移动/虚影跟随
    void on_left_down(wxMouseEvent& event);   //左键按下：放置元件/命中检测
    void on_left_up(wxMouseEvent& event);     //左键松开：结束元件移动
    void on_item_left_down(CanvasItem* item, wxMouseEvent& event);
    void onPaint(wxPaintEvent& event);

    //统一取鼠标在画布客户区内的坐标：
    //事件可能来自画布本身，也可能来自元件节点/虚影等子窗口转发，
    //而event.GetPosition()是相对“事件窗口”的，因此统一用屏幕坐标换算，保证各来源一致
    wxPoint mouse_position(const wxMouseEvent& event) const;

    //坐标转换
    canvasPos coords_to_pos(wxPoint coords);//从画布逻辑坐标 => Canvas面板视觉坐标
    wxPoint pos_to_coords(canvasPos pos);   //Canvas面板视觉坐标 => 画布逻辑坐标
    wxPoint snap_coords(wxPoint coords);    //网格吸附

    //网格步长（逻辑坐标单位）：网格线绘制与虚影/元件吸附都按此步长对齐
    static constexpr int GridStep = 10;

    //摆放与显示
    void reput_items(); //重新摆放与显示元件
    void reput();       //重新绘制元件的位置
    void clear_ghost(); //销毁虚影
    void place_at_mouse(const wxMouseEvent& event); //在鼠标位置实例化元件
    void bind_ui_events(CanvasItem* item);         //显式转发元件节点上的鼠标事件（鼠标事件不自动传播）
    CanvasItem* item_at(const wxPoint& pos);       //命中检测
};

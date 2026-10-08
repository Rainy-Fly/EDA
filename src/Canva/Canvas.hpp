#pragma once
#include<wx/wx.h>
#include<unordered_set>
#include<functional>
#include<vector>
#include<tuple>
#include"./CanvasItem.hpp"
#include"./ToolBar.hpp"
using canvasPos=std::tuple<float,float>;

//点到线段的最短距离（窗口像素），用于导线命中检测
float point_segment_distance(const wxPoint& pos, const wxPoint& a, const wxPoint& b);

// 画布编辑工具（选择/删除/克隆，作用于已放置的元件）
// 注意：Windows 的 winnt.h 把 DELETE 定义成访问权限宏 (0x00010000L)，与本枚举的 DELETE 冲突，
// MSVC 下会报语法错误。这里只在声明枚举的几行内临时取消该宏，随后立刻恢复
#ifdef DELETE
    #define TINYEDA_HAD_DELETE_MACRO
    #pragma push_macro("DELETE")
    #undef DELETE
#endif

enum class EditTool{
    NONE,    //无编辑工具（默认：点击拖动）
    SELECT,  //选择：点击元件→跟随鼠标移动；右键取消跟随；Del删除
    ERASE,  //删除：点击元件即删除
    CLONE,   //克隆：点击元件→复制出跟随鼠标的虚影，再点击放置
};

#ifdef TINYEDA_HAD_DELETE_MACRO
    #pragma pop_macro("DELETE")
    #undef TINYEDA_HAD_DELETE_MACRO
#endif

// 画布：保持内部逻辑坐标系，像摄像机一样在逻辑网格coords上移动。
// 元件附着在逻辑坐标coords上（自身不变），在画布窗口上的视觉位置pos随offset_coords与scale变化。
class Canvas : public wxPanel{
public:
    Canvas(wxWindow* parent, wxWindowID id = wxID_ANY);
    ~Canvas() override;

    //选中工具栏工具：设置放置模式。门类元件产生跟随鼠标的虚影，点击画布后实例化；导线暂不处理
    void select_tool(const std::string& name, ItemType type);
    //从资源树(Explorer)等外部来源按名字进入放置模式（门/通用元件；导线类忽略）
    void select_tool_by_name(const std::string& name);
    //选中工具栏动作工具（选择/删除/克隆）
    void select_action(ToolAction action);
    //结束放置模式：清除虚影与工具栏选中态
    void cancel_tool();

    //注册“当前元件变化”回调（供属性栏等联动）。
    //点击选中/放置元件时回调该元件；点击空白或开始新放置时回调nullptr
    void set_current_item_callback(std::function<void(CanvasItem*)> callback);

    //----- 项目文件（DealProjectXML）支持 -----
    const std::unordered_set<CanvasItem*>& get_items() const; //全部元件（门/通用元件/导线）
    wxPoint get_offset() const { return offset_coords; }      //视口：画布左上角逻辑坐标
    float get_scale() const { return scale; }                 //缩放比例
    int get_grid_step() const { return GridStep; }            //网格步长
    void set_view(wxPoint offset, float s);                   //设置视口并重排
    void add_item(CanvasItem* item);                          //插入元件（创建UI/绑定事件/摆放）
    void clear_all_items();                                   //清空全部元件并复位绘制状态

    //注册内容变化回调：放置/删除/移动元件、画完导线、修改属性后触发
    //（供菜单栏标记“有未保存的修改”）
    void set_changed_callback(std::function<void()> callback);

    friend class ToolBar;   //工具栏需要转发滚轮事件给画布

private:
    wxPoint offset_coords; //此窗口左上角在画布坐标系的网格位置
    float scale; //窗口缩放比例 单位pos/coords比
    std::unordered_set<CanvasItem*>* canvasItemCollection; //元件集合

    ToolBar* toolbar;      //放置在画布上的工具栏
    bool placing;          //是否处于放置模式（门类元件虚影）
    CanvasItem* ghost;     //跟随鼠标的虚影
    CanvasItem* dragging_item;  //正在拖动的元件（门或导线）
    bool middle_dragging;  //按住中键拖动画布中
    wxPoint middle_last_pos;
    CanvasItem* current_item;                //当前选中的元件（nullptr=未选择）
    std::function<void(CanvasItem*)> current_item_callback;  //当前元件变化回调
    std::function<void()> changed_callback;  //内容变化回调（放置/删除/移动/改属性后触发）
    bool move_dirty;                         //本次移动是否真的改变了位置（只点击不算修改）

    //----- 导线(wire)状态 -----
    bool wire_placing;               //是否处于导线绘制模式
    std::vector<wxPoint> wire_points;   //正在绘制的导线折点（逻辑坐标coords，已吸附）
    wxPoint wire_mouse_pos;          //橡皮筋预览的鼠标逻辑坐标
    wxPoint wire_drag_origin;        //拖动导线时抓取点（吸附后coords）
    std::vector<wxPoint> wire_drag_saved;  //拖动开始时各折点的原始coords

    //----- 编辑工具(选择/删除/克隆)状态 -----
    EditTool edit_tool;                   //当前编辑工具
    CanvasItem* select_follow_item;       //选择模式下正在跟随鼠标的元件（nullptr=无）

    //更新当前元件并触发回调（指针未变化时不触发）
    void set_current_item(CanvasItem* item);
    //触发内容变化回调（供菜单栏标记“未保存”）
    void notify_changed();

    //删除元件（从集合移除、销毁UI节点并释放）
    void delete_item(CanvasItem* item);
    //选择模式：点击元件后开始跟随鼠标（吸附网格），右键取消，Del删除
    void start_select_follow(CanvasItem* item);
    void stop_select_follow();
    //克隆模式：点击元件后复制出一个跟随鼠标的虚影，再点击放置
    void start_clone(CanvasItem* item);
    CanvasItem* clone_item(const CanvasItem* item) const;  //复制元件（含元数据，新ID）

    //事件处理
    void on_mouse_scroll(wxMouseEvent& event);//鼠标中键滚动时触发（缩放）
    void on_middle_down(wxMouseEvent& event); //按住中键开始拖动画布
    void on_middle_up(wxMouseEvent& event);   //松开中键结束拖动
    void on_mouse_move(wxMouseEvent& event);  //鼠标移动：画布拖动/元件移动/虚影跟随/导线预览
    void on_left_down(wxMouseEvent& event);   //左键按下：放置元件/命中检测/导线加点
    void on_left_up(wxMouseEvent& event);     //左键松开：结束元件移动
    void on_item_left_down(CanvasItem* item, wxMouseEvent& event);
    void on_wire_left_down(const wxMouseEvent& event); //导线模式：添加折点
    void on_wire_right_down(wxMouseEvent& event);      //右键：完成或取消导线
    void on_right_down(wxMouseEvent& event);           //右键：先处理选择跟随取消，再转导线右键
    void onPaint(wxPaintEvent& event);

    //导线
    void finish_wire();      //提交正在绘制的导线到集合并结束绘制模式
    void cancel_wire();      //丢弃正在绘制的导线并结束绘制模式
    void draw_wires(wxPaintDC& dc);  //在画布上绘制所有导线+预览+选中高亮
    void draw_wire_path(wxPaintDC& dc, const std::vector<wxPoint>& points,
                        const wxColour& colour, int width);  //折线绘制
    bool wire_hit(Linking* wire, const wxPoint& pos); //点到线段命中检测（窗口px）

    //统一取鼠标在画布客户区内的坐标：
    //事件可能来自画布本身，也可能来自元件节点/虚影等子窗口转发，
    //而event.GetPosition()是相对“事件窗口”的，因此统一用屏幕坐标换算，保证各来源一致
    wxPoint mouse_position(const wxMouseEvent& event) const;

    //坐标转换
    canvasPos coords_to_pos(wxPoint coords);//从画布逻辑坐标 => Canvas面板视觉坐标
    wxPoint pos_to_coords(canvasPos pos);   //Canvas面板视觉坐标 => 画布逻辑坐标
    wxPoint snap_coords(wxPoint coords);    //网格吸附
    wxPoint coords_to_pos_px(wxPoint coords);//coords => 窗口整数像素坐标（导线绘制/命中用）

    //网格步长（逻辑坐标单位）：网格线绘制与虚影/元件吸附都按此步长对齐
    static constexpr int GridStep = 10;

    //摆放与显示
    void reput_items(); //重新摆放与显示元件
    void reput();       //重新绘制元件的位置
    void clear_ghost(); //销毁虚影
    void begin_placement(const std::string& name); //按名字创建跟随鼠标的虚影（进入放置模式）
    void place_at_mouse(const wxMouseEvent& event); //在鼠标位置实例化元件
    void bind_ui_events(CanvasItem* item);         //显式转发元件节点上的鼠标事件（鼠标事件不自动传播）
    CanvasItem* item_at(const wxPoint& pos);       //命中检测
};

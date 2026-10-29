#pragma once
#include<wx/wx.h>
#include<unordered_set>
#include<map>
#include<functional>
#include<vector>
#include<tuple>
#include"./CanvasItem.hpp"
#include"./ToolBar.hpp"
using canvasPos=std::tuple<float,float>;

// 项目级电气注解（XML 2.0）。画布当前不提供创建入口，但加载/保存必须原样保留，
// 否则含 junction/label/no-connect 的项目会在保存时丢失电气语义。
struct CanvasJunction{
    std::string id;
    wxPoint position;
};
struct CanvasLabel{
    std::string id;
    std::string text;
    std::string scope = "local";
    wxPoint position;
};
struct CanvasNoConnect{
    int component_id = 0;
    std::string pin_id;
};
struct CanvasLibrary{
    std::string id = "builtin";
    std::string version = "1.0";
};

//点到线段的最短距离（窗口像素），用于导线命中检测
float point_segment_distance(const wxPoint& pos, const wxPoint& a, const wxPoint& b);

//仿真会话（src/Simulation；画布只借指针读取值来着色，不拥有它）
namespace Simulation{ class SimulationSession; }

// 画布编辑工具（选择/删除/克隆，作用于已放置的元件）
// 枚举值用 ERASE 而不是 DELETE：Windows 的 winnt.h 把 DELETE 定义成访问权限宏
// (0x00010000L)，用 DELETE 作枚举名在 MSVC 下会报语法错误
enum class EditTool{
    NONE,    //无编辑工具（默认：单击元件即跟随鼠标、再单击放下；按住左键可直接拖动）
    SELECT,  //选择：点击元件→跟随鼠标移动；右键取消跟随；Del删除
    ACTIVATE,//激活：单击开关/输入端子切换 0/1（只有此模式会改元件状态）
    ERASE,   //删除：点击元件即删除
    CLONE,   //克隆：点击元件→复制出跟随鼠标的虚影，再点击放置
};

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

    //注册“光标位置变化”回调（供状态栏显示坐标）；valid=false 表示光标离开画布
    void set_cursor_callback(std::function<void(const wxPoint& coords, bool valid)> callback);
    //注册“当前模式变化”回调（供状态栏显示状态提示）；提示内容没变时不回调
    void set_status_callback(std::function<void(const wxString&)> callback);
    //注册“视口变化”回调（供状态栏显示缩放比例/网格步长）；注册时立即同步一次
    void set_view_callback(std::function<void(float scale, int grid_step)> callback);

    //工具栏（元件调色板）现在由主窗口创建在左侧栏里，画布只借它的指针做按钮高亮清理
    void set_toolbar(ToolBar* tb) { toolbar = tb; }

    //----- 项目文件（DealProjectXML）支持 -----
    const std::unordered_set<CanvasItem*>& get_items() const; //全部元件（门/通用元件/导线）
    wxPoint get_offset() const { return offset_coords; }      //视口：画布左上角逻辑坐标
    float get_scale() const { return scale; }                 //缩放比例
    int get_grid_step() const { return GridStep; }            //网格步长
    void set_view(wxPoint offset, float s);                   //设置视口并重排
    void add_item(CanvasItem* item);                          //插入元件（按当前scale渲染位图并加入集合）
    void clear_all_items();                                   //清空全部元件并复位绘制状态
    CanvasItem* find_item_by_id(int id);                      //按实例ID查找（供Agent工具等）
    void remove_item(CanvasItem* item);                       //移除并释放指定元件（供Agent工具等）

    //注册内容变化回调：放置/删除/移动元件、画完导线、修改属性后触发
    //（供菜单栏标记“有未保存的修改”）
    void set_changed_callback(std::function<void()> callback);

    //----- 仿真联动（类似 Logisim：元件状态与导线电平直接画在画布上）-----
    //注入仿真会话（不转移所有权；主窗口负责生命周期，析构前需设回 nullptr）
    void set_simulation(Simulation::SimulationSession* session);
    //内容/加载变化时通知主窗口重建仿真。
    //与 changed_callback 独立：FileActions 已经占用 changed_callback 标记“未保存”，
    //这里再挂一个不会把它覆盖掉
    void set_simulation_dirty_callback(std::function<void()> callback);
    //主窗口重建完仿真后调用：整块重绘 + 刷新状态栏提示
    void refresh_simulation_display();

    // 项目级电气注解（由 DealProjectXML 读写；当前无交互式创建入口）
    std::vector<CanvasJunction> junctions;
    std::vector<CanvasLabel> labels;
    std::vector<CanvasNoConnect> no_connects;
    std::vector<CanvasLibrary> libraries;

private:
    wxPoint offset_coords; //此窗口左上角在画布坐标系的网格位置
    float scale; //窗口缩放比例 单位pos/coords比
    std::unordered_set<CanvasItem*>* canvasItemCollection; //元件集合

    ToolBar* toolbar;      //元件调色板（由主窗口创建在左侧栏；画布只用它清按钮高亮）
    bool placing;          //是否处于放置模式（门类元件虚影）
    CanvasItem* ghost;     //跟随鼠标的虚影
    CanvasItem* dragging_item;  //正在拖动的元件（门或导线）
    bool middle_dragging;  //按住中键拖动画布中
    wxPoint middle_last_pos;
    CanvasItem* current_item;                //当前选中的元件（nullptr=未选择）
    std::function<void(CanvasItem*)> current_item_callback;  //当前元件变化回调
    std::function<void()> changed_callback;  //内容变化回调（放置/删除/移动/改属性后触发）
    std::function<void()> simulation_dirty_callback;  //内容变化回调（供主窗口重建仿真）
    Simulation::SimulationSession* simulation;        //仿真会话（不拥有）
    wxFont badge_font;                                //元件状态徽标字体
    std::function<void(const wxPoint&, bool)> cursor_callback;  //光标逻辑坐标（状态栏坐标字段）
    std::function<void(const wxString&)> status_callback;       //当前模式提示（状态栏状态字段）
    std::function<void(float, int)> view_callback;              //缩放/网格（状态栏缩放到字段）
    wxString last_status;                    //上一次发出去的提示（避免重复刷状态栏）
    bool move_dirty;                         //本次移动是否真的改变了位置（只点击不算修改）

    //----- 导线(wire)状态 -----
    bool wire_placing;               //是否处于导线绘制模式
    std::vector<wxPoint> wire_points;   //正在绘制的导线折点（逻辑坐标coords，已吸附）
    wxPoint wire_mouse_pos;          //橡皮筋预览的鼠标逻辑坐标
    wxPoint wire_drag_origin;        //拖动导线时抓取点（吸附后coords）
    std::vector<wxPoint> wire_drag_saved;  //拖动开始时各折点的原始coords
    bool wire_corner_locked;         //橡皮筋拐角方向是否已锁定（由首个鼠标移动方向决定）
    bool wire_corner_horizontal_first; //true=拐角先横后竖；false=先竖后横

    //----- 编辑工具(选择/删除/克隆)状态 -----
    EditTool edit_tool;                   //当前编辑工具
    CanvasItem* select_follow_item;       //选择模式下正在跟随鼠标的元件（nullptr=无）

    //更新当前元件并触发回调（指针未变化时不触发）
    void set_current_item(CanvasItem* item);
    //触发内容变化回调（供菜单栏标记“未保存”）
    void notify_changed();
    //把当前模式（放置/画线/编辑工具/跟随）转成一句提示发给 status_callback
    void update_status();

    //删除元件（从集合移除并释放）
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
    void commit_wire();      //提交当前折点为导线元件并清空绘制状态（不退出导线模式）
    void cancel_wire();      //丢弃正在绘制的导线并结束绘制模式
    void draw_wires(wxDC& dc);  //在画布上绘制所有导线+预览+选中高亮
    void draw_wire_path(wxDC& dc, const std::vector<wxPoint>& points,
                        const wxColour& colour, int width);  //折线绘制
    void draw_wire_path_gapped(wxDC& dc, const std::vector<wxPoint>& points,
                               const wxColour& colour, int width,
                               const std::map<size_t,
                                              std::vector<std::pair<int, int>>>& gaps);  //带缺口折线绘制
    void draw_items(wxDC& dc);  //绘制所有元件的位图（带alpha ⇒ 真透明；画在导线之上）
    void draw_state_overlay(wxDC& dc);   //第二遍：端子方框数值/状态徽标/灯亮光晕/不支持标记
    void draw_item_state(wxDC& dc, const CanvasItem* item, const wxRect& rect); //单个元件的状态标注
    void draw_ghost(wxDC& dc);  //绘制放置/克隆中的虚影（最上层）
    bool wire_hit(Linking* wire, const wxPoint& pos); //点到线段命中检测（窗口px）
    void draw_pins(wxDC& dc); //绘制所有元件的引脚（小圆点，网格对齐）
    //元件是不是"输入开关"：按元数据类别判断（新放置、仿真还没重建时也能正确识别）
    bool item_is_switch(const CanvasItem* item) const;
    //单击开关：State 0<->1（写进实例参数，随项目一起保存），并通知重建
    void toggle_switch(CanvasItem* item);
    //引脚颜色：仿真有值时按 1/0/X 着色，否则用默认深灰
    wxColour pin_colour(const CanvasItem* item, const std::string& pinId) const;
    bool snap_to_pin(const wxPoint& pos_px, wxPoint& out_coords); //鼠标附近吸附引脚

    //统一取鼠标在画布客户区内的坐标：
    //元件已不再是子窗口（位图由画布自绘），但事件仍可能来自工具栏等子窗口转发，
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
    CanvasItem* item_at(const wxPoint& pos);       //命中检测
};

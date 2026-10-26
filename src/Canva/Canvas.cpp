#include"./Canvas.hpp"
#include"./ItemSVG.hpp"
#include"../Simulation/SimulationSession.hpp"
#include<cmath>
#include<algorithm>
//wxAutoBufferedPaintDC：双缓冲。元件改成画布自绘后，拖动/跟随鼠标时整块画布每帧重画，
//不缓冲的话MSW上网格+元件会明显闪烁（与构造函数里的 SetBackgroundStyle(wxBG_STYLE_PAINT) 配套）
#include<wx/dcbuffer.h>

//每滚一格缩放的倍率：1.2 = 每格放大/缩小20%（过大的倍率会导致缩放幅度太大）
const float MoveRatio=1.2f;

//缩放范围限制（滚轮缩放与set_scale共用，避免两处写死的上下限漂移）
static const float MinScale=0.01f;
static const float MaxScale=100.0f;

//导线端点吸附引脚的距离阈值（窗口像素）
const float PinSnapPx = 14.0f;

//wxPoint -> canvasPos 辅助转换
static canvasPos to_canvas_pos(const wxPoint& p){
    return std::make_tuple((float)p.x, (float)p.y);
}

//点到线段的最短距离（窗口像素）
float point_segment_distance(const wxPoint& pos, const wxPoint& a, const wxPoint& b){
    const float abx = (float)(b.x - a.x);
    const float aby = (float)(b.y - a.y);
    const float apx = (float)(pos.x - a.x);
    const float apy = (float)(pos.y - a.y);
    const float len2 = abx*abx + aby*aby;
    float t = 0.0f;
    if(len2 > 0.0f) t = (apx*abx + apy*aby) / len2;   //投影比例
    t = std::clamp(t, 0.0f, 1.0f);                      //限制在线段内
    const float cx = (float)a.x + t * abx;
    const float cy = (float)a.y + t * aby;
    const float dx = (float)pos.x - cx;
    const float dy = (float)pos.y - cy;
    return std::sqrt(dx*dx + dy*dy);
}

Canvas::Canvas(wxWindow* parent, wxWindowID id)
    //wxFULL_REPAINT_ON_RESIZE：改尺寸时整块重画（配合下面的 wxEVT_SIZE 处理）。
    //画布是 wxBG_STYLE_PAINT（背景不由系统擦除），拖动分栏实时改尺寸时若只重画
    //新露出的那条，Windows 会先把还没画的区域显示成黑带 —— 拖动时就会看到黑边
    : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE),
      offset_coords(0, 0),
      scale(1.0f),
      canvasItemCollection(new std::unordered_set<CanvasItem*>()),
      toolbar(nullptr),
      placing(false),
      ghost(nullptr),
      dragging_item(nullptr),
      middle_dragging(false),
      middle_last_pos(0, 0),
      current_item(nullptr),
      current_item_callback(),
      changed_callback(),
      simulation_dirty_callback(),
      simulation(nullptr),
      cursor_callback(),
      status_callback(),
      view_callback(),
      last_status(),
      move_dirty(false),
      wire_placing(false),
      wire_points(),
      wire_mouse_pos(0, 0),
      wire_drag_origin(0, 0),
      wire_drag_saved(),
      wire_corner_locked(false),
      wire_corner_horizontal_first(false),
      edit_tool(EditTool::NONE),
      select_follow_item(nullptr){
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    //元件状态徽标（0/1/X）用小号粗体：小尺寸下也不至于压住符号
    badge_font = GetFont();
    badge_font.SetPointSize(7);
    badge_font.SetWeight(wxFONTWEIGHT_BOLD);
    Bind(wxEVT_PAINT,        &Canvas::onPaint, this);
    Bind(wxEVT_MOUSEWHEEL,   &Canvas::on_mouse_scroll, this);
    Bind(wxEVT_MIDDLE_DOWN,  &Canvas::on_middle_down, this);
    Bind(wxEVT_MIDDLE_UP,    &Canvas::on_middle_up, this);
    Bind(wxEVT_MOTION,       &Canvas::on_mouse_move, this);
    Bind(wxEVT_LEFT_DOWN,    &Canvas::on_left_down, this);
    Bind(wxEVT_LEFT_UP,      &Canvas::on_left_up, this);
    Bind(wxEVT_RIGHT_DOWN,   &Canvas::on_right_down, this);
    //双击：门类放置时吞掉避免一次双击触发两次放置；导线模式下双击=完成绘制
    Bind(wxEVT_LEFT_DCLICK,  [this](wxMouseEvent&){
        if(wire_placing) finish_wire();
    });
    //Esc：取消/完成正在绘制的导线、退出编辑工具；Del：删除跟随/选中的元件
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e){
        const int code = e.GetKeyCode();
        if(code == WXK_ESCAPE){
            bool handled = false;
            if(wire_placing){
                if(wire_points.size() >= 2) finish_wire();
                else{ cancel_wire(); if(toolbar) toolbar->clear_selection(); Refresh(); }
                handled = true;
            }else if(select_follow_item || edit_tool != EditTool::NONE){
                //退出编辑工具，或结束「单击跟随」（元件留在当前位置）
                edit_tool = EditTool::NONE;
                stop_select_follow();
                if(toolbar) toolbar->clear_selection();
                Refresh();
                handled = true;
            }
            if(handled){ update_status(); return; }
        }else if(code == WXK_DELETE){
            if(select_follow_item){ delete_item(select_follow_item); return; }
            if(current_item){ delete_item(current_item); return; }
        }
        e.Skip();
    });
    //拖动分栏（画布边框）时画布会被连续改尺寸：每次都请求整块重画，
    //避免 wxBG_STYLE_PAINT 下新露出的区域来不及画而闪出黑带
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e){ Refresh(false); e.Skip(); });
    //光标进出画布：进/出时同步状态栏坐标（离开时清空，避免一直显示上一次的位置）。
    //工具栏（元件调色板）已经搬到左侧栏，画布上不再有覆盖物，所以不用再为它做特殊处理
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& e){
        if(cursor_callback)
            cursor_callback(snap_coords(pos_to_coords(to_canvas_pos(mouse_position(e)))), true);
        e.Skip();
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e){
        if(cursor_callback) cursor_callback(wxPoint(), false);
        e.Skip();
    });
}

Canvas::~Canvas(){
    //析构期间不再回调上层：状态栏/属性栏可能已经在析构，回调过去就是悬空访问
    current_item_callback = nullptr;
    changed_callback = nullptr;
    simulation_dirty_callback = nullptr;
    simulation = nullptr;
    cursor_callback = nullptr;
    status_callback = nullptr;
    view_callback = nullptr;
    cancel_tool();
    for(CanvasItem* item : *canvasItemCollection) delete item;
    delete canvasItemCollection;
}

void Canvas::set_current_item_callback(std::function<void(CanvasItem*)> callback){
    current_item_callback = std::move(callback);
    //注册后立即同步一次当前状态，方便界面初始化
    if(current_item_callback) current_item_callback(current_item);
}

void Canvas::set_current_item(CanvasItem* item){
    if(current_item == item) return;   //指针未变化不触发，避免拖动时反复刷新
    current_item = item;
    if(current_item_callback) current_item_callback(item);
}

void Canvas::set_changed_callback(std::function<void()> callback){
    changed_callback = std::move(callback);
}

void Canvas::set_simulation(Simulation::SimulationSession* session){
    simulation = session;
    Refresh();
}

void Canvas::set_simulation_dirty_callback(std::function<void()> callback){
    simulation_dirty_callback = std::move(callback);
}

void Canvas::refresh_simulation_display(){
    Refresh();
    update_status();
}

void Canvas::set_cursor_callback(std::function<void(const wxPoint&, bool)> callback){
    cursor_callback = std::move(callback);
}

void Canvas::set_status_callback(std::function<void(const wxString&)> callback){
    status_callback = std::move(callback);
    update_status();   //注册后立即同步一次，状态栏一开始就有内容
}

void Canvas::set_view_callback(std::function<void(float, int)> callback){
    view_callback = std::move(callback);
    if(view_callback) view_callback(scale, GridStep);   //注册后立即同步一次
}

//把当前模式翻译成一句提示交给状态栏（字段0）。
//只在提示内容真的变化时才回调：这样文件菜单写的"已保存 xxx"之类不会被鼠标一动就刷掉。
//注意：提示语里刻意不出现 "Esc"/"Del" 这类半角字母——状态栏是原生控件，GDI 对
//「中文+半角字符」混排会按字符集分段换字体，中文段会渲染得发虚发花（实测过）
void Canvas::update_status(){
    if(!status_callback) return;
    wxString text;
    if(select_follow_item){
        text = wxString::Format(wxString::FromUTF8("跟随移动 %s：移动鼠标调整位置，单击放下，按退出键取消"),
                                wxString::FromUTF8(select_follow_item->name.c_str()));
    }else if(placing && ghost){
        text = wxString::Format(wxString::FromUTF8("放置 %s：在画布上单击放下，按退出键或右键取消"),
                                wxString::FromUTF8(ghost->name.c_str()));
    }else if(wire_placing){
        text = wxString::FromUTF8("绘制导线：左键连续加点，点回起点/双击完成，右键取消本段，按退出键退出");
    }else if(edit_tool == EditTool::SELECT){
        text = wxString::FromUTF8("选择工具：单击元件即可让它跟随鼠标");
    }else if(edit_tool == EditTool::ACTIVATE){
        text = wxString::FromUTF8("激活工具：单击开关/输入端子切换 ０/１");
    }else if(edit_tool == EditTool::ERASE){
        text = wxString::FromUTF8("删除工具：单击元件即删除");
    }else if(edit_tool == EditTool::CLONE){
        text = wxString::FromUTF8("克隆工具：单击元件复制一个跟随鼠标的副本，再单击放下");
    }else{
        text = wxString::FromUTF8("就绪：从左侧挑元件或工具；中键拖动画布、滚轮缩放");
        //仿真开启时提示"怎么改输入"。数字用全角：状态栏是原生控件，
        //中文里混半角数字/字母会被 GDI 的字体链接渲染得发虚（实测过）
        if(simulation && simulation->enabled() && simulation->hasModel())
            text += wxString::FromUTF8("；仿真运行中：用激活工具单击开关切换 ０/１");
    }
    if(text == last_status) return;
    last_status = text;
    status_callback(text);
}

void Canvas::notify_changed(){
    if(changed_callback) changed_callback();
    //仿真重建走独立回调：不会覆盖 FileActions 挂在 changed_callback 上的"未保存"标记
    if(simulation_dirty_callback) simulation_dirty_callback();
}

//中键滚轮缩放：鼠标在屏幕的pos不变，画布的逻辑坐标（offset_coords）改变
void Canvas::on_mouse_scroll(wxMouseEvent& event){
    const float rotation = event.GetWheelRotation() / (float)event.GetWheelDelta();
    wxPoint mouse_pos = mouse_position(event);

    const float old_scale = scale;
    float new_scale = old_scale * std::pow(MoveRatio, rotation);
    if(new_scale < MinScale) new_scale = MinScale;
    if(new_scale > MaxScale) new_scale = MaxScale;
    if(new_scale == old_scale) return;

    const float mx = (float)mouse_pos.x;
    const float my = (float)mouse_pos.y;
    //保持鼠标下方的逻辑坐标不变
    const float new_offset_x = offset_coords.x + mx / old_scale - mx / new_scale;
    const float new_offset_y = offset_coords.y + my / old_scale - my / new_scale;
    offset_coords.x = (int)std::lround(new_offset_x);
    offset_coords.y = (int)std::lround(new_offset_y);
    scale = new_scale;
    if(view_callback) view_callback(scale, GridStep);   //状态栏：缩放比例

    //性能关键：这里不做同步重渲染。Refresh()把重绘交给事件循环，
    //连续多次滚轮会合并成少数几次onPaint，由onPaint统一对元件重渲染图片，
    //避免每次滚轮都同步对全部元件做图片缩放导致的卡顿
    Refresh();
}

void Canvas::on_middle_down(wxMouseEvent& event){
    middle_dragging = true;
    middle_last_pos = mouse_position(event);
    if(!HasCapture()) CaptureMouse();
}

void Canvas::on_middle_up(wxMouseEvent& event){
    middle_dragging = false;
    if(HasCapture()) ReleaseMouse();
}

//鼠标移动：按住中键拖动画布；拖动元件；放置模式下虚影跟随
void Canvas::on_mouse_move(wxMouseEvent& event){
    wxPoint pos = mouse_position(event);

    //状态栏坐标：光标的画布逻辑坐标（已吸附网格）——与放置元件/画导线/写进XML用的是同一个值
    if(cursor_callback) cursor_callback(snap_coords(pos_to_coords(to_canvas_pos(pos))), true);

    //按住中键滑动：画布在逻辑坐标系中移动，元件的窗口pos跟着变
    if(middle_dragging){
        wxPoint delta = pos - middle_last_pos;
        offset_coords.x -= (int)std::lround(delta.x / scale);
        offset_coords.y -= (int)std::lround(delta.y / scale);
        middle_last_pos = pos;
        reput();
        Refresh();
        return;
    }

    //选择模式：被点击的元件跟随鼠标（吸附网格），无需按住按钮；右键取消，Del删除
    if(select_follow_item){
        wxPoint coords = snap_coords(pos_to_coords(to_canvas_pos(pos)));
        if(Linking* wire = dynamic_cast<Linking*>(select_follow_item)){
            //导线：整体平移折点（当前位移加到各点，保持折线形状）
            if(!wire->points.empty()){
                const wxPoint delta = coords - wire->points.front();
                if(delta.x != 0 || delta.y != 0){
                    for(wxPoint& p : wire->points) p = p + delta;
                    move_dirty = true;   //位置真的变了：关窗口/新建时要提示保存
                }
            }
            Refresh();
        }else{
            if(coords.x != select_follow_item->coords_x ||
               coords.y != select_follow_item->coords_y){
                select_follow_item->coords_x = coords.x;
                select_follow_item->coords_y = coords.y;
                move_dirty = true;
                Refresh();   //元件由画布自绘（不再有子窗口），位置变了要重画
            }
        }
        return;
    }

    //拖动元件/导线：coords改变，窗口pos跟着变
    if(dragging_item){
        wxPoint coords = snap_coords(pos_to_coords(to_canvas_pos(pos)));
        //导线：整体平移所有折点（抓取点与当前点的位移加到各点）
        if(Linking* wire = dynamic_cast<Linking*>(dragging_item)){
            if(!wire_drag_saved.empty()){
                const wxPoint delta = coords - wire_drag_origin;
                if(delta.x != 0 || delta.y != 0){
                    for(size_t i = 0; i < wire->points.size() && i < wire_drag_saved.size(); ++i){
                        wire->points[i] = wire_drag_saved[i] + delta;
                    }
                    move_dirty = true;
                }
            }
            Refresh();   //导线由onPaint绘制
            return;
        }
        if(coords.x != dragging_item->coords_x || coords.y != dragging_item->coords_y){
            dragging_item->coords_x = coords.x;
            dragging_item->coords_y = coords.y;
            move_dirty = true;
            Refresh();   //元件由画布自绘（不再有子窗口），位置变了要重画
        }
        return;
    }

    //导线绘制模式：橡皮筋预览跟随鼠标（网格吸附 + 靠近引脚时吸附引脚）
    if(wire_placing){
        wxPoint coords = snap_coords(pos_to_coords(to_canvas_pos(pos)));
        wxPoint pin;
        if(snap_to_pin(pos, pin)) coords = pin;   //引脚优先于网格吸附
        if(coords != wire_mouse_pos){
            wire_mouse_pos = coords;
            //锁定橡皮筋拐角方向：以「离开上一个折点的首个移动方向」为准。
            //之后鼠标方向再怎么变，拐角都保持先竖后横/先横后竖，不会中途突变
            if(!wire_points.empty() && !wire_corner_locked && coords != wire_points.back()){
                const wxPoint last = wire_points.back();
                wire_corner_horizontal_first =
                    std::abs(coords.x - last.x) >= std::abs(coords.y - last.y);
                wire_corner_locked = true;
            }
            Refresh();   //重绘预览线
        }
        return;
    }

    //放置模式：虚影跟随鼠标并吸附到网格
    if(placing && ghost){
        wxPoint coords = snap_coords(pos_to_coords(to_canvas_pos(pos)));
        if(Linking* wire_ghost = dynamic_cast<Linking*>(ghost)){
            //克隆的导线虚影：无UI节点，整体平移折点跟随鼠标（由onPaint预览绘制）
            if(!wire_ghost->points.empty()){
                const wxPoint delta = coords - wire_ghost->points.front();
                for(wxPoint& p : wire_ghost->points) p = p + delta;
            }
            Refresh();
            return;
        }
        ghost->coords_x = coords.x;
        ghost->coords_y = coords.y;
        ghost->update_bitmap(scale);
        Refresh();   //虚影由画布自绘（不再有子窗口），位置变了要重画
    }
}

void Canvas::on_left_down(wxMouseEvent& event){
    const wxPoint pos = mouse_position(event);
    //注意：工具栏（元件调色板）已搬到左侧栏，画布上不再需要"点在工具栏区域就不放置"的判断

    if(select_follow_item){
        stop_select_follow();   //内部会：若move_dirty则notify_changed，清空指针
        Refresh();
        return;
    }


    if(wire_placing){
        on_wire_left_down(event);   //导线模式：加点/完成
        return;
    }
    if(placing){
        place_at_mouse(event);
        return;
    }
    //编辑工具：命中元件则执行对应动作，空白处只清除选中（工具保持激活）
    if(edit_tool == EditTool::ERASE){
        if(CanvasItem* hit = item_at(pos)) delete_item(hit);
        else set_current_item(nullptr);
        return;
    }
    //激活工具：只有此模式会改变元件状态（开关/输入端子切换 0/1），其它元件仅选中
    if(edit_tool == EditTool::ACTIVATE){
        if(CanvasItem* hit = item_at(pos)){
            if(item_is_switch(hit)) toggle_switch(hit);
            else set_current_item(hit);
        }else{
            set_current_item(nullptr);
        }
        return;
    }
    if(edit_tool == EditTool::SELECT){
        if(CanvasItem* hit = item_at(pos)) start_select_follow(hit);
        else{ stop_select_follow(); set_current_item(nullptr); }
        return;
    }
    if(edit_tool == EditTool::CLONE){
        if(CanvasItem* hit = item_at(pos)) start_clone(hit);
        return;
    }
    //兜底命中检测（正常情况下元件节点上的点击已由节点事件转发处理）
    if(CanvasItem* hit = item_at(pos)){
        on_item_left_down(hit, event);
    }else{
        set_current_item(nullptr);   //点击空白：取消当前选中
    }
}

void Canvas::on_left_up(wxMouseEvent& event){
    //单击（按下后没有真的拖动）＝让元件进入「跟随鼠标」状态：之后移动鼠标它跟着走，
    //再点一下左键放下（与「选择」工具一致）。按住拖动仍然是直接拖动，老习惯不变。
    //只对门/通用元件生效：导线单击仍然只是选中，免得整根线被拽到光标下。
    //（开关/输入端子的 0/1 切换已移至「激活」工具，普通模式下点击只做选择/移动）
    if(dragging_item && !move_dirty && !dynamic_cast<Linking*>(dragging_item)){
        CanvasItem* item = dragging_item;
        dragging_item = nullptr;
        if(HasCapture()) ReleaseMouse();
        start_select_follow(item);   //内部会set_current_item / 吸附到当前鼠标位置 / Refresh
        return;
    }
    //元件/导线被真的拖动过才通知“内容已修改”（决定关闭/新建前要不要提示保存）
    if(dragging_item && move_dirty) notify_changed();
    move_dirty = false;
    dragging_item = nullptr;
    if(HasCapture()) ReleaseMouse();
}

void Canvas::on_item_left_down(CanvasItem* item, wxMouseEvent& event){
     if(select_follow_item){
        stop_select_follow();
        Refresh();
        return;
    }
    
    
    if(wire_placing){  //导线模式下点击任何位置（含元件）都是给导线加点
        on_wire_left_down(event);
        return;
    }
    if(placing){  //放置模式下点击（虚影/元件）都视为放置
        place_at_mouse(event);
        return;
    }
    //编辑工具（点击的是元件节点）
    if(edit_tool == EditTool::ERASE){
        delete_item(item);
        return;
    }
    if(edit_tool == EditTool::SELECT){
        start_select_follow(item);
        return;
    }
    if(edit_tool == EditTool::CLONE){
        start_clone(item);
        return;
    }
    //点击元件后进入移动，并把它设为当前选中（通知属性栏）
    set_current_item(item);
    dragging_item = item;
    move_dirty = false;   //开始新的拖动：先认为没有移动
    if(Linking* wire = dynamic_cast<Linking*>(item)){
        //导线整体拖动：记录抓取点与各折点原始坐标
        const wxPoint pos = mouse_position(event);
        wire_drag_origin = snap_coords(pos_to_coords(to_canvas_pos(pos)));
        wire_drag_saved  = wire->points;
    }
    if(!HasCapture()) CaptureMouse();
}

//导线绘制模式左键：把鼠标位置（吸附网格）加入折点序列；
//连续重复点（双击产生两次DOWN）忽略；点击回起点（≥2点）视为完成
void Canvas::on_wire_left_down(const wxMouseEvent& event){
    const wxPoint pos = mouse_position(event);
    wxPoint coords = snap_coords(pos_to_coords(to_canvas_pos(pos)));
    wxPoint pin;
    if(snap_to_pin(pos, pin)) coords = pin;   //靠近引脚时吸附到引脚（引脚在网格上）
    if(!wire_points.empty() && coords == wire_points.back()){
        return;   //双击会先来两次DOWN（位置相同），忽略重复点，由DCLICK完成
    }
    if(wire_points.size() >= 2 && coords == wire_points.front()){
        finish_wire();   //点回起点：完成
        return;
    }
    wire_points.push_back(coords);
    wire_mouse_pos = coords;
    wire_corner_locked = false;   //新折点后拐角方向重新按首个移动方向决定
    Refresh();
}

//右键：已有足够折点时提交当前导线，否则丢弃本段；
//两种情况都保持导线模式——接下来左键即可从当前位置开始连新的导线，无需再点工具栏
void Canvas::on_wire_right_down(wxMouseEvent& event){
    (void)event;
    if(!wire_placing) return;
    if(wire_points.size() >= 2){
        commit_wire();
    }else{
        wire_points.clear();
        wire_corner_locked = false;
        Refresh();
        update_status();
    }
}

//画布右键总入口：选择跟随中→取消跟随（元件留在原位）；否则转导线右键
void Canvas::on_right_down(wxMouseEvent& event){
    if(select_follow_item){
        return;
    }
    on_wire_right_down(event);
}

//统一取鼠标在画布客户区内的坐标（事件可能来自子窗口，GetPosition相对事件窗口，不能用）
wxPoint Canvas::mouse_position(const wxMouseEvent& event) const{
    (void)event;
    return ScreenToClient(wxGetMousePosition());
}

canvasPos Canvas::coords_to_pos(wxPoint coords){
    wxPoint relative = coords - offset_coords;
    return std::make_tuple((float)relative.x * scale, (float)relative.y * scale);
}

wxPoint Canvas::pos_to_coords(canvasPos pos){
    auto [pos_x, pos_y] = pos;
    return wxPoint((int)std::lround(pos_x / scale),
                   (int)std::lround(pos_y / scale)) + offset_coords;
}

wxPoint Canvas::snap_coords(wxPoint coords){
    //网格吸附：以GridStep（逻辑坐标）为单位取整，与画布绘制的网格线对齐。
    //  吸附后坐标 = round(coords / GridStep) × GridStep
    return wxPoint((int)std::lround((float)coords.x / GridStep) * GridStep,
                   (int)std::lround((float)coords.y / GridStep) * GridStep);
}

wxPoint Canvas::coords_to_pos_px(wxPoint coords){
    auto [x, y] = coords_to_pos(coords);
    return wxPoint((int)std::lround(x), (int)std::lround(y));
}

void Canvas::select_tool(const std::string& name, ItemType type){
    clear_ghost();
    placing = false;
    cancel_wire();          //结束上一个工具的进行中状态（不碰工具栏高亮，on_tool_click已设置）
    edit_tool = EditTool::NONE;   //退出编辑工具（选择跟随随之取消）
    stop_select_follow();
    set_current_item(nullptr);
    if(type == ItemType::WIRE){
        //导线：进入绘制模式——左键加点（点回起点/双击/右键/Esc完成），移动时橡皮筋预览
        wire_placing = true;
        wire_points.clear();
        Refresh();
        update_status();
        return;
    }
    begin_placement(name);
}

//从资源树(Explorer)按名字进入放置模式：与工具栏门类放置共用begin_placement，
//唯一区别是不关联工具栏按钮（清除其高亮）；导线类元件无SVG虚影，忽略
void Canvas::select_tool_by_name(const std::string& name){
    clear_ghost();
    placing = false;
    cancel_wire();
    edit_tool = EditTool::NONE;
    stop_select_follow();
    set_current_item(nullptr);
    if(toolbar) toolbar->clear_selection();
    if(!ItemSVG_bundle(name)){ Refresh(); update_status(); return; }   //导线/总线等无SVG：忽略（清掉旧虚影的残影）
    begin_placement(name);
}

//按名字创建跟随鼠标的虚影（门/通用元件）。调用方需先清理上一个工具状态
void Canvas::begin_placement(const std::string& name){
    placing = true;
    ghost = ItemSVG(name);
    ghost->ghost_mode = true;   //半透明虚影
    //虚影没有自己的窗口：位图由onPaint画在最上层，鼠标事件全部直接在画布上处理，
    //不会再出现「事件被虚影窗口吞掉 → 虚影冻结、光标逃出后才跳变追赶」的老问题
    //立刻把虚影放到当前鼠标位置（吸附网格）并渲染，无需先移动鼠标
    const wxPoint pos = ScreenToClient(wxGetMousePosition());
    const wxPoint coords = snap_coords(pos_to_coords(to_canvas_pos(pos)));
    ghost->coords_x = coords.x;
    ghost->coords_y = coords.y;
    ghost->update_bitmap(scale);
    Refresh();       //让虚影立刻显示出来
    update_status(); //状态栏：提示"在画布上单击放下"
}

//进入编辑工具模式（选择/删除/克隆），同时结束放置/导线等其它进行中状态
void Canvas::select_action(ToolAction action){
    clear_ghost();
    placing = false;
    cancel_wire();
    stop_select_follow();
    dragging_item = nullptr;
    set_current_item(nullptr);
    switch(action){
        case ToolAction::SELECT:   edit_tool = EditTool::SELECT;   break;
        case ToolAction::ACTIVATE: edit_tool = EditTool::ACTIVATE; break;
        case ToolAction::ERASE:    edit_tool = EditTool::ERASE;    break;
        case ToolAction::CLONE:    edit_tool = EditTool::CLONE;    break;
    }
    Refresh();   //虚影是画布自绘的：clear_ghost后必须显式重画，否则残影留在屏幕上
    update_status();
}

void Canvas::cancel_tool(){
    placing = false;
    cancel_wire();
    stop_select_follow();
    clear_ghost();
    if(toolbar) toolbar->clear_selection();
    update_status();
}

//提交正在绘制的导线：折点≥2时创建Linking加入集合，结束绘制模式
void Canvas::finish_wire(){
    if(!wire_placing) return;
    commit_wire();
    wire_placing = false;
    wire_corner_locked = false;
    if(toolbar) toolbar->clear_selection();
    Refresh();
    update_status();
}

//提交当前折点为导线元件并清空绘制状态（不退出导线模式，由调用方决定是否退出）
void Canvas::commit_wire(){
    if(wire_points.size() >= 2){
        //去掉连续重复点后按正交展开（相邻锚点间插入拐角，保证只有水平/垂直段）
        std::vector<wxPoint> clean;
        for(const wxPoint& p : wire_points){
            if(clean.empty() || clean.back() != p) clean.push_back(p);
        }
        if(clean.size() >= 2){
            clean = orthogonal_expand(clean);
            CanvasItem* item = ItemSVG("导线");   //Linking，无UI节点，由onPaint绘制
            if(Linking* wire = dynamic_cast<Linking*>(item)){
                wire->points = clean;
            }
            canvasItemCollection->insert(item);
            set_current_item(item);   //刚画的导线成为当前选中（属性栏显示导线元数据）
            notify_changed();         //项目内容变了（供菜单栏标记“未保存”）
        }
        //点数不足2的导线不创建，直接丢弃
    }
    wire_points.clear();
    wire_corner_locked = false;
    Refresh();
    update_status();
}

//丢弃正在绘制的导线并退出绘制模式（不碰工具栏高亮）
void Canvas::cancel_wire(){
    wire_placing = false;
    wire_points.clear();
    wire_corner_locked = false;
    update_status();
}

//----- 项目文件（DealProjectXML）支持 -----
const std::unordered_set<CanvasItem*>& Canvas::get_items() const{
    return *canvasItemCollection;
}

void Canvas::set_view(wxPoint offset, float s){
    //从项目文件恢复视口：缩放必须落在合法范围内（手工改过的文件可能写着极端值）
    if(s < MinScale) s = MinScale;
    if(s > MaxScale) s = MaxScale;
    offset_coords = offset;
    scale = s;
    reput();
    if(view_callback) view_callback(scale, GridStep);   //状态栏：打开项目后同步缩放比例
    Refresh();
}

//插入元件：创建UI节点、绑定事件转发、按当前视口摆放（导线无UI节点，仅入集合由onPaint绘制）
void Canvas::add_item(CanvasItem* item){
    if(!item) return;
    item->update_bitmap(scale);   //按当前scale渲染好位图（画布自绘）
    canvasItemCollection->insert(item);
    Refresh();
    //加载项目时走这条路（批量 add_item）：主窗口会去抖后统一重建一次仿真
    if(simulation_dirty_callback) simulation_dirty_callback();
}

//清空全部元件：结束绘制/编辑状态、销毁所有UI节点与实例
void Canvas::clear_all_items(){
    cancel_tool();   //结束放置/导线/编辑工具并清工具栏高亮
    //cancel_tool不会清拖动状态：清空后这些指针会悬空，必须在这里断开（否则后续鼠标移动会访问已释放内存）
    dragging_item = nullptr;
    wire_drag_saved.clear();
    for(CanvasItem* item : *canvasItemCollection){
        delete item;   //没有子窗口要销毁，位图随对象一起释放
    }
    canvasItemCollection->clear();
    junctions.clear();
    labels.clear();
    no_connects.clear();
    libraries.clear();
    set_current_item(nullptr);
    Refresh();
    if(simulation_dirty_callback) simulation_dirty_callback();   //新建项目：仿真也要清空
}

//按实例ID查找元件（供 Agent 工具按id操作）；找不到返回nullptr
CanvasItem* Canvas::find_item_by_id(int id){
    for(CanvasItem* item : *canvasItemCollection){
        if(item->id == id) return item;
    }
    return nullptr;
}

//移除并释放指定元件（供 Agent 工具删除用）
void Canvas::remove_item(CanvasItem* item){
    delete_item(item);
}

//删除元件：从集合移除并释放（门/导线均可）
void Canvas::delete_item(CanvasItem* item){
    if(!item) return;
    canvasItemCollection->erase(item);
    if(current_item == item) set_current_item(nullptr);   //通知属性栏清空
    if(select_follow_item == item) select_follow_item = nullptr;
    if(dragging_item == item) dragging_item = nullptr;
    if(ghost == item) ghost = nullptr;   //模式互斥，正常不会发生
    delete item;
    notify_changed();   //删除也是内容修改
    Refresh();
}

//选择模式：点击元件后开始跟随鼠标（“重新回到跟随移动状态”），吸附网格；
//右键取消跟随，Del删除
void Canvas::start_select_follow(CanvasItem* item){
    if(!item) return;
    set_current_item(item);
    select_follow_item = item;
    move_dirty = false;   //本次跟随是否真的移动过，由下面的位移判断
    //立即吸附到当前鼠标位置
    const wxPoint pos = ScreenToClient(wxGetMousePosition());
    const wxPoint coords = snap_coords(pos_to_coords(to_canvas_pos(pos)));
    if(Linking* wire = dynamic_cast<Linking*>(item)){
        if(!wire->points.empty()){
            const wxPoint delta = coords - wire->points.front();
            if(delta.x != 0 || delta.y != 0){
                for(wxPoint& p : wire->points) p = p + delta;
                move_dirty = true;   //点击瞬间就把导线挪走了，这也是一次修改
            }
        }
        Refresh();
    }else{
        if(coords.x != item->coords_x || coords.y != item->coords_y) move_dirty = true;
        item->coords_x = coords.x;
        item->coords_y = coords.y;
        Refresh();   //元件由画布自绘，位置变了要重画
    }
    update_status();   //状态栏：提示"单击放下 / Esc 取消"
}

//取消跟随：元件停留在当前位置（若跟随期间真的移动过，记一次内容修改）
void Canvas::stop_select_follow(){
    if(select_follow_item && move_dirty) notify_changed();
    move_dirty = false;
    select_follow_item = nullptr;
    update_status();
}

//克隆模式：点击元件后复制一个跟随鼠标的虚影，再点击放置（复用放置/虚影机制）
void Canvas::start_clone(CanvasItem* item){
    if(!item) return;
    CanvasItem* copy = clone_item(item);
    if(!copy) return;
    clear_ghost();
    placing = true;
    ghost = copy;
    ghost->ghost_mode = true;
    //同样没有子窗口：导线虚影没有位图（由draw_wires预览），门类虚影由draw_ghost画
    //立即放到当前鼠标位置（吸附网格）
    const wxPoint pos = ScreenToClient(wxGetMousePosition());
    const wxPoint coords = snap_coords(pos_to_coords(to_canvas_pos(pos)));
    if(Linking* wire_ghost = dynamic_cast<Linking*>(ghost)){
        if(!wire_ghost->points.empty()){
            const wxPoint delta = coords - wire_ghost->points.front();
            for(wxPoint& p : wire_ghost->points) p = p + delta;
        }
    }else{
        ghost->coords_x = coords.x;
        ghost->coords_y = coords.y;
        ghost->update_bitmap(scale);
    }
    Refresh();
    update_status();   //状态栏：提示"单击放下"
}

//复制元件：同类型新实例（新ID、独立元数据副本、共享SVG缓存），
//复制元数据（含属性栏修改过的值）与导线折点
CanvasItem* Canvas::clone_item(const CanvasItem* item) const{
    if(!item) return nullptr;
    CanvasItem* copy = ItemSVG(item->name);
    if(!copy) return nullptr;
    // 实例级状态（变换/属性/焊盘覆盖）必须随克隆一起复制，否则放下后属性会丢失。
    copy->rotation = item->rotation;
    copy->mirror = item->mirror;
    copy->reference = item->reference;
    copy->value = item->value;
    copy->footprint = item->footprint;
    copy->pin_number_overrides = item->pin_number_overrides;
    if(const Linking* src = dynamic_cast<const Linking*>(item)){
        if(Linking* dst = dynamic_cast<Linking*>(copy)){
            dst->points = src->points;
            dst->start_endpoint = src->start_endpoint;
            dst->end_endpoint = src->end_endpoint;
        }
    }
    if(item->metadata && copy->metadata){
        copy->metadata->params = item->metadata->params;
        if(LogicGateMetaData* dst = dynamic_cast<LogicGateMetaData*>(copy->metadata)){
            if(const LogicGateMetaData* src = dynamic_cast<const LogicGateMetaData*>(item->metadata)){
                dst->set_inputs(src->get_inputs());
                dst->set_outputs(src->get_outputs());
                dst->set_data_bits(src->get_data_bits());
            }
        }
        copy->metadata->set_description(item->metadata->get_description());
    }
    return copy;
}

void Canvas::clear_ghost(){
    //没有子窗口要销毁：位图随对象一起释放，画布下一次重画就看不到它了
    delete ghost;
    ghost = nullptr;
}

//在鼠标位置实例化元件（用虚影的名字），然后结束放置模式。
//直接采用虚影当前的coords（已吸附到网格），保证放置位置与虚影显示位置完全一致
void Canvas::place_at_mouse(const wxMouseEvent& event){
    if(!placing || !ghost) return;
    //克隆的导线虚影：无UI节点，只复制折点
    if(Linking* wire_ghost = dynamic_cast<Linking*>(ghost)){
        CanvasItem* item = ItemSVG(ghost->name);
        if(Linking* dst = dynamic_cast<Linking*>(item)){
            dst->points = wire_ghost->points;
            dst->start_endpoint = wire_ghost->start_endpoint;
            dst->end_endpoint = wire_ghost->end_endpoint;
        }
        item->pin_number_overrides = ghost->pin_number_overrides;
        canvasItemCollection->insert(item);
        cancel_tool();
        edit_tool = EditTool::NONE;   //克隆放置完成：退出克隆模式（一次性）
        set_current_item(item);
        notify_changed();             //放置了新内容
        Refresh();
        update_status();
        return;
    }
    CanvasItem* item = ItemSVG(ghost->name);
    item->coords_x = ghost->coords_x;
    item->coords_y = ghost->coords_y;
    // 克隆放置时保留虚影身上的实例状态（普通放置的虚影为默认值，不受影响）。
    item->rotation = ghost->rotation;
    item->mirror = ghost->mirror;
    item->reference = ghost->reference;
    item->value = ghost->value;
    item->footprint = ghost->footprint;
    item->pin_number_overrides = ghost->pin_number_overrides;
    if(item->metadata && ghost->metadata){
        item->metadata->params = ghost->metadata->params;
        item->metadata->set_description(ghost->metadata->get_description());
    }
    item->update_bitmap(scale);   //按当前scale渲染好位图（画布自绘）
    canvasItemCollection->insert(item);
    cancel_tool();  //结束放置：之后再点击该元件可移动它
    edit_tool = EditTool::NONE;   //克隆放置完成：退出克隆模式（对门类放置无影响，本来就为NONE）
    set_current_item(item);  //刚放置的元件成为当前选中（通知属性栏）
    notify_changed();        //项目内容变了（供菜单栏标记“未保存”）
    Refresh();
    update_status();         //结束放置：状态栏回到"就绪"
}

CanvasItem* Canvas::item_at(const wxPoint& pos){
    //先命中导线（线段距离检测，导线没有UI节点）
    for(CanvasItem* item : *canvasItemCollection){
        if(Linking* wire = dynamic_cast<Linking*>(item)){
            if(wire_hit(wire, pos)) return wire;
        }
    }
    //再命中普通元件（显示矩形，和画布画出来的位置完全一致）
    for(CanvasItem* item : *canvasItemCollection){
        if(dynamic_cast<Linking*>(item)) continue;   //导线没有位图，上面已按线段判定
        if(item->hit_test(pos, scale, offset_coords)) return item;
    }
    return nullptr;
}

//导线命中检测：鼠标到任一段（或端点）的距离 < 阈值（窗口像素）
bool Canvas::wire_hit(Linking* wire, const wxPoint& pos){
    const auto& pts = wire->points;
    if(pts.size() < 2) return false;
    const float threshold = 7.0f;

    //端点命中
    for(const wxPoint& p : pts){
        const wxPoint pp = coords_to_pos_px(p);
        const int dx = pp.x - pos.x;
        const int dy = pp.y - pos.y;
        if((float)(dx*dx + dy*dy) <= threshold * threshold) return true;
    }
    //线段命中：点到线段距离
    for(size_t i = 1; i < pts.size(); ++i){
        if(point_segment_distance(pos, coords_to_pos_px(pts[i-1]),
                                      coords_to_pos_px(pts[i])) <= threshold) return true;
    }
    return false;
}

//绘制所有元件的引脚：小圆点，位置=锚点coords+引脚偏移（均为网格的倍数⇒在网格上）
//仿真开启时按引脚所在网络的电平着色（1=亮绿、0=深绿、X=蓝），直观看出每个脚的电平
void Canvas::draw_pins(wxDC& dc){
    dc.SetPen(*wxTRANSPARENT_PEN);
    for(CanvasItem* item : *canvasItemCollection){
        if(dynamic_cast<Linking*>(item)) continue;   //导线无引脚
        for(const Pin& pin : item_pins(*item)){
            dc.SetBrush(wxBrush(pin_colour(item, pin.id)));
            dc.DrawCircle(coords_to_pos_px(wxPoint(item->coords_x + pin.offset.x,
                                                    item->coords_y + pin.offset.y)), 3);
        }
    }
}

//引脚颜色：仿真有值时按 1/0/X 着色，否则用默认深灰
wxColour Canvas::pin_colour(const CanvasItem* item, const std::string& pinId) const{
    if(!simulation || !item || !simulation->pinHasValue(item->id, pinId))
        return wxColour(25, 25, 25);
    switch(simulation->pinValue(item->id, pinId)){
        case Simulation::LogicValue::One:  return wxColour(0, 190, 80);
        case Simulation::LogicValue::Zero: return wxColour(20, 110, 60);
        default: break;
    }
    return wxColour(80, 110, 210);
}

//元件是不是"输入开关"：按元数据类别判断。
//用元数据而不是仿真会话，是为了新放置的元件（仿真还没重建）也能正确识别并响应点击
bool Canvas::item_is_switch(const CanvasItem* item) const{
    if(!item || !item->metadata) return false;
    if(item->metadata->get_type() == "IN_TERMINAL") return true;   //输入端子也是可点击的 0/1 源
    if(item->metadata->get_category() == "Switch") return true;
    return item->metadata->get_type().rfind("Switch-COM-", 0) == 0;
}

//单击开关：State 在 0/1 之间切换，存进实例参数（随项目一起保存），然后通知重建仿真
void Canvas::toggle_switch(CanvasItem* item){
    if(!item || !item->metadata) return;
    std::string& state = item->metadata->params["State"];
    state = (state == "1") ? "0" : "1";
    notify_changed();
    Refresh();
    update_status();
}

//鼠标位置(pos_px, 窗口像素)靠近某元件引脚（<=PinSnapPx）时吸附：
//返回true并把out_coords设为引脚所在网格坐标
bool Canvas::snap_to_pin(const wxPoint& pos_px, wxPoint& out_coords){
    bool found = false;
    float best = PinSnapPx;
    for(CanvasItem* item : *canvasItemCollection){
        if(dynamic_cast<Linking*>(item)) continue;
        for(const Pin& pin : item_pins(*item)){
            const wxPoint pp = coords_to_pos_px(wxPoint(item->coords_x + pin.offset.x,
                                                        item->coords_y + pin.offset.y));
            const int dx = pp.x - pos_px.x;
            const int dy = pp.y - pos_px.y;
            const float d = std::sqrt((float)(dx * dx + dy * dy));
            if(d <= best){
                best = d;
                out_coords = wxPoint(item->coords_x + pin.offset.x,
                                     item->coords_y + pin.offset.y);
                found = true;
            }
        }
    }
    return found;
}

void Canvas::reput_items(){
    //按当前scale刷新各元件的位图缓存（尺寸没变时不重绘，等于零成本）；
    //位置不在这里算——画布绘制时用 display_rect(scale, offset_coords) 现算
    for(CanvasItem* item : *canvasItemCollection){
        item->update_bitmap(scale);
    }
    if(ghost) ghost->update_bitmap(scale);
}

void Canvas::reput(){
    reput_items();
}

//线段内部交叉（不含端点）求交点；返回 true 并在 out 里给出交点逻辑坐标。
//平行/共线（线段重合）不视为交叉——重合是布线问题，由生成端避免。
static bool segment_intersection(const wxPoint& a, const wxPoint& b,
                                 const wxPoint& c, const wxPoint& d, wxPoint* out){
    const long abx = b.x - a.x, aby = b.y - a.y;
    const long cdx = d.x - c.x, cdy = d.y - c.y;
    const long denom = abx * cdy - aby * cdx;
    if(denom == 0) return false;
    const long acx = c.x - a.x, acy = c.y - a.y;
    const double t = (double)(acx * cdy - acy * cdx) / denom;
    const double u = (double)(acx * aby - acy * abx) / denom;
    if(t <= 0.0 || t >= 1.0 || u <= 0.0 || u >= 1.0) return false;
    if(out){
        out->x = a.x + (int)std::lround(t * abx);
        out->y = a.y + (int)std::lround(t * aby);
    }
    return true;
}

//在画布上绘制所有导线：已放置的导线（选中高亮 + 端点实心圆 + 交叉跳线弧）+ 正在绘制的预览
void Canvas::draw_wires(wxDC& dc){
    //收集已放置导线
    std::vector<Linking*> wires;
    for(CanvasItem* item : *canvasItemCollection){
        Linking* wire = dynamic_cast<Linking*>(item);
        if(wire && wire->points.size() >= 2) wires.push_back(wire);
    }

    //交叉点：两条导线在线段内部相交（非端点连接）时，其中一条画跳线弧绕过另一条。
    //用 id 较大的导线画弧（决定结果稳定）；弧凸向该线段方向（水平→上半圆，竖直→右半圆）
    struct Crossing{ wxPoint pos; bool horizontal; };
    std::map<Linking*, std::vector<Crossing>> arcs_by_wire;
    for(size_t i = 0; i < wires.size(); ++i){
        const auto& pi = wires[i]->points;
        for(size_t j = i + 1; j < wires.size(); ++j){
            const auto& pj = wires[j]->points;
            for(size_t si = 1; si < pi.size(); ++si){
                for(size_t sj = 1; sj < pj.size(); ++sj){
                    wxPoint p;
                    if(!segment_intersection(pi[si - 1], pi[si],
                                             pj[sj - 1], pj[sj], &p)){
                        continue;
                    }
                    Linking* arc_wire = (wires[i]->id > wires[j]->id) ? wires[i] : wires[j];
                    const bool horizontal = (arc_wire == wires[i])
                        ? (std::abs(pi[si].x - pi[si - 1].x) >=
                           std::abs(pi[si].y - pi[si - 1].y))
                        : (std::abs(pj[sj].x - pj[sj - 1].x) >=
                           std::abs(pj[sj].y - pj[sj - 1].y));
                    arcs_by_wire[arc_wire].push_back({p, horizontal});
                }
            }
        }
    }

    //已放置的导线
    for(Linking* wire : wires){
        const bool selected = (wire == current_item);
        if(selected){
            draw_wire_path(dc, wire->points, wxColour(0, 102, 204), 4);   //选中：粗蓝底
        }

        //仿真电平着色（类似 Logisim）：1=亮绿粗线(+淡绿外发光)、0=中灰粗线、X=蓝、
        //未参与仿真=深灰细线。0 特意用中灰而不是深绿：深绿和亮绿肉眼几乎分不出来，
        //容易被当成"已经变 1 了"（其实还是 0）
        wxColour colour(40, 40, 40);
        int width = 2;
        bool has_value = false;
        Simulation::LogicValue value = Simulation::LogicValue::Unknown;
        if(simulation && simulation->wireHasValue(wire->id)){
            has_value = true;
            value = simulation->wireValue(wire->id);
        }
        if(has_value){
            width = 3;
            switch(value){
                case Simulation::LogicValue::One:
                    //高电平：先铺一层淡绿外发光，再画主线（选中时不铺，保持蓝色高亮）
                    if(!selected) draw_wire_path(dc, wire->points, wxColour(175, 240, 205), 7);
                    colour = wxColour(0, 200, 90);
                    break;
                case Simulation::LogicValue::Zero:
                    colour = wxColour(120, 120, 120);
                    break;
                default:
                    colour = wxColour(80, 110, 210);
                    break;
            }
        }
        draw_wire_path(dc, wire->points,
                       selected ? wxColour(0, 120, 255) : colour,
                       selected ? 2 : width);

        //起点/终点实心圆：端点就是连接点，用线色画实心圆点
        dc.SetBrush(wxBrush(colour));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawCircle(coords_to_pos_px(wire->points.front()), 3);
        dc.DrawCircle(coords_to_pos_px(wire->points.back()), 3);

        //跳线弧：交叉处画小半圆绕过另一条线（与线同色）
        const auto arcs = arcs_by_wire.find(wire);
        if(arcs != arcs_by_wire.end()){
            dc.SetPen(wxPen(selected ? wxColour(0, 120, 255) : colour,
                            std::max(width, 1)));
            //关键：DrawArc 在 GTK 会"填充"扇形区域（含两条半径线），
            //画弧前必须把画刷设回透明，否则跳线弧会变成实心半圆
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            for(const Crossing& c : arcs->second){
                const wxPoint px = coords_to_pos_px(c.pos);
                //半径按线宽计算：至少 6，且随线宽线性增长，
                //保证半圆环的内外径差（r - 线宽）明显，不会被线宽填满成实心
                const int r = std::max(6, width * 2);
                if(c.horizontal){
                    //水平线在交叉处画上半圆（逆时针绕过上方）
                    dc.DrawArc(px.x - r, px.y, px.x + r, px.y, px.x, px.y);
                }else{
                    //竖直线在交叉处画右半圆（逆时针绕过右侧）
                    dc.DrawArc(px.x, px.y - r, px.x, px.y + r, px.x, px.y);
                }
            }
        }
    }

    //正在绘制的导线：已确定的折线（正交展开）+ 橡皮筋预览（正交拐角）+ 端点圆点
    if(wire_placing && !wire_points.empty()){
        if(wire_points.size() >= 2){
            draw_wire_path(dc, orthogonal_expand(wire_points), wxColour(0, 120, 255), 2);
        }
        //橡皮筋：最后一个折点 -> 拐角 -> 鼠标（只走横竖段，无斜线）。
        //拐角方向已由首个鼠标移动方向锁定：鼠标方向改变后拐角不会中途突变
        const wxPoint last  = wire_points.back();
        const wxPoint mouse = wire_mouse_pos;
        const bool horizontal_first = wire_corner_locked
                                          ? wire_corner_horizontal_first
                                          : (std::abs(mouse.x - last.x) >=
                                             std::abs(mouse.y - last.y));
        const wxPoint corner = horizontal_first
                                   ? wxPoint(mouse.x, last.y)   //先横后竖
                                   : wxPoint(last.x, mouse.y);  //先竖后横
        dc.SetPen(wxPen(wxColour(0, 150, 255), 2, wxPENSTYLE_SHORT_DASH));
        if(corner != last) dc.DrawLine(coords_to_pos_px(last), coords_to_pos_px(corner));
        if(mouse != corner) dc.DrawLine(coords_to_pos_px(corner), coords_to_pos_px(mouse));
        //端点圆点
        dc.SetBrush(wxBrush(wxColour(0, 150, 255)));
        dc.SetPen(*wxTRANSPARENT_PEN);
        for(const wxPoint& p : wire_points){
            dc.DrawCircle(coords_to_pos_px(p), 3);
        }
    }

    //克隆的导线虚影预览（无UI节点，跟随鼠标时由onPaint绘制）
    if(placing && ghost){
        if(Linking* wire_ghost = dynamic_cast<Linking*>(ghost)){
            if(wire_ghost->points.size() >= 2){
                draw_wire_path(dc, wire_ghost->points, wxColour(0, 150, 255), 2);
            }
            dc.SetBrush(wxBrush(wxColour(0, 150, 255)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            for(const wxPoint& p : wire_ghost->points){
                dc.DrawCircle(coords_to_pos_px(p), 3);
            }
        }
    }
}

//折线绘制：把coords折点序列画成窗口折线
void Canvas::draw_wire_path(wxDC& dc, const std::vector<wxPoint>& points,
                            const wxColour& colour, int width){
    if(points.size() < 2) return;
    dc.SetPen(wxPen(colour, width));
    for(size_t i = 1; i < points.size(); ++i){
        dc.DrawLine(coords_to_pos_px(points[i-1]), coords_to_pos_px(points[i]));
    }
}

//绘制所有元件：位图画到各自的display_rect里。
//带alpha的位图在MSW下走AlphaBlend（wxMSWDCImpl::DoDrawBitmap对HasAlpha的位图调AlphaBlt），
//所以SVG的透明区域直接透出白底与网格——不再有wxStaticBitmap那层灰色底
void Canvas::draw_items(wxDC& dc){
    for(CanvasItem* item : *canvasItemCollection){
        if(dynamic_cast<Linking*>(item)) continue;   //导线没有位图，由draw_wires画
        const wxBitmap& bmp = item->bitmap();
        if(!bmp.IsOk()) continue;
        const wxRect r = item->display_rect(scale, offset_coords);
        dc.DrawBitmap(bmp, r.x, r.y, true);
    }
}

//仿真状态覆盖层：等所有位图都画完之后再统一画一遍。
//为什么单独一遍：徽标/方框数值如果夹在位图中间画，后面画的相邻元件位图会把它盖掉，
//看起来就像"某个元件显示的是别人的值"——这正是之前"线变绿了但门显示 0"最可能的来源之一
void Canvas::draw_state_overlay(wxDC& dc){
    if(!simulation || !simulation->hasModel()) return;
    for(CanvasItem* item : *canvasItemCollection){
        if(dynamic_cast<Linking*>(item)) continue;
        const wxRect r = item->display_rect(scale, offset_coords);
        if(r.width <= 0 || r.height <= 0) continue;
        draw_item_state(dc, item, r);
    }
}

//元件状态标注（Logisim 风格）：
//   - 端子：方框内直接显示 0/1/X（输入端子再描一圈"可点击"提示）
//   - 门/开关/灯：右上角小徽标显示该元件的值（门=输出、开关=状态、灯/探针=观察电平）
//   - 灯在值为 1 时画一圈黄色光晕
//   - 不参与仿真的元件在左上角画一个灰色 ✕
void Canvas::draw_item_state(wxDC& dc, const CanvasItem* item, const wxRect& rect){
    if(!simulation || !simulation->hasModel()) return;
    const int id = item->id;
    const std::string type = item->metadata ? item->metadata->get_type() : std::string();
    const bool input_terminal = (type == "IN_TERMINAL");
    const bool output_terminal = (type == "OUT_TERMINAL");

    //不支持的元件：灰色 ✕（只标记真实存在、且确实被判为不支持的元件）
    if(!simulation->isSupported(id)){
        dc.SetPen(wxPen(wxColour(150, 150, 150), 2));
        const int s = 5;
        const int x = rect.x + 3;
        const int y = rect.y + 3;
        dc.DrawLine(x, y, x + s, y + s);
        dc.DrawLine(x + s, y, x, y + s);
        return;
    }

    const bool has_value = simulation->componentHasValue(id);
    const Simulation::LogicValue value = has_value ? simulation->componentValue(id)
                                                   : Simulation::LogicValue::Unknown;

    //---- 端子：数值直接写在方框里 ----
    if(input_terminal || output_terminal){
        wxColour fill(235, 235, 235);
        wxColour ink(70, 70, 70);
        switch(value){
            case Simulation::LogicValue::One:
                fill = wxColour(198, 246, 214);
                ink  = wxColour(0, 120, 50);
                break;
            case Simulation::LogicValue::Zero:
                fill = wxColour(228, 228, 228);
                ink  = wxColour(60, 60, 60);
                break;
            default:
                fill = wxColour(212, 224, 250);
                ink  = wxColour(40, 70, 170);
                break;
        }
        const int inset = std::max(4, rect.width / 8);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(fill));
        dc.DrawRectangle(rect.x + inset, rect.y + inset,
                         rect.width - inset * 2, rect.height - inset * 2);
        //输入端子是"可点击"的：再描一圈边框（值为 1 时用绿色，提示当前是 1）
        if(input_terminal){
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetPen(wxPen(value == Simulation::LogicValue::One
                                ? wxColour(0, 170, 80) : wxColour(150, 150, 150), 2));
            dc.DrawRectangle(rect.x + inset, rect.y + inset,
                             rect.width - inset * 2, rect.height - inset * 2);
        }
        wxFont font = badge_font;
        font.SetPointSize(std::max(10, rect.height / 5));
        dc.SetFont(font);
        dc.SetTextForeground(ink);
        const wxString text = wxString::FromUTF8(Simulation::to_string(value));
        const wxSize extent = dc.GetTextExtent(text);
        dc.DrawText(text, rect.x + (rect.width - extent.GetWidth()) / 2,
                    rect.y + (rect.height - extent.GetHeight()) / 2);
        return;
    }

    if(!has_value) return;

    if(simulation->isLamp(id) && value == Simulation::LogicValue::One){
        const int cx = rect.x + rect.width / 2;
        const int cy = rect.y + rect.height / 2;
        const int radius = std::max(6, std::min(rect.width, rect.height) / 2 + 3);
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(wxColour(255, 200, 0), 3));
        dc.DrawCircle(cx, cy, radius);
    }

    const wxString text = wxString::FromUTF8(Simulation::to_string(value));
    dc.SetFont(badge_font);
    const wxSize extent = dc.GetTextExtent(text);
    const int boxW = std::max(14, extent.GetWidth() + 8);
    const int boxH = 15;
    const int x = rect.x + rect.width - boxW + 2;
    const int y = rect.y - 5;

    wxColour background(90, 90, 90);
    if(value == Simulation::LogicValue::One)       background = wxColour(0, 170, 80);
    else if(value == Simulation::LogicValue::Unknown) background = wxColour(70, 110, 210);

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(background));
    dc.DrawRoundedRectangle(x, y, boxW, boxH, 4);
    dc.SetTextForeground(*wxWHITE);
    dc.DrawText(text, x + (boxW - extent.GetWidth()) / 2,
                y + (boxH - extent.GetHeight()) / 2);
}

//绘制放置/克隆中的虚影（apply_ghost_alpha已经把它处理成半透明），画在元件之上
void Canvas::draw_ghost(wxDC& dc){
    if(!ghost) return;
    if(dynamic_cast<Linking*>(ghost)) return;        //导线虚影由draw_wires画
    const wxBitmap& bmp = ghost->bitmap();
    if(!bmp.IsOk()) return;
    const wxRect r = ghost->display_rect(scale, offset_coords);
    dc.DrawBitmap(bmp, r.x, r.y, true);

    //端子虚影补画状态外框：实物放置后会画一个几乎占满位图矩形的状态方框
    //（比SVG符号本体大）。只画SVG会让虚影看起来明显小于实物（放置瞬间"突然变大"），
    //这里按"未定值"样式画同样的外框（半透明保持虚影质感），让虚影与实物尺寸一致
    const std::string type = ghost->metadata ? ghost->metadata->get_type() : std::string();
    if(type == "IN_TERMINAL" || type == "OUT_TERMINAL"){
        const int inset = std::max(4, r.width / 8);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(wxColour(212, 224, 250, 128)));
        dc.DrawRectangle(r.x + inset, r.y + inset,
                         r.width - inset * 2, r.height - inset * 2);
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(wxColour(150, 150, 150, 140), 2));
        dc.DrawRectangle(r.x + inset, r.y + inset,
                         r.width - inset * 2, r.height - inset * 2);
    }
}

void Canvas::onPaint(wxPaintEvent& event){
    wxAutoBufferedPaintDC dc(this);   //双缓冲：拖动/跟随时整块重画也不闪
    const wxSize size = GetClientSize();

    //背景
    dc.SetBackground(wxBrush(wxColour(255, 255, 255)));
    dc.Clear();

    //网格：每GridStep个逻辑坐标画一条浅色线（与吸附网格一致）
    const int GRID_STEP = GridStep;
    const float x0 = (float)offset_coords.x;
    const float y0 = (float)offset_coords.y;
    const float x1 = (float)offset_coords.x + (float)size.GetWidth()  / scale;
    const float y1 = (float)offset_coords.y + (float)size.GetHeight() / scale;

    dc.SetPen(wxPen(wxColour(228, 228, 228), 1));
    int gx0 = (int)std::floor(x0 / GRID_STEP) * GRID_STEP;
    int gx1 = (int)std::ceil (x1 / GRID_STEP) * GRID_STEP;
    for(int gx = gx0; gx <= gx1; gx += GRID_STEP){
        const int px = (int)std::lround((gx - offset_coords.x) * scale);
        dc.DrawLine(px, 0, px, size.GetHeight());
    }
    int gy0 = (int)std::floor(y0 / GRID_STEP) * GRID_STEP;
    int gy1 = (int)std::ceil (y1 / GRID_STEP) * GRID_STEP;
    for(int gy = gy0; gy <= gy1; gy += GRID_STEP){
        const int py = (int)std::lround((gy - offset_coords.y) * scale);
        dc.DrawLine(0, py, size.GetWidth(), py);
    }

    //坐标轴（逻辑原点）高亮
    dc.SetPen(wxPen(wxColour(180, 180, 180), 1));
    const int ax = (int)std::lround((0 - offset_coords.x) * scale);
    const int ay = (int)std::lround((0 - offset_coords.y) * scale);
    dc.DrawLine(ax, 0, ax, size.GetHeight());
    dc.DrawLine(0, ay, size.GetWidth(), ay);

    //引脚（网格之上、导线之下：小圆点，网格对齐）
    draw_pins(dc);

    //导线（在网格之上、元件之下绘制）
    draw_wires(dc);

    //元件与虚影：画布自绘（不再有子窗口）。缩放后先按当前scale刷新位图缓存
    reput_items();
    draw_items(dc);
    draw_state_overlay(dc);   //第二遍：端子数值/状态徽标/灯亮光晕（不会被位图盖住）
    draw_ghost(dc);
}

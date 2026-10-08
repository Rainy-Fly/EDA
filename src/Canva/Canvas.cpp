#include"./Canvas.hpp"
#include"./ItemSVG.hpp"
#include<cmath>
#include<algorithm>

//Windows 的 winnt.h 把 DELETE 定义成访问权限宏 (0x00010000L)，
//与 EditTool::DELETE / ToolAction::DELETE 冲突（MSVC下会报语法错误）。
//本文件用不到该宏，这里直接取消；头文件里的枚举声明另有 push_macro/undef/pop_macro 保护
#ifdef DELETE
    #undef DELETE
#endif

//每滚一格缩放的倍率：1.2 = 每格放大/缩小20%（过大的倍率会导致缩放幅度太大）
const float MoveRatio=1.2f;

//缩放范围限制（滚轮缩放与set_scale共用，避免两处写死的上下限漂移）
static const float MinScale=0.01f;
static const float MaxScale=100.0f;

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
    : wxPanel(parent, id),
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
      move_dirty(false),
      wire_placing(false),
      wire_points(),
      wire_mouse_pos(0, 0),
      wire_drag_origin(0, 0),
      wire_drag_saved(),
      edit_tool(EditTool::NONE),
      select_follow_item(nullptr){
    SetBackgroundStyle(wxBG_STYLE_PAINT);
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
            }else if(edit_tool != EditTool::NONE){
                //退出编辑工具（选择跟随随之取消）
                edit_tool = EditTool::NONE;
                stop_select_follow();
                if(toolbar) toolbar->clear_selection();
                Refresh();
                handled = true;
            }
            if(handled) return;
        }else if(code == WXK_DELETE){
            if(select_follow_item){ delete_item(select_follow_item); return; }
            if(current_item){ delete_item(current_item); return; }
        }
        e.Skip();
    });
    toolbar = new ToolBar(this, this);
}

Canvas::~Canvas(){
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

void Canvas::notify_changed(){
    if(changed_callback) changed_callback();
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
                select_follow_item->update_ui(scale, offset_coords);
                move_dirty = true;
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
            dragging_item->update_ui(scale, offset_coords);
            move_dirty = true;
        }
        return;
    }

    //导线绘制模式：橡皮筋预览跟随鼠标（吸附网格）
    if(wire_placing){
        wxPoint coords = snap_coords(pos_to_coords(to_canvas_pos(pos)));
        if(coords != wire_mouse_pos){
            wire_mouse_pos = coords;
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
        if(!ghost->ui_node->IsShown()) ghost->ui_node->Show(true);
        ghost->update_ui(scale, offset_coords);
    }
}

void Canvas::on_left_down(wxMouseEvent& event){
    const wxPoint pos = mouse_position(event);
    //工具栏区域不响应放置
    if(toolbar && toolbar->GetRect().Contains(pos)) return;
    if(wire_placing){
        on_wire_left_down(event);   //导线模式：加点/完成
        return;
    }
    if(placing){
        place_at_mouse(event);
        return;
    }
    //编辑工具：命中元件则执行对应动作，空白处只清除选中（工具保持激活）
    if(edit_tool == EditTool::DELETE){
        if(CanvasItem* hit = item_at(pos)) delete_item(hit);
        else set_current_item(nullptr);
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
    //元件/导线被真的拖动过才通知“内容已修改”（决定关闭/新建前要不要提示保存）
    if(dragging_item && move_dirty) notify_changed();
    move_dirty = false;
    dragging_item = nullptr;
    if(HasCapture()) ReleaseMouse();
}

void Canvas::on_item_left_down(CanvasItem* item, wxMouseEvent& event){
    if(wire_placing){  //导线模式下点击任何位置（含元件）都是给导线加点
        on_wire_left_down(event);
        return;
    }
    if(placing){  //放置模式下点击（虚影/元件）都视为放置
        place_at_mouse(event);
        return;
    }
    //编辑工具（点击的是元件节点）
    if(edit_tool == EditTool::DELETE){
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
    if(!wire_points.empty() && coords == wire_points.back()){
        return;   //双击会先来两次DOWN（位置相同），忽略重复点，由DCLICK完成
    }
    if(wire_points.size() >= 2 && coords == wire_points.front()){
        finish_wire();   //点回起点：完成
        return;
    }
    wire_points.push_back(coords);
    wire_mouse_pos = coords;
    Refresh();
}

//右键：完成或取消导线
void Canvas::on_wire_right_down(wxMouseEvent& event){
    (void)event;
    if(!wire_placing) return;
    if(wire_points.size() >= 2) finish_wire();
    else{
        cancel_wire();
        if(toolbar) toolbar->clear_selection();
        Refresh();
    }
}

//画布右键总入口：选择跟随中→取消跟随（元件留在原位）；否则转导线右键
void Canvas::on_right_down(wxMouseEvent& event){
    if(select_follow_item){
        stop_select_follow();
        Refresh();
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
    if(!ItemSVG_bundle(name)) return;   //导线/总线等无SVG：忽略
    begin_placement(name);
}

//按名字创建跟随鼠标的虚影（门/通用元件）。调用方需先清理上一个工具状态
void Canvas::begin_placement(const std::string& name){
    placing = true;
    ghost = ItemSVG(name);
    ghost->ghost_mode = true;   //半透明虚影
    ghost->create_ui(this);
    //关键：虚影节点也要绑定完整的事件转发（MOTION/MIDDLE/滚轮/LEFT）。
    //否则鼠标悬停在虚影上时，移动事件被虚影窗口吞掉且不向父窗口传播，
    //虚影会冻结不动、只能等光标逃出范围后跳变追赶（表现为卡顿+非网格跳变）。
    bind_ui_events(ghost);
    //立刻把虚影放到当前鼠标位置（吸附网格）并显示，无需先移动鼠标
    {
        const wxPoint pos = ScreenToClient(wxGetMousePosition());
        const wxPoint coords = snap_coords(pos_to_coords(to_canvas_pos(pos)));
        ghost->coords_x = coords.x;
        ghost->coords_y = coords.y;
        ghost->update_ui(scale, offset_coords);
        ghost->ui_node->Show(true);
    }
    if(toolbar) toolbar->Raise();
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
        case ToolAction::SELECT: edit_tool = EditTool::SELECT; break;
        case ToolAction::DELETE: edit_tool = EditTool::DELETE; break;
        case ToolAction::CLONE:  edit_tool = EditTool::CLONE;  break;
    }
}

void Canvas::cancel_tool(){
    placing = false;
    cancel_wire();
    stop_select_follow();
    clear_ghost();
    if(toolbar) toolbar->clear_selection();
}

//提交正在绘制的导线：折点≥2时创建Linking加入集合，结束绘制模式
void Canvas::finish_wire(){
    if(!wire_placing) return;
    wire_placing = false;
    if(wire_points.size() >= 2){
        //去掉连续重复点后提交
        std::vector<wxPoint> clean;
        for(const wxPoint& p : wire_points){
            if(clean.empty() || clean.back() != p) clean.push_back(p);
        }
        if(clean.size() >= 2){
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
    if(toolbar) toolbar->clear_selection();
    Refresh();
}

//丢弃正在绘制的导线并退出绘制模式（不碰工具栏高亮）
void Canvas::cancel_wire(){
    wire_placing = false;
    wire_points.clear();
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
    Refresh();
}

//插入元件：创建UI节点、绑定事件转发、按当前视口摆放（导线无UI节点，仅入集合由onPaint绘制）
void Canvas::add_item(CanvasItem* item){
    if(!item) return;
    item->create_ui(this);
    bind_ui_events(item);
    item->update_ui(scale, offset_coords);
    canvasItemCollection->insert(item);
    Refresh();
}

//清空全部元件：结束绘制/编辑状态、销毁所有UI节点与实例
void Canvas::clear_all_items(){
    cancel_tool();   //结束放置/导线/编辑工具并清工具栏高亮
    //cancel_tool不会清拖动状态：清空后这些指针会悬空，必须在这里断开（否则后续鼠标移动会访问已释放内存）
    dragging_item = nullptr;
    wire_drag_saved.clear();
    for(CanvasItem* item : *canvasItemCollection){
        if(item->ui_node) item->ui_node->Destroy();   //wx延迟销毁，安全
        item->ui_node = nullptr;
        delete item;
    }
    canvasItemCollection->clear();
    set_current_item(nullptr);
    Refresh();
}

//删除元件：从集合移除、销毁UI节点并释放（门/导线均可）
void Canvas::delete_item(CanvasItem* item){
    if(!item) return;
    canvasItemCollection->erase(item);
    if(current_item == item) set_current_item(nullptr);   //通知属性栏清空
    if(select_follow_item == item) select_follow_item = nullptr;
    if(dragging_item == item) dragging_item = nullptr;
    if(ghost == item) ghost = nullptr;   //模式互斥，正常不会发生
    if(item->ui_node) item->ui_node->Destroy();   //wx延迟销毁，事件中调用安全
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
        item->update_ui(scale, offset_coords);
    }
}

//取消跟随：元件停留在当前位置（若跟随期间真的移动过，记一次内容修改）
void Canvas::stop_select_follow(){
    if(select_follow_item && move_dirty) notify_changed();
    move_dirty = false;
    select_follow_item = nullptr;
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
    if(!ghost->ui_node) ghost->create_ui(this);   //导线(Linking)无UI节点
    if(ghost->ui_node) bind_ui_events(ghost);
    //立即放到当前鼠标位置（吸附网格）
    const wxPoint pos = ScreenToClient(wxGetMousePosition());
    const wxPoint coords = snap_coords(pos_to_coords(to_canvas_pos(pos)));
    if(Linking* wire_ghost = dynamic_cast<Linking*>(ghost)){
        if(!wire_ghost->points.empty()){
            const wxPoint delta = coords - wire_ghost->points.front();
            for(wxPoint& p : wire_ghost->points) p = p + delta;
        }
        Refresh();
    }else{
        ghost->coords_x = coords.x;
        ghost->coords_y = coords.y;
        ghost->update_ui(scale, offset_coords);
        ghost->ui_node->Show(true);
    }
    if(toolbar) toolbar->Raise();
}

//复制元件：同类型新实例（新ID、独立元数据副本、共享SVG缓存），
//复制元数据（含属性栏修改过的值）与导线折点
CanvasItem* Canvas::clone_item(const CanvasItem* item) const{
    if(!item) return nullptr;
    CanvasItem* copy = ItemSVG(item->name);
    if(!copy) return nullptr;
    if(const Linking* src = dynamic_cast<const Linking*>(item)){
        if(Linking* dst = dynamic_cast<Linking*>(copy)) dst->points = src->points;
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
    if(ghost){
        if(ghost->ui_node) ghost->ui_node->Destroy();
        ghost->ui_node = nullptr;
        delete ghost;
        ghost = nullptr;
    }
}

//在鼠标位置实例化元件（用虚影的名字），然后结束放置模式。
//直接采用虚影当前的coords（已吸附到网格），保证放置位置与虚影显示位置完全一致
void Canvas::place_at_mouse(const wxMouseEvent& event){
    if(!placing || !ghost) return;
    //克隆的导线虚影：无UI节点，只复制折点
    if(Linking* wire_ghost = dynamic_cast<Linking*>(ghost)){
        CanvasItem* item = ItemSVG(ghost->name);
        if(Linking* dst = dynamic_cast<Linking*>(item)) dst->points = wire_ghost->points;
        canvasItemCollection->insert(item);
        cancel_tool();
        edit_tool = EditTool::NONE;   //克隆放置完成：退出克隆模式（一次性）
        set_current_item(item);
        notify_changed();             //放置了新内容
        Refresh();
        return;
    }
    CanvasItem* item = ItemSVG(ghost->name);
    item->coords_x = ghost->coords_x;
    item->coords_y = ghost->coords_y;
    item->create_ui(this);
    item->update_ui(scale, offset_coords);
    bind_ui_events(item);
    canvasItemCollection->insert(item);
    if(toolbar) toolbar->Raise();
    cancel_tool();  //结束放置：之后再点击该元件可移动它
    edit_tool = EditTool::NONE;   //克隆放置完成：退出克隆模式（对门类放置无影响，本来就为NONE）
    set_current_item(item);  //刚放置的元件成为当前选中（通知属性栏）
    notify_changed();        //项目内容变了（供菜单栏标记“未保存”）
    Refresh();
}

//wxWidgets的鼠标事件不会自动传播到父窗口，这里显式把元件节点上的鼠标事件转发给Canvas
void Canvas::bind_ui_events(CanvasItem* item){
    if(!item->ui_node) return;
    item->ui_node->Bind(wxEVT_LEFT_DOWN, [this, item](wxMouseEvent& e){
        on_item_left_down(item, e);
    });
    item->ui_node->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e){
        on_left_up(e);
    });
    item->ui_node->Bind(wxEVT_MOTION, [this](wxMouseEvent& e){
        on_mouse_move(e);
    });
    item->ui_node->Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& e){
        on_mouse_scroll(e);
    });
    item->ui_node->Bind(wxEVT_MIDDLE_DOWN, [this](wxMouseEvent& e){
        on_middle_down(e);
    });
    item->ui_node->Bind(wxEVT_MIDDLE_UP, [this](wxMouseEvent& e){
        on_middle_up(e);
    });
    item->ui_node->Bind(wxEVT_RIGHT_DOWN, [this](wxMouseEvent& e){
        on_right_down(e);   //选择跟随中取消跟随；导线模式下完成/取消
    });
}

CanvasItem* Canvas::item_at(const wxPoint& pos){
    //先命中导线（线段距离检测，导线没有UI节点）
    for(CanvasItem* item : *canvasItemCollection){
        if(Linking* wire = dynamic_cast<Linking*>(item)){
            if(wire_hit(wire, pos)) return wire;
        }
    }
    //再命中普通元件（节点矩形）
    for(CanvasItem* item : *canvasItemCollection){
        if(item->ui_node && item->ui_node->GetRect().Contains(pos)) return item;
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

void Canvas::reput_items(){
    //从canvasItemCollection中拿出每一个元件，重新绘制，coords不变，计算出pos把元件的图片放在相应位置
    for(CanvasItem* item : *canvasItemCollection){
        item->update_ui(scale, offset_coords);
    }
    if(ghost && ghost->ui_node){
        ghost->update_ui(scale, offset_coords);
    }
}

void Canvas::reput(){
    reput_items();
}

//在画布上绘制所有导线：已放置的导线（选中高亮）+ 正在绘制的预览
void Canvas::draw_wires(wxPaintDC& dc){
    //已放置的导线
    for(CanvasItem* item : *canvasItemCollection){
        Linking* wire = dynamic_cast<Linking*>(item);
        if(!wire || wire->points.size() < 2) continue;
        const bool selected = (item == current_item);
        if(selected){
            draw_wire_path(dc, wire->points, wxColour(0, 102, 204), 4);   //选中：粗蓝底
        }
        draw_wire_path(dc, wire->points,
                       selected ? wxColour(0, 120, 255) : wxColour(40, 40, 40), 2);
    }

    //正在绘制的导线：已确定的折线 + 橡皮筋预览 + 端点圆点
    if(wire_placing && !wire_points.empty()){
        if(wire_points.size() >= 2){
            draw_wire_path(dc, wire_points, wxColour(0, 120, 255), 2);
        }
        //橡皮筋：从最后一个折点到当前鼠标位置（虚线）
        dc.SetPen(wxPen(wxColour(0, 150, 255), 2, wxPENSTYLE_SHORT_DASH));
        dc.DrawLine(coords_to_pos_px(wire_points.back()), coords_to_pos_px(wire_mouse_pos));
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
void Canvas::draw_wire_path(wxPaintDC& dc, const std::vector<wxPoint>& points,
                            const wxColour& colour, int width){
    if(points.size() < 2) return;
    dc.SetPen(wxPen(colour, width));
    for(size_t i = 1; i < points.size(); ++i){
        dc.DrawLine(coords_to_pos_px(points[i-1]), coords_to_pos_px(points[i]));
    }
}

void Canvas::onPaint(wxPaintEvent& event){
    wxPaintDC dc(this);
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

    //导线（在网格之上、元件子窗口之下绘制）
    draw_wires(dc);

    reput();
}

#include"./Canvas.hpp"
#include"./ItemPNG.hpp"
#include<cmath>
#include<algorithm>

//每滚一格缩放的倍率：1.2 = 每格放大/缩小20%（过大的倍率会导致缩放幅度太大）
const float MoveRatio=1.2f;

//wxPoint -> canvasPos 辅助转换
static canvasPos to_canvas_pos(const wxPoint& p){
    return std::make_tuple((float)p.x, (float)p.y);
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
      current_item_callback(){
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT,        &Canvas::onPaint, this);
    Bind(wxEVT_MOUSEWHEEL,   &Canvas::on_mouse_scroll, this);
    Bind(wxEVT_MIDDLE_DOWN,  &Canvas::on_middle_down, this);
    Bind(wxEVT_MIDDLE_UP,    &Canvas::on_middle_up, this);
    Bind(wxEVT_MOTION,       &Canvas::on_mouse_move, this);
    Bind(wxEVT_LEFT_DOWN,    &Canvas::on_left_down, this);
    Bind(wxEVT_LEFT_UP,      &Canvas::on_left_up, this);
    //吞掉双击事件，避免一次双击触发两次放置
    Bind(wxEVT_LEFT_DCLICK,  [this](wxMouseEvent&){});
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

//中键滚轮缩放：鼠标在屏幕的pos不变，画布的逻辑坐标（offset_coords）改变
void Canvas::on_mouse_scroll(wxMouseEvent& event){
    const float rotation = event.GetWheelRotation() / (float)event.GetWheelDelta();
    wxPoint mouse_pos = mouse_position(event);

    const float old_scale = scale;
    float new_scale = old_scale * std::pow(MoveRatio, rotation);
    const float min_scale = 0.01f;
    const float max_scale = 100.0f;
    if(new_scale < min_scale) new_scale = min_scale;
    if(new_scale > max_scale) new_scale = max_scale;
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

    //拖动元件：coords改变，窗口pos跟着变
    if(dragging_item){
        wxPoint coords = snap_coords(pos_to_coords(to_canvas_pos(pos)));
        dragging_item->coords_x = coords.x;
        dragging_item->coords_y = coords.y;
        dragging_item->update_ui(scale, offset_coords);
        return;
    }

    //放置模式：虚影跟随鼠标并吸附到网格
    if(placing && ghost){
        wxPoint coords = snap_coords(pos_to_coords(to_canvas_pos(pos)));
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
    if(placing){
        place_at_mouse(event);
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
    dragging_item = nullptr;
    if(HasCapture()) ReleaseMouse();
}

void Canvas::on_item_left_down(CanvasItem* item, wxMouseEvent& event){
    if(placing){  //放置模式下点击（虚影/元件）都视为放置
        place_at_mouse(event);
        return;
    }
    //点击元件后进入移动，并把它设为当前选中（通知属性栏）
    set_current_item(item);
    dragging_item = item;
    if(!HasCapture()) CaptureMouse();
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

void Canvas::select_tool(const std::string& name, ItemType type){
    clear_ghost();
    if(type == ItemType::WIRE){
        //导线：点击后先不做处理，以后再说
        return;
    }
    set_current_item(nullptr);   //开始新放置：清空当前选中（尚未实例化的虚影不算元件）
    placing = true;
    ghost = ItemPNG(name);
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

void Canvas::cancel_tool(){
    placing = false;
    clear_ghost();
    if(toolbar) toolbar->clear_selection();
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
    CanvasItem* item = ItemPNG(ghost->name);
    item->coords_x = ghost->coords_x;
    item->coords_y = ghost->coords_y;
    item->create_ui(this);
    item->update_ui(scale, offset_coords);
    bind_ui_events(item);
    canvasItemCollection->insert(item);
    if(toolbar) toolbar->Raise();
    cancel_tool();  //结束放置：之后再点击该元件可移动它
    set_current_item(item);  //刚放置的元件成为当前选中（通知属性栏）
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
}

CanvasItem* Canvas::item_at(const wxPoint& pos){
    for(CanvasItem* item : *canvasItemCollection){
        if(item->ui_node && item->ui_node->GetRect().Contains(pos)) return item;
    }
    return nullptr;
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

    reput();
}


#include"./Canvas.hpp"
const float MoveRatio=2.0;
Canvas::Canvas():wxPanel(){
    offset_coords=wxPoint(0,0);
    canvasItemCollection=new std::unordered_set<CanvasItem*>();
    Bind(wxEVT_PAINT,&Canvas::onPaint,this);
}



void Canvas::on_mouse_scroll(wxMouseEvent& event){
    wxSize size = GetClientSize();
    float rotation = event.GetWheelRotation() / (float)event.GetWheelDelta();
    wxPoint mouse_coords = event.GetPosition();

    float old_scale = scale;
    float new_scale = old_scale * std::pow(MoveRatio, rotation);

    const float min_scale = 0.01f;
    const float max_scale = 100.0f;
    if (new_scale < min_scale) new_scale = min_scale;
    if (new_scale > max_scale) new_scale = max_scale;
    if (new_scale == old_scale) return;   

    float mx = static_cast<float>(mouse_coords.x);
    float my = static_cast<float>(mouse_coords.y);

    float new_offset_x = offset_coords.x + mx / old_scale - mx / new_scale;
    float new_offset_y = offset_coords.y + my / old_scale - my / new_scale;

    offset_coords.x = static_cast<int>(std::lround(new_offset_x));
    offset_coords.y = static_cast<int>(std::lround(new_offset_y));
    scale = new_scale;

    Refresh();
}

canvasPos Canvas::coords_to_pos(wxPoint coords){
    wxPoint relative=coords-offset_coords;
    float pos_x=relative.x*scale;
    float pos_y=relative.y*scale;
    return std::make_tuple(pos_x,pos_y);
}

wxPoint Canvas::pos_to_coords(canvasPos pos){
    auto [pos_x, pos_y] = pos;              // 结构化绑定解包
    float relative_x = pos_x / scale;       // 逆缩放
    float relative_y = pos_y / scale;
    wxPoint relative(static_cast<int>(relative_x),
                     static_cast<int>(relative_y));
    return relative + offset_coords;        // 加回偏移
}

void Canvas::reput(){
    //从canvasItemCollection中拿出每一个元件，重新绘制，coords不变，计算出pos把元件的图片放在相应位置
}

void Canvas::onPaint(wxPaintEvent& event){
    reput();
}
#include<wx/wx.h>
#include<unordered_set>
#include"./CanvasItem.hpp"
#include<tuple>
using canvasPos=std::tuple<float,float>;
class Canvas : wxPanel{
private:
    wxPoint offset_coords; //此窗口左上角在画布坐标系的网格位置
    float scale; //窗口缩放比例 单位pos/coords比
    std::unordered_set<CanvasItem*>* canvasItemCollection; //元件集合
    void on_mouse_scroll(wxMouseEvent& event);//鼠标中键滚动时触发
    void reput_items(); //重新摆放与显示元件
    void reput();//重新绘制元件的位置
    canvasPos coords_to_pos(wxPoint coords);//从画布逻辑坐标 => Canvas面板视觉坐标
    wxPoint pos_to_coords(canvasPos pos);
    void onPaint(wxPaintEvent& event);
public:
    Canvas();
};
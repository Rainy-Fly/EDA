#include"./WireRouting.hpp"
#include<cstdlib>

WireLeadAxis wire_lead_from_delta(const wxPoint& delta, int threshold){
    if(threshold < 1) threshold = 1;
    const int adx = std::abs(delta.x);
    const int ady = std::abs(delta.y);
    // 位移还太小：先不表态，等鼠标真的动了再定方向
    if(adx < threshold && ady < threshold) return WireLeadAxis::None;
    return (adx >= ady) ? WireLeadAxis::Horizontal : WireLeadAxis::Vertical;
}

WireLeadAxis wire_lead_update(WireLeadAxis axis, const wxPoint& anchor,
                              const wxPoint& from, const wxPoint& to){
    if(axis == WireLeadAxis::None || from == to) return axis;

    // 用 long long 乘：坐标是 int，直接相乘在极端坐标下会溢出。
    // 判定“回到或越过这条线”：终点正好落在线上，或两端点分居线两侧。
    // 只从线上往外走不算——那是第一段在正常延长，不该反过来改方向。
    auto reached = [](int line, int a, int b){
        if(b == line) return true;
        return (long long)(a - line) * (long long)(b - line) < 0;
    };

    if(axis == WireLeadAxis::Vertical){
        // 竖直段现在长度为 0（鼠标在同一条水平线上），另一条腿是水平的
        if(to.x != anchor.x && reached(anchor.y, from.y, to.y))
            return WireLeadAxis::Horizontal;
    }else{
        // 水平段现在长度为 0（鼠标在同一条竖线上），另一条腿是竖直的
        if(to.y != anchor.y && reached(anchor.x, from.x, to.x))
            return WireLeadAxis::Vertical;
    }
    return axis;
}

wxPoint wire_elbow(const wxPoint& anchor, const wxPoint& mouse, WireLeadAxis axis){
    WireLeadAxis use = axis;
    if(use == WireLeadAxis::None){
        use = (std::abs(mouse.x - anchor.x) >= std::abs(mouse.y - anchor.y))
                  ? WireLeadAxis::Horizontal
                  : WireLeadAxis::Vertical;
    }
    return (use == WireLeadAxis::Horizontal) ? wxPoint(mouse.x, anchor.y)
                                             : wxPoint(anchor.x, mouse.y);
}

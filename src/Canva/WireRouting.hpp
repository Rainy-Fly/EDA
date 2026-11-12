#pragma once
#include<wx/gdicmn.h>   //wxPoint（只用到坐标类型，不把整个GUI头拉进来，自检可以独立编译）

// 导线正交橡皮筋的“初次延长方向”：
//   None       还没判断出来（鼠标几乎没动，或刚加完一个折点）
//   Horizontal 先横后竖：从起点水平延长到鼠标的 x，再竖直落到鼠标
//   Vertical   先竖后横：从起点竖直延长到鼠标的 y，再水平走到鼠标
enum class WireLeadAxis{
    None,
    Horizontal,
    Vertical,
};

// 用“鼠标相对起点的位移”判断初次延长方向（位移可以是屏幕像素，也可以是逻辑坐标）。
// 两个轴都还没超过 threshold 时返回 None：鼠标刚按下时那一两像素的抖动不该决定整条线，
// 旧实现就是在这里被网格吸附量化成整格，结果几乎总是判成“先横后竖”。
// 两轴位移相等时按水平处理（与旧的 >= 保持一致）。
WireLeadAxis wire_lead_from_delta(const wxPoint& delta, int threshold = 3);

// 鼠标移动后更新初次延长方向。
//   anchor 是这一段导线的起点（上一个折点），from/to 是鼠标移动前后的位置（已吸附）。
// 方向锁定后不会因为鼠标随便移动而改变；只有鼠标回到（或一步越过）起点垂直于当前
// 延长方向的那条线，并且另一条腿确实有长度时，才把“当前延长方向”当作新的初始方向：
//   竖直起步 + 鼠标回到起点的水平线 -> 竖直段长度为 0，当前是水平在延长 -> 改成先横后竖
//   水平起步 + 鼠标回到起点的竖线   -> 水平段长度为 0，当前是竖直在延长 -> 改成先竖后横
// 这样不用重新点一下就能换到另一条 L 形路线，而鼠标只是小幅抖动时路线不会来回翻。
WireLeadAxis wire_lead_update(WireLeadAxis axis, const wxPoint& anchor,
                              const wxPoint& from, const wxPoint& to);

// 橡皮筋拐点：先横后竖 -> (mouse.x, anchor.y)；先竖后横 -> (anchor.x, mouse.y)。
// axis 为 None 时按两点位移的较长轴兜底（只可能在方向还没判断出来时用到）。
wxPoint wire_elbow(const wxPoint& anchor, const wxPoint& mouse, WireLeadAxis axis);

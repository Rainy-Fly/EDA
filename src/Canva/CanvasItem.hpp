#pragma once
#include<string>
// 可放置元件的父类
class CanvasItem{
public:
    std::string name;
    int id;
    int coords_x;  //网格坐标x，用于吸附
    int coords_y;  // 网格坐标y,用于吸附
};
#pragma once
#include<string>
#include<wx/wx.h>
#include"./CanvasItem.hpp"

// ItemSVG：传入元件名字，调用读取配置的函数（CanvasItem::load_config），
// 返回 CanvasItem 里对应类型的引用（新建实例；SVG引用来自共享缓存，不拥有）。
// SVG缺失时不报错（svg 保持空指针），与之前PNG缺失不报错的行为一致。
CanvasItem* ItemSVG(const std::string& name);

// 从assets下提取元件名字对应的SVG（wxBitmapBundle，矢量、可任意尺寸重绘）：
// 按候选路径搜索（当前目录/可执行文件相对路径，
// src/Canva/assets/Symbols/ 优先，src/assets/electronic-symbols/SVG/ 兜底），
// 每类元件只加载一次并共享缓存（调用者不得释放返回的指针）。
// 缺失时返回nullptr，不报错。
const wxBitmapBundle* ItemSVG_bundle(const std::string& name);

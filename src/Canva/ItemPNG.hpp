#pragma once
#include<string>
#include"./CanvasItem.hpp"

// ItemPNG：传入元件名字，调用读取配置的函数（CanvasItem::load_config，具体怎么读暂未实现），
// 返回 CanvasItem 里对应类型的引用（新建实例；图片引用来自共享缓存，不拥有）。
// 由于图片尚未准备好，缺失时不报错（image 保持空指针）。
CanvasItem* ItemPNG(const std::string& name);

// 从assets下提取元件名字对应的PNG图片：按候选路径搜索（当前目录/可执行文件相对路径，
// src/Canva/assets/Symbols/ 优先，src/assets/electronic-symbols/PNG/ 兜底），
// 每类元件只加载一次并共享缓存（调用者不得释放返回的指针）。
// 缺失时返回nullptr，不报错。
const wxBitmap* ItemPNG_image(const std::string& name);

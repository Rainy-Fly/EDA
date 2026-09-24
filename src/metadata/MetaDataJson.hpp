#pragma once
#include<string>

class MetaData;

// 元件元数据读取器：
// 从 src/metadata/ 目录下的多个JSON文件（gates.json/wire.json/...，见各文件头部注释）
// 读取所有元件的元数据定义，建立索引。
// key 可以是元件中文名（"与非门"）、英文名（"NAND"）或类型字符串（"NAND"/"WIRE"）。
//
// 找到定义则返回【新建】的 MetaData*（调用者拥有并负责释放，通常交给 CanvasItem，
// CanvasItem 析构时会 delete metadata；每次调用都返回独立副本，互不影响）；
// 找不到返回 nullptr（不报错，与“图片缺失不报错”保持一致的容错原则）。
MetaData* load_metadata(const std::string& key);

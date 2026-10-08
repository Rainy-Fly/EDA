#pragma once
#include<string>
#include<vector>
#include<utility>
#include<wx/string.h>
#include"../Canva/CanvasItem.hpp"

class Canvas;

// 项目描述文件（.xml）的读写，与界面无关，分两层：
//   circuit_export / circuit_import ：画布 <-> 内存表示（不碰文件系统，便于单独验证）
//   circuit_write  / circuit_read  ：内存表示 <-> xml 文件
//
// 格式严格按 README「项目描述文件（.xml）定义」（schema-version = 1.0）：
//   <?xml version="1.0" encoding="UTF-8"?>
//   <teda-project schema-version="1.0">
//     <info>
//       <name>我的第一个电路</name>
//       <author>nokna</author>
//       <created>2025-10-08T15:30:00+08:00</created>
//       <modified>2025-10-08T15:31:00+08:00</modified>
//       <description>与非门驱动 LED 示例</description>
//       <view offset-x="-40" offset-y="-30" scale="1.0" grid-step="10"/>
//     </info>
//     <circuit>
//       <component id="1" type="NAND" name="与非门" category="LogicGate" x="0" y="0">
//         <description>与非门：两个输入均为1时输出0，否则输出1</description>
//         <param key="DataBits">1</param>
//       </component>
//       <wire id="4" type="WIRE" name="导线">
//         <param key="DataBits">1</param>
//         <point x="75" y="0"/>
//         <point x="75" y="60"/>
//       </wire>
//     </circuit>
//   </teda-project>
//
// 约定（与README一致）：
//  * component@type 是加载时恢复元数据的主键（如 "NAND"、"Resistor-IEC-Standard"），
//    name 是冗余的显示名兼兜底键，category 也是冗余字段；
//  * 所有坐标都是逻辑网格坐标（整数），网格步长由 info/view@grid-step 决定（默认10）；
//  * param 的 key 是参数名、元素文本是值（写入 MetaData::params）；
//    description 覆盖元数据默认描述（写入 MetaData::set_description）；
//  * 容错：忽略未知元素/未知属性/无法解析的字段；type 找不到定义时按空元数据创建；
//    schema-version 不是 1.0 时只提示、不拒绝。
//  * 已知规范空缺：README 给 <wire> 定义的子元素只有 param 与 point，
//    所以导线上被编辑过的 description 不会写进文件（读取时也按规范忽略）。
struct CircuitItemDef{
    bool is_wire = false;                 //true=<wire>(Linking)，false=<component>
    int id = 0;                           //实例ID，在<circuit>内唯一
    std::string type;                     //元数据类型串（主键），如 "NAND"/"Resistor-IEC-Standard"/"WIRE"
    std::string name;                     //中文显示名（冗余，兼作兜底键）
    std::string category;                 //类别（冗余），如 "LogicGate"
    int x = 0;                            //元件锚点（图片中心）逻辑坐标；导线不用
    int y = 0;
    bool has_description = false;         //是否有 <description>（存在则覆盖元数据默认描述）
    std::string description;
    //通用参数（DataBits/resistance/...）：保持读取/写入顺序，便于对比文件
    std::vector<std::pair<std::string, std::string>> params;
    std::vector<wxPoint> points;          //仅导线：折点序列（≥2）
};

// 一整份项目文件的内存表示
struct CircuitDocument{
    //<info>：项目信息（画布不包含这些，导出时留空、由调用方按文件名/当前时间填写）
    std::string name;
    std::string author;
    std::string created;                  //ISO 8601
    std::string modified;                 //ISO 8601
    std::string description;
    //<info><view>：画布视口
    bool has_view = false;
    wxPoint offset_coords;
    double scale = 1.0;
    int grid_step = 10;

    std::vector<CircuitItemDef> items;
};

//当前文件格式版本（写入 teda-project@schema-version；读取时不一致只提示）
extern const char* const CircuitSchemaVersion;

// 画布 -> 内存表示（元件按id升序，保证生成的文件内容稳定、便于对比）。
// <info> 的名字/作者/时间/描述留空，由调用方填写
void circuit_export(const Canvas& canvas, CircuitDocument& out);

// 内存表示 -> 画布：先清空画布再逐个重建（元件/导线/视口）。
// 调用方必须先用 circuit_read 把整个文件读完，确认没问题再调用本函数，
// 这样文件有问题时画布保持原样（不会“打开失败还把原电路清空了”）。
// 返回需要提示的非致命问题（如某元件的元数据找不到），可能为空
wxString circuit_import(const CircuitDocument& doc, Canvas& canvas);

// 写xml文件；失败时error给出原因（路径不存在/文件被占用/没有写权限等）
bool circuit_write(const CircuitDocument& doc, const wxString& path, wxString& error);

// 读xml文件。失败返回false且不修改out；
// 成功但有需要注意的地方（版本不同、导线折点不足、元数据类型缺失等）时通过warning返回（可为空）
bool circuit_read(const wxString& path, CircuitDocument& out,
                  wxString& error, wxString& warning);

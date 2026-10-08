#pragma once
#include<string>
#include<map>
#include<vector>
#include<wx/wx.h>

class Canvas;

// 项目文件(.xml)读写模块（src/DealProjectXML）：
// 为上层（File菜单的新建/打开/保存按钮）提供“画布状态 <-> XML项目文件”的互转功能，
// 本模块不实现任何按钮/界面，只提供对xml的操作功能。
// 格式定义见 README.md「项目描述文件（.xml）定义」。
//
// 用法：
//   DealProjectXML::ProjectInfo info;                        // 项目信息（由上层持有）
//   DealProjectXML::SaveProject(canvas, "project.xml", info);// 保存：画布 -> XML
//   DealProjectXML::LoadProject(canvas, "project.xml", &info);// 打开：XML -> 画布（还原）
namespace DealProjectXML{

// 项目信息（保存时写入；打开时读出交给上层管理）
struct ProjectInfo{
    std::string name = "Untitled";  //项目名
    std::string author;             //作者
    std::string created;            //创建时间（ISO 8601，保存时自动生成）
    std::string modified;           //最近修改时间（ISO 8601，保存时自动生成）
    std::string description;        //项目描述
};

// 元件（<component>）：门类/通用元件
struct ComponentData{
    int id = 0;                                      //实例ID（circuit内唯一）
    std::string type;                                //类型串（元数据主键，如 "NAND"/"Resistor-IEC-Standard"）
    std::string name;                                //中文显示名
    std::string category;                            //类别（冗余）
    int x = 0, y = 0;                                //逻辑网格锚点坐标
    std::string description;                         //描述（覆盖元数据默认值）
    std::map<std::string, std::string> params;       //通用参数（DataBits/resistance/...）
};

// 导线/总线（<wire>）：折点序列
struct WireData{
    int id = 0;
    std::string type;                                //"WIRE"/"BUS"
    std::string name;                                //中文显示名
    std::map<std::string, std::string> params;       //通用参数
    std::vector<wxPoint> points;                     //折点（逻辑网格坐标，≥2）
};

// 完整项目数据
struct ProjectData{
    ProjectInfo info;
    wxPoint offset;          //视口：画布左上角逻辑坐标
    float scale = 1.0f;      //缩放比例
    int grid_step = 10;      //网格步长
    std::vector<ComponentData> components;
    std::vector<WireData> wires;
};

// ---- 纯XML读写（不依赖画布，可独立测试）----

// 序列化并写入XML文件（成功返回true）
bool WriteXml(const ProjectData& data, const std::string& path);
// 解析XML文件（成功返回true并填充data；失败返回false且不改动data）
bool ReadXml(const std::string& path, ProjectData* data);

// ---- 画布互转（上层使用的便捷入口）----

// 从画布收集状态（视口 + 全部元件/导线：位置/参数/折点）
ProjectData CollectFromCanvas(const Canvas* canvas, const ProjectInfo& info);
// 把状态还原到画布（先清空现有元件再重建；type缺失的元件按空元数据创建）
void ApplyToCanvas(Canvas* canvas, const ProjectData& data);

// 保存：收集画布状态并写入XML（成功返回true）
bool SaveProject(Canvas* canvas, const std::string& path, const ProjectInfo& info);
// 打开：解析XML并还原画布（成功返回true；失败返回false，画布保持不变；
// info非空时把项目信息读出）
bool LoadProject(Canvas* canvas, const std::string& path, ProjectInfo* info = nullptr);

} // namespace DealProjectXML

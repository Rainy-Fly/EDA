#pragma once
#include<string>
#include<map>
#include<vector>
#include<functional>
#include<json/json.h>

class Canvas;   //仅前向声明（RegisterCanvasTools 的参数）

// 工具注册表（src/Agent/Skills/）
// list.xml 定义每个工具的对外描述（name/description/参数JSON Schema，发给模型的函数定义）；
// 本模块负责：
//   1. LoadToolDefs：解析 list.xml -> ToolDef 列表（纯函数，不依赖画布，可独立测试）
//   2. ToolRegistry：把 handler 名绑定到 C++ 处理函数，供 Controller 执行工具调用
namespace Agent{

// 一个工具的静态定义（来自 list.xml）
struct ToolDef{
    std::string name;        //函数名（模型调用时用这个名字）
    std::string handler;     //C++ 处理函数注册名（默认同 name）
    std::string description; //函数说明（发给模型）
    std::string when_to_use; //何时使用（并入 description 发给模型）
    Json::Value parameters;  //JSON Schema 参数定义（object）
};

// 解析工具注册表 xml；成功返回true并填充 out（失败返回false且清空 out）。
// 容错：未知元素/属性忽略；name 缺失的工具跳过；parameters 损坏按空对象
bool LoadToolDefs(const std::string& xml_path, std::vector<ToolDef>& out);

// 工具执行注册表：handler 名 -> 处理函数（入参 JSON 参数对象，返回 JSON 字符串结果）
class ToolRegistry{
public:
    using Handler = std::function<std::string(const Json::Value& args)>;

    void bind(const std::string& handler, Handler fn);
    bool has(const std::string& handler) const;

    //执行：按 handler 名调用；未注册返回 {"ok":false,"error":...}；
    //处理函数抛异常时同样转成错误JSON返回
    std::string run(const std::string& handler, const Json::Value& args) const;

private:
    std::map<std::string, Handler> handlers_;
};

// 注册全部画布工具（get_canvas_state/place_component/connect_wire/...）。
// canvas 为操作对象（传nullptr时工具统一返回错误）。见 Tools.cpp。
// 注意：这些工具会触碰画布控件，只能由主线程调用。
void RegisterCanvasTools(ToolRegistry& reg, Canvas* canvas);

} // namespace Agent

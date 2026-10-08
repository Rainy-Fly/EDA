#include "ExplorerPane.h"
#include "file.xpm"
#include "folder.xpm"
#include "wx/clntdata.h"
#include "../metadata/MetaDataJson.hpp"
#include "../Canva/MetaData.hpp"

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

namespace{

// 树叶子节点数据：持有元件类型字符串（点击后交给画布放置）
class ComponentItemData : public wxTreeItemData{
public:
    explicit ComponentItemData(const wxString& type) : type_str(type) {}
    wxString type_str;
};

// 资源树展示的15个分类：英文类别 -> (中文文件夹名, 显示顺序)。
// 与 src/assets/electronic-symbols/SVG/ 的文件名前缀分类一致；
// 其余类别（LogicGate/Wire/Passive/Source/Connector 等）不在此列，不显示在资源树
struct CategoryInfo{ std::string cn; int order; };
const std::map<std::string, CategoryInfo>& category_info(){
    static const std::map<std::string, CategoryInfo> m = {
        {"Antenna",      {"天线",         0}},
        {"Audio",        {"音频",         1}},
        {"Capacitor",    {"电容",         2}},
        {"Diode",        {"二极管",       3}},
        {"Fuse",         {"保险丝",       4}},
        {"Ground",       {"接地",         5}},
        {"IC",           {"集成电路",     6}},
        {"Inductor",     {"电感",         7}},
        {"Miscellaneous",{"杂项",         8}},
        {"Relay",        {"继电器",       9}},
        {"Resistor",     {"电阻",        10}},
        {"Source",       {"电源/信号源", 11}},
        {"Switch",       {"开关",        12}},
        {"Transformer",  {"变压器",      13}},
        {"Transistor",   {"晶体管",      14}},
    };
    return m;
}

} // namespace

ExplorerPane::ExplorerPane(wxWindow* parent)
    : wxPanel(parent, wxID_ANY), m_callback(nullptr)
{
    // 使用垂直布局管理器 (Sizer) 来排列内部控件
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // 2. 创建树状控件
    m_treeCtrl = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 
                                wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_LINES_AT_ROOT);
    
    InitTree(); // 调用封装好的节点初始化逻辑
    
    m_treeCtrl->Bind(wxEVT_TREE_SEL_CHANGED, &ExplorerPane::OnItemClick, this);

    // 将树状控件加入布局，比例为 1，标志为 wxEXPAND，这意味着它会填满下方所有剩余空间
    mainSizer->Add(m_treeCtrl, 1, wxEXPAND | wxALL, 0);

    // 应用布局
    this->SetSizer(mainSizer);
}

void ExplorerPane::SetComponentSelectedCallback(ComponentSelectedCallback cb){
    m_callback = std::move(cb);
}

void ExplorerPane::InitTree()
{
    // 配置图标列表
    m_imageList = new wxImageList(16, 16);
    m_imageList->Add(wxIcon(folder_xpm));
    m_imageList->Add(wxIcon(file_xpm));
    m_treeCtrl->AssignImageList(m_imageList);

    wxTreeItemId rootId = m_treeCtrl->AddRoot(wxT("Untitled*"), 0, 0);

    // 从元数据枚举所有元件，只取资源树展示的15个分类，
    // 按“分类 -> 元件列表”分组（顺序稳定：分类按定义顺序、元件按id）
    std::vector<MetaData*> all = load_all_metadata();
    std::map<std::string, std::vector<MetaData*>> by_category;
    for(MetaData* md : all){
        const auto& info = category_info();
        if(info.find(md->get_category()) == info.end()) continue;   //非展示分类
        by_category[md->get_category()].push_back(md);
    }

    // 按分类顺序构建文件夹，文件夹内按id顺序添加元件
    std::vector<std::pair<std::string, CategoryInfo>> cats(category_info().begin(),
                                                           category_info().end());
    std::sort(cats.begin(), cats.end(), [](const auto& a, const auto& b){
        return a.second.order < b.second.order;
    });
    for(const auto& kv : cats){
        const std::string& cat = kv.first;
        auto it = by_category.find(cat);
        if(it == by_category.end() || it->second.empty()) continue;   //该分类无元件
        wxTreeItemId folderId = m_treeCtrl->AppendItem(rootId,
                    wxString::FromUTF8(kv.second.cn.c_str()), 0, 0);
        std::sort(it->second.begin(), it->second.end(), [](MetaData* a, MetaData* b){
            return a->get_id() < b->get_id();
        });
        for(MetaData* md : it->second){
            // 叶子节点：标签=中文名，数据=类型字符串（点击后交给画布放置）
            wxTreeItemId itemId = m_treeCtrl->AppendItem(folderId,
                    wxString::FromUTF8(md->get_name().c_str()), 1, 1);
            m_treeCtrl->SetItemData(itemId,
                    new ComponentItemData(wxString::FromUTF8(md->get_type().c_str())));
        }
    }

    for(MetaData* md : all) delete md;   //元数据副本用完即释放

    m_treeCtrl->Expand(rootId); // 默认展开根节点
}

void ExplorerPane::OnItemClick(wxTreeEvent& event)
{
    // 获取被点击的节点 ID
    wxTreeItemId clickedItem = event.GetItem();

    // 确保点中的不是空白处
    if (clickedItem.IsOk())
    {
        // 只有元件叶子节点带类型数据；文件夹节点没有数据，忽略
        ComponentItemData* data =
            dynamic_cast<ComponentItemData*>(m_treeCtrl->GetItemData(clickedItem));
        if(data && m_callback){
            // 通知外部（画布）按类型字符串进入放置模式
            m_callback(data->type_str.ToStdString());
        }
    }
}

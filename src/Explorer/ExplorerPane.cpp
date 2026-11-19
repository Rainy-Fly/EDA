#include "ExplorerPane.h"
#include "file.xpm"
#include "folder.xpm"
#include "wx/clntdata.h"
#include "../metadata/MetaDataJson.hpp"
#include "../Canva/MetaData.hpp"
#include "../Theme/Theme.hpp"

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

// 资源树展示的16个分类：英文类别 -> (中文文件夹名, 显示顺序)。
// 与 src/assets/electronic-symbols/SVG/ 的文件名前缀分类一致；
// 其余类别（Wire/Passive/Source/Connector/Simulation 等）不在此列，不显示在资源树
struct CategoryInfo{ std::string cn; int order; };
const std::map<std::string, CategoryInfo>& category_info(){
    // 顺序沿用英文类别名的字母序（Antenna→Audio→…→Transistor），
    // 新增分类只要把 order 插到字母序对应的位置，其余依次后移。
    // 「门」= LogicGate（元数据见 src/metadata/gates.json），
    // 是与非门/与门/或门/或非门/异或门/同或门/非门这一组逻辑门，
    // 此前只出现在上方工具栏里，资源树没有对应文件夹
    static const std::map<std::string, CategoryInfo> m = {
        {"Antenna",      {"天线",         0}},
        {"Audio",        {"音频",         1}},
        {"Capacitor",    {"电容",         2}},
        {"Diode",        {"二极管",       3}},
        {"Fuse",         {"保险丝",       4}},
        {"LogicGate",    {"逻辑门",       5}},
        {"Ground",       {"接地",         6}},
        {"IC",           {"集成电路",     7}},
        {"Inductor",     {"电感",         8}},
        {"Miscellaneous",{"杂项",         9}},
        {"Relay",        {"继电器",      10}},
        {"Resistor",     {"电阻",        11}},
        {"Sequential",   {"时序元件",    12}},
        {"Source",       {"电源/信号源", 13}},
        {"Switch",       {"开关",        14}},
        {"Timing",       {"时钟",        15}},
        {"Transformer",  {"变压器",      16}},
        {"Transistor",   {"晶体管",      17}},
    };
    return m;
}

} // namespace

ExplorerPane::ExplorerPane(wxWindow* parent)
    : CardPanel(parent, wxID_ANY, wxString::FromUTF8("元件库"), false,
                parent->FromDIP(UiTheme::SpaceXs)),
      m_treeCtrl(nullptr), m_search(nullptr), m_imageList(nullptr),
      m_callback(nullptr)
{
    // 元件搜索框：按中文名/英文名/类型/分类过滤下面的元件树。
    // 元件多起来以后（电阻就 15 种、IC 12 种）纯靠翻树很难找。
    m_search = new wxSearchCtrl(this, wxID_ANY);
    m_search->SetBackgroundColour(UiTheme::Surface);
    m_search->SetForegroundColour(UiTheme::Text);
    m_search->ShowCancelButton(true);
    m_search->SetDescriptiveText(wxString::FromUTF8("搜索元件（名称/英文/类型/分类）"));
    m_search->Bind(wxEVT_TEXT, &ExplorerPane::OnSearchChanged, this);
    // 点输入框右侧的 × ：清空过滤条件、还原完整元件树
    m_search->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent& e){
        if(m_search) m_search->ChangeValue(wxString());
        BuildTree(wxString());
        e.Skip();
    });
    // 搜索框里按 Esc：清空并还原；同时吞掉这个 Esc，
    // 避免它冒泡出去把画布上正在绘制的导线/放置模式取消掉
    m_search->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e){
        const int code = e.GetKeyCode();
        if(code == WXK_ESCAPE){
            if(m_search && !m_search->IsEmpty()){
                m_search->ChangeValue(wxString());
                BuildTree(wxString());
            }
            return;
        }
        if(code == WXK_RETURN) return;   //回车也不让它跑到别处
        e.Skip();
    });

    // 创建树状控件：去掉原生 3D 边框，让树和卡片内容区自然融合
    m_treeCtrl = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                wxTR_HAS_BUTTONS | wxTR_SINGLE |
                                wxTR_LINES_AT_ROOT | wxBORDER_NONE);
    m_treeCtrl->SetBackgroundColour(UiTheme::Surface);
    m_treeCtrl->SetForegroundColour(UiTheme::Text);
    
    InitTree(); // 调用封装好的节点初始化逻辑
    
    m_treeCtrl->Bind(wxEVT_TREE_SEL_CHANGED, &ExplorerPane::OnItemClick, this);

    // 搜索框在上（高度按需），树在下（占满剩余空间）
    Content()->Add(m_search, 0, wxEXPAND | wxBOTTOM, parent->FromDIP(UiTheme::SpaceXs));
    Content()->Add(m_treeCtrl, 1, wxEXPAND);
}

void ExplorerPane::SetComponentSelectedCallback(ComponentSelectedCallback cb){
    m_callback = std::move(cb);
}

void ExplorerPane::InitTree()
{
    // 配置图标列表（只在初始化时建一次；重建树时复用，不重复创建）
    m_imageList = new wxImageList(16, 16);
    m_imageList->Add(wxIcon(folder_xpm));
    m_imageList->Add(wxIcon(file_xpm));
    m_treeCtrl->AssignImageList(m_imageList);

    BuildTree(wxString());   //空过滤词 = 显示全部元件
}

// 按过滤词重建整棵树（filter 为空 = 全量）。
// 之所以“重建”而不是“隐藏不匹配节点”：wxTreeCtrl 没有可靠的隐藏 API，
// 重建最直接，也不会残留上一次的展开/选中状态。
void ExplorerPane::BuildTree(const wxString& filter)
{
    if(!m_treeCtrl) return;

    //清空：wxTreeCtrl 会连同每个节点的 wxTreeItemData 一起释放，不会泄漏
    m_treeCtrl->DeleteAllItems();

    const wxString query = filter.Strip(wxString::both).Lower();
    const bool filtering = !query.empty();

    wxTreeItemId rootId = m_treeCtrl->AddRoot(wxT("Untitled*"), 0, 0);

    // 从元数据枚举所有元件，只取资源树展示的分类，按“分类 -> 元件列表”分组
    std::vector<MetaData*> all = load_all_metadata();
    std::map<std::string, std::vector<MetaData*>> by_category;
    for(MetaData* md : all){
        const auto& info = category_info();
        auto catIt = info.find(md->get_category());
        if(catIt == info.end()) continue;   //非展示分类
        if(filtering){
            //分类中文名命中（例如输入"电阻"）=> 整个分类都列出来
            const bool categoryHit = wxString::FromUTF8(catIt->second.cn.c_str())
                                         .Lower().Find(query) != wxNOT_FOUND;
            //元件自身的中文名/英文名/类型串命中
            const bool selfHit =
                wxString::FromUTF8(md->get_name().c_str()).Lower().Find(query) != wxNOT_FOUND ||
                wxString::FromUTF8(md->get_name_en().c_str()).Lower().Find(query) != wxNOT_FOUND ||
                wxString::FromUTF8(md->get_type().c_str()).Lower().Find(query) != wxNOT_FOUND;
            if(!categoryHit && !selfHit) continue;
        }
        by_category[md->get_category()].push_back(md);
    }

    // 按分类顺序构建文件夹，文件夹内按id顺序添加元件
    std::vector<std::pair<std::string, CategoryInfo>> cats(category_info().begin(),
                                                           category_info().end());
    std::sort(cats.begin(), cats.end(), [](const auto& a, const auto& b){
        return a.second.order < b.second.order;
    });
    int shown = 0;
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
            ++shown;
        }
    }

    for(MetaData* md : all) delete md;   //元数据副本用完即释放

    if(filtering && shown == 0){
        //没有匹配项时给一句提示（该节点没有组件数据，点了不会触发放置）
        m_treeCtrl->AppendItem(rootId, wxString::FromUTF8("（没有匹配的元件）"), 0, 0);
    }

    //过滤时把所有分类展开（结果一眼可见）；平时只展开根节点
    if(filtering) m_treeCtrl->ExpandAll();
    else          m_treeCtrl->Expand(rootId);
}

void ExplorerPane::OnSearchChanged(wxCommandEvent& event)
{
    BuildTree(m_search ? m_search->GetValue() : wxString());
    event.Skip();
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

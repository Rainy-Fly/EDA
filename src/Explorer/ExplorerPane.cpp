#include "ExplorerPane.h"
#include "file.xpm"
#include "folder.xpm"

ExplorerPane::ExplorerPane(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    // 使用垂直布局管理器 (Sizer) 来排列内部控件
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // 1. (预留位置) 未来这里可以添加快捷工具栏的 Sizer 或控件
    // wxBoxSizer* toolBarSizer = new wxBoxSizer(wxHORIZONTAL);
    // ... 添加各个小按钮 ...
    // mainSizer->Add(toolBarSizer, 0, wxEXPAND | wxALL, 2);

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

void ExplorerPane::InitTree()
{
    // 配置图标列表
    m_imageList = new wxImageList(16, 16);
    m_imageList->Add(wxIcon(folder_xpm));
    m_imageList->Add(wxIcon(file_xpm));
    m_treeCtrl->AssignImageList(m_imageList);

    // 添加节点 (使用之前的逻辑)
    wxTreeItemId rootId = m_treeCtrl->AddRoot(wxT("Untitled*"), 0, 0);
    
    wxTreeItemId wiringId = m_treeCtrl->AppendItem(rootId, wxT("Wiring"), 0, 0);
    m_treeCtrl->AppendItem(wiringId, wxT("Pin"), 1, 1);
    
    wxTreeItemId gatesId = m_treeCtrl->AppendItem(rootId, wxT("Gates"), 0, 0);
    m_treeCtrl->AppendItem(gatesId, wxT("NOT Gate"), 1, 1);
    m_treeCtrl->AppendItem(gatesId, wxT("AND Gate"), 1, 1);

    m_treeCtrl->Expand(rootId); // 默认展开根节点
}

void ExplorerPane::OnItemClick(wxTreeEvent& event)
{
    // 获取被点击的节点 ID
    wxTreeItemId clickedItem = event.GetItem();

    // 确保点中的不是空白处
    if (clickedItem.IsOk())
    {
        // 获取节点的名字
        wxString itemName = m_treeCtrl->GetItemText(clickedItem);

        // 弹窗显示（用来测试我们是否抓取成功）
        wxMessageBox(wxT("你点击了: ") + itemName, wxT("测试"), wxOK | wxICON_INFORMATION);
    }
}
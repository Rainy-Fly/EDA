#include "wx/wx.h"
#include "wx/treectrl.h"

// 继承自 wxPanel，它是一个可以容纳其他控件的通用容器
class ExplorerPane : public wxPanel 
{
public:
    // 构造函数需要传入父窗口指针
    ExplorerPane(wxWindow* parent);

    // 对外提供获取内部树控件的方法，方便后续与其他面板联动
    wxTreeCtrl* GetTreeCtrl() const { return m_treeCtrl; }

private:
    wxTreeCtrl* m_treeCtrl;
    wxImageList* m_imageList;

    // 内部初始化函数，保持构造函数整洁
    void InitTree(); 
    void OnItemClick(wxTreeEvent& event);
};
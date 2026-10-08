#pragma once
#include "wx/wx.h"
#include "wx/treectrl.h"
#include <functional>
#include <string>

// 继承自 wxPanel，它是一个可以容纳其他控件的通用容器。
// 资源树：从 src/metadata/ 加载所有元件元数据，按类别分组为文件夹
// （天线/音频/电容/二极管/…/晶体管，与电子符号库SVG目录分类一致），
// 每个文件夹下是该类的各个元件；点击元件通过回调通知外部（画布放置）。
class ExplorerPane : public wxPanel 
{
public:
    // 元件被点击回调：参数为元件的类型字符串（如 "Resistor-IEC-Standard"），
    // 供画布按名字进入放置模式（虚影跟随鼠标、点击放置，与工具栏门类一致）
    using ComponentSelectedCallback = std::function<void(const std::string& type)>;

    // 构造函数需要传入父窗口指针
    ExplorerPane(wxWindow* parent);

    // 对外提供获取内部树控件的方法，方便后续与其他面板联动
    wxTreeCtrl* GetTreeCtrl() const { return m_treeCtrl; }

    // 注册“元件被点击”回调
    void SetComponentSelectedCallback(ComponentSelectedCallback cb);

private:
    wxTreeCtrl* m_treeCtrl;
    wxImageList* m_imageList;
    ComponentSelectedCallback m_callback;

    // 内部初始化函数，保持构造函数整洁
    void InitTree(); 
    void OnItemClick(wxTreeEvent& event);
};

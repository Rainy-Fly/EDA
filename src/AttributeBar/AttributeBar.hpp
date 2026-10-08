#pragma once
#include<wx/wx.h>
#include<string>

class CanvasItem;
class MetaData;

// 属性栏：wxPanel，维持对“当前元件”(CanvasItem*)的引用，
// 显示该元件的信息（名称/ID/类型/坐标），以及其元数据中的 DataBits、输入/输出数、描述、通用参数等。
// 元数据值可编辑：编辑框内容变化时写回【当前元件自己的元数据】(MetaData)，不影响其他元件。
// 由外部（demoMainFrame 等）把画布选中/放置的元件通过 set_item 注入；
// 传 nullptr 表示当前没有选中元件（显示占位提示）。
class AttributeBar : public wxPanel{
public:
    explicit AttributeBar(wxWindow* parent);
    ~AttributeBar() override = default;

    // 设置当前元件并刷新显示
    void set_item(CanvasItem* item);
    // 当前元件（nullptr 表示未选择）
    CanvasItem* get_item() const { return current_item; }

private:
    CanvasItem* current_item;   //指向当前元件的引用（不拥有，由画布管理）
    wxStaticText* title_text;   //标题（显示元件名）
    wxSizer* content_sizer;     //属性行容器（每次刷新重建）
    bool updating;              //重建期间抑制编辑框写回

    void rebuild();                                             //按 current_item 重建属性行
    void add_label_value(wxSizer* sizer, const wxString& label,
                         const wxString& value);                //只读行“标签: 值”
    void add_editable_value(wxSizer* sizer, const wxString& label,
                            const std::string& meta_key,
                            const wxString& value);             //可编辑行：改后写回元数据
    static void set_metadata_value(MetaData* md, const std::string& key,
                                   const std::string& value);   //把新值写入元数据
};

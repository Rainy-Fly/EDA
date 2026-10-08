#pragma once
#include<wx/string.h>

class wxWindow;

// 打开项目时先问“从哪里开始找”：1.最近位置 2.项目根目录 3.默认目录。
// 返回用户选中的目录；用户取消时返回空字符串。
// 目录不存在的选项会被禁用（例如从没打开过项目时“最近位置”不可选）。
wxString AskOpenLocation(wxWindow* parent,
                         const wxString& recent_dir,
                         const wxString& root_dir,
                         const wxString& default_dir);

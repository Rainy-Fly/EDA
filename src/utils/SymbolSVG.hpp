#pragma once

#include <string>

#include <wx/gdicmn.h>
#include <wx/image.h>
#include <wx/statbmp.h>


class SymbolSVG : public wxStaticBitmap
{
public:
    SymbolSVG(
        wxWindow* parent,
        const std::string& name,
        const wxSize& size,
        const wxPoint& position
    );

    // 基于 origin 重新生成 Bitmap
    // x、y 为缩放倍率
    void scale(float x, float y);

    // 移动指定的偏移量
    void move(float x, float y);

    const wxPoint& getPosition() const;
    const wxSize& getSize() const;

private:
    // 从 SVGPool 获取的原始图片
    // 创建后永远不修改
    wxImage origin;

    wxPoint position;
    wxSize size;
};
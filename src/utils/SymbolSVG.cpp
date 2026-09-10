#include "SymbolSVG.hpp"

#include "SVGPool.hpp"
#include<iostream>


SymbolSVG::SymbolSVG(
    wxWindow* parent,
    const std::string& name,
    const wxSize& size,
    const wxPoint& position
)
    : wxStaticBitmap(
          parent,
          wxID_ANY,
          wxNullBitmap,
          position,
          size
      ),
      position(position),
      size(size)
{
    // 从 SVGPool 获取原始 SVG 图片
    const wxImage* image =
        SVGPool::getInstance()->getImage(name);

    if (image == nullptr)
    {
        std::cout << "[!] SVG image is nullptr: "
                  << name << std::endl;
        return;
    }

    if (!image->IsOk())
    {
        std::cout << "[!] SVG image is invalid: "
                  << name << std::endl;
        return;
    }

    // 保存原始图片，之后永远不修改
    origin = *image;

    std::cout << "[+] origin loaded: "
              << origin.GetWidth() << "x"
              << origin.GetHeight()
              << std::endl;

    // 根据初始尺寸生成显示图片
    wxImage display =
        origin.Scale(
            size.x,
            size.y,
            wxIMAGE_QUALITY_HIGH
        );

    std::cout << "[+] display image: "
              << display.GetWidth() << "x"
              << display.GetHeight()
              << " ok=" << display.IsOk()
              << std::endl;

    wxBitmap bitmap(display);

    std::cout << "[+] bitmap ok = "
              << bitmap.IsOk()
              << std::endl;

    SetBitmap(bitmap);

    std::cout << "[+] GetBitmap ok = "
              << GetBitmap().IsOk()
              << std::endl;
}


void SymbolSVG::scale(float x, float y)
{
    // 更新尺寸
    size.SetWidth(
        static_cast<int>(size.x * x)
    );

    size.SetHeight(
        static_cast<int>(size.y * y)
    );

    // 永远从 origin 重新生成
    wxImage display =
        origin.Scale(
            size.x,
            size.y,
            wxIMAGE_QUALITY_HIGH
        );

    SetBitmap(wxBitmap(display));

    // 同步 StaticBitmap 控件尺寸
    SetSize(size);
}


void SymbolSVG::move(float x, float y)
{
    // 更新位置
    position.x += static_cast<int>(x);
    position.y += static_cast<int>(y);

    // 移动控件
    SetPosition(position);
}


const wxPoint& SymbolSVG::getPosition() const
{
    return position;
}


const wxSize& SymbolSVG::getSize() const
{
    return size;
}
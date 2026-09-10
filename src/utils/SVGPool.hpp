#pragma once

#include <string>
#include <unordered_map>
#include<unordered_map>
#include <wx/image.h>


class SVGPool
{
public:
    SVGPool() = default;

    bool load(const std::string& manifestPath);

    const wxImage* getImage(const std::string& name) const;

    bool contains(const std::string& name) const; 
    static SVGPool* getInstance();
    SVGPool(std::string path);

private:
    std::unordered_map<std::string, wxImage> images_;
    static SVGPool* instance;
};
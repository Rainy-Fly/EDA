#include "SVGPool.hpp"

#include <fstream>
// #include <iostream>
#include <json/json.h>

SVGPool* SVGPool::instance = nullptr;

SVGPool* SVGPool::getInstance()
{
    return SVGPool::instance;
}

bool SVGPool::load(const std::string& manifestPath)
{
    std::ifstream file(manifestPath);
    if (!file.is_open())
        return false;

    Json::Value manifest;
    Json::CharReaderBuilder builder;
    std::string errors;

    if (!Json::parseFromStream(builder, file, &manifest, &errors))
        return false;

    std::string basePath = manifestPath;
    size_t separator = basePath.find_last_of("/\\");
    if (separator != std::string::npos)
        basePath = basePath.substr(0, separator + 1);
    else
        basePath.clear();

    images_.clear();

    for (const auto& item : manifest)
    {
        const std::string id = item["id"].asString();
        const std::string filename = item["filename"].asString();

        if (id.empty() || filename.empty())
            continue;

        const std::string imagePath =
            basePath + "PNG/" + filename + ".png";

        wxImage image(
            wxString::FromUTF8(imagePath),
            wxBITMAP_TYPE_PNG
        );

        if (!image.IsOk())
            continue;

        images_.emplace(id, std::move(image));
    }

    return true;
}

const wxImage* SVGPool::getImage(const std::string& name) const
{
    auto it = images_.find(name);

    if (it == images_.end())
        return nullptr;

    return &it->second;
}

bool SVGPool::contains(const std::string& name) const
{
    return images_.contains(name);
}

SVGPool::SVGPool(std::string manifestPath)
{
    wxInitAllImageHandlers();
    bool result = load(manifestPath);

    SVGPool::instance = this;
}
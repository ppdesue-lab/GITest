#pragma once

#include <Object3D.h>
#include <filesystem>

class TexturePlaneObject : public Object3D
{
public:
    bool LoadFromImageFile(const std::filesystem::path& filepath, float maxSize = 100.0f);
};

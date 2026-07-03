#pragma once

#include "Vector2DDocument.h"

#include <filesystem>
#include <string>

class DxfLoader
{
public:
    static bool Load(const std::filesystem::path& filepath, Vector2DDocument& document, std::string& error);
};


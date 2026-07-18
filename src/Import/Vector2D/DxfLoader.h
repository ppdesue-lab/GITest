#pragma once

#include "Vector2DDocument.h"

#include <filesystem>
#include <string>

enum class DxfImportMode
{
    LinesOnly,
    LinesWithArcFit,
    NativePrimitives
};

class DxfLoader
{
public:
    static bool Load(const std::filesystem::path& filepath, Vector2DDocument& document, std::string& error,
        DxfImportMode mode = DxfImportMode::LinesWithArcFit, bool bPostProcess = true);
};

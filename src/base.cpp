#include "stdsfx.h"
#include "base.h"

#include <filesystem>

std::string GetFilePath(const std::string& filename)
{
    std::filesystem::path rootPath = std::filesystem::current_path();
    std::filesystem::path filePath(filename);

    std::filesystem::path fullPath = rootPath / filePath;
    if(!std::filesystem::exists(fullPath))
    {
        //search parent directory
        fullPath = rootPath.parent_path() / filePath;
        if(!std::filesystem::exists(fullPath))
        {
            ERROR("File does not exist: {0}", fullPath.string());
            return "";
        }
    }
    return fullPath.string();
}
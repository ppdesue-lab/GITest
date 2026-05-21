#include "stdsfx.h"
#include "base.h"

#include <filesystem>
#ifdef PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

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

std::wstring Utf8ToWide(const std::string& utf8Str)
{
#ifdef PLATFORM_WINDOWS
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return std::wstring();
    std::wstring wstr(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, &wstr[0], wlen);
    if (!wstr.empty() && wstr.back() == L'\0') wstr.pop_back();
    return wstr;
#else
    return std::wstring(utf8Str.begin(), utf8Str.end());
#endif
}

std::string LocalToUtf8(const std::string& localStr)
{
#ifdef PLATFORM_WINDOWS
    int wlen = MultiByteToWideChar(CP_ACP, 0, localStr.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return localStr;
    std::wstring wstr(wlen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, localStr.c_str(), -1, &wstr[0], wlen);
    int utflen = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utflen <= 0) return localStr;
    std::string utf8(utflen, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8[0], utflen, nullptr, nullptr);
    if (!utf8.empty() && utf8.back() == '\0') utf8.pop_back();
    return utf8;
#else
    return localStr;
#endif
}

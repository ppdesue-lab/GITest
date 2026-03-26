#pragma once

#include "base.h"

enum class PixelType
{
    BYTE,
    WORD,
    FLOAT
};


struct Image
{
    int Width, Height, Channel;
    PixelType Type = PixelType::BYTE;
    unsigned char* Data = nullptr;

    ~Image()
    {
        Width = 0;Height = 0;Channel = 0;
        if (Data) delete[] Data;
        Data = nullptr;
    }

    static Ref<Image> Load(const char* path, PixelType type = PixelType::BYTE);
};
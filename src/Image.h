#pragma once

#include "base.h"
#include <cstddef>

enum class PixelType
{
    BYTE,
    WORD,
    FLOAT
};


struct Image
{
    int Width=0, Height = 0, Channel = 0;
    PixelType Type = PixelType::BYTE;
    unsigned char* Data = nullptr;

    Image() {}

	//create image from memory, this will take ownership of the data pointer and delete it when the image is destroyed
    Image(int w,int h,int c, unsigned char* data, PixelType type = PixelType::BYTE)
		: Width(w), Height(h), Channel(c), Data(data), Type(type) {
	}

    ~Image()
    {
        Width = 0;Height = 0;Channel = 0;
        if (Data) delete[] Data;
        Data = nullptr;
    }

    void Save(const std::string& path) const;

    static Ref<Image> Load(const char* path, PixelType type = PixelType::BYTE);
    static Ref<Image> LoadFromMemory(const unsigned char* data, size_t size, PixelType type = PixelType::BYTE);
};

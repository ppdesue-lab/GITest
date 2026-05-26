#include "stdsfx.h"
#include "Image.h"
#include <cstring>
#include <filesystem>
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace
{
Ref<Image> CopyDecodedImage(unsigned char* decoded, int width, int height, PixelType type)
{
    if (!decoded)
        return nullptr;

    const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    unsigned char* data = new unsigned char[byteCount];
    std::memcpy(data, decoded, byteCount);
    stbi_image_free(decoded);
    return CreateRef<Image>(width, height, 4, data, type);
}
}

Ref<Image> Image::Load(const char* path,PixelType type)
{
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    if (!file) {
        std::cerr << "Failed to load image: " << path << std::endl;
        return nullptr;
    }

    std::vector<unsigned char> encoded(
        (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Ref<Image> image = LoadFromMemory(encoded.data(), encoded.size(), type);
    if (!image)
        std::cerr << "Failed to decode image: " << path << std::endl;
    return image;
}

Ref<Image> Image::LoadFromMemory(const unsigned char* encodedData, size_t size, PixelType type)
{
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load_from_memory(encodedData, static_cast<int>(size), &width, &height, &channels, 4);
    return CopyDecodedImage(data, width, height, type);
}

void Image::Save(const std::string& path) const
{
	stbi_flip_vertically_on_write(true);
    int success = stbi_write_png(path.c_str(), Width, Height, Channel, Data, Width * Channel);
    if (success) {
        INFO("{} saved successfully!\n", path);
    }
    else {
        ERROR("Failed to save image: {}\n", path);
    }

}

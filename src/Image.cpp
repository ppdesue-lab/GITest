#include "stdsfx.h"
#include "Image.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"



Ref<Image> Image::Load(const char* path,PixelType type)
{
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path, &width, &height, &channels, 4);
    if (!data) {
        std::cerr << "Failed to load image: " << path << std::endl;
        return nullptr;
    }

    Ref<Image> img = CreateRef<Image>();
    img->Width = width;
    img->Height = height;
    img->Channel = channels;
    img->Type = type;
    img->Data = data;
    return img;
}

void Image::Save(const std::string& path) const
{
    stbi_write_png(path.c_str(), Width, Height, Channel, Data, Width * Channel);
}
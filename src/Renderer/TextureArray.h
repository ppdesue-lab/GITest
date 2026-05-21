#pragma once

#include "base.h"
#include <Image.h>
#include <vector>

class TextureArray
{
public:
    virtual ~TextureArray() = default;

    virtual void Bind(uint32_t slot = 0) const = 0;
    virtual void Unbind() const = 0;

    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;
    virtual uint32_t GetLayerCount() const = 0;

    static Ref<TextureArray> Create(uint32_t width, uint32_t height, uint32_t layers,
                                     const std::vector<Ref<Image>>& images = {});
};

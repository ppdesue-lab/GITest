#pragma once

#include <Image.h>
#include <unordered_map>
#include <string>

#include <memory>
#include <base.h>

class Texture
{
public:
    virtual ~Texture() = default;

    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;

    virtual void Bind(uint32_t slot = 0) const=0;
    virtual void Unbind() const=0;

    virtual bool operator == (const Texture& other) const
    {
        return m_RendererID == other.m_RendererID;
    }
    void Unload();

    unsigned int m_RendererID;
    Ref<Image> SourceImage;
};

class TextureLibrary
{
    static std::unordered_map<std::string, Ref<Texture>> s_Textures;
public:
    static Ref<Texture> GetTexture(const std::string& name);
    static Ref<Texture> GetTexture(const std::string& name, const Ref<Image>& image);
};

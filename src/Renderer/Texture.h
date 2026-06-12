#pragma once

#include <Image.h>
#include <Renderer/ResourceHandle.h>
#include <unordered_map>
#include <string>
#include <vector>

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

    unsigned int m_RendererID = 0;
    Ref<Image> SourceImage;
};

class TextureLibrary
{
    struct TextureSlot
    {
        Ref<Texture> Resource;
        Ref<Image> SourceImage;
        std::string Name;
        uint32_t Generation = 1;
        bool Alive = false;
    };

    static std::unordered_map<std::string, TextureHandle> s_TextureHandles;
    static std::vector<TextureSlot> s_TextureSlots;
    static std::vector<uint32_t> s_FreeTextureSlots;

public:
    static TextureHandle LoadTexture(const std::string& name);
    static TextureHandle LoadTexture(const std::string& name, const Ref<Image>& image);
    static Ref<Texture> Resolve(TextureHandle handle);
    static bool IsValid(TextureHandle handle);
    static bool Release(TextureHandle handle);
    static bool Release(const std::string& name);

    static Ref<Texture> GetTexture(const std::string& name);
    static Ref<Texture> GetTexture(const std::string& name, const Ref<Image>& image);
};

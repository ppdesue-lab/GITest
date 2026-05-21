#pragma once

#include <Renderer/TextureArray.h>

class OpenGLTextureArray : public TextureArray
{
public:
    OpenGLTextureArray(uint32_t width, uint32_t height, uint32_t layers,
                       const std::vector<Ref<Image>>& images);
    virtual ~OpenGLTextureArray();

    virtual void Bind(uint32_t slot = 0) const override;
    virtual void Unbind() const override;

    virtual uint32_t GetWidth() const override { return m_Width; }
    virtual uint32_t GetHeight() const override { return m_Height; }
    virtual uint32_t GetLayerCount() const override { return m_Layers; }

private:
    uint32_t m_Width, m_Height, m_Layers;
    uint32_t m_RendererID = 0;
};

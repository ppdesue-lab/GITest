#pragma once

#include <Renderer/Texture.h>

class OpenGLTexture : public Texture
{
public:
    OpenGLTexture(Ref<Image> img=nullptr);
    explicit OpenGLTexture(const std::string& filepath, bool srgb = false);
    virtual ~OpenGLTexture();

    virtual uint32_t GetWidth() const override { return m_Width; }
    virtual uint32_t GetHeight() const override { return m_Height; }

    virtual void Bind(uint32_t slot = 0) const override;
    virtual void Unbind() const override;

private:
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
};

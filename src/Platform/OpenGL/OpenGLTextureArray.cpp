#include "stdsfx.h"
#include "OpenGLTextureArray.h"
#include <glad/glad.h>

OpenGLTextureArray::OpenGLTextureArray(uint32_t width, uint32_t height, uint32_t layers,
                                       const std::vector<Ref<Image>>& images)
    : m_Width(width), m_Height(height), m_Layers(layers)
{
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &m_RendererID);

    // Determine internal format from channel count
    GLenum internalFormat = GL_RGBA8;
    if (!images.empty() && images[0])
    {
        switch (images[0]->Channel)
        {
        case 1: internalFormat = GL_R8;     break;
        case 2: internalFormat = GL_RG8;    break;
        case 3: internalFormat = GL_RGB8;   break;
        default: internalFormat = GL_RGBA8; break;
        }
    }

    glTextureStorage3D(m_RendererID, 1, internalFormat, width, height, layers);

    glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // Upload each image as a layer
    for (uint32_t i = 0; i < layers && i < (uint32_t)images.size(); i++)
    {
        if (!images[i] || !images[i]->Data)
            continue;

        GLenum format = GL_RGBA;
        switch (images[i]->Channel)
        {
        case 1: format = GL_RED;  break;
        case 2: format = GL_RG;   break;
        case 3: format = GL_RGB;  break;
        default: format = GL_RGBA; break;
        }

        glTextureSubImage3D(m_RendererID, 0,
            0, 0, i,
            width, height, 1,
            format, GL_UNSIGNED_BYTE,
            images[i]->Data);
    }
}

OpenGLTextureArray::~OpenGLTextureArray()
{
    glDeleteTextures(1, &m_RendererID);
}

void OpenGLTextureArray::Bind(uint32_t slot) const
{
    glBindTextureUnit(slot, m_RendererID);
}

void OpenGLTextureArray::Unbind() const
{
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

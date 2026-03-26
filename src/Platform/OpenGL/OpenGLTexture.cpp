#include "stdsfx.h"
#include "OpenGLTexture.h"

#include <glad/glad.h>


OpenGLTexture::OpenGLTexture(Ref<Image> img)
{
    assert(img);
	//generate opengl texture with image
	glGenTextures(1, &m_RendererID);
    glBindTexture(GL_TEXTURE_2D, m_RendererID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img->Width, img->Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img->Data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glGenerateMipmap(GL_TEXTURE_2D);


    glBindTexture(GL_TEXTURE_2D,0);

    m_Width = img->Width;
    m_Height= img->Height;
    
};

OpenGLTexture::~OpenGLTexture()
{
    if(m_RendererID)
    glDeleteTextures(1, &m_RendererID);
}

void OpenGLTexture::Bind(uint32_t slot) const
{
	glActiveTexture(GL_TEXTURE0 + slot);
	glBindTexture(GL_TEXTURE_2D, m_RendererID);
}

void OpenGLTexture::Unbind() const
{
	glBindTexture(GL_TEXTURE_2D, 0);
}
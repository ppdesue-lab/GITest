#include "stdsfx.h"
#include "OpenGLTexture.h"

#include <glad/glad.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <vector>

namespace
{
uint32_t ReadLe32(const std::vector<uint8_t>& bytes, size_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1]) << 8u) |
        (static_cast<uint32_t>(bytes[offset + 2]) << 16u) |
        (static_cast<uint32_t>(bytes[offset + 3]) << 24u);
}

uint32_t FourCc(char a, char b, char c, char d)
{
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
        (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8u) |
        (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16u) |
        (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24u);
}

std::vector<uint8_t> ReadBinaryFile(const std::string& filepath)
{
    std::ifstream file(filepath, std::ios::binary);
    if (!file)
    {
        ERROR("Failed to open DDS texture: {}", filepath);
        return {};
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file)
    {
        ERROR("Failed to read DDS texture: {}", filepath);
        return {};
    }
    return bytes;
}

bool LoadDdsTexture2D(const std::string& filepath, bool srgb, uint32_t& rendererId, uint32_t& width, uint32_t& height)
{
    constexpr GLenum GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_VALUE = 0x83F1;
    constexpr GLenum GL_COMPRESSED_RGBA_S3TC_DXT3_EXT_VALUE = 0x83F2;
    constexpr GLenum GL_COMPRESSED_RGBA_S3TC_DXT5_EXT_VALUE = 0x83F3;
    constexpr GLenum GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT_VALUE = 0x8C4D;
    constexpr GLenum GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT_VALUE = 0x8C4E;
    constexpr GLenum GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT_VALUE = 0x8C4F;

    const std::vector<uint8_t> bytes = ReadBinaryFile(filepath);
    if (bytes.size() < 128 || ReadLe32(bytes, 0) != FourCc('D', 'D', 'S', ' '))
    {
        ERROR("Not a DDS texture: {}", filepath);
        return false;
    }

    height = ReadLe32(bytes, 12);
    width = ReadLe32(bytes, 16);
    const uint32_t mipCountRaw = ReadLe32(bytes, 28);
    const uint32_t pfFlags = ReadLe32(bytes, 80);
    const uint32_t fourCc = ReadLe32(bytes, 84);
    const uint32_t rgbBits = ReadLe32(bytes, 88);
    const uint32_t rMask = ReadLe32(bytes, 92);
    const uint32_t gMask = ReadLe32(bytes, 96);
    const uint32_t bMask = ReadLe32(bytes, 100);
    const uint32_t aMask = ReadLe32(bytes, 104);
    const uint32_t mipCount = std::max(1u, mipCountRaw);

    glGenTextures(1, &rendererId);
    glBindTexture(GL_TEXTURE_2D, rendererId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipCount > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(mipCount - 1));

    size_t offset = 128;
    uint32_t w = width;
    uint32_t h = height;
    if ((pfFlags & 0x4u) != 0u)
    {
        GLenum format = 0;
        uint32_t blockBytes = 0;
        if (fourCc == FourCc('D', 'X', 'T', '1'))
        {
            format = srgb ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT_VALUE : GL_COMPRESSED_RGBA_S3TC_DXT1_EXT_VALUE;
            blockBytes = 8;
        }
        else if (fourCc == FourCc('D', 'X', 'T', '3'))
        {
            format = srgb ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT_VALUE : GL_COMPRESSED_RGBA_S3TC_DXT3_EXT_VALUE;
            blockBytes = 16;
        }
        else if (fourCc == FourCc('D', 'X', 'T', '5'))
        {
            format = srgb ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT_VALUE : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT_VALUE;
            blockBytes = 16;
        }
        else
        {
            ERROR("Unsupported DDS compression in {}", filepath);
            return false;
        }

        for (uint32_t level = 0; level < mipCount; ++level)
        {
            const uint32_t blockWidth = std::max(1u, (w + 3u) / 4u);
            const uint32_t blockHeight = std::max(1u, (h + 3u) / 4u);
            const uint32_t levelSize = blockWidth * blockHeight * blockBytes;
            if (offset + levelSize > bytes.size())
            {
                ERROR("DDS mip data is truncated: {}", filepath);
                return false;
            }
            glCompressedTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), format,
                static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0,
                static_cast<GLsizei>(levelSize), bytes.data() + offset);
            offset += levelSize;
            w = std::max(1u, w / 2u);
            h = std::max(1u, h / 2u);
        }
    }
    else if (rgbBits == 32 && rMask == 0x00ff0000u && gMask == 0x0000ff00u &&
        bMask == 0x000000ffu && aMask == 0xff000000u)
    {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        for (uint32_t level = 0; level < mipCount; ++level)
        {
            const uint32_t levelSize = w * h * 4u;
            if (offset + levelSize > bytes.size())
            {
                ERROR("DDS mip data is truncated: {}", filepath);
                return false;
            }
            glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8,
                static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0, GL_BGRA, GL_UNSIGNED_BYTE, bytes.data() + offset);
            offset += levelSize;
            w = std::max(1u, w / 2u);
            h = std::max(1u, h / 2u);
        }
    }
    else if (rgbBits == 24 && rMask == 0x00ff0000u && gMask == 0x0000ff00u && bMask == 0x000000ffu)
    {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        for (uint32_t level = 0; level < mipCount; ++level)
        {
            const uint32_t levelSize = w * h * 3u;
            if (offset + levelSize > bytes.size())
            {
                ERROR("DDS mip data is truncated: {}", filepath);
                return false;
            }
            glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), srgb ? GL_SRGB8 : GL_RGB8,
                static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0, GL_BGR, GL_UNSIGNED_BYTE, bytes.data() + offset);
            offset += levelSize;
            w = std::max(1u, w / 2u);
            h = std::max(1u, h / 2u);
        }
    }
    else
    {
        ERROR("Unsupported DDS pixel format in {}", filepath);
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}
}


OpenGLTexture::OpenGLTexture(Ref<Image> img)
{
    assert(img);
	//generate opengl texture with image
	glGenTextures(1, &m_RendererID);
    glBindTexture(GL_TEXTURE_2D, m_RendererID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img->Width, img->Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img->Data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glGenerateMipmap(GL_TEXTURE_2D);


    glBindTexture(GL_TEXTURE_2D,0);

    m_Width = img->Width;
    m_Height= img->Height;
    
};

OpenGLTexture::OpenGLTexture(const std::string& filepath, bool srgb)
{
    if (!LoadDdsTexture2D(filepath, srgb, m_RendererID, m_Width, m_Height))
    {
        m_RendererID = 0;
        m_Width = 0;
        m_Height = 0;
    }
}

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

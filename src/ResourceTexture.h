#pragma once

#include "Resource.h"

#include <cstring>
#include <cmath>

// ============================================
// 纹理资源
// ============================================

enum class TextureFormat : uint8_t {
    R8 = 0,
    RG8 = 1,
    RGB8 = 2,
    RGBA8 = 3,
    R16F = 4,
    RGBA16F = 5,
    R32F = 6,
    RGBA32F = 7,
    BC1 = 8,
    BC3 = 9,
    BC5 = 10,
    BC7 = 11,
    Depth = 12,
    Stencil = 13
};


class ResourceTexture : public Resource<ResourceType::Texture> {
public:
    ResourceTexture() {
        m_metadata.type = ResourceType::Texture;
    }
    
    ~ResourceTexture() override {
        Unload();
    }
    
    bool Load(const std::string& path) override;
    
    void Unload() override {
        if (GetState() == ResourceState::Loaded) {
            std::cout << "[Texture] Unloading: " << GetPath() << std::endl;
            
            // 在实际引擎中：
            // 1. 释放GPU显存
            // 2. 删除纹理对象
            
            m_gpuHandle = 0;
            m_pixelData.clear();
            
            SetState(ResourceState::Unloaded);
        }
    }
    
    // 纹理特有接口
    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }
    TextureFormat GetFormat() const { return m_format; }
    uint32_t GetMipLevels() const { return m_mipLevels; }
    uint64_t GetGPUHandle() const { return m_gpuHandle; }
    
    
private:
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    TextureFormat m_format = TextureFormat::RGBA8;
    uint32_t m_mipLevels = 1;
    uint64_t m_gpuHandle = 0;
    std::vector<uint8_t> m_pixelData;
};


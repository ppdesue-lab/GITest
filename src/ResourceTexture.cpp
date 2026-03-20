#include "ResourceTexture.h"

//include stb image
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

bool ResourceTexture::Load(const std::string& path) {
        std::cout << "[Texture] Loading: " << path << std::endl;
        
        // 模拟加载过程
        // 在实际引擎中，这里会：
        // 1. 从文件系统读取图像文件
        // 2. 解析图像格式（PNG/JPG/DDS等）
        // 3. 上传数据到GPU
        // 4. 获取GPU句柄
        
        SetState(ResourceState::Loading);
        
        // 模拟IO延迟
        //std::this_thread::sleep_for(std::chrono::milliseconds(50));
        //load image
        int width, height, channels;
        stbi_set_flip_vertically_on_load(true);
        unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
        if (!data) {
            std::cerr << "Failed to load image: " << path << std::endl;
            return false;
        }
        
        
        // 模拟纹理数据
        m_width = width;
        m_height = height;
        m_format = TextureFormat::RGBA8;

        stbi_image_free(data);

        m_mipLevels = 1;
        m_gpuHandle = 0xDEADBEEF; // 模拟GPU句柄
        
        // 模拟纹理数据
        m_pixelData.resize(m_width * m_height * 4);
        SetSizeBytes(m_pixelData.size());
        
        std::cout << "[Texture] Loaded successfully: " << m_width << "x" << m_height 
                  << ", MipLevels: " << m_mipLevels << std::endl;
        
        return true;
    }
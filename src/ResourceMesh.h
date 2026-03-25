#pragma once


#include "Resource.h"
#include <glm/glm.hpp>


class ResourceMesh : public Resource<ResourceType::Mesh> {

public:
    ResourceMesh() {
        m_metadata.type = ResourceType::Mesh;
    }
    
    ~ResourceMesh() override {
        Unload();
    }
    
    bool Load(const std::string& path) override;
    
    void Unload() override {
        if (GetState() == ResourceState::Loaded) {
            std::cout << "[Mesh] Unloading: " << GetPath() << std::endl;
            
            // 在实际引擎中：
            // 1. 释放GPU显存
            // 2. 删除网格对象
            
            m_gpuHandle = 0;
            m_vertexData.clear();
            m_indexData.clear();
            
            SetState(ResourceState::Unloaded);
        }
    }
    
    // 网格特有接口
    uint32_t GetVertexCount() const { return m_vertexCount; }
    uint32_t GetIndexCount() const { return m_indexCount; }
    uint64_t GetGPUHandle() const { return m_gpuHandle; }
    


private:
    uint32_t m_vertexCount = 0;
    uint32_t m_indexCount = 0;
    uint64_t m_gpuHandle = 0;
    std::vector<uint8_t> m_vertexData;
    std::vector<uint8_t> m_indexData;
};

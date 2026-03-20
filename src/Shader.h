#pragma once

#include "Resource.h"
#include <string>
#include <unordered_map>
#include <vector>

enum ShaderType
{
    VERTEX,
    FRAGMENT,
    COMPUTE
};

class ResourceShader : public Resource<ResourceType::Shader> {
public:
    ResourceShader() {
        m_metadata.type = ResourceType::Shader;
    }
    
    ~ResourceShader() override {
        Unload();
    }
    
    bool Load(const std::string& path) override;
    
    void Unload() override {
        if (GetState() == ResourceState::Loaded) {
            std::cout << "[Shader] Unloading: " << GetPath() << std::endl;
            
            // In actual engine:
            // 1. Release GPU program
            // 2. Delete shader objects
            
            m_gpuHandle = 0;
            m_shaderSource.clear();
            m_uniformLocations.clear();
            
            SetState(ResourceState::Unloaded);
        }
    }
    
    // Shader-specific interface
    uint64_t GetGPUHandle() const { return m_gpuHandle; }
    const std::string& GetSource() const { return m_shaderSource; }
    ShaderType GetShaderType() const { return m_shaderType; }
    
    // Uniform location caching
    int32_t GetUniformLocation(const std::string& name) const {
        auto it = m_uniformLocations.find(name);
        if (it != m_uniformLocations.end()) {
            return it->second;
        }
        return -1;
    }
    
    void CacheUniformLocation(const std::string& name, int32_t location) {
        m_uniformLocations[name] = location;
    }
    
    std::string GetTypeName() const override {
        return "Shader";
    }
    
private:
    uint64_t m_gpuHandle = 0;
    std::string m_shaderSource;
    ShaderType m_shaderType = VERTEX;
    std::unordered_map<std::string, int32_t> m_uniformLocations;
};

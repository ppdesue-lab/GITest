#include "ResourceShader.h"
#include "Log.h"
#include <fstream>
#include <sstream>

bool ResourceShader::Load(const std::string& path) {
    SetPath(path);
    SetState(ResourceState::Loading);
    
    std::ifstream file(path);
    if (!file.is_open()) {
        ERROR("Failed to open shader file: {0}", path);
        SetState(ResourceState::Failed);
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    m_shaderSource = buffer.str();
    file.close();
    
    if (m_shaderSource.empty()) {
        ERROR("Shader file is empty: {0}", path);
        SetState(ResourceState::Failed);
        return false;
    }
    
    // Determine shader type from file extension
    std::string extension = path.substr(path.find_last_of(".") + 1);
    if (extension == "vert") {
        m_shaderType = VERTEX;
    } else if (extension == "frag") {
        m_shaderType = FRAGMENT;
    } else if (extension == "comp") {
        m_shaderType = COMPUTE;
    } else {
        WARN("Unknown shader extension: {0}, defaulting to VERTEX", extension);
        m_shaderType = VERTEX;
    }
    
    // In actual engine, compile shader here:
    // m_gpuHandle = CompileShader(m_shaderSource, m_shaderType);
    // if (m_gpuHandle == 0) {
    //     ERROR("Failed to compile shader: {0}", path);
    //     SetState(ResourceState::Failed);
    //     return false;
    // }
    
    SetSizeBytes(m_shaderSource.size());
    SetState(ResourceState::Loaded);
    
    INFO("Loaded shader: {0} ({1} bytes)", path, m_shaderSource.size());
    return true;
}

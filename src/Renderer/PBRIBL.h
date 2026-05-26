#pragma once

#include "base.h"
#include "Shader.h"
#include <string>

class PBRIBL
{
public:
    explicit PBRIBL(const std::string& hdrPath = "");
    ~PBRIBL();

    void Bind(const Ref<Shader>& shader) const;
    void BindEnvironment(const Ref<Shader>& shader) const;

    bool& Enabled() { return m_Enabled; }
    bool& DiffuseEnabled() { return m_DiffuseEnabled; }
    bool& SpecularEnabled() { return m_SpecularEnabled; }
    float& DiffuseIntensity() { return m_DiffuseIntensity; }
    float& SpecularIntensity() { return m_SpecularIntensity; }
    int& DebugMode() { return m_DebugMode; }

private:
    void BuildEnvironment(const std::string& hdrPath);

    bool m_Enabled = true;
    bool m_DiffuseEnabled = true;
    bool m_SpecularEnabled = true;
    float m_DiffuseIntensity = 1.0f;
    float m_SpecularIntensity = 1.0f;
    int m_DebugMode = 0;
    uint32_t m_EnvironmentMap = 0;
    uint32_t m_IrradianceMap = 0;
    uint32_t m_PrefilterMap = 0;
    uint32_t m_BRDFLUT = 0;
    uint32_t m_CaptureFBO = 0;
    uint32_t m_CubeVAO = 0;
    uint32_t m_CubeVBO = 0;
    uint32_t m_QuadVAO = 0;
};

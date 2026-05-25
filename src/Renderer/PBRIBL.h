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

private:
    void BuildEnvironment(const std::string& hdrPath);

    uint32_t m_EnvironmentMap = 0;
    uint32_t m_IrradianceMap = 0;
    uint32_t m_PrefilterMap = 0;
    uint32_t m_BRDFLUT = 0;
    uint32_t m_CaptureFBO = 0;
    uint32_t m_CubeVAO = 0;
    uint32_t m_CubeVBO = 0;
    uint32_t m_QuadVAO = 0;
};

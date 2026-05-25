#pragma once

#include "base.h"
#include "Shader.h"

#include <glm/glm.hpp>
#include <vector>

class SSAO
{
public:
    SSAO(uint32_t width, uint32_t height);
    ~SSAO();

    void Resize(uint32_t width, uint32_t height);
    void Render(uint64_t colorTexture, uint64_t positionTexture, uint64_t normalTexture,
        const glm::mat4& view, const glm::mat4& projection);

    bool& Enabled() { return m_Enabled; }
    float& Radius() { return m_Radius; }
    float& Bias() { return m_Bias; }
    float& Strength() { return m_Strength; }
    int& DebugMode() { return m_DebugMode; }
    uint64_t GetAOTexture() const { return m_BlurTexture; }
    uint64_t GetOutputTexture() const { return m_OutputTexture; }

private:
    void Invalidate();

    bool m_Enabled = true;
    float m_Radius = 5.0f;
    float m_Bias = 0.04f;
    float m_Strength = 1.3f;
    int m_DebugMode = 0;
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    uint32_t m_QuadVAO = 0;
    uint32_t m_NoiseTexture = 0;
    uint32_t m_AOFBO = 0;
    uint32_t m_AOTexture = 0;
    uint32_t m_BlurFBO = 0;
    uint32_t m_BlurTexture = 0;
    uint32_t m_OutputFBO = 0;
    uint32_t m_OutputTexture = 0;
    Ref<Shader> m_AOShader;
    Ref<Shader> m_BlurShader;
    Ref<Shader> m_CompositeShader;
    std::vector<glm::vec3> m_SampleKernel;
};

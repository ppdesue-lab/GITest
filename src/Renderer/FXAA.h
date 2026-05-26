#pragma once

#include "base.h"
#include "Shader.h"

#include <cstdint>

class FXAA
{
public:
    FXAA(uint32_t width, uint32_t height);
    ~FXAA();

    void Resize(uint32_t width, uint32_t height);
    void Render(uint64_t colorTexture);

    bool& Enabled() { return m_Enabled; }
    float& EdgeThreshold() { return m_EdgeThreshold; }
    float& EdgeThresholdMin() { return m_EdgeThresholdMin; }
    float& SubpixelQuality() { return m_SubpixelQuality; }
    float& SpanMax() { return m_SpanMax; }
    int& DebugMode() { return m_DebugMode; }
    uint64_t GetOutputTexture() const { return m_OutputTexture; }

private:
    void Invalidate();

    bool m_Enabled = true;
    float m_EdgeThreshold = 0.0833f;
    float m_EdgeThresholdMin = 0.0156f;
    float m_SubpixelQuality = 1.0f;
    float m_SpanMax = 8.0f;
    int m_DebugMode = 0;
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    uint32_t m_QuadVAO = 0;
    uint32_t m_OutputFBO = 0;
    uint32_t m_OutputTexture = 0;
    Ref<Shader> m_Shader;
};

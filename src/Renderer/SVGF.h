#pragma once

#include <base.h>
#include <cstdint>

class Camera;
class PathTracer;

class SVGF
{
public:
    SVGF(uint32_t width, uint32_t height);
    ~SVGF();

    void Resize(uint32_t width, uint32_t height);
    void Render(const PathTracer& tracer, const Camera& camera);
    void ResetHistory();

    bool& Enabled() { return m_Enabled; }
    int& Iterations() { return m_Iterations; }
    float& HistoryAlpha() { return m_HistoryAlpha; }
    float& PhiColor() { return m_PhiColor; }
    float& PhiNormal() { return m_PhiNormal; }
    float& PhiDepth() { return m_PhiDepth; }

    uint64_t GetOutputTexture() const;
    bool HasHistory() const;

private:
    struct Impl;
    Scope<Impl> m_Impl;

    bool m_Enabled = false;
    int m_Iterations = 4;
    float m_HistoryAlpha = 0.92f;
    float m_PhiColor = 4.0f;
    float m_PhiNormal = 128.0f;
    float m_PhiDepth = 1.0f;
};

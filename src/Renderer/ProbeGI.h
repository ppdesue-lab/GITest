#pragma once

#include "base.h"
#include "CSM.h"
#include "Shader.h"

#include <glm/glm.hpp>
#include <vector>

class ProbeGI
{
public:
    static constexpr int MaxProbeCount = 64;

    ProbeGI();

    void Update(const DirectionalLight& light);
    void Bind(const Ref<Shader>& shader) const;
    void DrawDebug() const;

    bool& Enabled() { return m_Enabled; }
    bool& ShowProbes() { return m_ShowProbes; }
    float& Intensity() { return m_Intensity; }
    int& DebugMode() { return m_DebugMode; }
    glm::vec3& Origin() { return m_Origin; }
    float& Spacing() { return m_Spacing; }
    glm::ivec3 GetCounts() const { return m_Counts; }
    void SetCounts(const glm::ivec3& counts);
    int GetProbeCount() const { return (int)m_Irradiance.size(); }

private:
    void Rebuild();

    bool m_Enabled = true;
    bool m_ShowProbes = false;
    float m_Intensity = 0.85f;
    int m_DebugMode = 0;
    glm::vec3 m_Origin = glm::vec3(-30.0f, 3.0f, -30.0f);
    float m_Spacing = 20.0f;
    glm::ivec3 m_Counts = glm::ivec3(4, 3, 4);
    std::vector<glm::vec3> m_Positions;
    std::vector<glm::vec3> m_Irradiance;
};

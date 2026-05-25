#include "stdsfx.h"
#include "ProbeGI.h"

#include <Renderer/RenderCommand.h>
#include <glm/common.hpp>

ProbeGI::ProbeGI()
{
    Rebuild();
}

void ProbeGI::SetCounts(const glm::ivec3& counts)
{
    glm::ivec3 sanitized = glm::clamp(counts, glm::ivec3(1), glm::ivec3(4));
    if (sanitized != m_Counts)
    {
        m_Counts = sanitized;
        Rebuild();
    }
}

void ProbeGI::Rebuild()
{
    m_Positions.clear();
    m_Irradiance.clear();
    m_Positions.reserve(m_Counts.x * m_Counts.y * m_Counts.z);
    m_Irradiance.resize(m_Counts.x * m_Counts.y * m_Counts.z, glm::vec3(0.0f));

    for (int z = 0; z < m_Counts.z; ++z)
        for (int y = 0; y < m_Counts.y; ++y)
            for (int x = 0; x < m_Counts.x; ++x)
                m_Positions.push_back(m_Origin + glm::vec3(x, y, z) * m_Spacing);
}

void ProbeGI::Update(const DirectionalLight& light)
{
    if ((int)m_Positions.size() != m_Counts.x * m_Counts.y * m_Counts.z)
        Rebuild();

    glm::vec3 normalizedLight = glm::normalize(-light.Direction);
    float sunFromAbove = glm::clamp(normalizedLight.y, 0.0f, 1.0f);
    float top = glm::max(float(m_Counts.y - 1), 1.0f);
    const glm::vec3 skyAmbient(0.10f, 0.13f, 0.19f);
    const glm::vec3 warmBounce(0.20f, 0.15f, 0.09f);

    for (int z = 0; z < m_Counts.z; ++z)
    {
        for (int y = 0; y < m_Counts.y; ++y)
        {
            for (int x = 0; x < m_Counts.x; ++x)
            {
                int index = x + m_Counts.x * (y + m_Counts.y * z);
                m_Positions[index] = m_Origin + glm::vec3(x, y, z) * m_Spacing;
                float height = y / top;
                float directional = 0.08f + sunFromAbove * light.Intensity * 0.18f;
                m_Irradiance[index] = skyAmbient * (0.65f + height * 0.45f)
                    + light.Color * directional
                    + warmBounce * (1.0f - height) * sunFromAbove;
            }
        }
    }
}

void ProbeGI::Bind(const Ref<Shader>& shader) const
{
    if (!shader)
        return;

    shader->SetInt("u_giEnabled", m_Enabled ? 1 : 0);
    shader->SetInt("u_giDebugMode", m_DebugMode);
    shader->SetFloat("u_giIntensity", m_Intensity);
    shader->SetFloat3("u_giOrigin", m_Origin);
    shader->SetFloat("u_giSpacing", glm::max(m_Spacing, 0.01f));
    shader->SetFloat3("u_giCounts", glm::vec3(m_Counts));
    if (!m_Irradiance.empty())
        shader->SetVec3Array("u_probeIrradiance", (float*)&m_Irradiance[0].x, (uint32_t)m_Irradiance.size());
}

void ProbeGI::DrawDebug() const
{
    if (!m_Enabled || !m_ShowProbes)
        return;

    float radius = glm::clamp(m_Spacing * 0.07f, 0.3f, 1.2f);
    for (size_t i = 0; i < m_Positions.size(); ++i)
    {
        glm::vec3 c = glm::clamp(m_Irradiance[i] * 2.0f, glm::vec3(0.08f), glm::vec3(1.0f));
        glm::vec4 color(c, 1.0f);
        const glm::vec3& p = m_Positions[i];
        RenderCommand::FlushLine(p - glm::vec3(radius, 0, 0), p + glm::vec3(radius, 0, 0), color);
        RenderCommand::FlushLine(p - glm::vec3(0, radius, 0), p + glm::vec3(0, radius, 0), color);
        RenderCommand::FlushLine(p - glm::vec3(0, 0, radius), p + glm::vec3(0, 0, radius), color);
    }
}

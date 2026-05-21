#pragma once

#include "base.h"
#include <glm/glm.hpp>
#include <vector>

struct DirectionalLight
{
    glm::vec3 Direction = glm::vec3(0.5f, -1.0f, 0.3f);
    glm::vec3 Color = glm::vec3(1.0f, 1.0f, 1.0f);
    float Intensity = 1.0f;
};

class CSM
{
public:
    CSM(uint32_t cascadeCount = 3, uint32_t shadowMapSize = 2048, float splitLambda = 0.95f);
    ~CSM();

    void Update(const glm::mat4& view, const glm::mat4& proj, float nearPlane, float farPlane);

    void BeginShadowPass(uint32_t cascadeIndex);
    void EndShadowPass();

    void BindShadowTexture(uint32_t slot = 1) const;
    void UnbindShadowTexture() const;
    void SaveShadowMap(const std::string& filepath, uint32_t cascadeIndex = 0) const;
    uint32_t GetDebugTextureID(uint32_t cascadeIndex = 0);
    void UpdateDebugTexture(uint32_t cascadeIndex = 0);

    const std::vector<glm::mat4>& GetLightViewProjMatrices() const { return m_LightViewProj; }
    const std::vector<float>& GetCascadeDistances() const { return m_CascadeDistances; }
    uint32_t GetCascadeCount() const { return m_CascadeCount; }
    uint32_t GetShadowMapSize() const { return m_ShadowMapSize; }
    float GetSplitLambda() const { return m_SplitLambda; }
    void SetSplitLambda(float splitLambda) { m_SplitLambda = glm::clamp(splitLambda, 0.0f, 1.0f); }
    float GetMaxShadowDistance() const { return m_MaxShadowDistance; }
    void SetMaxShadowDistance(float distance) { m_MaxShadowDistance = glm::max(distance, 1.0f); }
    const DirectionalLight& GetLight() const { return m_Light; }
    DirectionalLight& GetLight() { return m_Light; }

private:
    void ComputeCascades(const glm::mat4& view, const glm::mat4& proj, float nearPlane, float farPlane);

    uint32_t m_CascadeCount;
    uint32_t m_ShadowMapSize;
    float m_SplitLambda;
    float m_MaxShadowDistance = 500.0f;
    DirectionalLight m_Light;

    std::vector<float> m_CascadeDistances;
    std::vector<glm::mat4> m_LightViewProj;

    // OpenGL resources
    uint32_t m_ShadowTextureID = 0;
    uint32_t m_ShadowFBO = 0;
    // Debug visualisation
    uint32_t m_DebugTextureID = 0;
    mutable bool m_DebugDirty = true;
};

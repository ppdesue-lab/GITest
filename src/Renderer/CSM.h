#pragma once

#include "base.h"
#include <glm/glm.hpp>
#include <vector>

#ifdef G_DX11
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11.h>
#include <wrl/client.h>
#endif

struct DirectionalLight
{
    glm::vec3 Direction = glm::vec3(0.6428f, -0.4178f, -0.6428f);
    glm::vec3 Color = glm::vec3(1.0f, 1.0f, 1.0f);
    float Intensity = 1.0f;
};

class CSM
{
public:
    CSM(uint32_t cascadeCount = 3, uint32_t shadowMapSize = 2048, float splitLambda = 0.65f);
    ~CSM();

    void Update(const glm::mat4& view, const glm::mat4& proj, float nearPlane, float farPlane);

    void BeginShadowPass(uint32_t cascadeIndex);
    void EndShadowPass();

    void BindShadowTexture(uint32_t slot = 1) const;
    void UnbindShadowTexture() const;
    void SaveShadowMap(const std::string& filepath, uint32_t cascadeIndex = 0) const;
    uint64_t GetDebugTextureID(uint32_t cascadeIndex = 0);
    void UpdateDebugTexture(uint32_t cascadeIndex = 0);

    const std::vector<glm::mat4>& GetLightViewProjMatrices() const { return m_LightViewProj; }
    const std::vector<float>& GetCascadeDistances() const { return m_CascadeDistances; }
    const std::vector<float>& GetCascadeTexelSizes() const { return m_CascadeTexelSizes; }
    uint32_t GetCascadeCount() const { return m_CascadeCount; }
    uint32_t GetShadowMapSize() const { return m_ShadowMapSize; }
    float GetSplitLambda() const { return m_SplitLambda; }
    void SetSplitLambda(float splitLambda) { m_SplitLambda = glm::clamp(splitLambda, 0.0f, 1.0f); }
    float GetMaxShadowDistance() const { return m_MaxShadowDistance; }
    void SetMaxShadowDistance(float distance) { m_MaxShadowDistance = glm::max(distance, 1.0f); }
    float& ConstantBias() { return m_ConstantBias; }
    float& SlopeBias() { return m_SlopeBias; }
    float& PolygonOffsetFactor() { return m_PolygonOffsetFactor; }
    float& PolygonOffsetUnits() { return m_PolygonOffsetUnits; }
    bool& Enabled() { return m_Enabled; }
    const DirectionalLight& GetLight() const { return m_Light; }
    DirectionalLight& GetLight() { return m_Light; }

private:
    void ComputeCascades(const glm::mat4& view, const glm::mat4& proj, float nearPlane, float farPlane);

    uint32_t m_CascadeCount;
    uint32_t m_ShadowMapSize;
    float m_SplitLambda;
    float m_MaxShadowDistance = 500.0f;
    float m_ConstantBias = 0.00012f;
    float m_SlopeBias = 0.00075f;
    float m_PolygonOffsetFactor = 2.0f;
    float m_PolygonOffsetUnits = 4.0f;
    bool m_Enabled = true;
    DirectionalLight m_Light;

    std::vector<float> m_CascadeDistances;
    std::vector<float> m_CascadeTexelSizes;
    std::vector<glm::mat4> m_LightViewProj;

    // OpenGL resources
    uint32_t m_ShadowTextureID = 0;
    uint32_t m_ShadowFBO = 0;
    // Debug visualisation
    uint32_t m_DebugTextureID = 0;
    mutable bool m_DebugDirty = true;

#ifdef G_DX11
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_ShadowTextureArray;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_ShadowSRV;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_ShadowSampler;
    std::vector<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>> m_CascadeDSVs;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_ShadowRasterizer;
    // Debug
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DebugTexture2D;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_DebugSRV;
    // Saved state for EndShadowPass
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_PrevRTV;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_PrevDSV;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_PrevRasterizer;
    D3D11_VIEWPORT m_PrevViewport{};
    UINT m_PrevViewportCount = 0;
    bool m_ShadowPassActive = false;
#endif
};

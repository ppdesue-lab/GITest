#pragma once

#include "base.h"
#include "Shader.h"
#include <string>
#ifdef G_DX11
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <wrl/client.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

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
#ifdef G_DX11
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DX11EnvironmentTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_DX11EnvironmentSRV;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_DX11EnvironmentSampler;
#endif
};

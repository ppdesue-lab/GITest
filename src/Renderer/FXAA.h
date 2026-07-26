#pragma once

#include "base.h"
#include "Shader.h"

#include <cstdint>

#ifdef G_DX11
#include <d3d11.h>
#include <wrl/client.h>
#endif

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
    uint64_t GetOutputTexture() const;

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

#ifdef G_DX11
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DX11OutputTexture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_DX11OutputRTV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_DX11OutputSRV;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_DX11VertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_DX11PixelShader;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_DX11ConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_DX11Sampler;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_DX11NoBlend;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_DX11NoDepth;
    bool m_DX11Ready = false;
#endif
};

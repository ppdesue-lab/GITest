#pragma once

#include "base.h"
#include "Shader.h"

#include <glm/glm.hpp>
#include <vector>

#ifdef G_DX11
#include <d3d11.h>
#include <wrl/client.h>
#endif

class SSAO
{
public:
    enum class Algorithm
    {
        KernelSSAO = 0,
        SSAO11HBAO = 1
    };

    SSAO(uint32_t width, uint32_t height);
    ~SSAO();

    void Resize(uint32_t width, uint32_t height);
    void Render(uint64_t colorTexture, uint64_t depthTexture, uint64_t normalTexture,
        const glm::mat4& view, const glm::mat4& projection);

    bool& Enabled() { return m_Enabled; }
    float& Radius() { return m_Radius; }
    float& Bias() { return m_Bias; }
    float& Strength() { return m_Strength; }
    Algorithm& CurrentAlgorithm() { return m_Algorithm; }
    int& StepSize() { return m_StepSize; }
    float& AngleBiasDegrees() { return m_AngleBiasDegrees; }
    float& PowerExponent() { return m_PowerExponent; }
    int& BlurRadius() { return m_BlurRadius; }
    float& BlurSharpness() { return m_BlurSharpness; }
    float& MaxRadiusPixels() { return m_MaxRadiusPixels; }
    int& DebugMode() { return m_DebugMode; }
    uint64_t GetAOTexture() const;
    uint64_t GetOutputTexture() const;

private:
    void Invalidate();
    void RenderKernelSSAO(uint64_t colorTexture, uint64_t depthTexture, uint64_t normalTexture,
        const glm::mat4& view, const glm::mat4& projection);
    void RenderHBAO(uint64_t colorTexture, uint64_t depthTexture, const glm::mat4& projection);
    void RenderHBAOBlur(uint32_t fbo, uint32_t inputTexture, float dx, float dy);

    bool m_Enabled = true;
    Algorithm m_Algorithm = Algorithm::KernelSSAO;
    float m_Radius = 5.0f;
    float m_Bias = 0.04f;
    float m_Strength = 1.3f;
    int m_StepSize = 4;
    float m_AngleBiasDegrees = 10.0f;
    float m_PowerExponent = 1.5f;
    int m_BlurRadius = 16;
    float m_BlurSharpness = 8.0f;
    float m_MaxRadiusPixels = 128.0f;
    int m_DebugMode = 0;
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    float m_AOScale = 0.5f;
    uint32_t m_AOWidth = 0;
    uint32_t m_AOHeight = 0;
    uint32_t m_QuadVAO = 0;
    uint32_t m_NoiseTexture = 0;
    uint32_t m_HBAORandomTexture = 0;
    uint32_t m_AOFBO = 0;
    uint32_t m_AOTexture = 0;
    uint32_t m_BlurFBO = 0;
    uint32_t m_BlurTexture = 0;
    uint32_t m_OutputFBO = 0;
    uint32_t m_OutputTexture = 0;
    uint32_t m_HBAOFBO = 0;
    uint32_t m_HBAOTexture = 0;
    uint32_t m_HBAOBlurFBO = 0;
    uint32_t m_HBAOBlurTexture = 0;
    Ref<Shader> m_AOShader;
    Ref<Shader> m_BlurShader;
    Ref<Shader> m_CompositeShader;
    Ref<Shader> m_HBAOShader;
    Ref<Shader> m_HBAOBlurShader;
    Ref<Shader> m_HBAOCompositeShader;
    std::vector<glm::vec3> m_SampleKernel;

#ifdef G_DX11
    void CreateDX11Resources();
    void DestroyDX11Resources();
    void RenderDX11KernelSSAO(uint64_t colorTexture, uint64_t depthTexture, uint64_t normalTexture,
        const glm::mat4& view, const glm::mat4& projection);
    void RenderDX11HBAO(uint64_t colorTexture, uint64_t depthTexture, const glm::mat4& projection);
    void DX11DrawFullscreenQuad(uint32_t width, uint32_t height, ID3D11SamplerState* sampler);

    // AO textures
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DX11AOTexture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_DX11AORTV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_DX11AOSRV;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DX11BlurTexture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_DX11BlurRTV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_DX11BlurSRV;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DX11OutputTexture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_DX11OutputRTV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_DX11OutputSRV;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DX11HBAOTexture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_DX11HBAORTV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_DX11HBAOSRV;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DX11HBAOBlurTexture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_DX11HBAOBlurRTV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_DX11HBAOBlurSRV;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DX11NoiseTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_DX11NoiseSRV;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DX11HBAORandomTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_DX11HBAORandomSRV;

    // Shaders (compiled directly, not through DX11Shader)
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_DX11VS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_DX11AOPS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_DX11BlurPS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_DX11CompositePS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_DX11HBAOPS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_DX11HBAOBlurPS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_DX11HBAOCompositePS;

    // Constant buffers
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_DX11AOCBuffer;     // AO kernel uniforms
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_DX11BlurCBuffer;   // Blur uniforms
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_DX11CompositeCBuffer; // Composite uniforms
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_DX11HBAOCBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_DX11HBAOBlurCBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_DX11HBAOCompositeCBuffer;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_DX11PointSampler;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_DX11LinearSampler;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_DX11InputLayout;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_DX11NoBlend;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_DX11NoDepth;
    bool m_DX11Ready = false;
    float m_DX11AOScale = 0.5f;
    uint32_t m_DX11AOWidth = 0;
    uint32_t m_DX11AOHeight = 0;
#endif
};

#include "stdsfx.h"
#include "FXAA.h"

#ifdef G_OPENGL
#include <glad/glad.h>
#elif defined(G_DX11)
#include <Platform/DX11/DX11Context.h>
#include <d3dcompiler.h>
#endif

#ifdef G_DX11
namespace
{
const char* s_FXAAVertexShader = R"(
struct VSOut
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

VSOut VSMain(uint vertexID : SV_VertexID)
{
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };
    VSOut output;
    float2 position = positions[vertexID];
    output.Position = float4(position, 0.0, 1.0);
    output.UV = float2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
    return output;
}
)";

const char* s_FXAAPixelShader = R"(
Texture2D<float4> u_SceneColor : register(t0);
SamplerState u_Sampler : register(s0);

cbuffer FXAAConstants : register(b0)
{
    float2 u_InvResolution;
    float u_EdgeThreshold;
    float u_EdgeThresholdMin;
    float u_SubpixelQuality;
    float u_SpanMax;
    int u_DebugMode;
    float u_Padding;
};

struct VSOut
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

float Luma(float3 color)
{
    return dot(color, float3(0.299, 0.587, 0.114));
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float4 center = u_SceneColor.Sample(u_Sampler, input.UV);
    float3 rgbN = u_SceneColor.Sample(u_Sampler, input.UV + float2(0.0, -u_InvResolution.y)).rgb;
    float3 rgbS = u_SceneColor.Sample(u_Sampler, input.UV + float2(0.0,  u_InvResolution.y)).rgb;
    float3 rgbW = u_SceneColor.Sample(u_Sampler, input.UV + float2(-u_InvResolution.x, 0.0)).rgb;
    float3 rgbE = u_SceneColor.Sample(u_Sampler, input.UV + float2( u_InvResolution.x, 0.0)).rgb;
    float3 rgbNW = u_SceneColor.Sample(u_Sampler, input.UV + float2(-u_InvResolution.x, -u_InvResolution.y)).rgb;
    float3 rgbNE = u_SceneColor.Sample(u_Sampler, input.UV + float2( u_InvResolution.x, -u_InvResolution.y)).rgb;
    float3 rgbSW = u_SceneColor.Sample(u_Sampler, input.UV + float2(-u_InvResolution.x,  u_InvResolution.y)).rgb;
    float3 rgbSE = u_SceneColor.Sample(u_Sampler, input.UV + float2( u_InvResolution.x,  u_InvResolution.y)).rgb;

    float lumaM = Luma(center.rgb);
    float lumaN = Luma(rgbN);
    float lumaS = Luma(rgbS);
    float lumaW = Luma(rgbW);
    float lumaE = Luma(rgbE);
    float lumaNW = Luma(rgbNW);
    float lumaNE = Luma(rgbNE);
    float lumaSW = Luma(rgbSW);
    float lumaSE = Luma(rgbSE);
    float lumaMin = min(lumaM, min(min(min(lumaN, lumaS), min(lumaW, lumaE)),
        min(min(lumaNW, lumaNE), min(lumaSW, lumaSE))));
    float lumaMax = max(lumaM, max(max(max(lumaN, lumaS), max(lumaW, lumaE)),
        max(max(lumaNW, lumaNE), max(lumaSW, lumaSE))));
    float lumaRange = lumaMax - lumaMin;

    if (lumaRange < max(u_EdgeThresholdMin, lumaMax * u_EdgeThreshold))
        return u_DebugMode == 1 ? float4(0.0, 0.0, 0.0, 1.0) : center;

    float2 direction = float2(
        -((lumaNW + lumaNE) - (lumaSW + lumaSE)),
        (lumaNW + lumaSW) - (lumaNE + lumaSE));
    float reduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125, 1.0 / 128.0);
    float inverseMinimum = 1.0 / (min(abs(direction.x), abs(direction.y)) + reduce);
    direction = clamp(direction * inverseMinimum,
        float2(-u_SpanMax, -u_SpanMax), float2(u_SpanMax, u_SpanMax));
    direction *= u_InvResolution;

    float3 rgbA = 0.5 * (
        u_SceneColor.Sample(u_Sampler, input.UV + direction * (1.0 / 3.0 - 0.5)).rgb +
        u_SceneColor.Sample(u_Sampler, input.UV + direction * (2.0 / 3.0 - 0.5)).rgb);
    float3 rgbB = rgbA * 0.5 + 0.25 * (
        u_SceneColor.Sample(u_Sampler, input.UV + direction * -0.5).rgb +
        u_SceneColor.Sample(u_Sampler, input.UV + direction *  0.5).rgb);
    float lumaB = Luma(rgbB);
    float3 filtered = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
    float3 result = lerp(center.rgb, filtered, u_SubpixelQuality);
    if (u_DebugMode == 1)
    {
        float edge = saturate(lumaRange * 4.0);
        return float4(edge, edge, edge, 1.0);
    }
    if (u_DebugMode == 2)
        return float4(abs(result - center.rgb) * 12.0, 1.0);
    return float4(result, center.a);
}
)";

Microsoft::WRL::ComPtr<ID3DBlob> CompileFXAAShader(const char* source, const char* entryPoint, const char* target)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG;
#endif
    Microsoft::WRL::ComPtr<ID3DBlob> shader;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT result = D3DCompile(source, strlen(source), "FXAA", nullptr, nullptr,
        entryPoint, target, flags, 0, &shader, &errors);
    if (FAILED(result))
    {
        if (errors)
            ERROR("DX11 FXAA shader compile error: {}", (const char*)errors->GetBufferPointer());
        else
            ERROR("DX11 FXAA shader compilation failed");
        return nullptr;
    }
    return shader;
}
}
#endif

FXAA::FXAA(uint32_t width, uint32_t height)
    : m_Width(width), m_Height(height)
{
#ifdef G_OPENGL
    const std::string fullscreenVertex = R"(
        #version 410 core
        out vec2 v_UV;
        void main()
        {
            vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
            v_UV = p;
            gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
        }
    )";
    const std::string fxaaFragment = R"(
        #version 410 core
        in vec2 v_UV;
        layout(location = 0) out vec4 o_Color;
        uniform sampler2D u_SceneColor;
        uniform vec2 u_InvResolution;
        uniform float u_EdgeThreshold;
        uniform float u_EdgeThresholdMin;
        uniform float u_SubpixelQuality;
        uniform float u_SpanMax;
        uniform int u_DebugMode;

        float Luma(vec3 color)
        {
            return dot(color, vec3(0.299, 0.587, 0.114));
        }

        void main()
        {
            vec4 center = texture(u_SceneColor, v_UV);
            vec3 rgbN = texture(u_SceneColor, v_UV + vec2(0.0, -u_InvResolution.y)).rgb;
            vec3 rgbS = texture(u_SceneColor, v_UV + vec2(0.0, u_InvResolution.y)).rgb;
            vec3 rgbW = texture(u_SceneColor, v_UV + vec2(-u_InvResolution.x, 0.0)).rgb;
            vec3 rgbE = texture(u_SceneColor, v_UV + vec2(u_InvResolution.x, 0.0)).rgb;
            vec3 rgbNW = texture(u_SceneColor, v_UV + vec2(-u_InvResolution.x, -u_InvResolution.y)).rgb;
            vec3 rgbNE = texture(u_SceneColor, v_UV + vec2(u_InvResolution.x, -u_InvResolution.y)).rgb;
            vec3 rgbSW = texture(u_SceneColor, v_UV + vec2(-u_InvResolution.x, u_InvResolution.y)).rgb;
            vec3 rgbSE = texture(u_SceneColor, v_UV + vec2(u_InvResolution.x, u_InvResolution.y)).rgb;

            float lumaM = Luma(center.rgb);
            float lumaN = Luma(rgbN);
            float lumaS = Luma(rgbS);
            float lumaW = Luma(rgbW);
            float lumaE = Luma(rgbE);
            float lumaNW = Luma(rgbNW);
            float lumaNE = Luma(rgbNE);
            float lumaSW = Luma(rgbSW);
            float lumaSE = Luma(rgbSE);
            float lumaMin = min(lumaM, min(min(min(lumaN, lumaS), min(lumaW, lumaE)),
                min(min(lumaNW, lumaNE), min(lumaSW, lumaSE))));
            float lumaMax = max(lumaM, max(max(max(lumaN, lumaS), max(lumaW, lumaE)),
                max(max(lumaNW, lumaNE), max(lumaSW, lumaSE))));
            float lumaRange = lumaMax - lumaMin;

            if (lumaRange < max(u_EdgeThresholdMin, lumaMax * u_EdgeThreshold))
            {
                o_Color = u_DebugMode == 1 ? vec4(0.0, 0.0, 0.0, 1.0) : center;
                return;
            }

            vec2 direction = vec2(
                -((lumaNW + lumaNE) - (lumaSW + lumaSE)),
                (lumaNW + lumaSW) - (lumaNE + lumaSE));
            float reduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125, 1.0 / 128.0);
            float inverseMinimum = 1.0 / (min(abs(direction.x), abs(direction.y)) + reduce);
            direction = clamp(direction * inverseMinimum, vec2(-u_SpanMax), vec2(u_SpanMax));
            direction *= u_InvResolution;

            vec3 rgbA = 0.5 * (
                texture(u_SceneColor, v_UV + direction * (1.0 / 3.0 - 0.5)).rgb +
                texture(u_SceneColor, v_UV + direction * (2.0 / 3.0 - 0.5)).rgb);
            vec3 rgbB = rgbA * 0.5 + 0.25 * (
                texture(u_SceneColor, v_UV + direction * -0.5).rgb +
                texture(u_SceneColor, v_UV + direction * 0.5).rgb);
            float lumaB = Luma(rgbB);
            vec3 filtered = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
            vec3 result = mix(center.rgb, filtered, u_SubpixelQuality);
            if (u_DebugMode == 1)
            {
                o_Color = vec4(vec3(clamp(lumaRange * 4.0, 0.0, 1.0)), 1.0);
                return;
            }
            if (u_DebugMode == 2)
            {
                o_Color = vec4(abs(result - center.rgb) * 12.0, 1.0);
                return;
            }
            o_Color = vec4(result, center.a);
        }
    )";
    m_Shader = Shader::Create("FXAA", fullscreenVertex, fxaaFragment);
    glCreateVertexArrays(1, &m_QuadVAO);
    Invalidate();
#elif defined(G_DX11)
    Invalidate();
#endif
}

FXAA::~FXAA()
{
#ifdef G_OPENGL
    glDeleteVertexArrays(1, &m_QuadVAO);
    glDeleteFramebuffers(1, &m_OutputFBO);
    glDeleteTextures(1, &m_OutputTexture);
#endif
}

void FXAA::Resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || (width == m_Width && height == m_Height))
        return;
    m_Width = width;
    m_Height = height;
    Invalidate();
}

void FXAA::Invalidate()
{
#ifdef G_OPENGL
    if (m_OutputFBO)
    {
        glDeleteFramebuffers(1, &m_OutputFBO);
        glDeleteTextures(1, &m_OutputTexture);
    }

    glCreateFramebuffers(1, &m_OutputFBO);
    glCreateTextures(GL_TEXTURE_2D, 1, &m_OutputTexture);
    glTextureStorage2D(m_OutputTexture, 1, GL_RGBA16F, m_Width, m_Height);
    glTextureParameteri(m_OutputTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_OutputTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_OutputTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_OutputTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_OutputFBO, GL_COLOR_ATTACHMENT0, m_OutputTexture, 0);
    if (glCheckNamedFramebufferStatus(m_OutputFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        ERROR("FXAA framebuffer is incomplete!");
#elif defined(G_DX11)
    m_DX11Ready = false;
    m_DX11OutputTexture.Reset();
    m_DX11OutputRTV.Reset();
    m_DX11OutputSRV.Reset();

    if (m_Width == 0 || m_Height == 0)
        return;

    auto device = DX11Context::GetDevice();
    if (!device)
        return;

    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = m_Width;
    textureDesc.Height = m_Height;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&textureDesc, nullptr, &m_DX11OutputTexture)) ||
        FAILED(device->CreateRenderTargetView(m_DX11OutputTexture.Get(), nullptr, &m_DX11OutputRTV)) ||
        FAILED(device->CreateShaderResourceView(m_DX11OutputTexture.Get(), nullptr, &m_DX11OutputSRV)))
    {
        ERROR("DX11 FXAA output texture creation failed");
        return;
    }

    if (!m_DX11VertexShader || !m_DX11PixelShader)
    {
        auto vertexBlob = CompileFXAAShader(s_FXAAVertexShader, "VSMain", "vs_5_0");
        auto pixelBlob = CompileFXAAShader(s_FXAAPixelShader, "PSMain", "ps_5_0");
        if (!vertexBlob || !pixelBlob)
        {
            return;
        }
        if (!m_DX11VertexShader &&
            FAILED(device->CreateVertexShader(vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(),
                nullptr, &m_DX11VertexShader)))
            return;
        if (!m_DX11PixelShader &&
            FAILED(device->CreatePixelShader(pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize(),
                nullptr, &m_DX11PixelShader)))
            return;
    }

    if (!m_DX11ConstantBuffer)
    {
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.ByteWidth = 32;
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&bufferDesc, nullptr, &m_DX11ConstantBuffer)))
            return;
    }

    if (!m_DX11Sampler)
    {
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&samplerDesc, &m_DX11Sampler)))
            return;
    }

    if (!m_DX11NoBlend)
    {
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device->CreateBlendState(&blendDesc, &m_DX11NoBlend)))
            return;
    }

    if (!m_DX11NoDepth)
    {
        D3D11_DEPTH_STENCIL_DESC depthDesc = {};
        depthDesc.DepthEnable = FALSE;
        depthDesc.StencilEnable = FALSE;
        if (FAILED(device->CreateDepthStencilState(&depthDesc, &m_DX11NoDepth)))
            return;
    }

    m_DX11Ready = m_DX11OutputRTV && m_DX11OutputSRV && m_DX11VertexShader &&
        m_DX11PixelShader && m_DX11ConstantBuffer && m_DX11Sampler &&
        m_DX11NoBlend && m_DX11NoDepth;
#endif
}

void FXAA::Render(uint64_t colorTexture)
{
#ifdef G_OPENGL
    if (!m_Enabled || !m_Shader || !m_OutputFBO || !colorTexture)
        return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, m_OutputFBO);
    glViewport(0, 0, m_Width, m_Height);
    glBindVertexArray(m_QuadVAO);

    m_Shader->Bind();
    m_Shader->SetInt("u_SceneColor", 0);
    m_Shader->SetFloat2("u_InvResolution", glm::vec2(1.0f / (float)m_Width, 1.0f / (float)m_Height));
    m_Shader->SetFloat("u_EdgeThreshold", m_EdgeThreshold);
    m_Shader->SetFloat("u_EdgeThresholdMin", m_EdgeThresholdMin);
    m_Shader->SetFloat("u_SubpixelQuality", m_SubpixelQuality);
    m_Shader->SetFloat("u_SpanMax", m_SpanMax);
    m_Shader->SetInt("u_DebugMode", m_DebugMode);
    glBindTextureUnit(0, (uint32_t)colorTexture);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
#elif defined(G_DX11)
    if (!m_Enabled || !m_DX11Ready || !colorTexture)
        return;

    struct FXAAConstants
    {
        glm::vec2 InvResolution;
        float EdgeThreshold;
        float EdgeThresholdMin;
        float SubpixelQuality;
        float SpanMax;
        int DebugMode;
        float Padding;
    };
    static_assert(sizeof(FXAAConstants) == 32, "FXAA constant buffer must match the HLSL layout");

    auto context = DX11Context::GetDeviceContext();
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(m_DX11ConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    const FXAAConstants constants = {
        glm::vec2(1.0f / (float)m_Width, 1.0f / (float)m_Height),
        m_EdgeThreshold,
        m_EdgeThresholdMin,
        m_SubpixelQuality,
        m_SpanMax,
        m_DebugMode,
        0.0f
    };
    memcpy(mapped.pData, &constants, sizeof(constants));
    context->Unmap(m_DX11ConstantBuffer.Get(), 0);

    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->PSSetShaderResources(0, 1, &nullSRV);
    ID3D11RenderTargetView* outputRTV = m_DX11OutputRTV.Get();
    context->OMSetRenderTargets(1, &outputRTV, nullptr);
    context->OMSetBlendState(m_DX11NoBlend.Get(), nullptr, 0xffffffff);
    context->OMSetDepthStencilState(m_DX11NoDepth.Get(), 0);
    D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (float)m_Width, (float)m_Height, 0.0f, 1.0f };
    context->RSSetViewports(1, &viewport);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(m_DX11VertexShader.Get(), nullptr, 0);
    context->PSSetShader(m_DX11PixelShader.Get(), nullptr, 0);
    ID3D11Buffer* constantBuffer = m_DX11ConstantBuffer.Get();
    context->PSSetConstantBuffers(0, 1, &constantBuffer);
    ID3D11ShaderResourceView* sceneColor = reinterpret_cast<ID3D11ShaderResourceView*>(colorTexture);
    context->PSSetShaderResources(0, 1, &sceneColor);
    ID3D11SamplerState* sampler = m_DX11Sampler.Get();
    context->PSSetSamplers(0, 1, &sampler);
    context->Draw(3, 0);
    context->PSSetShaderResources(0, 1, &nullSRV);
    DX11Context::BindBackBuffer();
#else
    (void)colorTexture;
#endif
}

uint64_t FXAA::GetOutputTexture() const
{
#ifdef G_DX11
    return m_DX11Ready ? reinterpret_cast<uint64_t>(m_DX11OutputSRV.Get()) : 0;
#else
    return m_OutputTexture;
#endif
}

#include "stdsfx.h"
#include "CSM.h"
#include <Renderer/RenderCommand.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <Image.h>

#ifdef G_OPENGL
#include <glad/glad.h>
#endif
#ifdef G_DX11
#include <Platform/DX11/DX11Context.h>
#endif

CSM::CSM(uint32_t cascadeCount, uint32_t shadowMapSize, float splitLambda)
    : m_CascadeCount(cascadeCount), m_ShadowMapSize(shadowMapSize), m_SplitLambda(splitLambda)
{
    m_CascadeDistances.resize(m_CascadeCount + 1);
    m_CascadeTexelSizes.resize(m_CascadeCount, 1.0f);
    m_LightViewProj.resize(m_CascadeCount);

#ifdef G_OPENGL
    // Create shadow map texture array (depth-only)
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &m_ShadowTextureID);
    glTextureStorage3D(m_ShadowTextureID, 1, GL_DEPTH_COMPONENT32F, m_ShadowMapSize, m_ShadowMapSize, m_CascadeCount);
    glTextureParameteri(m_ShadowTextureID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_ShadowTextureID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_ShadowTextureID, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTextureParameteri(m_ShadowTextureID, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glm::vec4 borderColor(1.0f);
    glTextureParameterfv(m_ShadowTextureID, GL_TEXTURE_BORDER_COLOR, &borderColor.x);
    // Manual depth comparison in shader — no GL_COMPARE_REF_TO_TEXTURE

    // Create FBO for shadow rendering
    glCreateFramebuffers(1, &m_ShadowFBO);

    // Create debug color texture for depth visualisation
    glCreateTextures(GL_TEXTURE_2D, 1, &m_DebugTextureID);
    glTextureStorage2D(m_DebugTextureID, 1, GL_RGBA8, m_ShadowMapSize, m_ShadowMapSize);
    glTextureParameteri(m_DebugTextureID, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_DebugTextureID, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(m_DebugTextureID, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_DebugTextureID, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#endif
#ifdef G_DX11
    auto device = DX11Context::GetDevice();
    if (!device)
    {
        ERROR("CSM DX11: null device");
        m_Enabled = false;
        return;
    }
    // Create 2D texture array for shadow depth
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = m_ShadowMapSize;
    texDesc.Height = m_ShadowMapSize;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = m_CascadeCount;
    texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&texDesc, nullptr, &m_ShadowTextureArray)))
    {
        ERROR("CSM DX11: failed to create shadow texture array");
        m_Enabled = false;
        return;
    }

    // Create shader resource view (read depth as float)
    if (m_ShadowTextureArray)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Texture2DArray.MipLevels = 1;
        srvDesc.Texture2DArray.ArraySize = m_CascadeCount;
        if (FAILED(device->CreateShaderResourceView(m_ShadowTextureArray.Get(), &srvDesc, &m_ShadowSRV)))
        {
            ERROR("CSM DX11: failed to create shadow SRV");
            m_Enabled = false;
            return;
        }
    }

    // Create one DSV per cascade layer
    m_CascadeDSVs.resize(m_CascadeCount);
    for (uint32_t i = 0; i < m_CascadeCount; i++)
    {
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.FirstArraySlice = i;
        dsvDesc.Texture2DArray.ArraySize = 1;
        if (FAILED(device->CreateDepthStencilView(m_ShadowTextureArray.Get(), &dsvDesc, &m_CascadeDSVs[i])))
        {
            ERROR("CSM DX11: failed to create cascade DSV {}", i);
            m_Enabled = false;
            return;
        }
    }

    // Shader-side bias is used for DX11 shadow comparison. A D3D depth bias of
    // the same numeric value as GL polygon offset is far too large for D32 and
    // causes visible peter-panning near casters.
    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_BACK;
    rsDesc.DepthClipEnable = TRUE;
    rsDesc.DepthBias = 0;
    rsDesc.DepthBiasClamp = 0.0f;
    rsDesc.SlopeScaledDepthBias = 0.0f;
    if (FAILED(device->CreateRasterizerState(&rsDesc, &m_ShadowRasterizer)))
    {
        ERROR("CSM DX11: failed to create shadow rasterizer state");
        m_Enabled = false;
        return;
    }

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    samplerDesc.BorderColor[0] = 1.0f;
    samplerDesc.BorderColor[1] = 1.0f;
    samplerDesc.BorderColor[2] = 1.0f;
    samplerDesc.BorderColor[3] = 1.0f;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &m_ShadowSampler)))
    {
        ERROR("CSM DX11: failed to create shadow sampler");
        m_Enabled = false;
        return;
    }

    // Create debug texture
    D3D11_TEXTURE2D_DESC debugDesc = {};
    debugDesc.Width = m_ShadowMapSize;
    debugDesc.Height = m_ShadowMapSize;
    debugDesc.MipLevels = 1;
    debugDesc.ArraySize = 1;
    debugDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    debugDesc.SampleDesc.Count = 1;
    debugDesc.Usage = D3D11_USAGE_DEFAULT;
    debugDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    device->CreateTexture2D(&debugDesc, nullptr, &m_DebugTexture2D);

    D3D11_SHADER_RESOURCE_VIEW_DESC debugSRVDesc = {};
    debugSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    debugSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    debugSRVDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(m_DebugTexture2D.Get(), &debugSRVDesc, &m_DebugSRV);
#endif
}

CSM::~CSM()
{
#ifdef G_OPENGL
    glDeleteTextures(1, &m_ShadowTextureID);
    glDeleteTextures(1, &m_DebugTextureID);
    glDeleteFramebuffers(1, &m_ShadowFBO);
#endif
#ifdef G_DX11
    // ComPtr releases automatically
#endif
}

void CSM::Update(const glm::mat4& view, const glm::mat4& proj, float nearPlane, float farPlane)
{
    ComputeCascades(view, proj, nearPlane, farPlane);
}

void CSM::BeginShadowPass(uint32_t cascadeIndex)
{
    if (cascadeIndex >= m_CascadeCount) return;
#ifdef G_OPENGL
    // Attach cascade layer to FBO depth attachment
    glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowFBO);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_ShadowTextureID, 0, cascadeIndex);

    // No color buffer needed
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        ERROR("Shadow FBO incomplete!");

    glViewport(0, 0, m_ShadowMapSize, m_ShadowMapSize);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(m_PolygonOffsetFactor, m_PolygonOffsetUnits);
    glClear(GL_DEPTH_BUFFER_BIT);
#endif
#ifdef G_DX11
    auto context = DX11Context::GetDeviceContext();
    if (!context || !m_Enabled || cascadeIndex >= m_CascadeDSVs.size() || !m_CascadeDSVs[cascadeIndex])
        return;

    // A resource cannot be bound as an SRV and DSV at the same time.
    ID3D11ShaderResourceView* nullShadowSRV = nullptr;
    context->PSSetShaderResources(2, 1, &nullShadowSRV);

    // Save previous state
    m_PrevViewportCount = 1;
    context->OMGetRenderTargets(1, m_PrevRTV.GetAddressOf(), m_PrevDSV.GetAddressOf());
    context->RSGetViewports(&m_PrevViewportCount, &m_PrevViewport);
    context->RSGetState(&m_PrevRasterizer);
    m_ShadowPassActive = true;

    // Bind shadow cascade DSV only (no color target)
    context->OMSetRenderTargets(0, nullptr, m_CascadeDSVs[cascadeIndex].Get());

    // Viewport
    D3D11_VIEWPORT vp = {};
    vp.Width = (float)m_ShadowMapSize;
    vp.Height = (float)m_ShadowMapSize;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context->RSSetViewports(1, &vp);

    // Apply shadow rasterizer (with depth bias)
    context->RSSetState(m_ShadowRasterizer.Get());

    // Clear depth
    context->ClearDepthStencilView(m_CascadeDSVs[cascadeIndex].Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
#endif
}

void CSM::EndShadowPass()
{
#ifdef G_OPENGL
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
#endif
#ifdef G_DX11
    auto context = DX11Context::GetDeviceContext();
    if (!context || !m_ShadowPassActive)
        return;

    // Detach the shadow DSV before it is used as a shader resource.
    context->OMSetRenderTargets(0, nullptr, nullptr);
    // Restore previous rasterizer state
    context->RSSetState(m_PrevRasterizer.Get());
    ID3D11RenderTargetView* previousRTV = m_PrevRTV.Get();
    context->OMSetRenderTargets(previousRTV ? 1 : 0,
        previousRTV ? &previousRTV : nullptr, m_PrevDSV.Get());
    if (m_PrevViewportCount)
        context->RSSetViewports(1, &m_PrevViewport);
    m_PrevRTV.Reset();
    m_PrevRasterizer.Reset();
    m_PrevDSV.Reset();
    m_PrevViewportCount = 0;
    m_ShadowPassActive = false;
#endif
}

void CSM::BindShadowTexture(uint32_t slot) const
{
#ifdef G_OPENGL
    glBindTextureUnit(slot, m_ShadowTextureID);
#endif
#ifdef G_DX11
    ID3D11ShaderResourceView* srv = m_ShadowSRV.Get();
    ID3D11SamplerState* sampler = m_ShadowSampler.Get();
    auto context = DX11Context::GetDeviceContext();
    context->PSSetShaderResources(slot, 1, &srv);
    context->PSSetSamplers(slot, 1, &sampler);
#endif
}

void CSM::UnbindShadowTexture() const
{
#ifdef G_OPENGL
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
#endif
#ifdef G_DX11
    ID3D11ShaderResourceView* nullSRV = nullptr;
    DX11Context::GetDeviceContext()->PSSetShaderResources(2, 1, &nullSRV);
#endif
}

void CSM::SaveShadowMap(const std::string& filepath, uint32_t cascadeIndex) const
{
#ifdef G_OPENGL
    if (cascadeIndex >= m_CascadeCount || !m_ShadowTextureID) return;

    std::vector<float> depthData(m_ShadowMapSize * m_ShadowMapSize);

    // Use glReadPixels via FBO for reliable depth reading
    glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowFBO);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_ShadowTextureID, 0, cascadeIndex);
    glReadPixels(0, 0, m_ShadowMapSize, m_ShadowMapSize, GL_DEPTH_COMPONENT, GL_FLOAT, depthData.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Diagnostic: print first few values
    float minVal = *std::min_element(depthData.begin(), depthData.end());
    float maxVal = *std::max_element(depthData.begin(), depthData.end());
    TRACE("Shadow cascade {}: min={} max={} first={} ", cascadeIndex, minVal, maxVal, depthData[0]);

    // Normalize depth to [0,255] and save as RGB
    std::vector<unsigned char> imgData(m_ShadowMapSize * m_ShadowMapSize * 3);
    float range = maxVal - minVal;
    if (range < 0.001f) range = 1.0f;

    for (size_t i = 0; i < depthData.size(); i++)
    {
        unsigned char v = (unsigned char)((depthData[i] - minVal) / range * 255.0f);
        imgData[i * 3 + 0] = v;
        imgData[i * 3 + 1] = v;
        imgData[i * 3 + 2] = v;
        // Show shadow vs lit pixels more clearly: red tint for shadowed areas
        if (depthData[i] > 0.5f && depthData[i] < 0.99f)
        {
            imgData[i * 3 + 1] = 0;
        }
    }

    unsigned char* owned = new unsigned char[imgData.size()];
    memcpy(owned, imgData.data(), imgData.size());
    auto saveImg = CreateRef<Image>((int)m_ShadowMapSize, (int)m_ShadowMapSize, 3, owned, PixelType::BYTE);
    saveImg->Save(filepath);
    INFO("Saved shadow map cascade {} to {}", cascadeIndex, filepath);
#endif
}

uint64_t CSM::GetDebugTextureID(uint32_t cascadeIndex)
{
    (void)cascadeIndex;
#ifdef G_OPENGL
    return m_DebugTextureID;
#endif
#ifdef G_DX11
    return reinterpret_cast<uint64_t>(m_DebugSRV.Get());
#endif
    return 0;
}

void CSM::UpdateDebugTexture(uint32_t cascadeIndex)
{
#ifdef G_OPENGL
    if (cascadeIndex >= m_CascadeCount || !m_ShadowTextureID) return;

    std::vector<float> depthData(m_ShadowMapSize * m_ShadowMapSize);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowFBO);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_ShadowTextureID, 0, cascadeIndex);
    glReadPixels(0, 0, m_ShadowMapSize, m_ShadowMapSize, GL_DEPTH_COMPONENT, GL_FLOAT, depthData.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::vector<unsigned char> rgba(m_ShadowMapSize * m_ShadowMapSize * 4);
    for (size_t i = 0; i < depthData.size(); i++)
    {
        unsigned char v = (unsigned char)(std::clamp(depthData[i], 0.0f, 1.0f) * 255.0f);
        rgba[i * 4 + 0] = v;
        rgba[i * 4 + 1] = v;
        rgba[i * 4 + 2] = v;
        rgba[i * 4 + 3] = 255;
    }
    glTextureSubImage2D(m_DebugTextureID, 0, 0, 0, m_ShadowMapSize, m_ShadowMapSize,
                        GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
#endif
#ifdef G_DX11
    if (cascadeIndex >= m_CascadeCount || !m_ShadowTextureArray) return;

    // Read depth from shadow texture array using staging resource
    auto device = DX11Context::GetDevice();
    auto context = DX11Context::GetDeviceContext();

    D3D11_TEXTURE2D_DESC stagingDesc = {};
    stagingDesc.Width = m_ShadowMapSize;
    stagingDesc.Height = m_ShadowMapSize;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    // Use a concrete float format for staging so it can be mapped by the CPU
    stagingDesc.Format = DXGI_FORMAT_R32_FLOAT;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    HRESULT hr = device->CreateTexture2D(&stagingDesc, nullptr, &staging);
    if (FAILED(hr) || !staging)
    {
        ERROR("CSM DX11: failed to create staging texture for debug readback (hr=0x{:X})", hr);
        return;
    }

    D3D11_BOX box = {};
    box.left = 0; box.top = 0; box.front = 0;
    box.right = m_ShadowMapSize; box.bottom = m_ShadowMapSize; box.back = 1;
    context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0,
        m_ShadowTextureArray.Get(), cascadeIndex, &box);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr))
    {
        ERROR("CSM DX11: failed to map staging texture (hr=0x{:X})", hr);
        return;
    }

    std::vector<unsigned char> rgba(m_ShadowMapSize * m_ShadowMapSize * 4);
    for (uint32_t y = 0; y < m_ShadowMapSize; ++y)
    {
        const float* row = reinterpret_cast<const float*>(
            static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch);
        for (uint32_t x = 0; x < m_ShadowMapSize; ++x)
        {
            const size_t i = static_cast<size_t>(y) * m_ShadowMapSize + x;
            unsigned char v = (unsigned char)(std::clamp(row[x], 0.0f, 1.0f) * 255.0f);
            rgba[i * 4 + 0] = v;
            rgba[i * 4 + 1] = v;
            rgba[i * 4 + 2] = v;
            rgba[i * 4 + 3] = 255;
        }
    }
    context->Unmap(staging.Get(), 0);

    // Create a debug texture (RGBA8) and SRV for visualization
    D3D11_TEXTURE2D_DESC debugDesc = {};
    debugDesc.Width = m_ShadowMapSize;
    debugDesc.Height = m_ShadowMapSize;
    debugDesc.MipLevels = 1;
    debugDesc.ArraySize = 1;
    debugDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    debugDesc.SampleDesc.Count = 1;
    debugDesc.Usage = D3D11_USAGE_DEFAULT;
    debugDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = rgba.data();
    initData.SysMemPitch = m_ShadowMapSize * 4;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> newDebugTex;
    hr = device->CreateTexture2D(&debugDesc, &initData, &newDebugTex);
    if (FAILED(hr) || !newDebugTex)
    {
        ERROR("CSM DX11: failed to create debug texture (hr=0x{:X})", hr);
        return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC debugSRVDesc = {};
    debugSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    debugSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    debugSRVDesc.Texture2D.MipLevels = 1;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> newDebugSRV;
    hr = device->CreateShaderResourceView(newDebugTex.Get(), &debugSRVDesc, &newDebugSRV);
    if (FAILED(hr) || !newDebugSRV)
    {
        ERROR("CSM DX11: failed to create debug SRV (hr=0x{:X})", hr);
        return;
    }

    // Replace previous debug resources
    m_DebugTexture2D = newDebugTex;
    m_DebugSRV = newDebugSRV;
#endif
}

void CSM::ComputeCascades(const glm::mat4& view, const glm::mat4& proj, float nearPlane, float farPlane)
{
    farPlane = glm::min(farPlane, m_MaxShadowDistance);
    farPlane = glm::max(farPlane, nearPlane + 1.0f);

    // Compute cascade split distances (practical split: blend of log and uniform)
    for (uint32_t i = 0; i <= m_CascadeCount; i++)
    {
        float p = (float)i / (float)m_CascadeCount;
        float logSplit = nearPlane * powf(farPlane / nearPlane, p);
        float uniformSplit = nearPlane + (farPlane - nearPlane) * p;
        m_CascadeDistances[i] = logSplit * m_SplitLambda + uniformSplit * (1.0f - m_SplitLambda);
    }

    glm::mat4 invView = glm::inverse(view);
    float tanHalfFovY = 1.0f / proj[1][1];
    float tanHalfFovX = 1.0f / proj[0][0];

    for (uint32_t i = 0; i < m_CascadeCount; i++)
    {
        float cn = m_CascadeDistances[i];
        float cf = m_CascadeDistances[i + 1];

        // Build the frustum slice directly in view space.
        const glm::vec3 viewCorners[8] = {
            {-cn * tanHalfFovX, -cn * tanHalfFovY, -cn},
            { cn * tanHalfFovX, -cn * tanHalfFovY, -cn},
            { cn * tanHalfFovX,  cn * tanHalfFovY, -cn},
            {-cn * tanHalfFovX,  cn * tanHalfFovY, -cn},
            {-cf * tanHalfFovX, -cf * tanHalfFovY, -cf},
            { cf * tanHalfFovX, -cf * tanHalfFovY, -cf},
            { cf * tanHalfFovX,  cf * tanHalfFovY, -cf},
            {-cf * tanHalfFovX,  cf * tanHalfFovY, -cf},
        };

        glm::vec3 frustumCorners[8];
        glm::vec3 center(0.0f);
        for (int j = 0; j < 8; j++)
        {
            glm::vec4 w = invView * glm::vec4(viewCorners[j], 1.0f);
            w /= w.w;
            frustumCorners[j] = glm::vec3(w);
            center += frustumCorners[j];
        }
        center /= 8.0f;

        glm::vec3 lightDir = glm::normalize(m_Light.Direction);
        glm::vec3 lightPos = center - lightDir * 2000.0f;
        glm::mat4 lightView = glm::lookAt(lightPos, center, glm::vec3(0.0f, 1.0f, 0.0f));

        // Compute tight ortho bounds
        float minX = INFINITY, maxX = -INFINITY;
        float minY = INFINITY, maxY = -INFINITY;
        float minZ = INFINITY, maxZ = -INFINITY;

        for (auto& c : frustumCorners)
        {
            glm::vec4 lvp = lightView * glm::vec4(c, 1.0f);
            minX = glm::min(minX, lvp.x); maxX = glm::max(maxX, lvp.x);
            minY = glm::min(minY, lvp.y); maxY = glm::max(maxY, lvp.y);
            minZ = glm::min(minZ, lvp.z); maxZ = glm::max(maxZ, lvp.z);
        }

        float width = maxX - minX;
        float height = maxY - minY;
        const float texelPadding = 4.0f;
        if (width > 0.0f && height > 0.0f && m_ShadowMapSize > 0)
        {
            float texelSizeX = width / static_cast<float>(m_ShadowMapSize);
            float texelSizeY = height / static_cast<float>(m_ShadowMapSize);
            minX -= texelSizeX * texelPadding;
            maxX += texelSizeX * texelPadding;
            minY -= texelSizeY * texelPadding;
            maxY += texelSizeY * texelPadding;

            width = maxX - minX;
            height = maxY - minY;
            texelSizeX = width / static_cast<float>(m_ShadowMapSize);
            texelSizeY = height / static_cast<float>(m_ShadowMapSize);

            float centerX = (minX + maxX) * 0.5f;
            float centerY = (minY + maxY) * 0.5f;
            centerX = glm::floor(centerX / texelSizeX) * texelSizeX;
            centerY = glm::floor(centerY / texelSizeY) * texelSizeY;
            minX = centerX - width * 0.5f;
            maxX = centerX + width * 0.5f;
            minY = centerY - height * 0.5f;
            maxY = centerY + height * 0.5f;
        }
        if (i < m_CascadeTexelSizes.size() && m_ShadowMapSize > 0)
            m_CascadeTexelSizes[i] = glm::max(maxX - minX, maxY - minY) / static_cast<float>(m_ShadowMapSize);

        // Expand Z to include potential shadow casters just outside the camera
        // slice. The minimum pad prevents close-up cascades from clipping.
        float zRange = maxZ - minZ;
        float zPadding = glm::max(zRange * 0.5f, 50.0f);
        minZ -= zPadding;
        maxZ += zPadding;

        // glm::ortho expects positive near/far distances along -Z in light view space.
        // In light space, objects in front have negative Z; negate to get distances.
        glm::mat4 lightProjection = glm::ortho(minX, maxX, minY, maxY, -maxZ, -minZ);
#ifdef G_DX11
        // GLM produces [-1, 1] clip-space Z; D3D11 depth uses [0, 1].
        glm::mat4 toD3DClip(1.0f);
        toD3DClip[2][2] = 0.5f;
        toD3DClip[3][2] = 0.5f;
        lightProjection = toD3DClip * lightProjection;
#endif
        m_LightViewProj[i] = lightProjection * lightView;
    }
}

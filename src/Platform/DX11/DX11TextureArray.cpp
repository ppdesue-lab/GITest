#include "stdsfx.h"
#include "DX11TextureArray.h"
#include "DX11Context.h"

DX11TextureArray::DX11TextureArray(uint32_t width, uint32_t height, uint32_t layers,
                                   const std::vector<Ref<Image>>& images)
    : m_Width(width), m_Height(height), m_Layers(layers)
{
    auto device = DX11Context::GetDevice();

    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (!images.empty() && images[0])
    {
        switch (images[0]->Channel)
        {
        case 1: format = DXGI_FORMAT_R8_UNORM;      break;
        case 2: format = DXGI_FORMAT_R8G8_UNORM;    break;
        case 3: format = DXGI_FORMAT_R8G8B8A8_UNORM; break;
        default: format = DXGI_FORMAT_R8G8B8A8_UNORM; break;
        }
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = layers;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = 0;

    // Prepare initial data for each layer
    std::vector<D3D11_SUBRESOURCE_DATA> initData;
    for (uint32_t i = 0; i < layers && i < (uint32_t)images.size(); i++)
    {
        if (images[i] && images[i]->Data)
        {
            D3D11_SUBRESOURCE_DATA data = {};
            data.pSysMem = images[i]->Data;
            data.SysMemPitch = width * images[i]->Channel;
            data.SysMemSlicePitch = 0;
            initData.push_back(data);
        }
    }

    device->CreateTexture2D(&desc, initData.empty() ? nullptr : initData.data(), &m_Texture);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = layers;
    device->CreateShaderResourceView(m_Texture.Get(), &srvDesc, &m_ShaderResourceView);

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    device->CreateSamplerState(&samplerDesc, &m_Sampler);
}

void DX11TextureArray::Bind(uint32_t slot) const
{
    auto ctx = DX11Context::GetDeviceContext();
    ctx->PSSetShaderResources(slot, 1, m_ShaderResourceView.GetAddressOf());
    ctx->PSSetSamplers(slot, 1, m_Sampler.GetAddressOf());
}

void DX11TextureArray::Unbind() const
{
    ID3D11ShaderResourceView* nullSRV = nullptr;
    DX11Context::GetDeviceContext()->PSSetShaderResources(0, 1, &nullSRV);
}

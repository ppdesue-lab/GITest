#include "stdsfx.h"
#include "DX11Texture.h"
#include "DX11Context.h"

DX11Texture::DX11Texture(Ref<Image> img)
{
	assert(img);
	m_Width = img->Width;
	m_Height = img->Height;
	m_RendererID = 0;

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = m_Width;
	desc.Height = m_Height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA data = {};
	data.pSysMem = img->Data;
	data.SysMemPitch = m_Width * 4;
	DX11Context::GetDevice()->CreateTexture2D(&desc, &data, &m_Texture);
	DX11Context::GetDevice()->CreateShaderResourceView(m_Texture.Get(), nullptr, &m_ShaderResourceView);

	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	DX11Context::GetDevice()->CreateSamplerState(&samplerDesc, &m_Sampler);
}

void DX11Texture::Bind(uint32_t slot) const
{
	ID3D11ShaderResourceView* srv = m_ShaderResourceView.Get();
	ID3D11SamplerState* sampler = m_Sampler.Get();
	DX11Context::GetDeviceContext()->PSSetShaderResources(slot, 1, &srv);
	DX11Context::GetDeviceContext()->PSSetSamplers(slot, 1, &sampler);
}

void DX11Texture::Unbind() const
{
	ID3D11ShaderResourceView* srv = nullptr;
	DX11Context::GetDeviceContext()->PSSetShaderResources(0, 1, &srv);
}

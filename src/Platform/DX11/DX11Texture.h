#pragma once

#include <Renderer/Texture.h>
#include <d3d11.h>
#include <wrl/client.h>

class DX11Texture : public Texture
{
public:
	DX11Texture(Ref<Image> img);
	~DX11Texture() override = default;

	uint32_t GetWidth() const override { return m_Width; }
	uint32_t GetHeight() const override { return m_Height; }
	void Bind(uint32_t slot = 0) const override;
	void Unbind() const override;

	ID3D11ShaderResourceView* GetShaderResourceView() const { return m_ShaderResourceView.Get(); }

private:
	uint32_t m_Width = 0;
	uint32_t m_Height = 0;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Texture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_ShaderResourceView;
	Microsoft::WRL::ComPtr<ID3D11SamplerState> m_Sampler;
};

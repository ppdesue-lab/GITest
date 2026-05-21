#pragma once

#include <Renderer/TextureArray.h>
#include <d3d11.h>
#include <wrl/client.h>

class DX11TextureArray : public TextureArray
{
public:
    DX11TextureArray(uint32_t width, uint32_t height, uint32_t layers,
                     const std::vector<Ref<Image>>& images);
    ~DX11TextureArray() override = default;

    void Bind(uint32_t slot = 0) const override;
    void Unbind() const override;

    uint32_t GetWidth() const override { return m_Width; }
    uint32_t GetHeight() const override { return m_Height; }
    uint32_t GetLayerCount() const override { return m_Layers; }

    ID3D11ShaderResourceView* GetShaderResourceView() const { return m_ShaderResourceView.Get(); }

private:
    uint32_t m_Width = 0, m_Height = 0, m_Layers = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_ShaderResourceView;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_Sampler;
};

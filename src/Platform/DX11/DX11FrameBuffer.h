#pragma once

#include <Renderer/FrameBuffer.h>
#include <d3d11.h>
#include <wrl/client.h>

class DX11FrameBuffer : public FrameBuffer
{
public:
	DX11FrameBuffer(const FrameBufferSpecification& spec);
	~DX11FrameBuffer() override = default;

	void Bind() override;
	void Unbind() override;
	void Resize(uint32_t width, uint32_t height) override;
	int ReadPixel(uint32_t attachmentIndex, int x, int y) override;
	void Save2File(const std::string& filename, uint32_t attachmentIndex) override;
	void ClearAttachment(uint32_t attachmentIndex, int value) override;
	uint32_t GetColorAttachmentRendererID(uint32_t index = 0) const override;
	const FrameBufferSpecification& GetSpecification() const override { return m_Specification; }

private:
	void Invalidate();

private:
	FrameBufferSpecification m_Specification;
	std::vector<FrameBufferTextureSpecification> m_ColorAttachmentSpecifications;
	FrameBufferTextureSpecification m_DepthAttachmentSpecification;
	std::vector<Microsoft::WRL::ComPtr<ID3D11Texture2D>> m_ColorTextures;
	std::vector<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>> m_ColorRTVs;
	std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> m_ColorSRVs;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DepthTexture;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_DepthDSV;
};

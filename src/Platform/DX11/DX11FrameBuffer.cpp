#include "stdsfx.h"
#include "DX11FrameBuffer.h"
#include "DX11Context.h"

static DXGI_FORMAT ToDXGIFormat(FrameBufferTextureFormat format)
{
	switch (format)
	{
	case FrameBufferTextureFormat::RGBA8: return DXGI_FORMAT_R8G8B8A8_UNORM;
	case FrameBufferTextureFormat::RED_INTEGER: return DXGI_FORMAT_R32_SINT;
	case FrameBufferTextureFormat::Depth24Stencil8: return DXGI_FORMAT_D24_UNORM_S8_UINT;
	default: return DXGI_FORMAT_UNKNOWN;
	}
}

static bool IsDepthFormat(FrameBufferTextureFormat format)
{
	return format == FrameBufferTextureFormat::Depth24Stencil8;
}

DX11FrameBuffer::DX11FrameBuffer(const FrameBufferSpecification& spec)
	: m_Specification(spec)
{
	for (auto attachment : m_Specification.Attachments.Attachments)
	{
		if (IsDepthFormat(attachment.TextureFormat))
			m_DepthAttachmentSpecification = attachment;
		else
			m_ColorAttachmentSpecifications.push_back(attachment);
	}
	Invalidate();
}

void DX11FrameBuffer::Bind()
{
	std::vector<ID3D11RenderTargetView*> rtvs;
	for (auto& rtv : m_ColorRTVs)
		rtvs.push_back(rtv.Get());
	DX11Context::GetDeviceContext()->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), m_DepthDSV.Get());

	D3D11_VIEWPORT viewport = {};
	viewport.Width = (float)m_Specification.Width;
	viewport.Height = (float)m_Specification.Height;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	DX11Context::GetDeviceContext()->RSSetViewports(1, &viewport);
	if (m_DepthDSV)
		DX11Context::GetDeviceContext()->ClearDepthStencilView(m_DepthDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void DX11FrameBuffer::Unbind()
{
	DX11Context::BindBackBuffer();
}

void DX11FrameBuffer::Resize(uint32_t width, uint32_t height)
{
	if (width == 0 || height == 0)
		return;
	m_Specification.Width = width;
	m_Specification.Height = height;
	Invalidate();
}

int DX11FrameBuffer::ReadPixel(uint32_t attachmentIndex, int x, int y)
{
	(void)attachmentIndex;
	(void)x;
	(void)y;
	WARN("DX11FrameBuffer::ReadPixel is not implemented yet");
	return -1;
}

void DX11FrameBuffer::Save2File(const std::string& filename, uint32_t attachmentIndex)
{
	(void)filename;
	(void)attachmentIndex;
	WARN("DX11FrameBuffer::Save2File is not implemented yet");
}

void DX11FrameBuffer::ClearAttachment(uint32_t attachmentIndex, int value)
{
	if (attachmentIndex >= m_ColorRTVs.size())
		return;
	float color[4] = { (float)value, 0.0f, 0.0f, 0.0f };
	DX11Context::GetDeviceContext()->ClearRenderTargetView(m_ColorRTVs[attachmentIndex].Get(), color);
}

uint32_t DX11FrameBuffer::GetColorAttachmentRendererID(uint32_t index) const
{
	(void)index;
	return 0;
}

void DX11FrameBuffer::Invalidate()
{
	m_ColorTextures.clear();
	m_ColorRTVs.clear();
	m_ColorSRVs.clear();
	m_DepthTexture.Reset();
	m_DepthDSV.Reset();

	auto device = DX11Context::GetDevice();
	for (auto attachment : m_ColorAttachmentSpecifications)
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = m_Specification.Width;
		desc.Height = m_Specification.Height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = ToDXGIFormat(attachment.TextureFormat);
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		device->CreateTexture2D(&desc, nullptr, &texture);

		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
		device->CreateRenderTargetView(texture.Get(), nullptr, &rtv);
		device->CreateShaderResourceView(texture.Get(), nullptr, &srv);
		m_ColorTextures.push_back(texture);
		m_ColorRTVs.push_back(rtv);
		m_ColorSRVs.push_back(srv);
	}

	if (m_DepthAttachmentSpecification.TextureFormat != FrameBufferTextureFormat::None)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = m_Specification.Width;
		desc.Height = m_Specification.Height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = ToDXGIFormat(m_DepthAttachmentSpecification.TextureFormat);
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		device->CreateTexture2D(&desc, nullptr, &m_DepthTexture);
		device->CreateDepthStencilView(m_DepthTexture.Get(), nullptr, &m_DepthDSV);
	}
}

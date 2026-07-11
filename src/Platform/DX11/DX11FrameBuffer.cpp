#include "stdsfx.h"
#include "DX11FrameBuffer.h"
#include "DX11Context.h"
#include "Renderer/ProfileTimer.h"

static DXGI_FORMAT ToDXGIFormat(FrameBufferTextureFormat format)
{
	switch (format)
	{
	case FrameBufferTextureFormat::RGBA8: return DXGI_FORMAT_R8G8B8A8_UNORM;
	case FrameBufferTextureFormat::RGBA16F: return DXGI_FORMAT_R16G16B16A16_FLOAT;
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

void DX11FrameBuffer::Bind(bool clearDepth)
{
	ID3D11ShaderResourceView* nullSRVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
	DX11Context::GetDeviceContext()->PSSetShaderResources(0, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, nullSRVs);

	static Microsoft::WRL::ComPtr<ID3D11BlendState> s_NoBlendState;
	if (!s_NoBlendState)
	{
		D3D11_BLEND_DESC blendDesc = {};
		for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
		{
			blendDesc.RenderTarget[i].BlendEnable = FALSE;
			blendDesc.RenderTarget[i].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			blendDesc.RenderTarget[i].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			blendDesc.RenderTarget[i].BlendOp = D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[i].SrcBlendAlpha = D3D11_BLEND_ONE;
			blendDesc.RenderTarget[i].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			blendDesc.RenderTarget[i].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		}
		DX11Context::GetDevice()->CreateBlendState(&blendDesc, &s_NoBlendState);
	}
	float blendFactor[4] = {};
	DX11Context::GetDeviceContext()->OMSetBlendState(s_NoBlendState.Get(), blendFactor, 0xffffffff);

	static Microsoft::WRL::ComPtr<ID3D11DepthStencilState> s_DepthWriteState;
	if (!s_DepthWriteState)
	{
		D3D11_DEPTH_STENCIL_DESC depthDesc = {};
		depthDesc.DepthEnable = TRUE;
		depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		DX11Context::GetDevice()->CreateDepthStencilState(&depthDesc, &s_DepthWriteState);
	}
	DX11Context::GetDeviceContext()->OMSetDepthStencilState(s_DepthWriteState.Get(), 0);

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
	if (clearDepth && m_DepthDSV)
		DX11Context::GetDeviceContext()->ClearDepthStencilView(m_DepthDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void DX11FrameBuffer::Unbind()
{
	DX11Context::BindBackBuffer();
}

void DX11FrameBuffer::Resize(uint32_t width, uint32_t height)
{
    PROFILE_SCOPE("DX11.FrameBuffer.Resize");
    if (width == 0 || height == 0)
		return;
	m_Specification.Width = width;
	m_Specification.Height = height;
	Invalidate();
}

int DX11FrameBuffer::ReadPixel(uint32_t attachmentIndex, int x, int y)
{
    PROFILE_SCOPE("DX11.FrameBuffer.ReadPixel");
    if (attachmentIndex >= m_ColorTextures.size() || x < 0 || y < 0)
		return -1;

	auto source = m_ColorTextures[attachmentIndex].Get();
	if (!source)
		return -1;

	D3D11_TEXTURE2D_DESC sourceDesc = {};
	source->GetDesc(&sourceDesc);
	if ((uint32_t)x >= sourceDesc.Width || (uint32_t)y >= sourceDesc.Height ||
		sourceDesc.SampleDesc.Count > 1)
		return -1;

	D3D11_TEXTURE2D_DESC stagingDesc = sourceDesc;
	stagingDesc.Width = 1;
	stagingDesc.Height = 1;
	stagingDesc.MipLevels = 1;
	stagingDesc.ArraySize = 1;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingDesc.MiscFlags = 0;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
	HRESULT hr = DX11Context::GetDevice()->CreateTexture2D(&stagingDesc, nullptr, &staging);
	if (FAILED(hr) || !staging)
		return -1;

	D3D11_BOX sourceBox = {};
	sourceBox.left = (UINT)x;
	sourceBox.top = (UINT)y;
	sourceBox.front = 0;
	sourceBox.right = (UINT)x + 1;
	sourceBox.bottom = (UINT)y + 1;
	sourceBox.back = 1;

	auto context = DX11Context::GetDeviceContext();
	context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, source, 0, &sourceBox);

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
	if (FAILED(hr))
		return -1;

	int pixelData = -1;
	if (m_ColorAttachmentSpecifications[attachmentIndex].TextureFormat == FrameBufferTextureFormat::RED_INTEGER)
		pixelData = *reinterpret_cast<const int*>(mapped.pData);
	else
		pixelData = static_cast<int>(*reinterpret_cast<const uint8_t*>(mapped.pData));

	context->Unmap(staging.Get(), 0);
	return pixelData;
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

uint64_t DX11FrameBuffer::GetColorAttachmentRendererID(uint32_t index) const
{
	if (index < m_ColorSRVs.size())
		return (uint64_t)m_ColorSRVs[index].Get();
	return 0;
}

uint64_t DX11FrameBuffer::GetDepthAttachmentRendererID() const
{
	return (uint64_t)m_DepthSRV.Get();
}

void DX11FrameBuffer::ResolveTo(const Ref<FrameBuffer>& target, const std::vector<uint32_t>& attachmentIndices,
	bool resolveDepth)
{
	Ref<DX11FrameBuffer> targetFramebuffer = std::dynamic_pointer_cast<DX11FrameBuffer>(target);
	if (!targetFramebuffer)
		return;

	auto context = DX11Context::GetDeviceContext();
	for (uint32_t attachmentIndex : attachmentIndices)
	{
		if (attachmentIndex >= m_ColorTextures.size() ||
			attachmentIndex >= targetFramebuffer->m_ColorTextures.size())
			continue;

		D3D11_TEXTURE2D_DESC sourceDesc = {};
		m_ColorTextures[attachmentIndex]->GetDesc(&sourceDesc);
		if (sourceDesc.SampleDesc.Count > 1)
			context->ResolveSubresource(targetFramebuffer->m_ColorTextures[attachmentIndex].Get(), 0,
				m_ColorTextures[attachmentIndex].Get(), 0, sourceDesc.Format);
		else
			context->CopyResource(targetFramebuffer->m_ColorTextures[attachmentIndex].Get(),
				m_ColorTextures[attachmentIndex].Get());
	}

	if (resolveDepth && m_DepthTexture && targetFramebuffer->m_DepthTexture)
		context->CopyResource(targetFramebuffer->m_DepthTexture.Get(), m_DepthTexture.Get());
}

void DX11FrameBuffer::Invalidate()
{
    PROFILE_SCOPE("DX11.FrameBuffer.Invalidate");
    m_ColorTextures.clear();
	m_ColorRTVs.clear();
	m_ColorSRVs.clear();
	m_DepthTexture.Reset();
	m_DepthDSV.Reset();
	m_DepthSRV.Reset();

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
		desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(device->CreateTexture2D(&desc, nullptr, &m_DepthTexture)))
		{
			ERROR("Failed to create DX11 depth texture");
			return;
		}

		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		if (FAILED(device->CreateDepthStencilView(m_DepthTexture.Get(), &dsvDesc, &m_DepthDSV)))
			ERROR("Failed to create DX11 depth DSV");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		if (FAILED(device->CreateShaderResourceView(m_DepthTexture.Get(), &srvDesc, &m_DepthSRV)))
			ERROR("Failed to create DX11 depth SRV");
	}
}

#include "stdsfx.h"
#include "DX11Renderer.h"
#include "DX11Context.h"

#include <d3d11.h>

void DX11Renderer::Init()
{
	EnableDepthTest(true);

	D3D11_RASTERIZER_DESC rasterizerDesc = {};
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	rasterizerDesc.DepthClipEnable = TRUE;
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState;
	DX11Context::GetDevice()->CreateRasterizerState(&rasterizerDesc, &rasterizerState);
	DX11Context::GetDeviceContext()->RSSetState(rasterizerState.Get());

	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	Microsoft::WRL::ComPtr<ID3D11BlendState> blendState;
	DX11Context::GetDevice()->CreateBlendState(&blendDesc, &blendState);
	float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	DX11Context::GetDeviceContext()->OMSetBlendState(blendState.Get(), blendFactor, 0xffffffff);
}

void DX11Renderer::SetViewport(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = (float)x;
	viewport.TopLeftY = (float)y;
	viewport.Width = (float)w;
	viewport.Height = (float)h;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	DX11Context::GetDeviceContext()->RSSetViewports(1, &viewport);
	DX11Context::ResizeBackBuffer(w, h);
}

void DX11Renderer::SetClearColor(const glm::vec4& color)
{
	m_ClearColor = color;
}

void DX11Renderer::Clear()
{
	float color[4] = { m_ClearColor.r, m_ClearColor.g, m_ClearColor.b, m_ClearColor.a };
	DX11Context::BindBackBuffer();
	DX11Context::GetDeviceContext()->ClearRenderTargetView(DX11Context::GetBackBufferRTV(), color);
	DX11Context::GetDeviceContext()->ClearDepthStencilView(DX11Context::GetDepthStencilView(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void DX11Renderer::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
{
	vertexArray->Bind();
	auto indexBuffer = vertexArray->GetIndexBuffer();
	uint32_t count = indexCount ? indexCount : (indexBuffer ? indexBuffer->GetCount() : 0);
	DX11Context::GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	if (indexBuffer)
		DX11Context::GetDeviceContext()->DrawIndexed(count, 0, 0);
	else
		DX11Context::GetDeviceContext()->Draw(count, 0);
}

void DX11Renderer::DrawLines(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
{
	vertexArray->Bind();
	DX11Context::GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
	if (vertexArray->GetIndexBuffer())
		DX11Context::GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
	else
		DX11Context::GetDeviceContext()->Draw(indexCount, 0);
}

void DX11Renderer::SetLineWidth(float width)
{
	(void)width;
}

void DX11Renderer::EnableDepthTest(bool enable)
{
	Microsoft::WRL::ComPtr<ID3D11DepthStencilState> state;
	D3D11_DEPTH_STENCIL_DESC desc = {};
	desc.DepthEnable = enable ? TRUE : FALSE;
	desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	DX11Context::GetDevice()->CreateDepthStencilState(&desc, &state);
	DX11Context::GetDeviceContext()->OMSetDepthStencilState(state.Get(), 0);
}

void DX11Renderer::SetDepthRange(float min, float max)
{
	UINT count = 1;
	D3D11_VIEWPORT viewport = {};
	DX11Context::GetDeviceContext()->RSGetViewports(&count, &viewport);
	viewport.MinDepth = min;
	viewport.MaxDepth = max;
	DX11Context::GetDeviceContext()->RSSetViewports(1, &viewport);
}

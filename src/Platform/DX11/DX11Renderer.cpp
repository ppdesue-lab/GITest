#include "stdsfx.h"
#include "DX11Renderer.h"
#include "DX11Context.h"
#include "DX11Shader.h"
#include "Renderer/ProfileTimer.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstring>
#include <wrl/client.h>

namespace
{
Microsoft::WRL::ComPtr<ID3D11VertexShader> s_InstancedLineVS;
Microsoft::WRL::ComPtr<ID3D11PixelShader> s_InstancedLinePS;
Microsoft::WRL::ComPtr<ID3D11Buffer> s_InstancedLineBuffer;
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> s_InstancedLineSRV;
Microsoft::WRL::ComPtr<ID3D11Buffer> s_InstancedLineCBuffer;
uint32_t s_InstancedLineCapacity = 0;

class DX11LineInstanceBuffer : public RendererLineInstanceBuffer
{
public:
	DX11LineInstanceBuffer(const RendererLineInstance* lines, uint32_t lineCount)
		: m_LineCount(lineCount)
	{
		if (!lines || lineCount == 0)
			return;

		ID3D11Device* device = DX11Context::GetDevice();
		if (!device)
			return;

		D3D11_BUFFER_DESC bufferDesc = {};
		bufferDesc.ByteWidth = lineCount * (uint32_t)sizeof(RendererLineInstance);
		bufferDesc.Usage = D3D11_USAGE_DEFAULT;
		bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bufferDesc.StructureByteStride = sizeof(RendererLineInstance);

		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = lines;
		if (FAILED(device->CreateBuffer(&bufferDesc, &initData, &m_Buffer)))
		{
			::Log::GetCoreLogger()->error("[DX11Renderer] Failed to create static instanced line buffer");
			m_LineCount = 0;
			return;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = lineCount;
		if (FAILED(device->CreateShaderResourceView(m_Buffer.Get(), &srvDesc, &m_SRV)))
		{
			::Log::GetCoreLogger()->error("[DX11Renderer] Failed to create static instanced line SRV");
			m_Buffer.Reset();
			m_LineCount = 0;
			return;
		}
	}

	uint32_t GetLineCount() const override { return m_LineCount; }
	ID3D11ShaderResourceView* GetSRV() const { return m_SRV.Get(); }
	bool IsValid() const { return m_SRV != nullptr && m_LineCount > 0; }

private:
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_Buffer;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SRV;
	uint32_t m_LineCount = 0;
};

HRESULT CompileInstancedLineShader(const char* source, const char* entry, const char* target, ID3DBlob** blob)
{
	ID3DBlob* errors = nullptr;
	const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR;
	HRESULT hr = D3DCompile(source, strlen(source), "RendererInstancedLine", nullptr, nullptr, entry, target, flags, 0, blob, &errors);
	if (FAILED(hr) && errors)
		::Log::GetCoreLogger()->error("[DX11Renderer] Failed to compile instanced line shader: {}", (const char*)errors->GetBufferPointer());
	if (errors)
		errors->Release();
	return hr;
}

bool EnsureInstancedLineShaders(ID3D11Device* device)
{
	if (s_InstancedLineVS && s_InstancedLinePS)
		return true;

	const char* shaderSource = R"(
        struct LineInstance
        {
            float4 Start;
            float4 End;
            float4 Color;
            float4 Meta0;
            float4 Meta1;
        };

        StructuredBuffer<LineInstance> u_Lines : register(t0);

        cbuffer LineCB : register(b0)
        {
            float4x4 u_View;
            float4x4 u_Projection;
            float4x4 u_Model;
            float4 u_LineParams;
        };

        struct VSOut
        {
            float4 Position : SV_POSITION;
            float4 Color : COLOR0;
            float4 Meta0 : TEXCOORD0;
            float LinearDepth : TEXCOORD1;
        };

        float4 TransformLinePoint(float3 localPosition, out float linearDepth)
        {
            float4 local = float4(localPosition, 1.0);
            float4 world = mul(u_Model, local);
            float4 view = mul(u_View, world);
            linearDepth = max(-view.z, 0.0001);
            return mul(u_Projection, view);
        }

        float4 ResolveLineColor(float4 inputColor, float4 meta0)
        {
            if (meta0.x >= 0.5 && meta0.x < 1.5)
            {
                float colorType = meta0.y;
                if (colorType < 0.5)
                    return float4(1.0, 0.12, 0.08, 1.0);
                if (colorType < 1.5)
                    return float4(0.08, 0.88, 0.18, 1.0);
                return float4(0.15, 0.45, 1.0, 1.0);
            }
            return inputColor;
        }

        VSOut VSMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
        {
            LineInstance segment = u_Lines[instanceID];
            float startDepth = 0.0;
            float endDepth = 0.0;
            float4 clipStart = TransformLinePoint(segment.Start.xyz, startDepth);
            float4 clipEnd = TransformLinePoint(segment.End.xyz, endDepth);
            float2 startNdc = clipStart.xy / max(abs(clipStart.w), 0.000001);
            float2 endNdc = clipEnd.xy / max(abs(clipEnd.w), 0.000001);
            float2 viewport = max(u_LineParams.xy, float2(1.0, 1.0));
            float2 startScreen = startNdc * viewport;
            float2 endScreen = endNdc * viewport;
            float2 dir = endScreen - startScreen;
            float len = length(dir);
            float2 normal = len > 0.0001 ? float2(-dir.y, dir.x) / len : float2(0.0, 1.0);

            float side = (vertexID == 0 || vertexID == 2) ? -1.0 : 1.0;
            bool useEnd = vertexID >= 2;
            float4 clipPos = useEnd ? clipEnd : clipStart;
            float2 offsetNdc = normal * side * (u_LineParams.z / viewport);

            VSOut output;
            output.Position = clipPos + float4(offsetNdc * clipPos.w, 0.0, 0.0);
            output.Color = ResolveLineColor(segment.Color, segment.Meta0);
            output.Meta0 = segment.Meta0;
            output.LinearDepth = useEnd ? endDepth : startDepth;
            return output;
        }

        float4 PSMain(VSOut input) : SV_TARGET
        {
            float4 color = input.Color;
            if (input.Meta0.x >= 0.5 && input.Meta0.x < 1.5)
            {
                float depthMetric = log2(input.LinearDepth + 10.0);
                float depthSlope = abs(ddx(depthMetric)) + abs(ddy(depthMetric));
                float shade = exp(-60.0 * depthSlope * 4.0);
                shade = clamp(lerp(1.0, shade, 0.6), 0.45, 1.0);
                color.rgb *= shade;
            }
            return color;
        }
    )";

	Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
	if (FAILED(CompileInstancedLineShader(shaderSource, "VSMain", "vs_5_0", vsBlob.GetAddressOf())) ||
		FAILED(CompileInstancedLineShader(shaderSource, "PSMain", "ps_5_0", psBlob.GetAddressOf())))
		return false;

	if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &s_InstancedLineVS)) ||
		FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &s_InstancedLinePS)))
	{
		::Log::GetCoreLogger()->error("[DX11Renderer] Failed to create instanced line shaders");
		s_InstancedLineVS.Reset();
		s_InstancedLinePS.Reset();
		return false;
	}

	return true;
}

bool EnsureInstancedLineCBuffer(ID3D11Device* device)
{
	if (s_InstancedLineCBuffer)
		return true;

	D3D11_BUFFER_DESC cbDesc = {};
	cbDesc.ByteWidth = sizeof(glm::mat4) * 3 + sizeof(glm::vec4);
	cbDesc.Usage = D3D11_USAGE_DYNAMIC;
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if (FAILED(device->CreateBuffer(&cbDesc, nullptr, &s_InstancedLineCBuffer)))
	{
		::Log::GetCoreLogger()->error("[DX11Renderer] Failed to create instanced line constant buffer");
		return false;
	}
	return true;
}

bool DrawInstancedLinesFromSRV(ID3D11ShaderResourceView* srv, uint32_t lineCount,
	const glm::mat4& view, const glm::mat4& proj, const glm::mat4& model,
	const glm::vec2& viewportSize, float lineWidth)
{
	if (!srv || lineCount == 0)
		return false;

	ID3D11Device* device = DX11Context::GetDevice();
	ID3D11DeviceContext* context = DX11Context::GetDeviceContext();
	if (!device || !context)
		return false;

	if (!EnsureInstancedLineShaders(device) || !EnsureInstancedLineCBuffer(device))
		return false;

	struct LineCBuffer
	{
		glm::mat4 View;
		glm::mat4 Projection;
		glm::mat4 Model;
		glm::vec4 Params;
	};

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	if (FAILED(context->Map(s_InstancedLineCBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return false;

	const glm::vec2 safeViewportSize(glm::max(viewportSize.x, 1.0f), glm::max(viewportSize.y, 1.0f));
	LineCBuffer cb = {};
	cb.View = view;
	cb.Projection = proj;
	cb.Model = model;
	cb.Params = glm::vec4(safeViewportSize, lineWidth, 0.0f);
	memcpy(mapped.pData, &cb, sizeof(cb));
	context->Unmap(s_InstancedLineCBuffer.Get(), 0);

	ID3D11Buffer* cbuffer = s_InstancedLineCBuffer.Get();
	context->IASetInputLayout(nullptr);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	context->VSSetShader(s_InstancedLineVS.Get(), nullptr, 0);
	context->VSSetConstantBuffers(0, 1, &cbuffer);
	context->VSSetShaderResources(0, 1, &srv);
	context->GSSetShader(nullptr, nullptr, 0);
	context->PSSetShader(s_InstancedLinePS.Get(), nullptr, 0);
	context->DrawInstanced(4, lineCount, 0, 0);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	context->VSSetShaderResources(0, 1, &nullSRV);
	return true;
}
}

void DX11Renderer::Init()
{
	EnableDepthTest(true);
	ApplyRasterizerState();

	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.RenderTarget[0].BlendEnable = FALSE;
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
	auto context = DX11Context::GetDeviceContext();
	ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
	ID3D11DepthStencilView* dsv = nullptr;
	context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);

	bool clearedColor = false;
	for (ID3D11RenderTargetView* rtv : rtvs)
	{
		if (!rtv)
			continue;
		context->ClearRenderTargetView(rtv, color);
		rtv->Release();
		clearedColor = true;
	}
	if (!clearedColor && DX11Context::GetBackBufferRTV())
		context->ClearRenderTargetView(DX11Context::GetBackBufferRTV(), color);

	if (dsv)
	{
		context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
		dsv->Release();
	}
}

void DX11Renderer::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
{
    PROFILE_SCOPE("DX11.DrawIndexed");
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
    PROFILE_SCOPE("DX11.DrawLines");
    vertexArray->Bind();
	DX11Context::GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
	if (vertexArray->GetIndexBuffer())
		DX11Context::GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
	else
		DX11Context::GetDeviceContext()->Draw(indexCount, 0);
}

void DX11Renderer::DrawPoints(const Ref<VertexArray>& vertexArray, uint32_t vertexCount)
{
    PROFILE_SCOPE("DX11.DrawPoints");
    vertexArray->Bind();
	auto context = DX11Context::GetDeviceContext();
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
	DX11Shader::SetPointExpansion(m_PointSize > 1.0f, m_PointSize);
	if (vertexArray->GetIndexBuffer())
		context->DrawIndexed(vertexCount, 0, 0);
	else
		context->Draw(vertexCount, 0);
	DX11Shader::SetPointExpansion(false, 1.0f);
}

Ref<RendererLineInstanceBuffer> DX11Renderer::CreateLineInstanceBuffer(
	const RendererLineInstance* lines, uint32_t lineCount)
{
	if (!lines || lineCount == 0)
		return nullptr;
	Ref<DX11LineInstanceBuffer> buffer = CreateRef<DX11LineInstanceBuffer>(lines, lineCount);
	return buffer->IsValid() ? buffer : nullptr;
}

bool DX11Renderer::DrawInstancedLines(const RendererLineInstance* lines, uint32_t lineCount,
	const glm::mat4& view, const glm::mat4& proj, const glm::mat4& model,
	const glm::vec2& viewportSize)
{
	PROFILE_SCOPE("DX11.DrawInstancedLines");
	if (!lines || lineCount == 0)
		return false;

	ID3D11Device* device = DX11Context::GetDevice();
	ID3D11DeviceContext* context = DX11Context::GetDeviceContext();
	if (!device || !context)
		return false;

	if (!EnsureInstancedLineShaders(device) || !EnsureInstancedLineCBuffer(device))
		return false;

	if (!s_InstancedLineBuffer || lineCount > s_InstancedLineCapacity)
	{
		s_InstancedLineBuffer.Reset();
		s_InstancedLineSRV.Reset();
		s_InstancedLineCapacity = lineCount;

		D3D11_BUFFER_DESC bufferDesc = {};
		bufferDesc.ByteWidth = (UINT)(s_InstancedLineCapacity * sizeof(RendererLineInstance));
		bufferDesc.Usage = D3D11_USAGE_DEFAULT;
		bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bufferDesc.StructureByteStride = sizeof(RendererLineInstance);

		if (FAILED(device->CreateBuffer(&bufferDesc, nullptr, &s_InstancedLineBuffer)))
		{
			::Log::GetCoreLogger()->error("[DX11Renderer] Failed to create instanced line buffer");
			s_InstancedLineCapacity = 0;
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = s_InstancedLineCapacity;
		if (FAILED(device->CreateShaderResourceView(s_InstancedLineBuffer.Get(), &srvDesc, &s_InstancedLineSRV)))
		{
			::Log::GetCoreLogger()->error("[DX11Renderer] Failed to create instanced line SRV");
			s_InstancedLineBuffer.Reset();
			s_InstancedLineCapacity = 0;
			return false;
		}
	}
	D3D11_BOX updateBox = {};
	updateBox.left = 0;
	updateBox.right = lineCount * sizeof(RendererLineInstance);
	updateBox.top = 0;
	updateBox.bottom = 1;
	updateBox.front = 0;
	updateBox.back = 1;
	context->UpdateSubresource(s_InstancedLineBuffer.Get(), 0, &updateBox, lines, 0, 0);
	return DrawInstancedLinesFromSRV(s_InstancedLineSRV.Get(), lineCount,
		view, proj, model, viewportSize, m_LineWidth);
}

bool DX11Renderer::DrawInstancedLines(const Ref<RendererLineInstanceBuffer>& lineBuffer, uint32_t lineCount,
	const glm::mat4& view, const glm::mat4& proj, const glm::mat4& model,
	const glm::vec2& viewportSize)
{
	PROFILE_SCOPE("DX11.DrawInstancedLines.Static");
	if (!lineBuffer || lineCount == 0)
		return false;

	const DX11LineInstanceBuffer* buffer = dynamic_cast<const DX11LineInstanceBuffer*>(lineBuffer.get());
	if (!buffer || !buffer->IsValid())
		return false;

	const uint32_t drawCount = glm::min(lineCount, buffer->GetLineCount());
	return DrawInstancedLinesFromSRV(buffer->GetSRV(), drawCount,
		view, proj, model, viewportSize, m_LineWidth);
}

void DX11Renderer::SetLineWidth(float width)
{
	m_LineWidth = glm::max(width, 1.0f);
}

void DX11Renderer::SetPointSize(float size)
{
	m_PointSize = glm::max(size, 1.0f);
}

void DX11Renderer::ApplyRasterizerState()
{
    PROFILE_SCOPE("DX11.CreateRasterizerState");
    D3D11_RASTERIZER_DESC desc = {};
	desc.FillMode = D3D11_FILL_SOLID;
	desc.CullMode = m_CullEnabled
		? (m_CullFace == "Front" ? D3D11_CULL_FRONT : D3D11_CULL_BACK)
		: D3D11_CULL_NONE;
	desc.DepthClipEnable = TRUE;
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> state;
	DX11Context::GetDevice()->CreateRasterizerState(&desc, &state);
	DX11Context::GetDeviceContext()->RSSetState(state.Get());
}

void DX11Renderer::Enable(const std::string& capability)
{
	if (capability == "CULL_FACE")
	{
		m_CullEnabled = true;
		ApplyRasterizerState();
	}
	else if (capability == "DEPTH_TEST")
	{
		EnableDepthTest(true);
	}
}

void DX11Renderer::Disable(const std::string& capability)
{
	if (capability == "CULL_FACE")
	{
		m_CullEnabled = false;
		ApplyRasterizerState();
	}
	else if (capability == "DEPTH_TEST")
	{
		EnableDepthTest(false);
	}
}

void DX11Renderer::Cull(const std::string& face)
{
	if (face == "Front" || face == "Back")
	{
		m_CullFace = face;
		if (m_CullEnabled)
			ApplyRasterizerState();
	}
}

void DX11Renderer::EnableDepthTest(bool enable)
{
    PROFILE_SCOPE("DX11.CreateDepthStencilState");
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

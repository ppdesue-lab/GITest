#include "stdsfx.h"
#include "DX11Buffer.h"
#include "DX11Context.h"

DX11VertexBuffer::DX11VertexBuffer(uint32_t size)
	: m_Size(size)
{
	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = size;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	DX11Context::GetDevice()->CreateBuffer(&desc, nullptr, &m_Buffer);
}

DX11VertexBuffer::DX11VertexBuffer(float* vertices, uint32_t size)
	: m_Size(size)
{
	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = size;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA data = {};
	data.pSysMem = vertices;
	DX11Context::GetDevice()->CreateBuffer(&desc, &data, &m_Buffer);
}

void DX11VertexBuffer::Bind() const
{
}

void DX11VertexBuffer::Unbind() const
{
}

void DX11VertexBuffer::SetData(const void* data, uint32_t size)
{
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	auto context = DX11Context::GetDeviceContext();
	if (SUCCEEDED(context->Map(m_Buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		memcpy(mapped.pData, data, (std::min)(size, m_Size));
		context->Unmap(m_Buffer.Get(), 0);
	}
}

DX11IndexBuffer::DX11IndexBuffer(uint32_t* indices, uint32_t count)
	: m_Count(count)
{
	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = count * sizeof(uint32_t);
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	D3D11_SUBRESOURCE_DATA data = {};
	data.pSysMem = indices;
	DX11Context::GetDevice()->CreateBuffer(&desc, indices ? &data : nullptr, &m_Buffer);
}

DX11IndexBuffer::DX11IndexBuffer(uint32_t count)
	: DX11IndexBuffer(nullptr, count)
{
}

void DX11IndexBuffer::Bind() const
{
	DX11Context::GetDeviceContext()->IASetIndexBuffer(m_Buffer.Get(), DXGI_FORMAT_R32_UINT, 0);
}

void DX11IndexBuffer::Unbind() const
{
	DX11Context::GetDeviceContext()->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
}

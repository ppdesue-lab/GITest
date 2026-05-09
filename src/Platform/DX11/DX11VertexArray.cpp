#include "stdsfx.h"
#include "DX11VertexArray.h"
#include "DX11Buffer.h"
#include "DX11Context.h"
#include "DX11Shader.h"

void DX11VertexArray::Bind() const
{
	std::vector<ID3D11Buffer*> buffers;
	std::vector<UINT> strides;
	std::vector<UINT> offsets;
	for (auto& vertexBuffer : m_VertexBuffers)
	{
		auto dxBuffer = std::dynamic_pointer_cast<DX11VertexBuffer>(vertexBuffer);
		if (!dxBuffer)
			continue;
		buffers.push_back(dxBuffer->GetBuffer());
		strides.push_back(dxBuffer->GetLayout().GetStride());
		offsets.push_back(0);
	}
	if (!buffers.empty())
		DX11Context::GetDeviceContext()->IASetVertexBuffers(0, (UINT)buffers.size(), buffers.data(), strides.data(), offsets.data());

	if (m_IndexBuffer)
		m_IndexBuffer->Bind();

	if (!m_VertexBuffers.empty())
		DX11Shader::ApplyCurrentInputLayout(m_VertexBuffers[0]->GetLayout());
}

void DX11VertexArray::Unbind() const
{
}

void DX11VertexArray::AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer)
{
	m_VertexBuffers.push_back(vertexBuffer);
}

void DX11VertexArray::SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer)
{
	m_IndexBuffer = indexBuffer;
}

#pragma once

#include <Renderer/VertexArray.h>

class DX11VertexArray : public VertexArray
{
public:
	DX11VertexArray() = default;
	~DX11VertexArray() override = default;

	void Bind() const override;
	void Unbind() const override;
	void AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer) override;
	void SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer) override;
	const std::vector<Ref<VertexBuffer>>& GetVertexBuffers() const override { return m_VertexBuffers; }
	const Ref<IndexBuffer>& GetIndexBuffer() const override { return m_IndexBuffer; }

private:
	std::vector<Ref<VertexBuffer>> m_VertexBuffers;
	Ref<IndexBuffer> m_IndexBuffer;
};

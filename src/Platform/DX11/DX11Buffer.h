#pragma once

#include <Renderer/Buffer.h>
#include <d3d11.h>
#include <wrl/client.h>

class DX11VertexBuffer : public VertexBuffer
{
public:
	DX11VertexBuffer(uint32_t size);
	DX11VertexBuffer(float* vertices, uint32_t size);
	~DX11VertexBuffer() override = default;

	void Bind() const override;
	void Unbind() const override;
	void SetData(const void* data, uint32_t size) override;
	const BufferLayout& GetLayout() const override { return m_Layout; }
	void SetLayout(const BufferLayout& layout) override { m_Layout = layout; }

	ID3D11Buffer* GetBuffer() const { return m_Buffer.Get(); }
	uint32_t GetSize() const { return m_Size; }

private:
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_Buffer;
	BufferLayout m_Layout;
	uint32_t m_Size = 0;
};

class DX11IndexBuffer : public IndexBuffer
{
public:
	DX11IndexBuffer(uint32_t* indices, uint32_t count);
	DX11IndexBuffer(uint32_t count);
	~DX11IndexBuffer() override = default;

	void Bind() const override;
	void Unbind() const override;
	uint32_t GetCount() const override { return m_Count; }

	ID3D11Buffer* GetBuffer() const { return m_Buffer.Get(); }

private:
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_Buffer;
	uint32_t m_Count = 0;
};

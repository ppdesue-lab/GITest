#include "Buffer.h"
#include <Renderer/Renderer.h>
#include <Platform/OpenGL/OpenGLBuffer.h>

Ref<VertexBuffer> VertexBuffer::Create(uint32_t size)
{
	switch(Renderer::GetAPI())
	{
		case Renderer::API::None:    CRITICAL("RendererAPI::None is currently not supported!"); return nullptr;
		case Renderer::API::OpenGL:  return std::make_shared<OpenGLVertexBuffer>(size);
	}
};

Ref<VertexBuffer> VertexBuffer::Create(float* vertices, uint32_t size)
{
	switch(Renderer::GetAPI())
	{
		case Renderer::API::None:    CRITICAL("RendererAPI::None is currently not supported!"); return nullptr;
		case Renderer::API::OpenGL:  return std::make_shared<OpenGLVertexBuffer>(vertices, size);
	}
};

Ref<IndexBuffer> IndexBuffer::Create(uint32_t* indices, uint32_t count)
{
	switch(Renderer::GetAPI())
	{
		case Renderer::API::None:    CRITICAL("RendererAPI::None is currently not supported!"); return nullptr;
		case Renderer::API::OpenGL:  return std::make_shared<OpenGLIndexBuffer>(indices, count);
	}
};
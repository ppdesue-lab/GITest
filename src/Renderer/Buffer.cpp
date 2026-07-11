#include "Buffer.h"
#include <Renderer/Renderer.h>
#ifdef G_OPENGL
#include <Platform/OpenGL/OpenGLBuffer.h>
#endif
#ifdef G_DX11
#include <Platform/DX11/DX11Buffer.h>
#endif

Ref<VertexBuffer> VertexBuffer::Create(uint32_t size)
{
	switch(Renderer::GetAPI())
	{
		case Renderer::API::None:    CRITICAL("RendererAPI::None is currently not supported!"); return nullptr;
#ifdef G_OPENGL
		case Renderer::API::OpenGL:  return std::make_shared<OpenGLVertexBuffer>(size);
#endif
#ifdef G_DX11
		case Renderer::API::DX11:    return std::make_shared<DX11VertexBuffer>(size);
#endif
	}
	return nullptr;
};

Ref<VertexBuffer> VertexBuffer::Create(float* vertices, uint32_t size)
{
	switch(Renderer::GetAPI())
	{
		case Renderer::API::None:    CRITICAL("RendererAPI::None is currently not supported!"); return nullptr;
#ifdef G_OPENGL
		case Renderer::API::OpenGL:  return std::make_shared<OpenGLVertexBuffer>(vertices, size);
#endif
#ifdef G_DX11
		case Renderer::API::DX11:    return std::make_shared<DX11VertexBuffer>(vertices, size);
#endif
	}
	return nullptr;
};

Ref<IndexBuffer> IndexBuffer::Create(uint32_t* indices, uint32_t count)
{
	switch(Renderer::GetAPI())
	{
		case Renderer::API::None:    CRITICAL("RendererAPI::None is currently not supported!"); return nullptr;
#ifdef G_OPENGL
		case Renderer::API::OpenGL:  return std::make_shared<OpenGLIndexBuffer>(indices, count);
#endif
#ifdef G_DX11
		case Renderer::API::DX11:    return std::make_shared<DX11IndexBuffer>(indices, count);
#endif
	}
	return nullptr;
};

Ref<IndexBuffer> IndexBuffer::Create(uint32_t count)
{
	switch (Renderer::GetAPI())
	{
	case Renderer::API::None:    CRITICAL("RendererAPI::None is currently not supported!"); return nullptr;
#ifdef G_OPENGL
	case Renderer::API::OpenGL:  return std::make_shared<OpenGLIndexBuffer>(count);
#endif
#ifdef G_DX11
	case Renderer::API::DX11:    return std::make_shared<DX11IndexBuffer>(count);
#endif
	}
	return nullptr;
};

#include "stdsfx.h"
#include "FrameBuffer.h"

#include "Renderer.h"
#include <Platform/OpenGL/OpenGLFrameBuffer.h>
#ifdef G_DX11
#include <Platform/DX11/DX11FrameBuffer.h>
#endif

Ref<FrameBuffer> FrameBuffer::Create(const FrameBufferSpecification& spec)
{
	switch (Renderer::GetAPI())
	{
	case Renderer::API::None:
		ERROR("RendererAPI::None is currently not supported!");
		return nullptr;
	case Renderer::API::OpenGL:
		return CreateRef<OpenGLFrameBuffer>(spec);
#ifdef G_DX11
	case Renderer::API::DX11:
		return CreateRef<DX11FrameBuffer>(spec);
#endif
	default:
		ERROR("Unknown RendererAPI!");
		return nullptr;
	}
}

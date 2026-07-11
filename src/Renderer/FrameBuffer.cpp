#include "stdsfx.h"
#include "FrameBuffer.h"

#include "Renderer.h"
#ifdef G_OPENGL
#include <Platform/OpenGL/OpenGLFrameBuffer.h>
#endif
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
#ifdef G_OPENGL
	case Renderer::API::OpenGL:
		return CreateRef<OpenGLFrameBuffer>(spec);
#endif
#ifdef G_DX11
	case Renderer::API::DX11:
		return CreateRef<DX11FrameBuffer>(spec);
#endif
	default:
		ERROR("Unknown RendererAPI!");
		return nullptr;
	}
}

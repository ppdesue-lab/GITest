#include "stdsfx.h"
#include "FrameBuffer.h"

#include "Renderer.h"
#include <Platform/OpenGL/OpenGLFrameBuffer.h>

Ref<FrameBuffer> FrameBuffer::Create(const FrameBufferSpecification& spec)
{
	switch (Renderer::GetAPI())
	{
	case Renderer::API::None:
		ERROR("RendererAPI::None is currently not supported!");
		return nullptr;
	case Renderer::API::OpenGL:
		return CreateRef<OpenGLFrameBuffer>(spec);
	default:
		ERROR("Unknown RendererAPI!");
		return nullptr;
	}
}
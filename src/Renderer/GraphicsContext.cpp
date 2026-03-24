#include "GraphicsContext.h"
#include "Renderer.h"

#include <Platform/OpenGL/OpenGLContext.h>

Scope<GraphicsContext> GraphicsContext::Create(void* window)
{
	switch (Renderer::GetAPI())
	{
	case Renderer::API::None:    return nullptr;
	case Renderer::API::OpenGL:  return CreateScope<OpenGLContext>(static_cast<GLFWwindow*>(window));
	}
	ERROR("Unknown RendererAPI!");
	return nullptr;
}
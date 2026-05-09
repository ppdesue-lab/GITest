#include "GraphicsContext.h"
#include "Renderer.h"

#include <Platform/OpenGL/OpenGLContext.h>
#ifdef G_DX11
#include <Platform/DX11/DX11Context.h>
#endif

Scope<GraphicsContext> GraphicsContext::Create(void* window)
{
	switch (Renderer::GetAPI())
	{
	case Renderer::API::None:    return nullptr;
	case Renderer::API::OpenGL:  return CreateScope<OpenGLContext>(static_cast<GLFWwindow*>(window));
#ifdef G_DX11
	case Renderer::API::DX11:    return CreateScope<DX11Context>(static_cast<GLFWwindow*>(window));
#endif
	}
	ERROR("Unknown RendererAPI!");
	return nullptr;
}

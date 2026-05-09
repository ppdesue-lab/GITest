#include "Renderer.h"

#include <Platform/OpenGL/OpenGLRenderer.h>
#ifdef G_DX11
#include <Platform/DX11/DX11Renderer.h>
#endif
#include <Renderer/RenderCommand.h>

#ifdef G_DX11
Renderer::API Renderer::s_API = Renderer::API::DX11;
#else
Renderer::API Renderer::s_API = Renderer::API::OpenGL;
#endif

Scope<Renderer> Renderer::Create()
{
	switch (s_API)
	{
	case API::None:    return nullptr;
	case API::OpenGL:  return CreateScope<OpenGLRenderer>();
#ifdef G_DX11
	case API::DX11:    return CreateScope<DX11Renderer>();
#endif
	}
	return nullptr;
}

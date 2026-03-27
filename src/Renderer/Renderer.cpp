#include "Renderer.h"

#include <Platform/OpenGL/OpenGLRenderer.h>
#include <Renderer/RenderCommand.h>

Renderer::API Renderer::s_API = Renderer::API::OpenGL;

Scope<Renderer> Renderer::Create()
{
	switch (s_API)
	{
	case API::None:    return nullptr;
	case API::OpenGL:  return CreateScope<OpenGLRenderer>();
	}
	return nullptr;
}

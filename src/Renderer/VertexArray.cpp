#include "VertexArray.h"

#include <Renderer/Renderer.h>
#include <Platform/OpenGL/VertexArrayOpenGL.h>
#ifdef G_DX11
#include <Platform/DX11/DX11VertexArray.h>
#endif

//create vertexarray
Ref<VertexArray> VertexArray::Create()
{
    switch (Renderer::GetAPI())
    {
    case Renderer::API::OpenGL:
        return CreateRef<VertexArrayOpenGL>();
#ifdef G_DX11
    case Renderer::API::DX11:
        return CreateRef<DX11VertexArray>();
#endif
    }

    return nullptr;
};

#include "VertexArray.h"

#include <Renderer/Renderer.h>
#include <Platform/OpenGL/VertexArrayOpenGL.h>

//create vertexarray
Ref<VertexArray> VertexArray::Create()
{
    switch (Renderer::GetAPI())
    {
    case Renderer::API::OpenGL:
        return CreateRef<VertexArrayOpenGL>();
    }

    return nullptr;
};
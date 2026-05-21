#include "stdsfx.h"
#include "TextureArray.h"

#include "Renderer.h"
#include <Platform/OpenGL/OpenGLTextureArray.h>
#ifdef G_DX11
#include <Platform/DX11/DX11TextureArray.h>
#endif

Ref<TextureArray> TextureArray::Create(uint32_t width, uint32_t height, uint32_t layers,
                                        const std::vector<Ref<Image>>& images)
{
    switch (Renderer::GetAPI())
    {
    case Renderer::API::None:
        ERROR("RendererAPI::None is not supported!");
        return nullptr;
    case Renderer::API::OpenGL:
        return CreateRef<OpenGLTextureArray>(width, height, layers, images);
#ifdef G_DX11
    case Renderer::API::DX11:
        return CreateRef<DX11TextureArray>(width, height, layers, images);
#endif
    default:
        ERROR("Unknown RendererAPI!");
        return nullptr;
    }
}

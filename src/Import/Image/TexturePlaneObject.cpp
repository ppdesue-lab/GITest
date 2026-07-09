#include "stdsfx.h"
#include "TexturePlaneObject.h"

#include <Renderer/Buffer.h>
#include <Renderer/Material.h>
#include <Renderer/Texture.h>
#include <Renderer/VertexArray.h>
#include <Renderer/VertexDesc.h>
#include <algorithm>

bool TexturePlaneObject::LoadFromImageFile(const std::filesystem::path& filepath, float maxSize)
{
    TextureHandle textureHandle = TextureLibrary::LoadTexture(filepath.u8string());
    Ref<Texture> texture = TextureLibrary::Resolve(textureHandle);
    if (!texture || texture->GetWidth() == 0 || texture->GetHeight() == 0)
    {
        ERROR("Failed to load image texture: {}", filepath.u8string());
        return false;
    }

    const float width = static_cast<float>(texture->GetWidth());
    const float height = static_cast<float>(texture->GetHeight());
    const float scale = std::max(maxSize, 0.001f) / std::max(width, height);
    const float halfWidth = width * scale * 0.5f;
    const float halfHeight = height * scale * 0.5f;
    const glm::vec3 normal(0.0f, 0.0f, 1.0f);

    std::vector<VertexNormalTexture> vertices = {
        VertexNormalTexture(glm::vec3(-halfWidth, -halfHeight, 0.0f), normal, glm::vec2(0.0f, 0.0f)),
        VertexNormalTexture(glm::vec3( halfWidth, -halfHeight, 0.0f), normal, glm::vec2(1.0f, 0.0f)),
        VertexNormalTexture(glm::vec3( halfWidth,  halfHeight, 0.0f), normal, glm::vec2(1.0f, 1.0f)),
        VertexNormalTexture(glm::vec3(-halfWidth,  halfHeight, 0.0f), normal, glm::vec2(0.0f, 1.0f)),
    };
    std::vector<uint32_t> indices = { 0, 1, 2, 2, 3, 0 };

    Ref<VertexArray> vertexArray = VertexArray::Create();
    Ref<VertexBuffer> vertexBuffer = VertexBuffer::Create(
        reinterpret_cast<float*>(vertices.data()),
        static_cast<uint32_t>(vertices.size() * sizeof(VertexNormalTexture)));
    vertexBuffer->SetLayout({
        { ShaderDataType::Float3, "a_Position", false },
        { ShaderDataType::Float3, "a_Normal", false },
        { ShaderDataType::Float2, "a_TexCoord", false },
    });
    vertexArray->AddVertexBuffer(vertexBuffer);
    Ref<IndexBuffer> indexBuffer = IndexBuffer::Create(indices.data(), static_cast<uint32_t>(indices.size()));
    vertexArray->SetIndexBuffer(indexBuffer);
    vertexArray->Unbind();

    Ref<Mesh> mesh = CreateRef<Mesh>();
    mesh->VertexObject = GeometryLibrary::Register(vertexArray);
    mesh->TraceVertices = vertices;
    mesh->TraceIndices = indices;
    mesh->UpdateBoundingSphere();

    Ref<MaterialPBR> material = CreateRef<MaterialPBR>(glm::vec3(1.0f), 0.0f, 0.8f);
    material->AlbedoMap = textureHandle;
    material->AmbientOcclusion = 1.0f;
    mesh->Mat = material;

    Meshes.clear();
    Meshes.push_back(mesh);
    UpdateBoundingSphere();
    return true;
}

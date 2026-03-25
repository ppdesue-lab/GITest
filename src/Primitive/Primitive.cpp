#include "stdsfx.h"
#include "Primitive.h"


Axis::Axis(const glm::vec3& default_len )
{
    Length = default_len;

    Create();
}


void Axis::Create()
{
    std::vector<glm::vec3>vertices =
    {
        glm::vec3(0.0f, 0.0f, 0.0f),glm::vec3(1.0f, 1.0f, 1.0f),
        glm::vec3(Length.x, 0.0f, 0.0f),glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, Length.y, 0.0f),glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, Length.z),glm::vec3(0.0f, 0.0f, 1.0f)
    };
    std::vector<unsigned int> indices = { 0, 1, 0, 2, 0, 3 };

    m_VertexArray = VertexArray::Create();

    uint32_t sizeInBytes = vertices.size() * sizeof(glm::vec3);
    auto vertexBuffer = VertexBuffer::Create((float*)&vertices[0].x, sizeInBytes);
    
    BufferLayout layout={
        BufferElement(ShaderDataType::Float3,"a_Position",false),
        BufferElement(ShaderDataType::Float3,"a_Color",false),
    };
    vertexBuffer->SetLayout(layout);
    m_VertexArray->AddVertexBuffer(vertexBuffer);
    
    auto m_IndexBuffer = IndexBuffer::Create(indices.data(), indices.size());
    m_VertexArray->SetIndexBuffer(m_IndexBuffer);

    m_Count = indices.size();

}

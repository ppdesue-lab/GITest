#include "stdsfx.h"
#include "Primitive.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <Renderer/VertexDesc.h>

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

    m_VertexArray->Unbind();

}


Sphere::Sphere(float radius, uint32_t sectorCount, uint32_t stackCount)
{
    Create(radius, sectorCount, stackCount);
}

void Sphere::Create(float radius, uint32_t sectorCount, uint32_t stackCount)
{
    std::vector<glm::vec3> vertices;
    std::vector<unsigned int> indices;
    for (uint32_t i = 0; i <= stackCount; ++i) {
        float stackAngle = glm::pi<float>() / 2 - i * glm::pi<float>() / stackCount; // 从 pi/2 到 -pi/2
        float xy = radius * cosf(stackAngle); // r * cos(u)
        float z = radius * sinf(stackAngle);  // r * sin(u)
        for (uint32_t j = 0; j <= sectorCount; ++j) {
            float sectorAngle = j * 2 * glm::pi<float>() / sectorCount; // 从 0 到 2pi
            float x = xy * cosf(sectorAngle); // r * cos(u) * cos(v)
            float y = xy * sinf(sectorAngle); // r * cos(u) * sin(v)
            vertices.emplace_back(x, y, z);
        }
    }
    for (uint32_t i = 0; i < stackCount; ++i) {
        uint32_t k1 = i * (sectorCount + 1);     // 当前栈的第一个顶点
        uint32_t k2 = k1 + sectorCount + 1;      // 下一个栈的第一个顶点
        for (uint32_t j = 0; j < sectorCount; ++j, ++k1, ++k2) {
            if (i != 0) {
                indices.push_back(k1);
                indices.push_back(k2);
                indices.push_back(k1 + 1);
            }
            if (i != (stackCount - 1)) {
                indices.push_back(k1 + 1);
                indices.push_back(k2);
                indices.push_back(k2 + 1);
            }
        }
    }
    m_VertexArray = VertexArray::Create();
    uint32_t sizeInBytes = vertices.size() * sizeof(glm::vec3);
    auto vertexBuffer = VertexBuffer::Create((float*)&vertices[0].x, sizeInBytes);
    BufferLayout layout = {
        BufferElement(ShaderDataType::Float3,"a_Position",false),
    };
	vertexBuffer->
        SetLayout(layout);
    m_VertexArray->AddVertexBuffer(vertexBuffer);
    auto m_IndexBuffer = IndexBuffer::Create(indices.data(), indices.size());
    m_VertexArray->SetIndexBuffer(m_IndexBuffer);
    m_Count = indices.size();
	m_VertexArray->Unbind();
}

Plane::Plane(float size)
{
    Create(size);
}

void Plane::Create(float size)
{
    std::vector<VertexColor> vertices = {
        VertexColor{{-size, 0.0f, -size}, {1.0f, 1.0f, 1.0f,0.5f}},
        VertexColor{{size, 0.0f, -size},  {1.0f, 0.0f, 1.0f,0.5f}},
        VertexColor{{size, 0.0f, size},   {1.0f, 1.0f, 0.0f,0.5f}},
        VertexColor{{-size, 0.0f, size},  {0.0f, 1.0f, 1.0f,0.5f}}
    };
    std::vector<unsigned int> indices = {
        0, 1, 2, 2, 3, 0
    };
    m_VertexArray = VertexArray::Create();
    uint32_t sizeInBytes = vertices.size() * sizeof(VertexColor);
    auto vertexBuffer = VertexBuffer::Create((float*)&vertices[0].Position.x,sizeInBytes);
    BufferLayout layout = {
        BufferElement(ShaderDataType::Float3,"a_Position",false),
        BufferElement(ShaderDataType::Float4,"a_Color",false),
    };
    vertexBuffer->SetLayout(layout);
    m_VertexArray->AddVertexBuffer(vertexBuffer);
    auto m_IndexBuffer = IndexBuffer::Create(indices.data(), indices.size());
    m_VertexArray->SetIndexBuffer(m_IndexBuffer);
    m_Count = indices.size();
    m_VertexArray->Unbind();
}




Cube::Cube(float size)
{
    Create(size);
}

void Cube::Create(float size)
{
    std::vector<glm::vec3> vertices = {
        {-size, -size, -size}, {size, -size, -size}, {size, size, -size}, {-size, size, -size},
        {-size, -size, size}, {size, -size, size}, {size, size, size}, {-size, size, size}
    };
    std::vector<unsigned int> indices = {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4,
        0, 1, 5, 5, 4, 0,
        2, 3, 7, 7, 6, 2,
        0, 3, 7, 7, 4, 0,
        1, 2, 6, 6, 5, 1
    };
    m_VertexArray = VertexArray::Create();
    uint32_t sizeInBytes = vertices.size() * sizeof(glm::vec3);
    auto vertexBuffer = VertexBuffer::Create((float*)&vertices[0].x,sizeInBytes);
    BufferLayout layout = {
        BufferElement(ShaderDataType::Float3,"a_Position",false),
    };
    vertexBuffer->SetLayout(layout);
    m_VertexArray->AddVertexBuffer(vertexBuffer);
    auto m_IndexBuffer = IndexBuffer::Create(indices.data(), indices.size());
    m_VertexArray->SetIndexBuffer(m_IndexBuffer);
    m_Count = indices.size();
    m_VertexArray->Unbind();
}
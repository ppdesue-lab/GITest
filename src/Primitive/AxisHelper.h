#pragma once

#include <Renderer/VertexArray.h>
#include <Renderer/Buffer.h>
#include <Renderer/VertexDesc.h>
#include <glm/glm.hpp>

class AxisHelper
{
public:
    AxisHelper(const glm::vec3& length = glm::vec3(100, 100, 100))
    {
        std::vector<glm::vec3> vertices = {
            {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f},
            {length.x, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
            {0.0f, length.y, 0.0f}, {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, length.z}, {0.0f, 0.0f, 1.0f}
        };
        std::vector<unsigned int> indices = {0, 1, 0, 2, 0, 3};

        m_VertexArray = VertexArray::Create();
        uint32_t sizeInBytes = vertices.size() * sizeof(glm::vec3);
        auto vb = VertexBuffer::Create((float*)&vertices[0].x, sizeInBytes);
        vb->SetLayout({
            {ShaderDataType::Float3, "a_Position", false},
            {ShaderDataType::Float3, "a_Color", false},
        });
        m_VertexArray->AddVertexBuffer(vb);
        auto ib = IndexBuffer::Create(indices.data(), indices.size());
        m_VertexArray->SetIndexBuffer(ib);
        m_Count = indices.size();
        m_VertexArray->Unbind();
    }

    Ref<VertexArray> GetVertexArray() const { return m_VertexArray; }
    uint32_t GetCount() const { return m_Count; }

private:
    Ref<VertexArray> m_VertexArray;
    uint32_t m_Count = 0;
};

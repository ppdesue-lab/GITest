#pragma once

#include "Renderer.h"
#include "VertexArray.h"


struct LineVertex
{
    glm::vec3 Position;
    glm::vec3 Color;
};

class RendererData
{
public:

    //line
    uint32_t DrawLineCalls = 0;
    uint32_t LineCount = 0;
    Ref<VertexArray> LineVertexArray;
    Ref<VertexBuffer> LineVertexBuffer;
    LineVertex* LineVertexBufferBase = nullptr;
    Ref<Shader> LineShader = nullptr;

    uint32_t MaxLineVertices = 10000;

    RendererData()
    {
        LineVertexArray = VertexArray::Create();
        LineVertexBuffer = VertexBuffer::Create(MaxLineVertices * sizeof(glm::vec3));
        LineVertexBuffer->SetLayout({ {
            BufferElement{ShaderDataType::Float3, "a_Position"},
            BufferElement{ShaderDataType::Float3, "a_Color"}
            } });

        LineVertexArray->AddVertexBuffer(LineVertexBuffer);
        auto lineIndexBuffer = IndexBuffer::Create(MaxLineVertices);
        LineVertexArray->SetIndexBuffer(lineIndexBuffer);

        LineVertexBufferBase = new LineVertex[MaxLineVertices];

    }

};

class RenderCommand
{
public:
    static void Init()
    {
        s_RendererAPI->Init();
		s_RendererData = CreateRef<RendererData>();
    }
    static void SetViewport(int x, int y, int w, int h)
    {
        s_RendererAPI->SetViewport(x, y, w, h);
    };

    static void SetClearColor(const glm::vec4& color)
    {
        s_RendererAPI->SetClearColor(color);
    };

    static void Clear()
    {
        s_RendererAPI->Clear();
    };

    static void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0)
    {
        s_RendererAPI->DrawIndexed(vertexArray, indexCount);
    };

    static void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount)
    {
        s_RendererAPI->DrawLines(vertexArray, vertexCount);
    };

    static void SetLineWidth(float width)
    {
        s_RendererAPI->SetLineWidth(width);
    }


    //flush data
    static void FlushLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& color);

    static void Flush();

private:
    static Scope<Renderer> s_RendererAPI;
	static Ref<RendererData> s_RendererData;
};
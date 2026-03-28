#pragma once

#include "Renderer.h"
#include "VertexArray.h"
#include <Renderer/Shader.h>

struct LineVertex
{
    glm::vec3 Position;
    glm::vec4 Color;
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

    //quad
    uint32_t QuadCount = 0;
    Ref<VertexArray> QuadVertexArray;
    Ref<VertexBuffer> QuadVertexBuffer;
    LineVertex* QuadVertexBufferBase = nullptr;
    Ref<Shader> QuadShader = nullptr;
    uint32_t MaxQuadVertices = 10000;

    RendererData();

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

    static void EnableDepthTest(bool enable)
    {
        s_RendererAPI->EnableDepthTest(enable);
    };

    static void SetDepthRange(float min = 0.0f, float max = 1.0f)
    {
        s_RendererAPI->SetDepthRange(min, max);
	};

    //flush data
    static void FlushLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color);
    static void FlushQuad(const glm::vec3& p0, const glm::vec3& p1, 
        const glm::vec3& p2, const glm::vec3& p3,
        const glm::vec4& color);
    static void FlushTriangle(const glm::vec3& p0, const glm::vec3& p1,
        const glm::vec3& p2,
        const glm::vec4& color);

    static void Flush();

private:
    static Scope<Renderer> s_RendererAPI;
	static Ref<RendererData> s_RendererData;
};
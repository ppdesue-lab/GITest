#pragma once

#include "Renderer.h"
#include "VertexArray.h"

class RenderCommand
{
public:
    static void Init()
    {
        s_RendererAPI->Init();
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

private:
    static Scope<Renderer> s_RendererAPI;
};
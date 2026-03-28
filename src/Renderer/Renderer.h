#pragma once

#include <glm/glm.hpp>
#include <base.h>
#include "VertexArray.h"
#include "Shader.h"

class Renderer
{
public:

    enum class API
    {
        None, OpenGL
    };


public:
    virtual ~Renderer() = default;

    //init
    virtual void Init() = 0;
    virtual void SetViewport(uint32_t x, uint32_t y, uint32_t w, uint32_t h) = 0;
    virtual void SetClearColor(const glm::vec4& color) = 0;
    virtual void Clear() = 0;

    //drawing
    virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) = 0;
    virtual void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t indexCount) = 0;

	virtual void SetLineWidth(float width) = 0;

	virtual void EnableDepthTest(bool enable) = 0;
    virtual void SetDepthRange(float min = 0.0f, float max = 1.0f) = 0;


    static API GetAPI() { return s_API; }
    static Scope<Renderer> Create();

private:
    static API s_API;
};
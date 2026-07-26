#pragma once

#include <glm/glm.hpp>
#include <base.h>
#include <string>
#include "VertexArray.h"
#include "Shader.h"

struct RendererLineInstance
{
    glm::vec4 Start = glm::vec4(0.0f);
    glm::vec4 End = glm::vec4(0.0f);
    glm::vec4 Color = glm::vec4(1.0f);
    glm::vec4 Meta0 = glm::vec4(0.0f);
    glm::vec4 Meta1 = glm::vec4(0.0f);
};

class RendererLineInstanceBuffer
{
public:
    virtual ~RendererLineInstanceBuffer() = default;
    virtual uint32_t GetLineCount() const = 0;
};

class Renderer
{
public:

    enum class API
    {
        None, OpenGL, DX11
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
    virtual void DrawPoints(const Ref<VertexArray>& vertexArray, uint32_t vertexCount) = 0;
    virtual Ref<RendererLineInstanceBuffer> CreateLineInstanceBuffer(
        const RendererLineInstance* lines, uint32_t lineCount) = 0;
    virtual bool DrawInstancedLines(const RendererLineInstance* lines, uint32_t lineCount,
        const glm::mat4& view, const glm::mat4& proj, const glm::mat4& model,
        const glm::vec2& viewportSize) = 0;
    virtual bool DrawInstancedLines(const Ref<RendererLineInstanceBuffer>& lineBuffer, uint32_t lineCount,
        const glm::mat4& view, const glm::mat4& proj, const glm::mat4& model,
        const glm::vec2& viewportSize) = 0;

	virtual void SetLineWidth(float width) = 0;
    virtual void SetPointSize(float size) = 0;

    virtual void Enable(const std::string& capability) = 0;
    virtual void Disable(const std::string& capability) = 0;
    virtual void Cull(const std::string& face) = 0;
	virtual void EnableDepthTest(bool enable) = 0;
    virtual void SetDepthRange(float min = 0.0f, float max = 1.0f) = 0;


    static API GetAPI() { return s_API; }
    static Scope<Renderer> Create();

private:
    static API s_API;
};

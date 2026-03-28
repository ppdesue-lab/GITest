#pragma once

#include <Renderer/Renderer.h>
#include <Renderer/VertexArray.h>

class OpenGLRenderer : public Renderer
{
public:
    virtual void Init() override;
    virtual void SetViewport(uint32_t x,uint32_t y,uint32_t w,uint32_t h) override;
    virtual void SetClearColor(const glm::vec4& color)override;
    virtual void Clear()override;

    //drawing
    virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount=0) override;
    virtual void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t indexCount) override;


    virtual void SetLineWidth(float width) override;

	virtual void EnableDepthTest(bool enable) override;
    virtual void SetDepthRange(float min = 0.0f, float max = 1.0f) override;

};
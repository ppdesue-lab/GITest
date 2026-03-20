#pragma once

#include <Renderer/Renderer.h>

class OpenGLRenderer : public Renderer
{
public:
    virtual void Init() override;
    virtual void SetViewport(uint32_t x,uint32_t y,uint32_t w,uint32_t h) override;
    virtual void SetClearColor(const glm::vec4& color)override;
    virtual void Clear()override;

    //drawing
    virtual void DrawIndexed() override;
    virtual void DrawLines() override;
};
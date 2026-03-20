#pragma once

#include "Layer.h"

class ImGuiLayer : public Layer
{
public:
    ImGuiLayer();
    virtual ~ImGuiLayer() = default;

    virtual void OnAttach() override;
    virtual void OnDetach() override;
    virtual void OnUpdate() override;
    virtual void OnRender() override;
    virtual void OnEvent(Event& event) override;

    void Begin();
    void End();

private:
    float m_Time = 0.0f;
};

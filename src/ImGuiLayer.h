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
    virtual void OnImGuiRender() override;
    virtual void OnEvent(Event& event) override;

    void SetDarkThemeColors();

    void Begin();
    void End();

private:
    float m_Time = 0.0f;
};

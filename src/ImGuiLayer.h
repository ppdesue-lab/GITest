#pragma once

#include <base.h>
#include "Layer.h"
#include "MouseEvent.h"
#include "KeyEvent.h"
#include <Camera/Camera.h>
#include <Transform.h>

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

    bool OnMouseButtonDown(MouseButtonPressedEvent& e);
    bool OnMouseButtonUp(MouseButtonReleasedEvent& e);
    bool OnMouseMove(MouseMovedEvent& e);

private:
    bool m_LeftDownCamera = false;
    bool m_LeftDownGizmo = false;

    glm::vec2 m_MousePos;
	Transform g_DefaultTransform;
	Ref<Camera> m_Camera;
    float m_Time = 0.0f;
};

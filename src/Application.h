#pragma once

#include "WindowInterface.h"
#include "Log.h"
#include "base.h"
#include "Event.h"
#include "MouseEvent.h"
#include "KeyEvent.h"
#include "ApplicationEvent.h"
#include "LayerStack.h"
#include "ImGuiLayer.h"
#include <Primitive/Primitive.h>
#include <memory>
#include <Renderer/Shader.h>
#include <Camera/Camera.h>
#include <Transform.h>

class Application
{
public:
    Application(int width=1200,int height=800);
    virtual ~Application() = default;
    virtual void Run();

    WindowInterface& GetWindow() { return *m_WindowInterface; }

    void OnEvent(Event& e);

    void PushLayer(std::shared_ptr<Layer> layer);
    void PushOverlay(std::shared_ptr<Layer> overlay);
    void PopLayer(std::shared_ptr<Layer> layer);
    void PopOverlay(std::shared_ptr<Layer> overlay);

    static Application& Get() { return *s_Instance; }

    bool OnWindowClose(WindowCloseEvent& e);
    bool OnWindowResize(WindowResizeEvent& e);

public:
	Ref<ShaderLibrary> GetShaderLibrary() { return m_ShaderLibrary; }
	Ref<Camera> GetCamera() { return m_Camera; }
	void BindGizmoTargetTransform(Transform* transform) { m_GizmoTargetTransform = transform; }
	Transform* GetGizmoTargetTransform() { return m_GizmoTargetTransform; }

private:
    WindowInterface* m_WindowInterface = nullptr;
    LayerStack m_LayerStack;
	bool m_Running = true;

	Ref<ImGuiLayer> m_ImGuiLayer;
	Ref<ShaderLibrary> m_ShaderLibrary;
	Ref<Camera> m_Camera;



    Scope<Cube> m_backgroundCube;

    glm::vec2 m_MousePos;
	Transform* m_GizmoTargetTransform = nullptr;

    static Application* s_Instance;
};
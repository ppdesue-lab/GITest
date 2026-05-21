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
#include "Scene.h"
#include <filesystem>
#include <Primitive/Primitive.h>
#include <memory>
#include <Renderer/Shader.h>
#include <Renderer/FrameBuffer.h>
#include <Camera/Camera.h>
#include <Transform.h>
#include <Object3D.h>

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
    void Close();

public:
	Ref<ShaderLibrary> GetShaderLibrary() { return m_ShaderLibrary; }
	Ref<Camera> GetCamera() { return m_Camera; }

	// Scene management
	Scene& GetScene() { return m_Scene; }
	Ref<Object3D> LoadObject3D(const std::filesystem::path& filepath);
	void ClearObject3Ds();
	Ref<Object3D> CreatePrimitive(const std::string& name, const std::vector<VertexColor>& vertices, const std::vector<uint32_t>& indices);

	// Viewport
	Ref<FrameBuffer> GetViewportFBO() { return m_ViewportFBO; }
	const glm::vec2& GetViewportSize() const { return m_ViewportSize; }
	void SetViewportSize(const glm::vec2& size);
	glm::vec2& GetViewportMousePos() { return m_ViewportMousePos; }
	const glm::vec2& GetViewportOrigin() const { return m_ViewportOrigin; }
	void SetViewportOrigin(const glm::vec2& origin) { m_ViewportOrigin = origin; }
	bool IsViewportHovered() const { return m_ViewportHovered; }
	void SetViewportHovered(bool hovered) { m_ViewportHovered = hovered; }

	// Selection (convenience delegates to Scene)
	int GetSelectedObjectIndex() const { return m_Scene.GetSelectedIndex(); }
	void SetSelectedObjectIndex(int index);
	Transform* GetGizmoTargetTransform() { return m_GizmoTargetTransform; }

	// Gizmo state
	int& GetGizmoMode() { return m_GizmoMode; }
	bool& GetGizmoLocal() { return m_GizmoLocal; }
	bool& GetGizmoView() { return m_GizmoView; }
	float& GetGizmoSize() { return m_GizmoSize; }
	float& GetGizmoLineWidth() { return m_GizmoLineWidth; }
	bool& GetLeftDownGizmo() { return m_LeftDownGizmo; }
	bool& GetLeftDownCamera() { return m_LeftDownCamera; }

	void ProcessKeyboardInput(float deltaTime);

	// App mode
	enum class AppMode { Editor, Game };
	AppMode GetAppMode() const { return m_AppMode; }
	void SetAppMode(AppMode mode) { m_AppMode = mode; }

private:
    WindowInterface* m_WindowInterface = nullptr;
    LayerStack m_LayerStack;
	bool m_Running = true;

	Ref<ImGuiLayer> m_ImGuiLayer;
	Ref<ShaderLibrary> m_ShaderLibrary;
	Ref<Camera> m_Camera;
	Scene m_Scene;

    Scope<Cube> m_backgroundCube;

    glm::vec2 m_MousePos;
	Transform* m_GizmoTargetTransform = nullptr;

	// Viewport
	Ref<FrameBuffer> m_ViewportFBO;
	glm::vec2 m_ViewportSize = { 1280.0f, 720.0f };
	glm::vec2 m_ViewportMousePos = { 0.0f, 0.0f };
	glm::vec2 m_ViewportOrigin = { 0.0f, 0.0f };
	bool m_ViewportHovered = false;

	// Gizmo state
	int m_GizmoMode = 0;
	bool m_GizmoLocal = false;
	bool m_GizmoView = false;
	float m_GizmoSize = 1.5f;
	float m_GizmoLineWidth = 2.5f;
	bool m_LeftDownGizmo = false;
	bool m_LeftDownCamera = false;

	// Keyboard state for continuous velocity-based movement
	bool m_KeyW = false, m_KeyS = false;
	bool m_KeyA = false, m_KeyD = false;
	bool m_KeyE = false, m_KeyQ = false;

	AppMode m_AppMode = AppMode::Editor;

    static Application* s_Instance;
};

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
#include <Primitive/AxisHelper.h>
#include <Renderer/CSM.h>
#include <Renderer/ProbeGI.h>
#include <Renderer/PBRIBL.h>
#include <Renderer/SSAO.h>
#include <Renderer/FXAA.h>
#include <Renderer/PathTracer.h>
#include <Renderer/SVGF.h>
#include <memory>
#include <Renderer/Shader.h>
#include <Renderer/FrameBuffer.h>
#include <Camera/Camera.h>
#include <Transform.h>
#include <Object3D.h>
#include <Animation/CameraAnimation.h>
#include <Animation/TimelineAnimation.h>
#include <Import/Vector2D/DxfLoader.h>

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
	bool OnFileDrop(FileDropEvent& e);
    void Close();

public:
	Ref<ShaderLibrary> GetShaderLibrary() { return m_ShaderLibrary; }
	Ref<Camera> GetCamera() { return m_Camera; }
	Ref<Camera> GetViewportCamera() const;

	// Scene management
	Scene& GetScene() { return m_Scene; }
	Ref<Object3D> LoadObject3D(const std::filesystem::path& filepath);
	Ref<Object3D> LoadTexturePlane(const std::filesystem::path& filepath);
	Ref<Object3D> LoadGCode(const std::filesystem::path& filepath);
	Ref<Object3D> LoadVector2D(const std::filesystem::path& filepath,
		DxfImportMode mode = DxfImportMode::LinesWithArcFit);
	Ref<Object3D> LoadManixVolume();
	Ref<Object3D> LoadDefaultTerrainCDLOD();
	Ref<Object3D> LoadTerrainHeightMap();
	Ref<Object3D> LoadWaterNode();
	Ref<Object3D> SliceSelectedModel(float layerHeight = 0.1f);
	bool LoadFileByExtension(const std::filesystem::path& filepath);
	void NewProject();
	void ClearObject3Ds();

	// Viewport
	enum class ViewportRenderMode { Editor, Rendering };
	enum class ViewportViewMode { View3D, View2D };
	Ref<FrameBuffer> GetViewportFBO() { return m_ViewportFBO; }
	uint64_t GetViewportColorTextureID() const;
	ViewportRenderMode GetViewportRenderMode() const { return m_ViewportRenderMode; }
	void SetViewportRenderMode(ViewportRenderMode mode);
	ViewportViewMode GetViewportViewMode() const { return m_ViewportViewMode; }
	void SetViewportViewMode(ViewportViewMode mode);
	bool IsViewport2D() const { return m_ViewportViewMode == ViewportViewMode::View2D; }
	bool IsViewport2DEditMode() const { return IsViewport2D() && m_Viewport2DEditMode; }
	void SetViewport2DEditMode(bool enabled);
	void ToggleViewport2DEditMode();
	bool SetSelected2DSubElementIndex(int index);
	bool SetSelected2DSubElementIndices(const std::vector<int>& indices);
	void ClearSelected2DSubElement();
	void PanViewport2D(const glm::vec2& deltaPixels);
	void ZoomViewport2D(float wheelDelta);
	void ResetViewport2D();
	int ReadPickupPixel(int x, int y);
	void InvalidatePickupPass() { m_PickupPassDirty = true; }
	void InvalidateSelectedMask() { m_SelectedMaskValid = false; m_SelectedOutlineValid = false; }
	void RefreshPickupPass() { InvalidatePickupPass(); RenderPickupPass(); }
	int GetMSAASamples() const { return m_MSAASamples; }
	void SetMSAASamples(int samples);
	bool IsMSAAEnabled() const { return m_MSAASamples > 1; }
	const glm::vec2& GetViewportSize() const { return m_ViewportSize; }
	void SetViewportSize(const glm::vec2& size);
	glm::vec2& GetViewportMousePos() { return m_ViewportMousePos; }
	const glm::vec2& GetViewportOrigin() const { return m_ViewportOrigin; }
	void SetViewportOrigin(const glm::vec2& origin) { m_ViewportOrigin = origin; }
	bool IsViewportHovered() const { return m_ViewportHovered; }
	void SetViewportHovered(bool hovered) { m_ViewportHovered = hovered; }

	// Selection (convenience delegates to Scene)
	int GetSelectedObjectIndex() const { return m_Scene.GetSelectedIndex(); }
	const std::vector<int>& GetSelectedObjectIndices() const { return m_Scene.GetSelectedIndices(); }
	int GetSelectedObjectCount() const { return m_Scene.GetSelectedCount(); }
	bool IsObjectSelected(int index) const { return m_Scene.IsSelected(index); }
	void SetSelectedObjectIndex(int index);
	void AddSelectedObjectIndex(int index);
	Transform* GetGizmoTargetTransform() { return m_GizmoTargetTransform; }
	void BindGizmoTargetTransform(Transform* transform) { m_GizmoTargetTransform = transform; }
	bool HasMultiSelected2DSubElements() const;
	int& GetViewport2DPivotIndex() { return m_Viewport2DPivotIndex; }
	void SetViewport2DPivotIndex(int index);

	// Gizmo state
	int& GetGizmoMode() { return m_GizmoMode; }
	bool& GetGizmoLocal() { return m_GizmoLocal; }
	bool& GetGizmoView() { return m_GizmoView; }
	float& GetGizmoSize() { return m_GizmoSize; }
	float& GetGizmoLineWidth() { return m_GizmoLineWidth; }
	bool& GetLeftDownGizmo() { return m_LeftDownGizmo; }
	bool& GetLeftDownCamera() { return m_LeftDownCamera; }

	CSM& GetCSM() { return *m_CSM; }
	ProbeGI& GetProbeGI() { return *m_ProbeGI; }
	PBRIBL& GetPBRIBL() { return *m_PBRIBL; }
	SSAO& GetSSAO() { return *m_SSAO; }
	FXAA& GetFXAA() { return *m_FXAA; }
	PathTracer& GetPathTracer() { return *m_PathTracer; }
	SVGF& GetSVGF() { return *m_SVGF; }
	int& GetBackgroundMode() { return m_BackgroundMode; }
	bool GetDebugCascadeView() const { return m_DebugCascadeView; }
	void SetDebugCascadeView(bool enabled) { m_DebugCascadeView = enabled; }
	int GetFrameRateLimit() const { return m_FrameRateLimit; }
	void SetFrameRateLimit(int fps) { m_FrameRateLimit = fps <= 0 ? 0 : (fps <= 30 ? 30 : 60); }

	void ProcessKeyboardInput(float deltaTime);

	// App mode
	enum class AppMode { Editor, Game };
	AppMode GetAppMode() const { return m_AppMode; }
	void SetAppMode(AppMode mode);
	float GetDeltaTime() const { return m_DeltaTime; }
	TimelineAnimation& GetTimelineAnimation() { return m_TimelineAnimation; }
	CameraAnimation& GetCameraAnimation() { return m_CameraAnimation; }

private:
	void CreateViewportFrameBuffers();
	void InitializeWeightedBlendedOIT();
	void ResizeWeightedBlendedOIT(uint32_t width, uint32_t height);
	uint64_t CompositeWeightedBlendedOIT(uint64_t opaqueTexture, uint64_t accumulationTexture,
		uint64_t revealageTexture);
	void InitializeTransparentStepEdgeResources();
	void ResizeTransparentStepEdgeResources(uint32_t width, uint32_t height);
	void RenderTransparentDepthPrepass();
	void RenderTransparentStepEdges(uint64_t sceneDepthTexture);
	void RenderPickupPass();
	void InitializeSelectedOutlineResources();
	void ResizeSelectedOutlineResources(uint32_t width, uint32_t height);
	void RenderSelectedMaskPass();
	uint64_t CompositeSelectedOutline(uint64_t sceneColorTexture);
	void DrawViewport2DGrid(const glm::mat4& view, const glm::mat4& projection);
	void ApplyGizmoDeltaToSelection(const Transform& before, const Transform& after);
	void ApplyGizmoDeltaToSelectedObject2DPivot(const Transform& before, const Transform& after);
	void ApplyGizmoDeltaToSelected2DSubElements(const Transform& before, const Transform& after);
	void UpdateViewport2DObjectGizmoTarget(bool forceRecenter = false);
	void Update2DSubElementGizmoTarget(bool forceRecenter = false);

    WindowInterface* m_WindowInterface = nullptr;
    LayerStack m_LayerStack;
	bool m_Running = true;

	Ref<ImGuiLayer> m_ImGuiLayer;
	Ref<ShaderLibrary> m_ShaderLibrary;
	Ref<Camera> m_Camera;
	Ref<Camera> m_Camera2D;
	Scene m_Scene;

	Ref<VertexArray> m_backgroundCubeVA;
	uint32_t m_backgroundCubeCount = 0;

    glm::vec2 m_MousePos;
	Transform* m_GizmoTargetTransform = nullptr;
	Transform m_Viewport2DPivotTransform;
	int m_Viewport2DPivotIndex = 4;

	// Viewport
	Ref<FrameBuffer> m_ViewportFBO;
	Ref<FrameBuffer> m_ViewportResolvedFBO;
	Ref<FrameBuffer> m_PickupFBO;
	Ref<FrameBuffer> m_SelectedMaskFBO;
	Ref<SSAO> m_SSAO;
	Ref<FXAA> m_FXAA;
	Ref<PathTracer> m_PathTracer;
	Ref<SVGF> m_SVGF;
	Ref<Shader> m_OITCompositeShader;
	Ref<Shader> m_PickupShader;
	Ref<Shader> m_SelectedMaskShader;
	Ref<Shader> m_SelectedEdgeShader;
	uint32_t m_OITCompositeFBO = 0;
	uint32_t m_OITCompositeTexture = 0;
	uint32_t m_OITQuadVAO = 0;
	bool m_OITCompositeValid = false;
	uint32_t m_SelectedOutlineFBO = 0;
	uint32_t m_SelectedOutlineTexture = 0;
	uint32_t m_SelectedOutlineQuadVAO = 0;
	Ref<Shader> m_TransparentDepthShader;
	Ref<Shader> m_TransparentStepEdgeShader;
	uint32_t m_TransparentDepthFBO = 0;
	uint32_t m_TransparentDepthTexture = 0;
	ViewportRenderMode m_ViewportRenderMode = ViewportRenderMode::Editor;
	ViewportViewMode m_ViewportViewMode = ViewportViewMode::View3D;
	bool m_Viewport2DEditMode = false;
	int m_MSAASamples = 1;
	glm::vec2 m_ViewportSize = { 1280.0f, 720.0f };
	glm::vec2 m_ViewportMousePos = { 0.0f, 0.0f };
	glm::vec2 m_ViewportOrigin = { 0.0f, 0.0f };
	bool m_ViewportHovered = false;
	float m_SelectedEdgeWidth = 2.0f;
	bool m_SelectedOutlineValid = false;
	bool m_SelectedMaskValid = false;
	bool m_PickupPassDirty = true;

	// Gizmo state
	int m_GizmoMode = 0;
	bool m_GizmoLocal = false;
	bool m_GizmoView = false;
	float m_GizmoSize = 1.5f;
	float m_GizmoLineWidth = 2.5f;
	bool m_LeftDownGizmo = false;
	bool m_LeftDownCamera = false;
	int m_FrameRateLimit = 60;

	Ref<CSM> m_CSM;
	Ref<ProbeGI> m_ProbeGI;
	Ref<PBRIBL> m_PBRIBL;
	int m_BackgroundMode = 1; // 0: SH map, 1: environment cube map
	bool m_DebugCascadeView = false;

	// Keyboard state for continuous velocity-based movement
	bool m_KeyW = false, m_KeyS = false;
	bool m_KeyA = false, m_KeyD = false;
	bool m_KeyE = false, m_KeyQ = false;

	AppMode m_AppMode = AppMode::Editor;
	float m_DeltaTime = 0.0f;
	TimelineAnimation m_TimelineAnimation;
	CameraAnimation m_CameraAnimation;

    static Application* s_Instance;
};

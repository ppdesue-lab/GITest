#include "stdsfx.h"
#include "Application.h"
#include "ImGuiLayer.h"
#include <ApplicationEvent.h>
#include <array>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <thread>
#include <limits>
#ifdef PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef ERROR
#undef ERROR
#endif
#include <Windows.h>
#ifdef CreateWindow
#undef CreateWindow
#endif
#ifdef ERROR
#undef ERROR
#endif
#define ERROR(...)    ::Log::GetCoreLogger()->error(__VA_ARGS__)
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif
#ifdef G_OPENGL
#include <glad/glad.h>
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3.h>

#include "Renderer/Renderer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Buffer.h"
#include "Renderer/VolumeObject.h"
#include "Renderer/TerrainCDLOD.h"
#include "Renderer/TerrainHeightMap.h"

#include "Camera/FPSCamera.h"
#include "Camera/OrthographicCamera2D.h"

#include <Primitive/Gizmo.h>
#include <GeometryProcess/GeometryProcess.h>
#include <Import/GeometryProcess/SlicePreviewObject.h>
#include <Import/GCode/GCodeObject.h>
#include <Import/Image/TexturePlaneObject.h>
#include <Import/Vector2D/DxfLoader.h>
#include <Import/Vector2D/Object2D.h>

namespace
{
void SleepUntilFrameLimit(std::chrono::steady_clock::time_point targetTime)
{
    const auto now = std::chrono::steady_clock::now();
    if (now >= targetTime)
        return;

#ifdef PLATFORM_WINDOWS
    using HundredNanoseconds = std::chrono::duration<long long, std::ratio<1, 10000000>>;
    static HANDLE waitableTimer = CreateWaitableTimerExW(
        nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!waitableTimer)
        waitableTimer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);

    if (waitableTimer)
    {
        LARGE_INTEGER dueTime;
        dueTime.QuadPart = -std::chrono::duration_cast<HundredNanoseconds>(targetTime - now).count();
        if (SetWaitableTimer(waitableTimer, &dueTime, 0, nullptr, nullptr, FALSE))
        {
            WaitForSingleObject(waitableTimer, INFINITE);
            return;
        }
    }
#endif

    std::this_thread::sleep_until(targetTime);
}

glm::vec3 PivotPointFromBounds(const glm::vec3& minimum, const glm::vec3& maximum, int pivotIndex)
{
    pivotIndex = std::clamp(pivotIndex, 0, 8);
    const int column = pivotIndex % 3;
    const int row = pivotIndex / 3;

    const float x = column == 0 ? minimum.x : (column == 1 ? (minimum.x + maximum.x) * 0.5f : maximum.x);
    const float y = row == 0 ? maximum.y : (row == 1 ? (minimum.y + maximum.y) * 0.5f : minimum.y);
    return glm::vec3(x, y, 0.0f);
}

bool ObjectXYBounds(const Object3D& object, glm::vec3& minimum, glm::vec3& maximum)
{
    if (const Object2D* object2D = dynamic_cast<const Object2D*>(&object))
    {
        if (object2D->GetObjectBounds(minimum, maximum))
        {
            minimum.z = maximum.z = object.Transfm.translation.z;
            return true;
        }
    }

    bool hasBounds = false;
    minimum = glm::vec3(std::numeric_limits<float>::max());
    maximum = glm::vec3(std::numeric_limits<float>::lowest());

    for (const Ref<Mesh>& mesh : object.Meshes)
    {
        if (!mesh || mesh->TraceVertices.empty())
            continue;

        const glm::mat4 transform = object.Transfm.GetMatrix() * mesh->Transfm.GetMatrix();
        for (const VertexNormalTexture& vertex : mesh->TraceVertices)
        {
            const glm::vec3 position = glm::vec3(transform * glm::vec4(vertex.Position, 1.0f));
            minimum = glm::min(minimum, position);
            maximum = glm::max(maximum, position);
            hasBounds = true;
        }
    }

    if (!hasBounds)
    {
        minimum = object.Transfm.translation;
        maximum = object.Transfm.translation;
        hasBounds = true;
    }

    minimum.z = maximum.z = object.Transfm.translation.z;
    return hasBounds;
}

class Frustum
{
public:
    explicit Frustum(const glm::mat4& viewProjection)
    {
        const glm::mat4 rows = glm::transpose(viewProjection);
        m_Planes = {
            rows[3] + rows[0],
            rows[3] - rows[0],
            rows[3] + rows[1],
            rows[3] - rows[1],
            rows[3] + rows[2],
            rows[3] - rows[2]
        };
        for (glm::vec4& plane : m_Planes)
        {
            const float normalLength = glm::length(glm::vec3(plane));
            if (normalLength > 0.000001f)
                plane /= normalLength;
        }
    }

    bool Intersects(const BoundingSphere& sphere) const
    {
        if (!sphere.Valid)
            return true;
        for (const glm::vec4& plane : m_Planes)
        {
            if (glm::dot(glm::vec3(plane), sphere.Center) + plane.w < -sphere.Radius)
                return false;
        }
        return true;
    }

private:
    std::array<glm::vec4, 6> m_Planes;
};
}


Application* Application::s_Instance = nullptr;

Application::Application(int w,int h)
{
    Log::Init();
    INFO("Application Initialized");
    s_Instance = this;

    m_WindowInterface = CreateWindow(w, h, "KEngine Application");
    m_WindowInterface->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));
    RenderCommand::Init();

    m_Camera = CreateRef<FPSCamera>(glm::vec3(100, 100, 100), glm::vec3(0, 0, 0), 45.0f, w / (float)h);
    m_Camera2D = CreateRef<OrthographicCamera2D>(w / (float)h, 200.0f);
    m_ViewportSize = glm::vec2((float)w, (float)h);

    // Push ImGui layer as overlay
    m_ImGuiLayer = std::make_shared<ImGuiLayer>();
    PushOverlay(m_ImGuiLayer);

	m_ShaderLibrary = CreateRef<ShaderLibrary>();
    m_ShaderLibrary->LoadDefault();
    m_PickupShader = m_ShaderLibrary->Get("ObjectPickup");
    m_SelectedMaskShader = m_ShaderLibrary->Get("SelectedMask");


    // Background skybox cube (simple position-only vertex data)
    {
        std::vector<glm::vec3> bgVerts = {
            {-1, -1, -1}, { 1, -1, -1}, { 1,  1, -1}, {-1,  1, -1},
            {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}
        };
        std::vector<uint32_t> bgIdx = {
            0,1,2, 2,3,0, 4,5,6, 6,7,4,
            0,1,5, 5,4,0, 2,3,7, 7,6,2,
            0,3,7, 7,4,0, 1,2,6, 6,5,1
        };
        m_backgroundCubeVA = VertexArray::Create();
        auto vb = VertexBuffer::Create((float*)&bgVerts[0].x, bgVerts.size() * sizeof(glm::vec3));
        vb->SetLayout({ {ShaderDataType::Float3, "a_Position", false} });
        m_backgroundCubeVA->AddVertexBuffer(vb);
        auto ib = IndexBuffer::Create(bgIdx.data(), bgIdx.size());
        m_backgroundCubeVA->SetIndexBuffer(ib);
        m_backgroundCubeVA->Unbind();
        m_backgroundCubeCount = bgIdx.size();
    }

    SetGizmoViewportSize(w,h);

    CreateViewportFrameBuffers();
    m_SSAO = CreateRef<SSAO>((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
    m_FXAA = CreateRef<FXAA>((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
    InitializeWeightedBlendedOIT();
    InitializeTransparentStepEdgeResources();
    InitializeSelectedOutlineResources();
    SetGizmoViewportSize((int)m_ViewportSize.x, (int)m_ViewportSize.y);

    // CSM must be created after OpenGL context is initialized
    m_CSM = CreateRef<CSM>();
    m_ProbeGI = CreateRef<ProbeGI>();
    const std::string environmentPath = R"(D:\algorithm\kengine\build\_deps\tinybvh-src\testdata\sky_15.hdr)";//R"(E:\githubs\glslpathtracer\assets\HDR\sunset.hdr)";
    m_PBRIBL = CreateRef<PBRIBL>(environmentPath);
    m_PathTracer = CreateRef<PathTracer>((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y, environmentPath);
    m_SVGF = CreateRef<SVGF>((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

    NewProject();
}

void Application::Run()
{
    using clock = std::chrono::steady_clock;
    constexpr auto frameLimitTolerance = std::chrono::duration<float>(0.002f);
    auto lastTime = clock::now();
    float titleElapsed = 0.0f;
    uint32_t titleFrames = 0;

    while (m_Running && !m_WindowInterface->ShouldClose())
    {
        auto frameStartTime = clock::now();
        auto currentTime = frameStartTime;
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        m_DeltaTime = deltaTime;
        lastTime = currentTime;
        titleElapsed += deltaTime;
        ++titleFrames;
        if (titleElapsed >= 0.5f)
        {
            float fps = (float)titleFrames / titleElapsed;
            float frameMs = titleElapsed * 1000.0f / (float)titleFrames;
            char title[128];
            snprintf(title, sizeof(title), "KEngine Application | %.1f FPS | %.2f ms", fps, frameMs);
            m_WindowInterface->SetTitle(title);
            titleElapsed = 0.0f;
            titleFrames = 0;
        }

        Ref<Camera> viewportCamera = GetViewportCamera();
        const bool viewport2D = IsViewport2D();

        // Process continuous keyboard input (velocity-based)
        ProcessKeyboardInput(deltaTime);
        for (const auto& entry : m_Scene.GetObjects())
        {
            Ref<GCodeObject> gcodeObject = std::dynamic_pointer_cast<GCodeObject>(entry.Object);
            if (gcodeObject)
                gcodeObject->Update(deltaTime);
        }
        m_TimelineAnimation.Update(m_Scene, deltaTime);
        m_CameraAnimation.Update(m_Camera, deltaTime);
        // --- RESIZE FBO IF VIEWPORT SIZE CHANGED SINCE LAST FRAME ---
        if (m_ViewportFBO && (m_ViewportFBO->GetSpecification().Width != (uint32_t)m_ViewportSize.x ||
                              m_ViewportFBO->GetSpecification().Height != (uint32_t)m_ViewportSize.y))
        {
            m_ViewportFBO->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            if (m_ViewportResolvedFBO)
                m_ViewportResolvedFBO->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            if (m_PickupFBO)
                m_PickupFBO->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            if (m_SelectedMaskFBO)
                m_SelectedMaskFBO->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_SSAO->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_FXAA->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_PathTracer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_SVGF->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            ResizeWeightedBlendedOIT((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            ResizeTransparentStepEdgeResources((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            ResizeSelectedOutlineResources((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        }

        // --- CSM SHADOW MAP UPDATE ---
        if (m_CSM->Enabled())
        {
            m_CSM->Update(viewportCamera->GetViewMatrix(), viewportCamera->GetProjectionMatrix(), viewportCamera->getNearPlane(), viewportCamera->getFarPlane());

            auto depthShader = GetShaderLibrary()->Get("ShadowDepth");
            auto& lightViewProj = m_CSM->GetLightViewProjMatrices();

            for (uint32_t i = 0; i < m_CSM->GetCascadeCount(); i++)
            {
                m_CSM->BeginShadowPass(i);
                depthShader->Bind();
                for (const auto& entry : m_Scene.GetObjects())
                {
                    if (!entry.Visible || !entry.Object || entry.Object->Opacity <= 0.001f) continue;
                    depthShader->SetMat4("u_LightViewProj", lightViewProj[i]);
                    for (auto& mesh : entry.Object->Meshes)
                    {
                        Ref<VertexArray> vertexObject = mesh ? GeometryLibrary::Resolve(mesh->VertexObject) : nullptr;
                        if (!vertexObject)
                            continue;
                        depthShader->SetMat4("u_Model", entry.Object->Transfm.GetMatrix() * mesh->Transfm.GetMatrix());
                        RenderCommand::DrawIndexed(vertexObject);
                    }
                }
                m_CSM->EndShadowPass();
            }
        }

        // --- PROBE GI UPDATE ---
        // The initial implementation maintains an irradiance volume on the CPU.
        // Its interface is ready for a future capture/projection update pass.
        m_ProbeGI->Update(m_CSM->GetLight());

        // --- SCENE RENDERING (common to both modes) ---
        // Determine target: Editor → FBO, Game → default framebuffer
        if (m_AppMode == AppMode::Editor)
        {
            if (m_ViewportFBO) m_ViewportFBO->Bind();
#ifdef G_OPENGL
            const GLenum opaqueBuffers[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
            glDrawBuffers(3, opaqueBuffers);
#endif
        }
        else
        {
            // In Game mode, render to full window
            RenderCommand::SetViewport(0, 0, m_WindowInterface->GetWidth(), m_WindowInterface->GetHeight());
        }

        RenderCommand::SetClearColor(viewport2D ? glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) : glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));
        RenderCommand::Clear();
#ifdef G_OPENGL
        if (m_AppMode == AppMode::Editor)
        {
            const float emptyGeometry[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            glClearBufferfv(GL_COLOR, 1, emptyGeometry);
            glClearBufferfv(GL_COLOR, 2, emptyGeometry);
        }
#endif
        // Draw selected environment background without writing depth.
        if (!viewport2D)
        {
            RenderCommand::SetDepthRange(0.99f, 1.0f);
#ifdef G_OPENGL
            glDepthMask(GL_FALSE);
#endif
            auto backshader = Application::Get().GetShaderLibrary()->Get("DefaultBackgroundSH");
            backshader->Bind();
            auto viewrotate = glm::mat4(glm::mat3(viewportCamera->GetViewMatrix()));
            auto invViewProj = glm::inverse(viewportCamera->GetProjectionMatrix() * viewrotate);
            backshader->SetMat4("u_invViewProj", invViewProj);
            backshader->SetInt("u_BackgroundMode", m_BackgroundMode);
            if (m_BackgroundMode == 1)
                m_PBRIBL->BindEnvironment(backshader);
            const glm::vec3 shCoeffs[9] = {
                glm::vec3(0.79,  0.44,  0.54),
                glm::vec3(0.39,  0.35,  0.60),
                glm::vec3(-0.34, -0.18, -0.27),
                glm::vec3(-0.29, -0.06,  0.01),
                glm::vec3(-0.11, -0.05, -0.12),
                glm::vec3(-0.26, -0.22, -0.47),
                glm::vec3(-0.16, -0.09, -0.15),
                glm::vec3(0.56,  0.21,  0.14),
                glm::vec3(0.21, -0.05, -0.30)
            };
            backshader->SetVec3Array("shCoeffs", (float*)&shCoeffs[0].x, 9);
                RenderCommand::DrawIndexed(m_backgroundCubeVA, m_backgroundCubeCount);
            RenderCommand::SetDepthRange(0.f, 1.0f);
#ifdef G_OPENGL
            glDepthMask(GL_TRUE);
#endif
        }

        // Update layers (draws axis, ground, etc.)
        if (!viewport2D)
        {
            for (auto& layer : m_LayerStack)
                layer->OnUpdate();
        }

        if (viewport2D && m_AppMode == AppMode::Editor)
        {
            auto gridShader = GetShaderLibrary()->Get("DefaultColor");
            gridShader->Bind();
            gridShader->SetMat4("u_View", viewportCamera->GetViewMatrix());
            gridShader->SetMat4("u_Projection", viewportCamera->GetProjectionMatrix());
            gridShader->SetMat4("u_Model", glm::mat4(1.0f));
            RenderCommand::EnableDepthTest(false);
#ifdef G_OPENGL
            glDepthMask(GL_FALSE);
#endif
            DrawViewport2DGrid(viewportCamera->GetViewMatrix(), viewportCamera->GetProjectionMatrix());
            RenderCommand::Flush();
#ifdef G_OPENGL
            glDepthMask(GL_TRUE);
#endif
            RenderCommand::EnableDepthTest(true);
        }

        // Draw visible objects intersecting the active camera frustum.
        const Frustum cameraFrustum(viewportCamera->GetProjectionMatrix() * viewportCamera->GetViewMatrix());
        bool hasVisibleTransparentObject = false;
        m_OITCompositeValid = false;
        for (const auto& entry : m_Scene.GetObjects())
        {
            if (entry.Visible && entry.Object &&
                entry.Object->Opacity > 0.001f && entry.Object->Opacity < 0.999f &&
                cameraFrustum.Intersects(entry.Object->GetWorldBoundingSphere()))
            {
                hasVisibleTransparentObject = true;
                break;
            }
        }
#ifdef G_OPENGL
        for (const auto& entry : m_Scene.GetObjects())
        {
            Ref<WaterNode> water = std::dynamic_pointer_cast<WaterNode>(entry.Object);
            if (entry.Visible && water && water->Opacity > 0.001f)
                water->PrepareSceneTextures(m_Scene, *viewportCamera, m_ViewportSize);
        }

        if (m_AppMode == AppMode::Editor)
        {
            for (const auto& entry : m_Scene.GetObjects())
                if (entry.Visible && entry.Object && entry.Object->Opacity >= 0.999f &&
                    cameraFrustum.Intersects(entry.Object->GetWorldBoundingSphere()))
                    entry.Object->Draw(viewportCamera->GetViewMatrix(), viewportCamera->GetProjectionMatrix());
        }
        else
#endif
        for (const auto& entry : m_Scene.GetObjects())
            if (entry.Visible && entry.Object && entry.Object->Opacity > 0.001f &&
                cameraFrustum.Intersects(entry.Object->GetWorldBoundingSphere()))
                entry.Object->Draw(viewportCamera->GetViewMatrix(), viewportCamera->GetProjectionMatrix());

            if (m_AppMode == AppMode::Editor)
            {
                if (!m_SelectedMaskValid && !IsViewport2DEditMode() && m_Scene.GetSelectedCount() > 0)
                    RenderSelectedMaskPass();
                if (m_ViewportFBO) m_ViewportFBO->Bind(false);
#ifdef G_OPENGL
                const GLenum editorBuffers[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
                glDrawBuffers(3, editorBuffers);
#endif

                // Reset model matrix to identity so gizmo lines render in world space
                {
                    auto resetShader = GetShaderLibrary()->Get("DefaultColor");
                    resetShader->Bind();
                    resetShader->SetMat4("u_View", viewportCamera->GetViewMatrix());
                    resetShader->SetMat4("u_Projection", viewportCamera->GetProjectionMatrix());
                    resetShader->SetMat4("u_Model", glm::mat4(1.0f));
                }

                // Directional light indicator
                if (!viewport2D && m_CSM->Enabled())
                {
                    glm::vec3 lightPos(0.0f, 100.0f, 0.0f);
                    glm::vec3 lightDir = glm::normalize(m_CSM->GetLight().Direction);
                    glm::vec3 dirEnd = lightPos + lightDir * 10.0f;

                    // Draw direction line (yellow)
                    RenderCommand::FlushLine(lightPos, dirEnd, glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));

                    // Draw a small sphere indicator (3 axis-aligned circles)
                    float radius = 1.5f;
                    int segments = 16;
                    glm::vec4 sphereColor(1.0f, 0.8f, 0.0f, 1.0f);
                    for (int axis = 0; axis < 3; axis++)
                    {
                        for (int i = 0; i < segments; i++)
                        {
                            float a1 = (float)i / (float)segments * 6.28318f;
                            float a2 = (float)(i + 1) / (float)segments * 6.28318f;
                            glm::vec3 p1, p2;
                            if (axis == 0) { // XZ circle
                                p1 = lightPos + glm::vec3(cosf(a1) * radius, 0, sinf(a1) * radius);
                                p2 = lightPos + glm::vec3(cosf(a2) * radius, 0, sinf(a2) * radius);
                            } else if (axis == 1) { // XY circle
                                p1 = lightPos + glm::vec3(cosf(a1) * radius, sinf(a1) * radius, 0);
                                p2 = lightPos + glm::vec3(cosf(a2) * radius, sinf(a2) * radius, 0);
                            } else { // YZ circle
                                p1 = lightPos + glm::vec3(0, cosf(a1) * radius, sinf(a1) * radius);
                                p2 = lightPos + glm::vec3(0, cosf(a2) * radius, sinf(a2) * radius);
                            }
                            RenderCommand::FlushLine(p1, p2, sphereColor);
                        }
                    }
                }

                // Draw gizmo
            if (!viewport2D)
                m_ProbeGI->DrawDebug();
            if (viewport2D && m_Viewport2DEditMode && !m_LeftDownGizmo)
                Update2DSubElementGizmoTarget();
            else if (viewport2D && !m_Viewport2DEditMode && !m_LeftDownGizmo)
                UpdateViewport2DObjectGizmoTarget();
            Transform* targetTransform = m_GizmoTargetTransform;
            if (targetTransform)
            {
                int gizmoFlags = 0;
                switch (m_GizmoMode)
                {
                case 0: gizmoFlags = GIZMO_TRANSLATE; break;
                case 1: gizmoFlags = GIZMO_ROTATE;    break;
                case 2: gizmoFlags = GIZMO_SCALE;     break;
                case 3: gizmoFlags = GIZMO_ALL;       break;
                default: gizmoFlags = GIZMO_TRANSLATE; break;
                }
                if (viewport2D)
                    gizmoFlags |= GIZMO_XY_PLANE;
                else
                {
                    if (m_GizmoLocal) gizmoFlags |= GIZMO_LOCAL;
                    if (m_GizmoView)  gizmoFlags |= GIZMO_VIEW;
                }

                SetGizmoSize(viewport2D ? m_GizmoSize * 0.5f : m_GizmoSize);
                SetGizmoLineWidth(m_GizmoLineWidth);
                SetGizmoViewportSize((int)m_ViewportSize.x, (int)m_ViewportSize.y);

                const Transform beforeGizmo = *targetTransform;
                if (DrawGizmo3D(viewportCamera->GetViewMatrix(), viewportCamera->GetProjectionMatrix(),
                    m_LeftDownGizmo, m_ViewportMousePos, gizmoFlags, targetTransform))
                {
                    if (viewport2D && m_Viewport2DEditMode)
                        ApplyGizmoDeltaToSelected2DSubElements(beforeGizmo, *targetTransform);
                    else if (viewport2D && targetTransform == &m_Viewport2DPivotTransform)
                        ApplyGizmoDeltaToSelectedObject2DPivot(beforeGizmo, *targetTransform);
                    else
                        ApplyGizmoDeltaToSelection(beforeGizmo, *targetTransform);
                    m_TimelineAnimation.RecordSelectedTransformChange(m_Scene);
                    InvalidateSelectedMask();
                    InvalidatePickupPass();
                    if (viewportCamera->isInputEnabled()) viewportCamera->setInputEnabled(false);
                }
            }

            // Flush gizmo lines on top of everything (override depth)
            RenderCommand::SetDepthRange(0, 0.001f);
            RenderCommand::Flush();
            RenderCommand::SetDepthRange(0, 1);

            if (m_ViewportFBO) m_ViewportFBO->Unbind();
            Ref<FrameBuffer> postProcessFBO = IsMSAAEnabled() ? m_ViewportResolvedFBO : m_ViewportFBO;
            if (IsMSAAEnabled())
                m_ViewportFBO->ResolveTo(m_ViewportResolvedFBO, { 0, 1, 2 }, true);
            uint64_t opaqueColor = postProcessFBO->GetColorAttachmentRendererID(0);
            const bool useSSAO = m_ViewportRenderMode == ViewportRenderMode::Editor && m_SSAO->Enabled();
            if (useSSAO)
            {
                m_SSAO->Render(
                    opaqueColor,
                    postProcessFBO->GetDepthAttachmentRendererID(),
                    postProcessFBO->GetColorAttachmentRendererID(2),
                    viewportCamera->GetViewMatrix(),
                    viewportCamera->GetProjectionMatrix());
            }
            uint64_t shadedOpaqueColor = useSSAO ? m_SSAO->GetOutputTexture() : opaqueColor;

#ifdef G_OPENGL
            if (hasVisibleTransparentObject)
            {
                // Render transparent geometry after SSAO, reusing the opaque G-buffer depth attachment.
                RenderTransparentDepthPrepass();
                m_ViewportFBO->Bind(false);
                const GLenum transparentBuffers[2] = { GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4 };
                const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                const float one[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
                glDrawBuffers(2, transparentBuffers);
                glClearBufferfv(GL_COLOR, 0, zero);
                glClearBufferfv(GL_COLOR, 1, one);
                glDepthMask(GL_FALSE);
                glEnable(GL_DEPTH_TEST);
                glEnable(GL_BLEND);
                glBlendEquation(GL_FUNC_ADD);
                glBlendFunci(0, GL_ONE, GL_ONE);
                glBlendFunci(1, GL_ZERO, GL_ONE_MINUS_SRC_COLOR);

                for (const auto& entry : m_Scene.GetObjects())
                    if (entry.Visible && entry.Object && entry.Object->Opacity > 0.001f &&
                        entry.Object->Opacity < 0.999f &&
                        cameraFrustum.Intersects(entry.Object->GetWorldBoundingSphere()))
                        entry.Object->Draw(viewportCamera->GetViewMatrix(), viewportCamera->GetProjectionMatrix(), true);

                glDepthMask(GL_TRUE);
                glBlendFunci(0, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glBlendFunci(1, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                const GLenum opaqueBuffers[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
                glDrawBuffers(3, opaqueBuffers);
                m_ViewportFBO->Unbind();
            }
#endif
            if (hasVisibleTransparentObject && IsMSAAEnabled())
                m_ViewportFBO->ResolveTo(m_ViewportResolvedFBO, { 3, 4 }, false);
            uint64_t sceneColor = hasVisibleTransparentObject
                ? CompositeWeightedBlendedOIT(
                    shadedOpaqueColor,
                    postProcessFBO->GetColorAttachmentRendererID(3),
                    postProcessFBO->GetColorAttachmentRendererID(4))
                : shadedOpaqueColor;
            if (hasVisibleTransparentObject)
                RenderTransparentStepEdges(postProcessFBO->GetDepthAttachmentRendererID());
            if (m_ViewportRenderMode == ViewportRenderMode::Editor && !viewport2D &&
                !IsMSAAEnabled() && m_FXAA->Enabled())
            {
                m_FXAA->Render(sceneColor);
            }
            if (m_ViewportRenderMode == ViewportRenderMode::Editor)
            {
                uint64_t outlineSource = sceneColor;
                if (!viewport2D && !IsMSAAEnabled() && m_FXAA && m_FXAA->Enabled() && m_FXAA->GetOutputTexture())
                    outlineSource = m_FXAA->GetOutputTexture();
                CompositeSelectedOutline(outlineSource);
            }
            if (m_ViewportRenderMode == ViewportRenderMode::Rendering)
            {
                InvalidateSelectedMask();
                m_PathTracer->Render(m_Scene, *viewportCamera);
                if (m_PathTracer->GetSampleCount() <= 1)
                    m_SVGF->ResetHistory();
                if (m_SVGF->Enabled())
                    m_SVGF->Render(*m_PathTracer, *viewportCamera);
            }
        }
        else
        {
            RenderCommand::Flush();
        }

        // --- IMGUI (Editor only) ---
        if (m_AppMode == AppMode::Editor)
        {
            m_ImGuiLayer->Begin();
            for (auto layer : m_LayerStack)
                layer->OnImGuiRender();
            m_ImGuiLayer->End();
        }

        RenderCommand::SetDepthRange(0,0.001f);
        RenderCommand::Flush();
        RenderCommand::SetDepthRange(0,1);

        m_WindowInterface->PollEvents();

        if (m_FrameRateLimit > 0)
        {
            const int frameRateLimit = std::clamp(m_FrameRateLimit, 30, 60);
            const auto targetFrameDuration = std::chrono::duration<float>(1.0f / (float)frameRateLimit);
            const auto targetFrameEndTime = frameStartTime + std::chrono::duration_cast<clock::duration>(targetFrameDuration);
            const auto frameEndTime = clock::now();
            if (frameEndTime + frameLimitTolerance < targetFrameEndTime)
                SleepUntilFrameLimit(targetFrameEndTime);
        }
    }
}

void Application::ProcessKeyboardInput(float deltaTime)
{
    // Clamp delta to avoid large jumps
    deltaTime = glm::min(deltaTime, 0.05f);

    int forward = (m_KeyW ? 1 : 0) - (m_KeyS ? 1 : 0);
    int right   = (m_KeyD ? 1 : 0) - (m_KeyA ? 1 : 0);
    int up      = (m_KeyE ? 1 : 0) - (m_KeyQ ? 1 : 0);

    if (forward != 0 || right != 0 || up != 0)
    {
        GetViewportCamera()->processKeyboard(forward, right, up, deltaTime);
        InvalidateSelectedMask();
        InvalidatePickupPass();
    }
}

Ref<Camera> Application::GetViewportCamera() const
{
    if (m_ViewportViewMode == ViewportViewMode::View2D && m_ViewportRenderMode == ViewportRenderMode::Editor && m_Camera2D)
        return m_Camera2D;
    return m_Camera;
}

void Application::OnEvent(Event& e)
{
    EventDispatcher dispatcher(e);
    dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(Application::OnWindowClose));
    dispatcher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(Application::OnWindowResize));
    dispatcher.Dispatch<FileDropEvent>(BIND_EVENT_FN(Application::OnFileDrop));


    // Dispatch event to layers in reverse order (overlays first)
    for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
    {
        if (e.Handled)
            break;

        (*it)->OnEvent(e);
    }


	// Track key states for continuous velocity movement
	KeyPressedEvent* kp = dynamic_cast<KeyPressedEvent*>(&e);
	KeyReleasedEvent* kr = dynamic_cast<KeyReleasedEvent*>(&e);

	if (kp)
	{
		switch (kp->GetKeyCode())
		{
		case 'W': m_KeyW = true; break;
		case 'S': m_KeyS = true; break;
		case 'A': m_KeyA = true; break;
		case 'D': m_KeyD = true; break;
		case 'E': m_KeyE = true; break;
		case 'Q': m_KeyQ = true; break;
		case GLFW_KEY_F11:
			SetAppMode((m_AppMode == AppMode::Editor) ? AppMode::Game : AppMode::Editor);
			break;
		case GLFW_KEY_ESCAPE:
			if (m_AppMode == AppMode::Game) SetAppMode(AppMode::Editor);
			break;
		}
	}
	if (kr)
	{
		switch (kr->GetKeyCode())
		{
		case 'W': m_KeyW = false; break;
		case 'S': m_KeyS = false; break;
		case 'A': m_KeyA = false; break;
		case 'D': m_KeyD = false; break;
		case 'E': m_KeyE = false; break;
		case 'Q': m_KeyQ = false; break;
		}
	}
}


bool Application::OnWindowClose(WindowCloseEvent& e)
{
    m_Running = false;
    return true;
}

void Application::Close()
{
    m_Running = false;
}

bool Application::OnWindowResize(WindowResizeEvent& e)
{

    if (e.GetWidth() == 0 || e.GetHeight() == 0)
    {
        return false;
    }

    RenderCommand::SetViewport(0,0,e.GetWidth(), e.GetHeight());

    SetGizmoViewportSize(e.GetWidth(), e.GetHeight());
    return false;
}

bool Application::OnFileDrop(FileDropEvent& e)
{
    bool loadedAny = false;
    for (const std::string& path : e.GetPaths())
    {
        std::filesystem::path filepath = std::filesystem::u8path(path);
        if (!std::filesystem::exists(filepath))
        {
            WARN("Dropped file does not exist: {}", path);
            continue;
        }

        std::string extension = filepath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".dxf")
        {
            if (m_ImGuiLayer)
            {
                m_ImGuiLayer->QueueDxfImport(filepath);
                loadedAny = true;
            }
            continue;
        }

        loadedAny |= LoadFileByExtension(filepath);
    }
    return loadedAny;
}


void Application::PushLayer(std::shared_ptr<Layer> layer)
{
    m_LayerStack.PushLayer(layer);
}

void Application::PushOverlay(std::shared_ptr<Layer> overlay)
{
    m_LayerStack.PushOverlay(overlay);
}

void Application::PopLayer(std::shared_ptr<Layer> layer)
{
    m_LayerStack.PopLayer(layer);
}

void Application::PopOverlay(std::shared_ptr<Layer> overlay)
{
    m_LayerStack.PopOverlay(overlay);
}

Ref<Object3D> Application::LoadObject3D(const std::filesystem::path& filepath)
{
    auto object = CreateRef<Object3D>();
    if (!object->LoadFromPath<VertexNormalTexture>(filepath))
        return nullptr;

    std::string displayName;
    try {
        displayName = filepath.filename().u8string();
    } catch (...) {
        displayName = filepath.filename().string();
    }
    m_Scene.AddObject(object, displayName, filepath.u8string());
    // Update gizmo target to the new selection
    m_GizmoTargetTransform = m_Scene.GetSelectedTransform();
    InvalidateSelectedMask();
    InvalidatePickupPass();
    return object;
}

Ref<Object3D> Application::LoadTexturePlane(const std::filesystem::path& filepath)
{
    Ref<TexturePlaneObject> object = CreateRef<TexturePlaneObject>();
    if (!object->LoadFromImageFile(filepath))
        return nullptr;

    std::string displayName;
    try {
        displayName = filepath.filename().u8string();
    } catch (...) {
        displayName = filepath.filename().string();
    }

    m_Scene.AddObject(object, displayName, filepath.u8string());
    SetViewportViewMode(ViewportViewMode::View3D);
    SetViewportRenderMode(ViewportRenderMode::Editor);
    m_GizmoTargetTransform = m_Scene.GetSelectedTransform();
    InvalidateSelectedMask();
    InvalidatePickupPass();
    return object;
}

Ref<Object3D> Application::LoadGCode(const std::filesystem::path& filepath)
{
    Ref<GCodeObject> object = CreateRef<GCodeObject>();
    if (!object->LoadFromFile(filepath))
        return nullptr;

    std::string displayName;
    try {
        displayName = filepath.filename().u8string();
    } catch (...) {
        displayName = filepath.filename().string();
    }

    m_Scene.AddObject(object, displayName, filepath.u8string());
    m_GizmoTargetTransform = m_Scene.GetSelectedTransform();
    InvalidateSelectedMask();
    InvalidatePickupPass();
    return object;
}

Ref<Object3D> Application::LoadVector2D(const std::filesystem::path& filepath, DxfImportMode mode)
{
    std::string extension = filepath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    Vector2DDocument document;
    std::string error;
    bool loaded = false;
    if (extension == ".dxf")
        loaded = DxfLoader::Load(filepath, document, error, mode);

    if (!loaded)
    {
        ERROR("Failed to load 2D file {}: {}", filepath.u8string(), error);
        return nullptr;
    }

    Ref<Object2D> object = CreateRef<Object2D>();
    if (!object->LoadFromDocument(document))
        return nullptr;

    const std::string displayName = document.SourceName.empty() ? filepath.filename().u8string() : document.SourceName;
    m_Scene.AddObject(object, displayName, filepath.u8string());
    SetViewportViewMode(ViewportViewMode::View2D);
    m_GizmoTargetTransform = m_Scene.GetSelectedTransform();
    InvalidateSelectedMask();
    InvalidatePickupPass();
    return object;
}

Ref<Object3D> Application::SliceSelectedModel(float layerHeight)
{
    Scene::Entry* selectedEntry = m_Scene.GetSelectedEntry();
    if (!selectedEntry || !selectedEntry->Object)
    {
        WARN("Slice failed: no selected model.");
        return nullptr;
    }

    Ref<Object2D> object2D = std::dynamic_pointer_cast<Object2D>(selectedEntry->Object);
    if (object2D)
    {
        WARN("Slice failed: selected object is 2D data, expected a 3D model.");
        return nullptr;
    }

    std::vector<glm::vec3> triangleVertices;
    for (const Ref<Mesh>& mesh : selectedEntry->Object->Meshes)
    {
        if (!mesh || mesh->TraceVertices.empty())
            continue;

        const glm::mat4 transform = selectedEntry->Object->Transfm.GetMatrix() * mesh->Transfm.GetMatrix();
        auto pushVertex = [&](uint32_t index)
        {
            if (index >= mesh->TraceVertices.size())
                return;
            const glm::vec3 position = mesh->TraceVertices[index].Position;
            triangleVertices.push_back(glm::vec3(transform * glm::vec4(position, 1.0f)));
        };

        if (!mesh->TraceIndices.empty())
        {
            for (size_t i = 0; i + 2 < mesh->TraceIndices.size(); i += 3)
            {
                pushVertex(mesh->TraceIndices[i]);
                pushVertex(mesh->TraceIndices[i + 1]);
                pushVertex(mesh->TraceIndices[i + 2]);
            }
        }
        else
        {
            for (size_t i = 0; i + 2 < mesh->TraceVertices.size(); i += 3)
            {
                triangleVertices.push_back(glm::vec3(transform * glm::vec4(mesh->TraceVertices[i].Position, 1.0f)));
                triangleVertices.push_back(glm::vec3(transform * glm::vec4(mesh->TraceVertices[i + 1].Position, 1.0f)));
                triangleVertices.push_back(glm::vec3(transform * glm::vec4(mesh->TraceVertices[i + 2].Position, 1.0f)));
            }
        }
    }

    if (triangleVertices.size() < 3)
    {
        WARN("Slice failed: selected model has no triangle data.");
        return nullptr;
    }

    GeometryProcess::SliceOptions options;
    options.Normal = glm::vec3(0.0f, 0.0f, 1.0f);
    options.LayerHeight = std::max(layerHeight, 0.0001f);
    GeometryProcess::SliceContours contours = GeometryProcess::SliceTriangleVertices(triangleVertices, options);
    if (contours.empty())
    {
        WARN("Slice failed: GeometryProcess returned no contours.");
        return nullptr;
    }

    Ref<SlicePreviewObject> preview = CreateRef<SlicePreviewObject>();
    if (!preview->LoadFromContours(contours))
    {
        WARN("Slice failed: unable to build slice preview geometry.");
        return nullptr;
    }

    const std::string sourceName = selectedEntry->Name.empty() ? "Selected Model" : selectedEntry->Name;
    m_Scene.AddObject(preview, sourceName + " Slices", selectedEntry->FilePath);
    SetViewportViewMode(ViewportViewMode::View3D);
    SetViewportRenderMode(ViewportRenderMode::Editor);
    m_GizmoTargetTransform = m_Scene.GetSelectedTransform();
    InvalidateSelectedMask();
    InvalidatePickupPass();

    INFO("Sliced {} triangles into {} layers.", triangleVertices.size() / 3, contours.size());
    return preview;
}

Ref<Object3D> Application::LoadManixVolume()
{
    const std::filesystem::path filepath = std::filesystem::u8path("D:/gitclones/VolumeRender/content/Textures/manix.dat");
    Ref<VolumeObject> object = CreateRef<VolumeObject>(filepath);
    if (!object->IsLoaded())
        return nullptr;

    m_Scene.AddObject(object, "Manix Volume", filepath.u8string());
    m_GizmoTargetTransform = m_Scene.GetSelectedTransform();
    InvalidateSelectedMask();
    InvalidatePickupPass();
    return object;
}

Ref<Object3D> Application::LoadDefaultTerrainCDLOD()
{
    const std::filesystem::path heightmapPath = TerrainCDLOD::DefaultHeightmapPath();
    Ref<TerrainCDLOD> object = CreateRef<TerrainCDLOD>(heightmapPath);
    if (!object->IsLoaded())
        return nullptr;

    m_Scene.AddObject(object, "CDLOD Hetch Terrain", heightmapPath.u8string());
    m_GizmoTargetTransform = m_Scene.GetSelectedTransform();
    InvalidateSelectedMask();
    InvalidatePickupPass();
    return object;
}

Ref<Object3D> Application::LoadTerrainHeightMap()
{
    Ref<TerrainHeightMap> terrain = CreateRef<TerrainHeightMap>(512, 512.0f);
    if (!terrain->IsLoaded())
        return nullptr;

    if (m_SSAO)
        m_SSAO->Enabled() = false;

    m_Scene.AddObject(terrain, "Terrain HeightMap", "procedural:island11-heightmap");
    SetSelectedObjectIndex(m_Scene.GetCount() - 1);
    return terrain;
}

Ref<Object3D> Application::LoadWaterNode()
{
    Ref<TerrainHeightMap> terrain;
    if (auto* selected = m_Scene.GetSelectedEntry())
        terrain = std::dynamic_pointer_cast<TerrainHeightMap>(selected->Object);

    if (!terrain)
    {
        for (const auto& entry : m_Scene.GetObjects())
        {
            terrain = std::dynamic_pointer_cast<TerrainHeightMap>(entry.Object);
            if (terrain)
                break;
        }
    }

    if (!terrain)
    {
        WARN("Water Node requires a Terrain HeightMap in the scene.");
        return nullptr;
    }

    Ref<WaterNode> water = CreateRef<WaterNode>(terrain);
    if (!water->IsLoaded())
        return nullptr;

    m_Scene.AddObject(water, "Water Node", "procedural:island11-water");
    SetSelectedObjectIndex(m_Scene.GetCount() - 1);
    return water;
}

bool Application::LoadFileByExtension(const std::filesystem::path& filepath)
{
    std::string extension = filepath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (extension == ".gcode" || extension == ".nc" || extension == ".cnc" || extension == ".tap")
        return LoadGCode(filepath) != nullptr;

    if (extension == ".dxf")
        return LoadVector2D(filepath) != nullptr;

    if (extension == ".jpg" || extension == ".jpeg" ||
        extension == ".png" || extension == ".bmp")
        return LoadTexturePlane(filepath) != nullptr;

    if (extension == ".obj" || extension == ".stl" || extension == ".ply" ||
        extension == ".gltf" || extension == ".glb" ||
        extension == ".pmx" || extension == ".pmd" ||
        extension == ".step" || extension == ".stp")
        return LoadObject3D(filepath) != nullptr;

    WARN("Unsupported dropped file: {}", filepath.u8string());
    return false;
}

void Application::NewProject()
{
    m_CameraAnimation.Clear();
    ClearObject3Ds();

    Ref<ScenePlane> plane = m_Scene.CreatePlane("XZ Plane", 100.0f);
    if (plane)
    {
        plane->Transfm.translation = glm::vec3(0.0f);
        plane->UpdateBoundingSphere();
    }

    if (Ref<FPSCamera> camera = std::dynamic_pointer_cast<FPSCamera>(m_Camera))
    {
        camera->setPosition(glm::vec3(100.0f, 100.0f, 100.0f));
        camera->lookAt(glm::vec3(0.0f));
    }

    m_Scene.ClearSelection();
    m_GizmoTargetTransform = nullptr;
    InvalidateSelectedMask();
    InvalidatePickupPass();
    if (m_PathTracer)
        m_PathTracer->ResetAccumulation();
    if (m_SVGF)
        m_SVGF->ResetHistory();
}

void Application::ClearObject3Ds()
{
    m_TimelineAnimation.Clear();
    m_Scene.Clear();
    m_GizmoTargetTransform = nullptr;
    InvalidateSelectedMask();
    InvalidatePickupPass();
}

void Application::SetAppMode(AppMode mode)
{
    if (m_AppMode == mode)
        return;

    m_AppMode = mode;
    if (m_AppMode == AppMode::Game)
    {
        m_TimelineAnimation.PlayAll(m_Scene);
        if (m_CameraAnimation.HasTimeline())
            m_CameraAnimation.Play(m_Camera);
    }
    else
    {
        m_TimelineAnimation.StopAll();
        m_CameraAnimation.Stop(m_Camera);
    }
}

void Application::CreateViewportFrameBuffers()
{
    FrameBufferSpecification specification{ (uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y,
        { FrameBufferTextureSpecification(FrameBufferTextureFormat::RGBA8),
          FrameBufferTextureSpecification(FrameBufferTextureFormat::RGBA16F),
          FrameBufferTextureSpecification(FrameBufferTextureFormat::RGBA16F),
          FrameBufferTextureSpecification(FrameBufferTextureFormat::RGBA16F),
          FrameBufferTextureSpecification(FrameBufferTextureFormat::RGBA16F),
          FrameBufferTextureSpecification(FrameBufferTextureFormat::Depth) } };
    specification.Samples = (uint32_t)std::max(m_MSAASamples, 1);
    m_ViewportFBO = FrameBuffer::Create(specification);

    specification.Samples = 1;
    m_ViewportResolvedFBO = FrameBuffer::Create(specification);

    FrameBufferSpecification pickupSpecification{ (uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y,
        { FrameBufferTextureSpecification(FrameBufferTextureFormat::RED_INTEGER),
          FrameBufferTextureSpecification(FrameBufferTextureFormat::Depth) } };
    pickupSpecification.Samples = 1;
    m_PickupFBO = FrameBuffer::Create(pickupSpecification);

    FrameBufferSpecification selectedMaskSpecification{ (uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y,
        { FrameBufferTextureSpecification(FrameBufferTextureFormat::RED_INTEGER),
          FrameBufferTextureSpecification(FrameBufferTextureFormat::Depth) } };
    selectedMaskSpecification.Samples = 1;
    m_SelectedMaskFBO = FrameBuffer::Create(selectedMaskSpecification);
}

void Application::SetMSAASamples(int samples)
{
    int normalizedSamples = 1;
    if (samples >= 8)
        normalizedSamples = 8;
    else if (samples >= 4)
        normalizedSamples = 4;
    else if (samples >= 2)
        normalizedSamples = 2;

    if (m_MSAASamples == normalizedSamples)
        return;

    m_MSAASamples = normalizedSamples;
    if (m_MSAASamples > 1 && m_FXAA)
        m_FXAA->Enabled() = false;
    CreateViewportFrameBuffers();
    InvalidateSelectedMask();
    InvalidatePickupPass();
}

void Application::SetViewportSize(const glm::vec2& size)
{
    const uint32_t width = static_cast<uint32_t>(std::max(size.x, 1.0f));
    const uint32_t height = static_cast<uint32_t>(std::max(size.y, 1.0f));
    const glm::vec2 pixelSize((float)width, (float)height);
    if (pixelSize == m_ViewportSize)
        return;

    m_ViewportSize = pixelSize;
    if (m_Camera)
        m_Camera->setAspectRatio(pixelSize.x / pixelSize.y);
    if (m_Camera2D)
        m_Camera2D->setAspectRatio(pixelSize.x / pixelSize.y);
    SetGizmoViewportSize((int)pixelSize.x, (int)pixelSize.y);
    InvalidateSelectedMask();
    InvalidatePickupPass();
}

void Application::InitializeWeightedBlendedOIT()
{
#ifdef G_OPENGL
    const std::string fullscreenVertex = R"(
        #version 330 core
        out vec2 v_UV;
        void main()
        {
            vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
            vec2 position = positions[gl_VertexID];
            v_UV = position * 0.5 + 0.5;
            gl_Position = vec4(position, 0.0, 1.0);
        }
    )";
    const std::string compositeFragment = R"(
        #version 330 core
        in vec2 v_UV;
        layout(location = 0) out vec4 color;
        uniform sampler2D u_Opaque;
        uniform sampler2D u_Accumulation;
        uniform sampler2D u_Revealage;
        void main()
        {
            vec3 opaque = texture(u_Opaque, v_UV).rgb;
            vec4 accumulation = texture(u_Accumulation, v_UV);
            float revealage = clamp(texture(u_Revealage, v_UV).r, 0.0, 1.0);
            vec3 transparent = accumulation.rgb / max(accumulation.a, 0.00001);
            color = vec4(mix(transparent, opaque, revealage), 1.0);
        }
    )";
    m_OITCompositeShader = Shader::Create("WeightedBlendedOITComposite", fullscreenVertex, compositeFragment);
    glCreateVertexArrays(1, &m_OITQuadVAO);
    glCreateFramebuffers(1, &m_OITCompositeFBO);
    ResizeWeightedBlendedOIT((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
#endif
}

void Application::ResizeWeightedBlendedOIT(uint32_t width, uint32_t height)
{
#ifdef G_OPENGL
    if (!m_OITCompositeFBO)
        return;
    m_OITCompositeValid = false;
    if (m_OITCompositeTexture)
        glDeleteTextures(1, &m_OITCompositeTexture);
    glCreateTextures(GL_TEXTURE_2D, 1, &m_OITCompositeTexture);
    glTextureStorage2D(m_OITCompositeTexture, 1, GL_RGBA16F, width, height);
    glTextureParameteri(m_OITCompositeTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_OITCompositeTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_OITCompositeTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_OITCompositeTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_OITCompositeFBO, GL_COLOR_ATTACHMENT0, m_OITCompositeTexture, 0);
    if (glCheckNamedFramebufferStatus(m_OITCompositeFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        ERROR("Weighted blended OIT composite framebuffer is incomplete!");
#else
    (void)width;
    (void)height;
#endif
}

uint64_t Application::CompositeWeightedBlendedOIT(uint64_t opaqueTexture, uint64_t accumulationTexture,
    uint64_t revealageTexture)
{
#ifdef G_OPENGL
    if (!m_OITCompositeShader || !m_OITCompositeFBO)
        return opaqueTexture;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, m_OITCompositeFBO);
    glViewport(0, 0, (GLsizei)m_ViewportSize.x, (GLsizei)m_ViewportSize.y);
    glBindVertexArray(m_OITQuadVAO);
    m_OITCompositeShader->Bind();
    m_OITCompositeShader->SetInt("u_Opaque", 0);
    m_OITCompositeShader->SetInt("u_Accumulation", 1);
    m_OITCompositeShader->SetInt("u_Revealage", 2);
    glBindTextureUnit(0, (uint32_t)opaqueTexture);
    glBindTextureUnit(1, (uint32_t)accumulationTexture);
    glBindTextureUnit(2, (uint32_t)revealageTexture);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    m_OITCompositeValid = true;
    return m_OITCompositeTexture;
#else
    (void)accumulationTexture;
    (void)revealageTexture;
    return opaqueTexture;
#endif
}

void Application::InitializeTransparentStepEdgeResources()
{
#ifdef G_OPENGL
    const std::string depthVertex = R"(
        #version 330 core
        layout(location = 0) in vec3 a_Position;
        uniform mat4 u_View;
        uniform mat4 u_Projection;
        uniform mat4 u_Model;
        void main()
        {
            gl_Position = u_Projection * u_View * u_Model * vec4(a_Position, 1.0);
        }
    )";
    const std::string depthFragment = R"(
        #version 330 core
        void main()
        {
        }
    )";
    const std::string edgeVertex = R"(
        #version 330 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec4 a_Color;
        uniform mat4 u_View;
        uniform mat4 u_Projection;
        uniform mat4 u_Model;
        out vec4 v_Color;
        void main()
        {
            v_Color = a_Color;
            gl_Position = u_Projection * u_View * u_Model * vec4(a_Position, 1.0);
        }
    )";
    const std::string edgeFragment = R"(
        #version 330 core
        layout(location = 0) out vec4 color;
        in vec4 v_Color;
        uniform sampler2D u_SceneDepth;
        uniform vec2 u_ViewportSize;
        uniform float u_DepthBias;
        void main()
        {
            vec2 uv = gl_FragCoord.xy / max(u_ViewportSize, vec2(1.0));
            float sceneDepth = texture(u_SceneDepth, uv).r;
            if (gl_FragCoord.z > sceneDepth + u_DepthBias)
                discard;
            gl_FragDepth = max(gl_FragCoord.z - u_DepthBias, 0.0);
            color = v_Color;
        }
    )";
    m_TransparentDepthShader = Shader::Create("TransparentStepDepth", depthVertex, depthFragment);
    m_TransparentStepEdgeShader = Shader::Create("TransparentStepEdge", edgeVertex, edgeFragment);
    glCreateFramebuffers(1, &m_TransparentDepthFBO);
    ResizeTransparentStepEdgeResources((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
#endif
}

void Application::ResizeTransparentStepEdgeResources(uint32_t width, uint32_t height)
{
#ifdef G_OPENGL
    if (!m_TransparentDepthFBO)
        return;
    if (m_TransparentDepthTexture)
        glDeleteTextures(1, &m_TransparentDepthTexture);
    glCreateTextures(GL_TEXTURE_2D, 1, &m_TransparentDepthTexture);
    glTextureStorage2D(m_TransparentDepthTexture, 1, GL_DEPTH_COMPONENT24, width, height);
    glTextureParameteri(m_TransparentDepthTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_TransparentDepthTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(m_TransparentDepthTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_TransparentDepthTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_TransparentDepthFBO, GL_DEPTH_ATTACHMENT, m_TransparentDepthTexture, 0);
    glNamedFramebufferDrawBuffer(m_TransparentDepthFBO, GL_NONE);
    glNamedFramebufferReadBuffer(m_TransparentDepthFBO, GL_NONE);
    if (glCheckNamedFramebufferStatus(m_TransparentDepthFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        ERROR("Transparent STEP edge depth framebuffer is incomplete!");
#else
    (void)width;
    (void)height;
#endif
}

void Application::InitializeSelectedOutlineResources()
{
#ifdef G_OPENGL
    const std::string fullscreenVertex = R"(
        #version 330 core
        out vec2 v_UV;
        void main()
        {
            vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
            vec2 position = positions[gl_VertexID];
            v_UV = position * 0.5 + 0.5;
            gl_Position = vec4(position, 0.0, 1.0);
        }
    )";
    const std::string edgeFragment = R"(
        #version 330 core
        in vec2 v_UV;
        layout(location = 0) out vec4 color;
        uniform sampler2D u_SceneColor;
        uniform isampler2D u_SelectedMask;
        uniform vec2 u_ViewportSize;
        uniform float u_EdgeWidth;
        uniform vec4 u_EdgeColor;
        uniform int u_FillSelectedPixels;

        int SampleMask(ivec2 pixel)
        {
            ivec2 size = textureSize(u_SelectedMask, 0);
            pixel = clamp(pixel, ivec2(0), size - ivec2(1));
            return texelFetch(u_SelectedMask, pixel, 0).r;
        }

        void main()
        {
            ivec2 pixel = ivec2(gl_FragCoord.xy);
            bool centerSelected = SampleMask(pixel) != 0;
            bool neighborSelected = false;
            int radius = int(clamp(ceil(u_EdgeWidth), 1.0, 8.0));
            for (int y = -radius; y <= radius; y++)
            {
                for (int x = -radius; x <= radius; x++)
                {
                    if (x == 0 && y == 0)
                        continue;
                    if (length(vec2(x, y)) > float(radius) + 0.001)
                        continue;
                    neighborSelected = neighborSelected || SampleMask(pixel + ivec2(x, y)) != 0;
                }
            }

            vec4 scene = texture(u_SceneColor, v_UV);
            if (u_FillSelectedPixels != 0)
            {
                bool hasEmptyNeighbor = false;
                for (int y = -1; y <= 1; y++)
                {
                    for (int x = -1; x <= 1; x++)
                    {
                        if (x == 0 && y == 0)
                            continue;
                        hasEmptyNeighbor = hasEmptyNeighbor || SampleMask(pixel + ivec2(x, y)) == 0;
                    }
                }
                color = (centerSelected && hasEmptyNeighbor) ? u_EdgeColor : scene;
                return;
            }

            color = (!centerSelected && neighborSelected) ? u_EdgeColor : scene;
        }
    )";
    m_SelectedEdgeShader = Shader::Create("SelectedEdgeComposite", fullscreenVertex, edgeFragment);
    glCreateVertexArrays(1, &m_SelectedOutlineQuadVAO);
    glCreateFramebuffers(1, &m_SelectedOutlineFBO);
    ResizeSelectedOutlineResources((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
#endif
}

void Application::ResizeSelectedOutlineResources(uint32_t width, uint32_t height)
{
#ifdef G_OPENGL
    InvalidateSelectedMask();
    if (!m_SelectedOutlineFBO)
        return;
    if (m_SelectedOutlineTexture)
        glDeleteTextures(1, &m_SelectedOutlineTexture);
    glCreateTextures(GL_TEXTURE_2D, 1, &m_SelectedOutlineTexture);
    glTextureStorage2D(m_SelectedOutlineTexture, 1, GL_RGBA8, width, height);
    glTextureParameteri(m_SelectedOutlineTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_SelectedOutlineTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_SelectedOutlineTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_SelectedOutlineTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_SelectedOutlineFBO, GL_COLOR_ATTACHMENT0, m_SelectedOutlineTexture, 0);
    if (glCheckNamedFramebufferStatus(m_SelectedOutlineFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        ERROR("Selected outline framebuffer is incomplete!");
#else
    (void)width;
    (void)height;
#endif
}

void Application::RenderTransparentDepthPrepass()
{
#ifdef G_OPENGL
    if (!m_TransparentDepthShader || !m_TransparentDepthFBO || !m_Camera)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, m_TransparentDepthFBO);
    glViewport(0, 0, (GLsizei)m_ViewportSize.x, (GLsizei)m_ViewportSize.y);
    glClearDepth(1.0);
    glClear(GL_DEPTH_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    Ref<Camera> viewportCamera = GetViewportCamera();
    const glm::mat4 view = viewportCamera->GetViewMatrix();
    const glm::mat4 projection = viewportCamera->GetProjectionMatrix();
    const Frustum cameraFrustum(projection * view);
    m_TransparentDepthShader->Bind();
    m_TransparentDepthShader->SetMat4("u_View", view);
    m_TransparentDepthShader->SetMat4("u_Projection", projection);
    for (const auto& entry : m_Scene.GetObjects())
    {
        if (!entry.Visible || !entry.Object || entry.Object->Opacity <= 0.001f ||
            entry.Object->Opacity >= 0.999f ||
            !cameraFrustum.Intersects(entry.Object->GetWorldBoundingSphere()))
            continue;
        for (const auto& mesh : entry.Object->Meshes)
        {
            Ref<VertexArray> vertexObject = mesh ? GeometryLibrary::Resolve(mesh->VertexObject) : nullptr;
            if (!vertexObject)
                continue;
            m_TransparentDepthShader->SetMat4("u_Model", entry.Object->Transfm.GetMatrix() * mesh->Transfm.GetMatrix());
            RenderCommand::DrawIndexed(vertexObject);
        }
    }

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_BLEND);
#endif
}

void Application::RenderTransparentStepEdges(uint64_t sceneDepthTexture)
{
#ifdef G_OPENGL
    if (!m_TransparentStepEdgeShader || !m_TransparentDepthTexture || !sceneDepthTexture ||
        !m_OITCompositeFBO || !m_Camera)
        return;

    Ref<Camera> viewportCamera = GetViewportCamera();
    const glm::mat4 view = viewportCamera->GetViewMatrix();
    const glm::mat4 projection = viewportCamera->GetProjectionMatrix();
    const Frustum cameraFrustum(projection * view);
    glBindFramebuffer(GL_FRAMEBUFFER, m_OITCompositeFBO);
    glViewport(0, 0, (GLsizei)m_ViewportSize.x, (GLsizei)m_ViewportSize.y);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_TransparentDepthTexture, 0);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTextureUnit(0, (uint32_t)sceneDepthTexture);

    m_TransparentStepEdgeShader->Bind();
    m_TransparentStepEdgeShader->SetMat4("u_View", view);
    m_TransparentStepEdgeShader->SetMat4("u_Projection", projection);
    m_TransparentStepEdgeShader->SetFloat2("u_ViewportSize", m_ViewportSize);
    m_TransparentStepEdgeShader->SetFloat("u_DepthBias", 0.00002f);
    m_TransparentStepEdgeShader->SetInt("u_SceneDepth", 0);
    RenderCommand::SetLineWidth(2.0f);

    for (const auto& entry : m_Scene.GetObjects())
    {
        if (!entry.Visible || !entry.Object || entry.Object->Opacity <= 0.001f ||
            entry.Object->Opacity >= 0.999f ||
            !cameraFrustum.Intersects(entry.Object->GetWorldBoundingSphere()))
            continue;
        for (const auto& mesh : entry.Object->Meshes)
        {
            Ref<VertexArray> edgeVertexObject = mesh ? GeometryLibrary::Resolve(mesh->EdgeVertexObject) : nullptr;
            if (!mesh || !mesh->ShowEdges || !edgeVertexObject || mesh->EdgeVertexCount == 0)
                continue;
            m_TransparentStepEdgeShader->SetMat4("u_Model", entry.Object->Transfm.GetMatrix() * mesh->Transfm.GetMatrix());
            RenderCommand::DrawLines(edgeVertexObject, mesh->EdgeVertexCount);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_DEPTH_TEST);
#else
    (void)sceneDepthTexture;
#endif
}

void Application::RenderPickupPass()
{
#ifdef G_OPENGL
    if (!m_PickupFBO || !m_PickupShader || !m_Camera)
        return;

    m_PickupFBO->Bind();
    m_PickupFBO->ClearAttachment(0, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);

    Ref<Camera> viewportCamera = GetViewportCamera();
    const glm::mat4 view = viewportCamera->GetViewMatrix();
    const glm::mat4 projection = viewportCamera->GetProjectionMatrix();
    const Frustum cameraFrustum(projection * view);

    m_PickupShader->Bind();
    m_PickupShader->SetMat4("u_View", view);
    m_PickupShader->SetMat4("u_Projection", projection);

    if (IsViewport2DEditMode())
    {
        Scene::Entry* selectedEntry = m_Scene.GetSelectedEntry();
        Ref<Object2D> object2D = selectedEntry ? std::dynamic_pointer_cast<Object2D>(selectedEntry->Object) : nullptr;
        if (selectedEntry && selectedEntry->Visible && object2D &&
            cameraFrustum.Intersects(object2D->GetWorldBoundingSphere()))
        {
            object2D->DrawSubElementPickup(view, projection, m_PickupShader);
        }
        m_PickupFBO->Unbind();
        glEnable(GL_BLEND);
        m_PickupPassDirty = false;
        return;
    }

    const auto& objects = m_Scene.GetObjects();
    for (int i = 0; i < (int)objects.size(); i++)
    {
        const auto& entry = objects[i];
        if (!entry.Visible || !entry.Object || entry.Object->Opacity <= 0.001f ||
            !cameraFrustum.Intersects(entry.Object->GetWorldBoundingSphere()))
            continue;

        const bool isWaterNode = (bool)std::dynamic_pointer_cast<WaterNode>(entry.Object);
        const bool xzInput = isWaterNode || std::dynamic_pointer_cast<TerrainHeightMap>(entry.Object);
        entry.Object->DrawPickup(view, projection, m_PickupShader, i + 1, xzInput, isWaterNode ? -0.5f : 0.0f);
    }

    m_PickupFBO->Unbind();
    glEnable(GL_BLEND);
    m_PickupPassDirty = false;
#endif
}

void Application::RenderSelectedMaskPass()
{
#ifdef G_OPENGL
    if (!m_SelectedMaskFBO || !m_SelectedMaskShader || !m_Camera)
        return;

    if (IsViewport2DEditMode())
    {
        m_SelectedMaskValid = false;
        return;
    }

    if (m_Scene.GetSelectedCount() == 0)
    {
        m_SelectedMaskValid = false;
        m_SelectedOutlineValid = false;
        return;
    }

    m_SelectedMaskFBO->Bind();
    m_SelectedMaskFBO->ClearAttachment(0, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);

    Ref<Camera> viewportCamera = GetViewportCamera();
    const glm::mat4 view = viewportCamera->GetViewMatrix();
    const glm::mat4 projection = viewportCamera->GetProjectionMatrix();
    const Frustum cameraFrustum(projection * view);

    m_SelectedMaskShader->Bind();
    m_SelectedMaskShader->SetMat4("u_View", view);
    m_SelectedMaskShader->SetMat4("u_Projection", projection);

    for (int selectedIndex : m_Scene.GetSelectedIndices())
    {
        Scene::Entry* entry = m_Scene.GetEntry(selectedIndex);
        if (!entry || !entry->Visible || !entry->Object || entry->Object->Opacity <= 0.001f ||
            !cameraFrustum.Intersects(entry->Object->GetWorldBoundingSphere()))
            continue;

        const bool isWaterNode = (bool)std::dynamic_pointer_cast<WaterNode>(entry->Object);
        const bool xzInput = isWaterNode || std::dynamic_pointer_cast<TerrainHeightMap>(entry->Object);
        entry->Object->DrawSelectedMask(view, projection, m_SelectedMaskShader, xzInput, isWaterNode ? -0.5f : 0.0f);
    }

    m_SelectedMaskFBO->Unbind();
    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    m_SelectedMaskValid = true;
#endif
}

uint64_t Application::CompositeSelectedOutline(uint64_t sceneColorTexture)
{
#ifdef G_OPENGL
    if (IsViewport2DEditMode())
    {
        m_SelectedOutlineValid = false;
        return sceneColorTexture;
    }
    if (!sceneColorTexture || !m_SelectedMaskFBO || !m_SelectedEdgeShader ||
        !m_SelectedOutlineFBO || !m_SelectedOutlineTexture)
    {
        m_SelectedOutlineValid = false;
        return sceneColorTexture;
    }

    if (m_Scene.GetSelectedCount() == 0)
    {
        m_SelectedOutlineValid = false;
        return sceneColorTexture;
    }

    if (!m_SelectedMaskValid)
        return sceneColorTexture;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, m_SelectedOutlineFBO);
    glViewport(0, 0, (GLsizei)m_ViewportSize.x, (GLsizei)m_ViewportSize.y);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBindVertexArray(m_SelectedOutlineQuadVAO);

    m_SelectedEdgeShader->Bind();
    m_SelectedEdgeShader->SetInt("u_SceneColor", 0);
    m_SelectedEdgeShader->SetInt("u_SelectedMask", 1);
    m_SelectedEdgeShader->SetFloat2("u_ViewportSize", m_ViewportSize);
    m_SelectedEdgeShader->SetFloat("u_EdgeWidth", m_SelectedEdgeWidth);
    m_SelectedEdgeShader->SetFloat4("u_EdgeColor", glm::vec4(1.0f, 0.85f, 0.05f, 1.0f));
    m_SelectedEdgeShader->SetInt("u_FillSelectedPixels", IsViewport2D() ? 1 : 0);
    glBindTextureUnit(0, (uint32_t)sceneColorTexture);
    glBindTextureUnit(1, (uint32_t)m_SelectedMaskFBO->GetColorAttachmentRendererID(0));
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    m_SelectedOutlineValid = true;
    return m_SelectedOutlineTexture;
#else
    return sceneColorTexture;
#endif
}

int Application::ReadPickupPixel(int x, int y)
{
    if (!m_PickupFBO)
        return 0;

    const auto& spec = m_PickupFBO->GetSpecification();
    if (x < 0 || y < 0 || x >= (int)spec.Width || y >= (int)spec.Height)
        return 0;

    if (m_PickupPassDirty)
        RenderPickupPass();

    m_PickupFBO->Bind(false);
    const int id = m_PickupFBO->ReadPixel(0, x, y);
    m_PickupFBO->Unbind();
    return id;
}

void Application::SetSelectedObjectIndex(int index)
{
    m_Scene.SetSelectedIndex(index);
    if (m_Viewport2DEditMode)
    {
        ClearSelected2DSubElement();
        m_GizmoTargetTransform = nullptr;
    }
    else
    {
        m_GizmoTargetTransform = m_Scene.GetSelectedTransform();
        if (IsViewport2D())
            UpdateViewport2DObjectGizmoTarget(true);
    }
    InvalidateSelectedMask();
    InvalidatePickupPass();
}

void Application::AddSelectedObjectIndex(int index)
{
    m_Scene.AddSelectedIndex(index);
    if (m_Viewport2DEditMode)
    {
        ClearSelected2DSubElement();
        m_GizmoTargetTransform = nullptr;
    }
    else
    {
        m_GizmoTargetTransform = m_Scene.GetSelectedTransform();
        if (IsViewport2D())
            UpdateViewport2DObjectGizmoTarget(true);
    }
    InvalidateSelectedMask();
    InvalidatePickupPass();
}

void Application::ApplyGizmoDeltaToSelection(const Transform& before, const Transform& after)
{
    if (m_Scene.GetSelectedCount() <= 1)
        return;

    const int activeIndex = m_Scene.GetSelectedIndex();
    const glm::vec3 translationDelta = after.translation - before.translation;
    const glm::quat rotationDelta = glm::normalize(after.rotation * glm::inverse(before.rotation));
    glm::vec3 scaleRatio(1.0f);
    for (int axis = 0; axis < 3; axis++)
    {
        const float beforeScale = before.scale[axis];
        scaleRatio[axis] = std::abs(beforeScale) > 0.000001f ? after.scale[axis] / beforeScale : 1.0f;
    }

    for (int selectedIndex : m_Scene.GetSelectedIndices())
    {
        if (selectedIndex == activeIndex)
            continue;

        Transform* transform = m_Scene.GetTransform(selectedIndex);
        if (!transform)
            continue;

        transform->translation += translationDelta;
        transform->rotation = glm::normalize(rotationDelta * transform->rotation);
        transform->scale *= scaleRatio;
    }
}

void Application::ApplyGizmoDeltaToSelectedObject2DPivot(const Transform& before, const Transform& after)
{
    if (!IsViewport2D() || m_Viewport2DEditMode || m_Scene.GetSelectedCount() != 1)
        return;

    Transform* transform = m_Scene.GetSelectedTransform();
    if (!transform)
        return;

    const glm::quat rotationDelta = glm::normalize(after.rotation * glm::inverse(before.rotation));
    glm::vec3 scaleRatio(1.0f);
    for (int axis = 0; axis < 3; axis++)
    {
        const float beforeScale = before.scale[axis];
        scaleRatio[axis] = std::abs(beforeScale) > 0.000001f ? after.scale[axis] / beforeScale : 1.0f;
    }

    const glm::vec3 relativeToPivot = transform->translation - before.translation;
    transform->translation = after.translation + glm::rotate(rotationDelta, relativeToPivot * scaleRatio);
    transform->rotation = glm::normalize(rotationDelta * transform->rotation);
    transform->scale *= scaleRatio;
    m_Viewport2DPivotTransform = after;
}

void Application::ApplyGizmoDeltaToSelected2DSubElements(const Transform& before, const Transform& after)
{
    Scene::Entry* entry = m_Scene.GetSelectedEntry();
    Ref<Object2D> object2D = entry ? std::dynamic_pointer_cast<Object2D>(entry->Object) : nullptr;
    if (!object2D)
        return;

    const auto& selectedIndices = object2D->GetSelectedSubElementIndices();
    if (selectedIndices.empty())
        return;

    const glm::quat rotationDelta = glm::normalize(after.rotation * glm::inverse(before.rotation));
    const glm::quat objectRotation = object2D->Transfm.rotation;
    const glm::quat localRotationDelta = glm::normalize(glm::inverse(objectRotation) * rotationDelta * objectRotation);
    const glm::mat4 inverseObjectTransform = glm::inverse(object2D->Transfm.GetMatrix());
    const glm::vec3 localBeforePivot = glm::vec3(inverseObjectTransform * glm::vec4(before.translation, 1.0f));
    const glm::vec3 localAfterPivot = glm::vec3(inverseObjectTransform * glm::vec4(after.translation, 1.0f));
    glm::vec3 scaleRatio(1.0f);
    for (int axis = 0; axis < 3; axis++)
    {
        const float beforeScale = before.scale[axis];
        scaleRatio[axis] = std::abs(beforeScale) > 0.000001f ? after.scale[axis] / beforeScale : 1.0f;
    }

    for (int selectedIndex : selectedIndices)
    {
        Transform* transform = object2D->GetSubElementTransform(selectedIndex);
        if (!transform)
            continue;

        const glm::vec3 relativeToPivot = transform->translation - localBeforePivot;
        transform->translation = localAfterPivot + glm::rotate(localRotationDelta, relativeToPivot * scaleRatio);
        transform->rotation = glm::normalize(localRotationDelta * transform->rotation);
        transform->scale *= scaleRatio;
    }

    m_Viewport2DPivotTransform = after;
}

bool Application::HasMultiSelected2DSubElements() const
{
    if (!IsViewport2D() || !m_Viewport2DEditMode)
        return false;

    const int selectedIndex = m_Scene.GetSelectedIndex();
    const auto& objects = m_Scene.GetObjects();
    if (selectedIndex < 0 || selectedIndex >= (int)objects.size())
        return false;

    Ref<Object2D> object2D = objects[(size_t)selectedIndex].Object ?
        std::dynamic_pointer_cast<Object2D>(objects[(size_t)selectedIndex].Object) : nullptr;
    return object2D && object2D->GetSelectedSubElementIndices().size() > 1;
}

void Application::SetViewport2DPivotIndex(int index)
{
    m_Viewport2DPivotIndex = std::clamp(index, 0, 8);
    Update2DSubElementGizmoTarget(true);
    UpdateViewport2DObjectGizmoTarget(true);
}

void Application::UpdateViewport2DObjectGizmoTarget(bool forceRecenter)
{
    if (!IsViewport2D() || m_Viewport2DEditMode)
        return;

    Scene::Entry* entry = m_Scene.GetSelectedEntry();
    if (!entry || !entry->Object || m_Scene.GetSelectedCount() != 1)
    {
        m_GizmoTargetTransform = m_Scene.GetSelectedTransform();
        return;
    }

    if (forceRecenter || m_GizmoTargetTransform != &m_Viewport2DPivotTransform)
    {
        glm::vec3 minimum;
        glm::vec3 maximum;
        if (ObjectXYBounds(*entry->Object, minimum, maximum))
        {
            m_Viewport2DPivotTransform = entry->Object->Transfm;
            m_Viewport2DPivotTransform.translation = PivotPointFromBounds(minimum, maximum, m_Viewport2DPivotIndex);
        }
    }

    m_GizmoTargetTransform = &m_Viewport2DPivotTransform;
}

void Application::Update2DSubElementGizmoTarget(bool forceRecenter)
{
    if (!IsViewport2DEditMode())
        return;

    Scene::Entry* entry = m_Scene.GetSelectedEntry();
    Ref<Object2D> object2D = entry ? std::dynamic_pointer_cast<Object2D>(entry->Object) : nullptr;
    if (!object2D)
    {
        m_GizmoTargetTransform = nullptr;
        return;
    }

    const auto& selectedIndices = object2D->GetSelectedSubElementIndices();
    if (selectedIndices.empty())
    {
        m_GizmoTargetTransform = nullptr;
        return;
    }

    if (forceRecenter || m_GizmoTargetTransform != &m_Viewport2DPivotTransform)
    {
        glm::vec3 minimum;
        glm::vec3 maximum;
        if (object2D->GetSelectedSubElementBounds(minimum, maximum))
        {
            m_Viewport2DPivotTransform = {};
            m_Viewport2DPivotTransform.translation = PivotPointFromBounds(minimum, maximum, m_Viewport2DPivotIndex);
        }
    }
    m_GizmoTargetTransform = &m_Viewport2DPivotTransform;
}

void Application::SetViewportRenderMode(ViewportRenderMode mode)
{
    if (m_ViewportRenderMode == mode)
        return;
    m_ViewportRenderMode = mode;
    if (m_PathTracer && mode == ViewportRenderMode::Rendering)
        m_PathTracer->ResetAccumulation();
    if (m_SVGF)
        m_SVGF->ResetHistory();
}

void Application::SetViewportViewMode(ViewportViewMode mode)
{
    if (m_ViewportViewMode == mode)
        return;

    m_ViewportViewMode = mode;
    if (mode == ViewportViewMode::View2D)
    {
        SetViewportRenderMode(ViewportRenderMode::Editor);
        UpdateViewport2DObjectGizmoTarget(true);
    }
    else
        SetViewport2DEditMode(false);
    InvalidateSelectedMask();
    InvalidatePickupPass();
}

void Application::SetViewport2DEditMode(bool enabled)
{
    enabled = enabled && IsViewport2D();
    if (m_Viewport2DEditMode == enabled)
        return;

    m_Viewport2DEditMode = enabled;
    if (!m_Viewport2DEditMode)
    {
        ClearSelected2DSubElement();
        m_GizmoTargetTransform = m_Scene.GetSelectedTransform();
    }
    else
    {
        Scene::Entry* entry = m_Scene.GetSelectedEntry();
        Ref<Object2D> object2D = entry ? std::dynamic_pointer_cast<Object2D>(entry->Object) : nullptr;
        m_GizmoTargetTransform = object2D ? object2D->GetSubElementTransform(object2D->GetSelectedSubElementIndex()) : nullptr;
        Update2DSubElementGizmoTarget(true);
    }
    InvalidateSelectedMask();
    InvalidatePickupPass();
}

void Application::ToggleViewport2DEditMode()
{
    SetViewport2DEditMode(!m_Viewport2DEditMode);
}

bool Application::SetSelected2DSubElementIndex(int index)
{
    Scene::Entry* entry = m_Scene.GetSelectedEntry();
    Ref<Object2D> object2D = entry ? std::dynamic_pointer_cast<Object2D>(entry->Object) : nullptr;
    if (!object2D)
        return false;

    object2D->SetSelectedSubElementIndex(index);
    Update2DSubElementGizmoTarget(true);
    InvalidateSelectedMask();
    InvalidatePickupPass();
    return true;
}

bool Application::SetSelected2DSubElementIndices(const std::vector<int>& indices)
{
    Scene::Entry* entry = m_Scene.GetSelectedEntry();
    Ref<Object2D> object2D = entry ? std::dynamic_pointer_cast<Object2D>(entry->Object) : nullptr;
    if (!object2D)
        return false;

    object2D->SetSelectedSubElementIndices(indices);
    Update2DSubElementGizmoTarget(true);
    InvalidateSelectedMask();
    InvalidatePickupPass();
    return true;
}

void Application::ClearSelected2DSubElement()
{
    for (const auto& entry : m_Scene.GetObjects())
    {
        Ref<Object2D> object2D = entry.Object ? std::dynamic_pointer_cast<Object2D>(entry.Object) : nullptr;
        if (object2D)
            object2D->ClearSelectedSubElement();
    }
    if (m_Viewport2DEditMode)
        m_GizmoTargetTransform = nullptr;
    InvalidateSelectedMask();
    InvalidatePickupPass();
}

void Application::PanViewport2D(const glm::vec2& deltaPixels)
{
    Ref<OrthographicCamera2D> camera2D = std::dynamic_pointer_cast<OrthographicCamera2D>(m_Camera2D);
    if (!camera2D)
        return;

    camera2D->PanPixels(deltaPixels, m_ViewportSize);
    InvalidateSelectedMask();
    InvalidatePickupPass();
}

void Application::ZoomViewport2D(float wheelDelta)
{
    Ref<OrthographicCamera2D> camera2D = std::dynamic_pointer_cast<OrthographicCamera2D>(m_Camera2D);
    if (!camera2D || wheelDelta == 0.0f)
        return;

    const float factor = wheelDelta > 0.0f ? 0.9f : 1.1f;
    const int steps = (int)std::abs(wheelDelta);
    for (int i = 0; i < std::max(1, steps); ++i)
        camera2D->Zoom(factor);
    InvalidateSelectedMask();
    InvalidatePickupPass();
}

void Application::ResetViewport2D()
{
    Ref<OrthographicCamera2D> camera2D = std::dynamic_pointer_cast<OrthographicCamera2D>(m_Camera2D);
    if (!camera2D)
        return;

    camera2D->ResetView();
    InvalidateSelectedMask();
    InvalidatePickupPass();
}

void Application::DrawViewport2DGrid(const glm::mat4&, const glm::mat4&)
{
    Ref<OrthographicCamera2D> camera2D = std::dynamic_pointer_cast<OrthographicCamera2D>(m_Camera2D);
    if (!camera2D || m_ViewportSize.x <= 0.0f || m_ViewportSize.y <= 0.0f)
        return;

    const glm::vec2 center = camera2D->GetCenter();
    const float height = camera2D->GetOrthoHeight();
    const float width = height * (m_ViewportSize.x / m_ViewportSize.y);
    const float halfWidth = width * 0.5f;
    const float halfHeight = height * 0.5f;
    const float minX = center.x - halfWidth;
    const float maxX = center.x + halfWidth;
    const float minY = center.y - halfHeight;
    const float maxY = center.y + halfHeight;

    float step = std::pow(10.0f, std::floor(std::log10((std::max)(height, 1.0f) / 12.0f)));
    if (height / step > 24.0f)
        step *= 2.0f;
    if (height / step > 24.0f)
        step *= 2.5f;

    const glm::vec4 minorColor(0.13f, 0.13f, 0.13f, 0.45f);
    const glm::vec4 majorColor(0.22f, 0.22f, 0.22f, 0.62f);
    const glm::vec4 xAxisColor(0.72f, 0.16f, 0.20f, 0.90f);
    const glm::vec4 yAxisColor(0.18f, 0.64f, 0.16f, 0.90f);
    const float z = -0.01f;

    int index = 0;
    for (float x = std::floor(minX / step) * step; x <= maxX; x += step, ++index)
    {
        const bool axis = std::abs(x) < step * 0.001f;
        const bool major = index % 5 == 0;
        RenderCommand::FlushLine(glm::vec3(x, minY, z), glm::vec3(x, maxY, z),
            axis ? yAxisColor : (major ? majorColor : minorColor));
    }

    index = 0;
    for (float y = std::floor(minY / step) * step; y <= maxY; y += step, ++index)
    {
        const bool axis = std::abs(y) < step * 0.001f;
        const bool major = index % 5 == 0;
        RenderCommand::FlushLine(glm::vec3(minX, y, z), glm::vec3(maxX, y, z),
            axis ? xAxisColor : (major ? majorColor : minorColor));
    }
}

uint64_t Application::GetViewportColorTextureID() const
{
    if (!m_ViewportFBO)
        return 0;
    if (m_ViewportRenderMode == ViewportRenderMode::Editor && m_SelectedOutlineValid && m_SelectedOutlineTexture)
        return m_SelectedOutlineTexture;
    if (m_ViewportRenderMode == ViewportRenderMode::Rendering && m_SVGF && m_SVGF->Enabled() &&
        m_SVGF->GetOutputTexture() && m_SVGF->HasHistory())
        return m_SVGF->GetOutputTexture();
    if (m_ViewportRenderMode == ViewportRenderMode::Rendering && m_PathTracer && m_PathTracer->GetOutputTexture())
        return m_PathTracer->GetOutputTexture();
    if (!IsViewport2D() && !IsMSAAEnabled() && m_FXAA && m_FXAA->Enabled() && m_FXAA->GetOutputTexture())
        return m_FXAA->GetOutputTexture();
    if (m_OITCompositeValid && m_OITCompositeTexture)
        return m_OITCompositeTexture;
    if (m_SSAO && m_SSAO->Enabled() && m_SSAO->GetOutputTexture())
        return m_SSAO->GetOutputTexture();
    return m_ViewportFBO->GetColorAttachmentRendererID(0);
}

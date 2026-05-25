#include "stdsfx.h"
#include "Application.h"
#include "ImGuiLayer.h"
#include <ApplicationEvent.h>
#include <filesystem>
#include <chrono>
#ifdef G_OPENGL
#include <glad/glad.h>
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3.h>

#include "Renderer/Renderer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Buffer.h"

#include "Camera/FPSCamera.h"

#include <Primitive/Gizmo.h>


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

    // Push ImGui layer as overlay
    m_ImGuiLayer = std::make_shared<ImGuiLayer>();
    PushOverlay(m_ImGuiLayer);

	m_ShaderLibrary = CreateRef<ShaderLibrary>();
    m_ShaderLibrary->LoadDefault();


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

    m_ViewportFBO = FrameBuffer::Create(FrameBufferSpecification{ (uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y,
        { FrameBufferTextureSpecification(FrameBufferTextureFormat::RGBA8),
          FrameBufferTextureSpecification(FrameBufferTextureFormat::RGBA16F),
          FrameBufferTextureSpecification(FrameBufferTextureFormat::RGBA16F),
          FrameBufferTextureSpecification(FrameBufferTextureFormat::Depth) } });
    m_SSAO = CreateRef<SSAO>((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
    SetGizmoViewportSize((int)m_ViewportSize.x, (int)m_ViewportSize.y);

    // CSM must be created after OpenGL context is initialized
    m_CSM = CreateRef<CSM>();
    m_ProbeGI = CreateRef<ProbeGI>();
    m_PBRIBL = CreateRef<PBRIBL>(
        "E:/githubs/MapleEngine-main/Assets/textures/HDR_110_Tunnel_Ref.hdr");
}

void Application::Run()
{
    using clock = std::chrono::steady_clock;
    auto lastTime = clock::now();

    while (m_Running && !m_WindowInterface->ShouldClose())
    {
        auto currentTime = clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        // Process continuous keyboard input (velocity-based)
        ProcessKeyboardInput(deltaTime);
        // --- RESIZE FBO IF VIEWPORT SIZE CHANGED SINCE LAST FRAME ---
        if (m_ViewportFBO && (m_ViewportFBO->GetSpecification().Width != (uint32_t)m_ViewportSize.x ||
                              m_ViewportFBO->GetSpecification().Height != (uint32_t)m_ViewportSize.y))
        {
            m_ViewportFBO->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_SSAO->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        }

        // --- CSM SHADOW MAP UPDATE ---
        {
            m_CSM->Update(m_Camera->GetViewMatrix(), m_Camera->GetProjectionMatrix(), m_Camera->getNearPlane(), m_Camera->getFarPlane());

            auto depthShader = GetShaderLibrary()->Get("ShadowDepth");
            auto& lightViewProj = m_CSM->GetLightViewProjMatrices();

            for (uint32_t i = 0; i < m_CSM->GetCascadeCount(); i++)
            {
                m_CSM->BeginShadowPass(i);
                depthShader->Bind();
                for (const auto& entry : m_Scene.GetObjects())
                {
                    if (!entry.Visible) continue;
                    depthShader->SetMat4("u_LightViewProj", lightViewProj[i]);
                    for (auto& mesh : entry.Object->Meshes)
                    {
                        depthShader->SetMat4("u_Model", mesh->Transfm.GetMatrix());
                        RenderCommand::DrawIndexed(mesh->VertexObject);
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
        }
        else
        {
            // In Game mode, render to full window
            RenderCommand::SetViewport(0, 0, m_WindowInterface->GetWidth(), m_WindowInterface->GetHeight());
        }

        RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
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
        {
            RenderCommand::SetDepthRange(0.99f, 1.0f);
#ifdef G_OPENGL
            glDepthMask(GL_FALSE);
#endif
            auto backshader = Application::Get().GetShaderLibrary()->Get("DefaultBackgroundSH");
            backshader->Bind();
            auto viewrotate = glm::mat4(glm::mat3(m_Camera->GetViewMatrix()));
            auto invViewProj = glm::inverse(m_Camera->GetProjectionMatrix() * viewrotate);
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
        for (auto& layer : m_LayerStack)
            layer->OnUpdate();

        // Draw all visible objects in the scene
        for (const auto& entry : m_Scene.GetObjects())
            if (entry.Visible)
                entry.Object->Draw(m_Camera->GetViewMatrix(), m_Camera->GetProjectionMatrix());

            if (m_AppMode == AppMode::Editor)
            {
                // Reset model matrix to identity so gizmo lines render in world space
                {
                    auto resetShader = GetShaderLibrary()->Get("DefaultColor");
                    resetShader->Bind();
                    resetShader->SetMat4("u_View", m_Camera->GetViewMatrix());
                    resetShader->SetMat4("u_Projection", m_Camera->GetProjectionMatrix());
                    resetShader->SetMat4("u_Model", glm::mat4(1.0f));
                }

                // Directional light indicator
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
            m_ProbeGI->DrawDebug();
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
                if (m_GizmoLocal) gizmoFlags |= GIZMO_LOCAL;
                if (m_GizmoView)  gizmoFlags |= GIZMO_VIEW;

                SetGizmoSize(m_GizmoSize);
                SetGizmoLineWidth(m_GizmoLineWidth);
                SetGizmoViewportSize((int)m_ViewportSize.x, (int)m_ViewportSize.y);

                if (DrawGizmo3D(m_Camera->GetViewMatrix(), m_Camera->GetProjectionMatrix(),
                    m_LeftDownGizmo, m_ViewportMousePos, gizmoFlags, targetTransform))
                {
                    if (m_Camera->isInputEnabled()) m_Camera->setInputEnabled(false);
                }
            }

            // Flush gizmo lines on top of everything (override depth)
            RenderCommand::SetDepthRange(0, 0.001f);
            RenderCommand::Flush();
            RenderCommand::SetDepthRange(0, 1);

            if (m_ViewportFBO) m_ViewportFBO->Unbind();
            if (m_SSAO->Enabled())
            {
                m_SSAO->Render(
                    m_ViewportFBO->GetColorAttachmentRendererID(0),
                    m_ViewportFBO->GetColorAttachmentRendererID(1),
                    m_ViewportFBO->GetColorAttachmentRendererID(2),
                    m_Camera->GetViewMatrix(),
                    m_Camera->GetProjectionMatrix());
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
        m_Camera->processKeyboard(forward, right, up, deltaTime);
}

void Application::OnEvent(Event& e)
{
    EventDispatcher dispatcher(e);
    dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(Application::OnWindowClose));
    dispatcher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(Application::OnWindowResize));


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
			m_AppMode = (m_AppMode == AppMode::Editor) ? AppMode::Game : AppMode::Editor;
			break;
		case GLFW_KEY_ESCAPE:
			if (m_AppMode == AppMode::Game) m_AppMode = AppMode::Editor;
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
    if (!object->LoadFromPath<VertexNormal>(filepath))
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
    return object;
}

void Application::ClearObject3Ds()
{
    m_Scene.Clear();
    m_GizmoTargetTransform = nullptr;
}

void Application::SetViewportSize(const glm::vec2& size)
{
    m_ViewportSize = size;
    SetGizmoViewportSize((int)size.x, (int)size.y);
}

void Application::SetSelectedObjectIndex(int index)
{
    m_Scene.SetSelectedIndex(index);
    m_GizmoTargetTransform = m_Scene.GetSelectedTransform();
}

uint64_t Application::GetViewportColorTextureID() const
{
    if (!m_ViewportFBO)
        return 0;
    if (m_SSAO && m_SSAO->Enabled() && m_SSAO->GetOutputTexture())
        return m_SSAO->GetOutputTexture();
    return m_ViewportFBO->GetColorAttachmentRendererID(0);
}

#include "stdsfx.h"
#include "Application.h"
#include "ImGuiLayer.h"
#include <ApplicationEvent.h>

#include "Renderer/Renderer.h"
#include "Renderer/RenderCommand.h"

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


    m_Camera = CreateRef<FPSCamera>(glm::vec3(0, 0, 2), glm::vec3(0, 0, 0), 45.0f, w / (float)h);
    //((FPSCamera*)m_Camera.get())->lookAt(glm::vec3(0, 0, 0));
    
    // Push ImGui layer as overlay
    m_ImGuiLayer = std::make_shared<ImGuiLayer>();
    PushOverlay(m_ImGuiLayer);

	m_ShaderLibrary = CreateRef<ShaderLibrary>();
    m_ShaderLibrary->LoadDefault();


    m_backgroundCube = CreateScope<Cube>(1.0f);

    SetGizmoViewportSize(w,h);
}

void Application::Run()
{


    while (!m_WindowInterface->ShouldClose())
    {
#pragma region background

        RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
        RenderCommand::Clear();
        //draw background
        {
            RenderCommand::SetDepthRange(0.99f, 1.0f);
            auto backshader = Application::Get().GetShaderLibrary()->Get("DefaultBackgroundSH");
            backshader->Bind();
            auto viewrotate = glm::mat4(glm::mat3(m_Camera->GetViewMatrix())); // 去除平移部分
            auto invViewProj = glm::inverse(m_Camera->GetProjectionMatrix() * viewrotate);
            backshader->SetMat4("u_invViewProj", invViewProj);
            const glm::vec3 shCoeffs[9] = {
                glm::vec3(0.79,  0.44,  0.54),   // L00
                glm::vec3(0.39,  0.35,  0.60),   // L1-1
                glm::vec3(-0.34, -0.18, -0.27),   // L10
                glm::vec3(-0.29, -0.06,  0.01),   // L11
                glm::vec3(-0.11, -0.05, -0.12),   // L2-2
                glm::vec3(-0.26, -0.22, -0.47),   // L2-1
                glm::vec3(-0.16, -0.09, -0.15),   // L20
                glm::vec3(0.56,  0.21,  0.14),   // L21
                glm::vec3(0.21, -0.05, -0.30)    // L22
            };
            backshader->SetVec3Array("shCoeffs", (float*) & shCoeffs[0].x, 9);
            RenderCommand::DrawIndexed(m_backgroundCube->GetVertexArray(), m_backgroundCube->GetCount());
            RenderCommand::SetDepthRange(0.f, 1.0f);
        }
#pragma endregion

        // Update all layers
        for (auto& layer : m_LayerStack)
        {
            layer->OnUpdate();
        }

        // Render all layers
        m_ImGuiLayer->Begin();
        for (auto layer : m_LayerStack)
            layer->OnImGuiRender();
		m_ImGuiLayer->End();


        RenderCommand::SetDepthRange(0,0.001f);
        RenderCommand::Flush();
        RenderCommand::SetDepthRange(0,1);

        m_WindowInterface->PollEvents();
    }
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


	//camera control

	//keyboard
	KeyPressedEvent* ke = dynamic_cast<KeyPressedEvent*>(&e);
    if(ke)
		m_Camera->processKeyboard((ke->GetKeyCode() == 'W' ? 1 : (ke->GetKeyCode() == 'S' ? -1 : 0)), 
			(ke->GetKeyCode() == 'D' ? 1 : (ke->GetKeyCode() == 'A' ? -1 : 0)), 
			(ke->GetKeyCode() == 'E' ? 1 : (ke->GetKeyCode() == 'Q' ? -1 : 0)), 
			0.016f);
    //keyboard
}


bool Application::OnWindowClose(WindowCloseEvent& e)
{
    m_Running = false;
    return true;
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
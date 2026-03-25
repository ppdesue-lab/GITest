#include "stdsfx.h"
#include "Application.h"
#include "ImGuiLayer.h"
#include <ApplicationEvent.h>

#include "Renderer/Renderer.h"
#include "Renderer/RenderCommand.h"

#include "Camera/FPSCamera.h"


Application* Application::s_Instance = nullptr;

Application::Application(int w,int h)
{
    Log::Init();
    INFO("Application Initialized");
    s_Instance = this;

    m_WindowInterface = CreateWindow(w, h, "KEngine Application");
    m_WindowInterface->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));
    RenderCommand::Init();
    // Push ImGui layer as overlay
    m_ImGuiLayer = std::make_shared<ImGuiLayer>();
    PushOverlay(m_ImGuiLayer);

	m_ShaderLibrary = CreateRef<ShaderLibrary>();
    m_ShaderLibrary->LoadDefault();

	m_Camera = CreateRef<FPSCamera>(glm::vec3(0,0,2),glm::vec3(0,0,0),45.0f, w / (float)h);
    //((FPSCamera*)m_Camera.get())->lookAt(glm::vec3(0, 0, 0));
}

void Application::Run()
{
    while (!m_WindowInterface->ShouldClose())
    {
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
    //mouse
    MouseMovedEvent* me = dynamic_cast<MouseMovedEvent*>(&e);
    if(me)
    	m_Camera->processMouseMovement(me->GetX(), me->GetY());
    //mouse button
	MouseButtonPressedEvent* mbpe = dynamic_cast<MouseButtonPressedEvent*>(&e);
    if(mbpe && mbpe->GetMouseButton() == 0)
        m_Camera->setInputEnabled(true);
    //mouse button
	MouseButtonReleasedEvent* mbre = dynamic_cast<MouseButtonReleasedEvent*>(&e);
    if(mbre && mbre->GetMouseButton() == 0)
        m_Camera->setInputEnabled(false);
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

    //Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());

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
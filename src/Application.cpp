#include "stdsfx.h"
#include "Application.h"
#include "ImGuiLayer.h"
#include <ApplicationEvent.h>

#include "Renderer/Renderer.h"
#include "Renderer/RenderCommand.h"


Application* Application::s_Instance = nullptr;

Application::Application()
{
    Log::Init();
    INFO("Application Initialized");
    s_Instance = this;

    m_WindowInterface = CreateWindow(800, 600, "KEngine Application");
    m_WindowInterface->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));
    RenderCommand::Init();
    // Push ImGui layer as overlay
    m_ImGuiLayer = std::make_shared<ImGuiLayer>();
    PushOverlay(m_ImGuiLayer);

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
#include "stdsfx.h"
#include "Application.h"
#include "ImGuiLayer.h"

Application::Application()
{
    Log::Init();
    INFO("Application Initialized");

    m_WindowInterface = CreateWindow(800, 600, "KEngine Application");
    
    // Push ImGui layer as overlay
    auto imguiLayer = std::make_shared<ImGuiLayer>();
    PushOverlay(imguiLayer);
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
        for (auto& layer : m_LayerStack)
        {
            layer->OnRender();
        }

        m_WindowInterface->PollEvents();
    }
}

void Application::OnEvent(Event& e)
{
    // Dispatch event to layers in reverse order (overlays first)
    for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
    {
        (*it)->OnEvent(e);
    }
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
#pragma once

#include "WindowInterface.h"
#include "Log.h"
#include "base.h"
#include "Event.h"
#include "LayerStack.h"
#include "ImGuiLayer.h"
#include <memory>

class Application
{
public:
    Application();
    virtual ~Application() = default;
    virtual void Run();

    WindowInterface& GetWindow() { return *m_WindowInterface; }

    void OnEvent(Event& e);

    void PushLayer(std::shared_ptr<Layer> layer);
    void PushOverlay(std::shared_ptr<Layer> overlay);
    void PopLayer(std::shared_ptr<Layer> layer);
    void PopOverlay(std::shared_ptr<Layer> overlay);

    static Application& Get() { return *s_Instance; }
private:
    WindowInterface* m_WindowInterface = nullptr;
    LayerStack m_LayerStack;

	Ref<ImGuiLayer> m_ImGuiLayer;

    static Application* s_Instance;
};
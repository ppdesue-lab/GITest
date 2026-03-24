#pragma once

#include "WindowInterface.h"
#include "Log.h"
#include "Event.h"
#include "LayerStack.h"
#include <memory>

class Application
{
public:
    Application();
    virtual ~Application() = default;
    virtual void Run();

    WindowInterface& GetWindowInterface() { return *m_WindowInterface; }

    void OnEvent(Event& e);

    void PushLayer(std::shared_ptr<Layer> layer);
    void PushOverlay(std::shared_ptr<Layer> overlay);
    void PopLayer(std::shared_ptr<Layer> layer);
    void PopOverlay(std::shared_ptr<Layer> overlay);

private:
    WindowInterface* m_WindowInterface = nullptr;
    LayerStack m_LayerStack;

};
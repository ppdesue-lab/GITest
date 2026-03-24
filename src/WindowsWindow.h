#pragma once

#ifdef PLATFORM_WINDOWS


#include "WindowInterface.h"
#include <Renderer/GraphicsContext.h>

class WindowsWindow : public WindowInterface
{
public:
    WindowsWindow();
    ~WindowsWindow() override;
    void Create(int width, int height, const std::string& title) override;
    void DestroyWindow() override;
    void PollEvents() override;
    bool ShouldClose() const override;
private:
    WindowData m_Data;

    Scope<GraphicsContext> m_Context;
};

#endif
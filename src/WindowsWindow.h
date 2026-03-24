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

	void SetEventCallback(const EventCallbackFn& callback) { m_Data.EventCallback = callback; }
	void* GetNativeWindow() const override { return m_Data.glfwWindow; }

    unsigned int GetWidth() const override { return m_Data.Width; }
    unsigned int GetHeight() const override { return m_Data.Height; }
private:
    WindowData m_Data;

    Scope<GraphicsContext> m_Context;
};

#endif
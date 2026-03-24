#pragma once

// Interface for window management
#include <string>
#include <Event.h>

class WindowInterface
{   
public:
    using EventCallbackFn = std::function<void(Event&)>;
    struct WindowData
    {
        int Width;
        int Height;
        std::string Title;
        void* glfwWindow;

        EventCallbackFn EventCallback;
    };


    virtual ~WindowInterface() = default;

    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;

    virtual void SetEventCallback(const EventCallbackFn& callback) = 0;

    virtual void Create(int width, int height, const std::string& title) = 0;
    virtual void DestroyWindow() = 0;
    virtual void PollEvents() = 0;
    virtual bool ShouldClose() const = 0;


    virtual void* GetNativeWindow() const = 0;
};

extern "C" WindowInterface* CreateWindow(int width, int height, const std::string& title);
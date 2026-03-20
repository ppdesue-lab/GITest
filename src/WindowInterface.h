#pragma once

// Interface for window management
#include <string>

class WindowInterface
{   
public:
    struct WindowData
    {
        int Width;
        int Height;
        std::string Title;
        void* glfwWindow;
    };
    
    virtual ~WindowInterface() = default;

    virtual void Create(int width, int height, const std::string& title) = 0;
    virtual void DestroyWindow() = 0;
    virtual void PollEvents() = 0;
    virtual bool ShouldClose() const = 0;
};

extern "C" WindowInterface* CreateWindow(int width, int height, const std::string& title);
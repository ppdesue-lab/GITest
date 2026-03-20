#include "stdsfx.h"
#include "WindowInterface.h"
#include "WindowsWindow.h"


WindowInterface* CreateWindow(int width, int height, const std::string& title)
{
#ifdef PLATFORM_WINDOWS
    WindowInterface* window = new WindowsWindow();
#else
    #error "Unsupported platform for window creation"
#endif
    window->Create(width, height, title);
    return window;
}
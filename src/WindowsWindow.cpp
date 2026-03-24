#include "stdsfx.h"
#ifdef PLATFORM_WINDOWS

#include "WindowsWindow.h"
#include <glfw/glfw3.h>
#include <stdexcept>

WindowsWindow::WindowsWindow()
{
    if (!glfwInit())
    {
        throw std::runtime_error("Failed to initialize GLFW");
    }
    m_Data.glfwWindow = nullptr;
}

WindowsWindow::~WindowsWindow()
{
    DestroyWindow();
    glfwTerminate();
}

void WindowsWindow::Create(int width, int height, const std::string& title)
{
    m_Data.Width = width;
    m_Data.Height = height;
    m_Data.Title = title;

    //glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    //glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_Data.glfwWindow = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_Data.glfwWindow)
    {
        throw std::runtime_error("Failed to create GLFW window");
    }

    //context for renderer
    m_Context = GraphicsContext::Create(m_Data.glfwWindow);
    m_Context->Init();
}
void WindowsWindow::DestroyWindow()
{
    if (m_Data.glfwWindow)
    {
        glfwDestroyWindow(static_cast<GLFWwindow*>(m_Data.glfwWindow));
        m_Data.glfwWindow = nullptr;
    }
}
void WindowsWindow::PollEvents()
{
    m_Context->SwapBuffers();
    //glfwSwapBuffers(static_cast<GLFWwindow*>(m_Data.glfwWindow));
    glfwPollEvents();
}

bool WindowsWindow::ShouldClose() const
{
    return glfwWindowShouldClose(static_cast<GLFWwindow*>(m_Data.glfwWindow));
}



#endif

//#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include "OpenGLContext.h"
void OpenGLContext::Init()
{
    glfwMakeContextCurrent(m_WindowHandle);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
    }
}
OpenGLContext::OpenGLContext(GLFWwindow* window)
{
    m_WindowHandle = window;
}


void OpenGLContext::SwapBuffers()
{
    glfwSwapBuffers(m_WindowHandle);
}

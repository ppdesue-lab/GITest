
//#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include "OpenGLContext.h"
#include "Renderer/ProfileTimer.h"

void OpenGLContext::Init()
{
    glfwMakeContextCurrent(m_WindowHandle);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
    }
    //glfwSwapInterval(0);
}

OpenGLContext::OpenGLContext(GLFWwindow* window)
{
    m_WindowHandle = window;
}


void OpenGLContext::SwapBuffers()
{
    PROFILE_SCOPE("OpenGL.SwapBuffers");
    glfwSwapBuffers(m_WindowHandle);
}

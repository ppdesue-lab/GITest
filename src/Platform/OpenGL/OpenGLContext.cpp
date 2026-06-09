
//#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include "OpenGLContext.h"

namespace
{
void APIENTRY OpenGLDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity,
    GLsizei length, const GLchar* message, const void* userParam)
{
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
        return;

    std::cerr << "OpenGL debug"
        << " source=0x" << std::hex << source
        << " type=0x" << type
        << " id=" << std::dec << id
        << " severity=0x" << std::hex << severity
        << std::dec << ": " << std::string(message, message + length) << std::endl;
    (void)userParam;
}
}

void OpenGLContext::Init()
{
    glfwMakeContextCurrent(m_WindowHandle);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
    }

    GLint contextFlags = 0;
    glGetIntegerv(GL_CONTEXT_FLAGS, &contextFlags);
    if ((contextFlags & GL_CONTEXT_FLAG_DEBUG_BIT) != 0 && glDebugMessageCallback)
    {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(OpenGLDebugCallback, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
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

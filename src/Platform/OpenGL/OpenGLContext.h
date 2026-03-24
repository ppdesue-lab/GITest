#pragma once

#include <GLFW/glfw3.h>
#include <Renderer/GraphicsContext.h>
class OpenGLContext : public GraphicsContext
{
public:
	OpenGLContext(GLFWwindow* windowHandle);

	virtual void Init() override;
	virtual void SwapBuffers() override;
private:
	GLFWwindow* m_WindowHandle;
};
#include "stdsfx.h"
#ifdef PLATFORM_WINDOWS

#include "WindowsWindow.h"
#include <glfw/glfw3.h>
#include <stdexcept>
#include <MouseEvent.h>
#include <KeyEvent.h>
#include <ApplicationEvent.h>

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

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_Data.glfwWindow = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_Data.glfwWindow)
    {
        throw std::runtime_error("Failed to create GLFW window");
    }

    //context for renderer
    m_Context = GraphicsContext::Create(m_Data.glfwWindow);
    m_Context->Init();

    //set glfw event callback for user input

	GLFWwindow* window = static_cast<GLFWwindow*>(m_Data.glfwWindow);
	glfwSetWindowUserPointer(window, &m_Data);

	// Set GLFW callbacks
	glfwSetWindowSizeCallback(window, [](GLFWwindow* window, int width, int height)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			data.Width = width;
			data.Height = height;

			WindowResizeEvent event(width, height);
			data.EventCallback(event);
		});

	glfwSetWindowCloseCallback(window, [](GLFWwindow* window)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			WindowCloseEvent event;
			data.EventCallback(event);
		});

	glfwSetKeyCallback(window, [](GLFWwindow* window, int key, int scancode, int action, int mods)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

			switch (action)
			{
			case GLFW_PRESS:
			{
				KeyPressedEvent event(key, 0);
				data.EventCallback(event);
				break;
			}
			case GLFW_RELEASE:
			{
				KeyReleasedEvent event(key);
				data.EventCallback(event);
				break;
			}
			case GLFW_REPEAT:
			{
				KeyPressedEvent event(key, true);
				data.EventCallback(event);
				break;
			}
			}
		});

	glfwSetCharCallback(window, [](GLFWwindow* window, unsigned int keycode)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

			KeyTypedEvent event(keycode);
			data.EventCallback(event);
		});

	glfwSetMouseButtonCallback(window, [](GLFWwindow* window, int button, int action, int mods)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

			switch (action)
			{
			case GLFW_PRESS:
			{
				MouseButtonPressedEvent event(button);
				data.EventCallback(event);
				break;
			}
			case GLFW_RELEASE:
			{
				MouseButtonReleasedEvent event(button);
				data.EventCallback(event);
				break;
			}
			}
		});

	glfwSetScrollCallback(window, [](GLFWwindow* window, double xOffset, double yOffset)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

			MouseScrolledEvent event((float)xOffset, (float)yOffset);
			data.EventCallback(event);
		});

	glfwSetCursorPosCallback(window, [](GLFWwindow* window, double xPos, double yPos)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

			MouseMovedEvent event((float)xPos, (float)yPos);
			data.EventCallback(event);
		});
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
#include "Window.h"
#include "../core/Logger.h"

Window::~Window() 
{
    destroy();
}

bool Window::create(int width, int height, const std::string& title) 
{
    if (!glfwInit()) 
    {
        logger::error("glfwInit() failed");
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);

    if (!m_window) 
    {
        logger::error("glfwCreateWindow() failed");
        glfwTerminate();
        return false;
    }

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);

    return true;
}

void Window::destroy() 
{
    if (m_window) 
    {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }

    glfwTerminate();
}

bool Window::shouldClose() const { return glfwWindowShouldClose(m_window) == GLFW_TRUE; }
void Window::poll() { glfwPollEvents(); }
void Window::waitEvents() { glfwWaitEvents(); }
void Window::getFramebufferSize(int& w, int& h) const { glfwGetFramebufferSize(m_window, &w, &h); }

void Window::framebufferResizeCallback(GLFWwindow* win, int, int) 
{
    auto* self = reinterpret_cast<Window*>(glfwGetWindowUserPointer(win));
    if (self) self->m_framebufferResized = true;
}
#include "Window.h"
#include "../util/Logger.h"

Window::~Window()
{
    destroy();
}

bool Window::create(int width, int height, const std::string& title)
{
    if (!glfwInit())
    {
        logger::error("glfwInit() failed.");

        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    mWindow = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);

    if (!mWindow)
    {
        logger::error("glfwCreateWindow() failed.");
        glfwTerminate();

        return false;
    }

    glfwSetWindowUserPointer(mWindow, this);
    glfwSetFramebufferSizeCallback(mWindow, framebufferResizeCallback);

    return true;
}

void Window::destroy()
{
    if (mWindow)
    {
        glfwDestroyWindow(mWindow);
        mWindow = nullptr;
    }

    glfwTerminate();
}

GLFWwindow* Window::handle() const
{
    return mWindow;
}

bool Window::shouldClose() const
{
    return glfwWindowShouldClose(mWindow) == GLFW_TRUE;
}

void Window::poll()
{
    glfwPollEvents();
}

void Window::waitEvents()
{
    glfwWaitEvents();
}

void Window::getFramebufferSize(int& width, int& height) const
{
    glfwGetFramebufferSize(mWindow, &width, &height);
}

bool Window::framebufferResized() const
{
    return mFramebufferResized;
}

void Window::clearFramebufferResized()
{
    mFramebufferResized = false;
}

int Window::keyState(int key) const
{
    return glfwGetKey(mWindow, key);
}

int Window::mouseButtonState(int button) const
{
    return glfwGetMouseButton(mWindow, button);
}

void Window::getCursorPos(double& x, double& y) const
{
    glfwGetCursorPos(mWindow, &x, &y);
}

void Window::setCursorMode(int mode)
{
    glfwSetInputMode(mWindow, GLFW_CURSOR, mode);
}

void Window::framebufferResizeCallback(GLFWwindow* window, int, int)
{
    auto* self = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));

    if (self)
    {
        self->mFramebufferResized = true;
    }
}
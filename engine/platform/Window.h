#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <string>

class Window 
{
public:
    Window() = default;
    ~Window();

    bool create(int width, int height, const std::string& title);
    void destroy();

    GLFWwindow* handle() const { return m_window; }
    bool shouldClose() const;
    void poll();
    void waitEvents();
    void getFramebufferSize(int& w, int& h) const;
    bool framebufferResized() const { return m_framebufferResized; }
    void clearFramebufferResized() { m_framebufferResized = false; }

private:
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    GLFWwindow* m_window = nullptr;
    bool m_framebufferResized = false;
};
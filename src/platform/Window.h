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

    GLFWwindow* handle() const;
    bool shouldClose() const;
    void poll();
    void waitEvents();
    void getFramebufferSize(int& width, int& height) const;
    bool framebufferResized() const;
    void clearFramebufferResized();
    int keyState(int key) const;
    int mouseButtonState(int button) const;
    void getCursorPos(double& x, double& y) const;
    void setCursorMode(int mode);

private:
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    GLFWwindow* mWindow = nullptr;
    bool mFramebufferResized = false;
};
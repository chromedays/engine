#include "engine/Window.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <stdexcept>

namespace engine {

Window::Window(const char* title, uint32_t width, uint32_t height)
{
    glfwSetErrorCallback([](int code, const char* desc) {
        std::fprintf(stderr, "[glfw] error %d: %s\n", code, desc);
    });
    if (!glfwInit())
        throw std::runtime_error("glfwInit failed");

    // WebGPU manages the swapchain; GLFW must not create a GL context.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_window = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title, nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        throw std::runtime_error("glfwCreateWindow failed");
    }
}

Window::~Window()
{
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool Window::shouldClose() const { return glfwWindowShouldClose(m_window); }

void Window::pollEvents() { glfwPollEvents(); }

void Window::framebufferSize(uint32_t& width, uint32_t& height) const
{
    int w = 0, h = 0;
    glfwGetFramebufferSize(m_window, &w, &h);
    width = static_cast<uint32_t>(w);
    height = static_cast<uint32_t>(h);
}

} // namespace engine

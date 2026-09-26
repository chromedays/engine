#pragma once

#include <cstdint>

struct GLFWwindow;

namespace engine {

class Window {
public:
    Window(const char* title, uint32_t width, uint32_t height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose() const;
    void pollEvents();

    // Framebuffer size in pixels (may differ from window size on HiDPI displays).
    void framebufferSize(uint32_t& width, uint32_t& height) const;

    GLFWwindow* handle() const { return m_window; }

private:
    GLFWwindow* m_window = nullptr;
};

} // namespace engine

#include "GlfwSurface.h"

#include <GLFW/glfw3.h>

#if defined(_WIN32)
#    define GLFW_EXPOSE_NATIVE_WIN32
#    include <windows.h>
#elif defined(__linux__)
#    define GLFW_EXPOSE_NATIVE_X11
#    define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#if !defined(__APPLE__)
#    include <GLFW/glfw3native.h>
#endif

#include <stdexcept>

namespace engine {

WGPUSurface createGlfwSurface(WGPUInstance instance, GLFWwindow* window)
{
    WGPUSurfaceDescriptor desc = WGPU_SURFACE_DESCRIPTOR_INIT;

#if defined(_WIN32)
    WGPUSurfaceSourceWindowsHWND src = WGPU_SURFACE_SOURCE_WINDOWS_HWND_INIT;
    src.hinstance = GetModuleHandle(nullptr);
    src.hwnd = glfwGetWin32Window(window);
    desc.nextInChain = &src.chain;
    return wgpuInstanceCreateSurface(instance, &desc);

#elif defined(__APPLE__)
    WGPUSurfaceSourceMetalLayer src = WGPU_SURFACE_SOURCE_METAL_LAYER_INIT;
    src.layer = getOrCreateMetalLayer(window);
    desc.nextInChain = &src.chain;
    return wgpuInstanceCreateSurface(instance, &desc);

#elif defined(__linux__)
    switch (glfwGetPlatform()) {
    case GLFW_PLATFORM_X11: {
        WGPUSurfaceSourceXlibWindow src = WGPU_SURFACE_SOURCE_XLIB_WINDOW_INIT;
        src.display = glfwGetX11Display();
        src.window = glfwGetX11Window(window);
        desc.nextInChain = &src.chain;
        return wgpuInstanceCreateSurface(instance, &desc);
    }
    case GLFW_PLATFORM_WAYLAND: {
        WGPUSurfaceSourceWaylandSurface src = WGPU_SURFACE_SOURCE_WAYLAND_SURFACE_INIT;
        src.display = glfwGetWaylandDisplay();
        src.surface = glfwGetWaylandWindow(window);
        desc.nextInChain = &src.chain;
        return wgpuInstanceCreateSurface(instance, &desc);
    }
    default:
        throw std::runtime_error("Unsupported GLFW platform");
    }
#else
#    error "Unsupported platform"
#endif
}

} // namespace engine

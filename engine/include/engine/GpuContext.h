#pragma once

#include <webgpu/webgpu.h>

namespace engine {

class Window;

// Owns the WebGPU instance, adapter, device, queue and the window surface.
class GpuContext {
public:
    explicit GpuContext(Window& window);
    ~GpuContext();

    GpuContext(const GpuContext&) = delete;
    GpuContext& operator=(const GpuContext&) = delete;

    // Reconfigures the surface when the framebuffer size changed.
    void resize(uint32_t width, uint32_t height);

    // Acquires the next swapchain texture view. Returns nullptr if the frame should be skipped.
    WGPUTextureView beginFrame();
    void endFrame();

    WGPUDevice device() const { return m_device; }
    WGPUQueue queue() const { return m_queue; }
    WGPUTextureFormat surfaceFormat() const { return m_surfaceFormat; }

private:
    void configureSurface();

    Window& m_window;
    WGPUInstance m_instance = nullptr;
    WGPUSurface m_surface = nullptr;
    WGPUAdapter m_adapter = nullptr;
    WGPUDevice m_device = nullptr;
    WGPUQueue m_queue = nullptr;
    WGPUTextureFormat m_surfaceFormat = WGPUTextureFormat_Undefined;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    WGPUTexture m_currentTexture = nullptr;
    WGPUTextureView m_currentView = nullptr;
};

} // namespace engine

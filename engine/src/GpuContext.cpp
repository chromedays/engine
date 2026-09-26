#include "engine/GpuContext.h"
#include "engine/Window.h"

#include "GlfwSurface.h"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace engine {

namespace {

std::string toString(WGPUStringView sv)
{
    if (!sv.data)
        return {};
    return sv.length == WGPU_STRLEN ? std::string(sv.data) : std::string(sv.data, sv.length);
}

template <typename T>
struct RequestResult {
    T handle = nullptr;
    bool done = false;
    std::string message;
};

WGPUAdapter requestAdapter(WGPUInstance instance, WGPUSurface surface)
{
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.compatibleSurface = surface;
    options.powerPreference = WGPUPowerPreference_HighPerformance;

    RequestResult<WGPUAdapter> result;
    WGPURequestAdapterCallbackInfo cb = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    cb.mode = WGPUCallbackMode_AllowProcessEvents;
    cb.userdata1 = &result;
    cb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* ud1, void*) {
        auto& r = *static_cast<RequestResult<WGPUAdapter>*>(ud1);
        if (status == WGPURequestAdapterStatus_Success)
            r.handle = adapter;
        else
            r.message = toString(message);
        r.done = true;
    };

    wgpuInstanceRequestAdapter(instance, &options, cb);
    while (!result.done)
        wgpuInstanceProcessEvents(instance);

    if (!result.handle)
        throw std::runtime_error("Failed to get WebGPU adapter: " + result.message);
    return result.handle;
}

WGPUDevice requestDevice(WGPUInstance instance, WGPUAdapter adapter)
{
    WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    desc.label = {"engine device", WGPU_STRLEN};
    desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    desc.deviceLostCallbackInfo.callback = [](WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView message,
                                              void*, void*) {
        if (reason != WGPUDeviceLostReason_Destroyed && reason != WGPUDeviceLostReason_CallbackCancelled)
            std::fprintf(stderr, "[wgpu] device lost (%d): %s\n", reason, toString(message).c_str());
    };
    desc.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType type, WGPUStringView message,
                                                   void*, void*) {
        std::fprintf(stderr, "[wgpu] error (%d): %s\n", type, toString(message).c_str());
    };

    RequestResult<WGPUDevice> result;
    WGPURequestDeviceCallbackInfo cb = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    cb.mode = WGPUCallbackMode_AllowProcessEvents;
    cb.userdata1 = &result;
    cb.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* ud1, void*) {
        auto& r = *static_cast<RequestResult<WGPUDevice>*>(ud1);
        if (status == WGPURequestDeviceStatus_Success)
            r.handle = device;
        else
            r.message = toString(message);
        r.done = true;
    };

    wgpuAdapterRequestDevice(adapter, &desc, cb);
    while (!result.done)
        wgpuInstanceProcessEvents(instance);

    if (!result.handle)
        throw std::runtime_error("Failed to get WebGPU device: " + result.message);
    return result.handle;
}

} // namespace

GpuContext::GpuContext(Window& window)
    : m_window(window)
{
    m_instance = wgpuCreateInstance(nullptr);
    if (!m_instance)
        throw std::runtime_error("wgpuCreateInstance failed");

    m_surface = createGlfwSurface(m_instance, window.handle());
    if (!m_surface)
        throw std::runtime_error("Failed to create WebGPU surface");

    m_adapter = requestAdapter(m_instance, m_surface);

    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(m_adapter, &info) == WGPUStatus_Success) {
        std::printf("[engine] adapter: %s (%s)\n", toString(info.device).c_str(), toString(info.description).c_str());
        wgpuAdapterInfoFreeMembers(info);
    }

    m_device = requestDevice(m_instance, m_adapter);
    m_queue = wgpuDeviceGetQueue(m_device);

    WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
    wgpuSurfaceGetCapabilities(m_surface, m_adapter, &caps);
    if (caps.formatCount == 0)
        throw std::runtime_error("Surface reports no supported formats");
    m_surfaceFormat = caps.formats[0];
    // Prefer an sRGB format so shader output is gamma-corrected on present.
    for (size_t i = 0; i < caps.formatCount; ++i) {
        if (caps.formats[i] == WGPUTextureFormat_BGRA8UnormSrgb || caps.formats[i] == WGPUTextureFormat_RGBA8UnormSrgb) {
            m_surfaceFormat = caps.formats[i];
            break;
        }
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    window.framebufferSize(m_width, m_height);
    configureSurface();
}

GpuContext::~GpuContext()
{
    if (m_currentView)
        wgpuTextureViewRelease(m_currentView);
    if (m_currentTexture)
        wgpuTextureRelease(m_currentTexture);
    if (m_surface)
        wgpuSurfaceUnconfigure(m_surface);
    if (m_queue)
        wgpuQueueRelease(m_queue);
    if (m_device)
        wgpuDeviceRelease(m_device);
    if (m_adapter)
        wgpuAdapterRelease(m_adapter);
    if (m_surface)
        wgpuSurfaceRelease(m_surface);
    if (m_instance)
        wgpuInstanceRelease(m_instance);
}

void GpuContext::configureSurface()
{
    if (m_width == 0 || m_height == 0)
        return;

    WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
    config.device = m_device;
    config.format = m_surfaceFormat;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = m_width;
    config.height = m_height;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(m_surface, &config);
}

void GpuContext::resize(uint32_t width, uint32_t height)
{
    if (width == m_width && height == m_height)
        return;
    m_width = width;
    m_height = height;
    configureSurface();
}

WGPUTextureView GpuContext::beginFrame()
{
    uint32_t w = 0, h = 0;
    m_window.framebufferSize(w, h);
    resize(w, h);
    if (m_width == 0 || m_height == 0)
        return nullptr; // minimized

    WGPUSurfaceTexture surfaceTexture = WGPU_SURFACE_TEXTURE_INIT;
    wgpuSurfaceGetCurrentTexture(m_surface, &surfaceTexture);

    switch (surfaceTexture.status) {
    case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
    case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
        break;
    case WGPUSurfaceGetCurrentTextureStatus_Timeout:
    case WGPUSurfaceGetCurrentTextureStatus_Outdated:
    case WGPUSurfaceGetCurrentTextureStatus_Lost:
        if (surfaceTexture.texture)
            wgpuTextureRelease(surfaceTexture.texture);
        configureSurface();
        return nullptr;
    default:
        throw std::runtime_error("wgpuSurfaceGetCurrentTexture failed");
    }

    m_currentTexture = surfaceTexture.texture;
    m_currentView = wgpuTextureCreateView(m_currentTexture, nullptr);
    return m_currentView;
}

void GpuContext::endFrame()
{
    wgpuSurfacePresent(m_surface);
    wgpuTextureViewRelease(m_currentView);
    wgpuTextureRelease(m_currentTexture);
    m_currentView = nullptr;
    m_currentTexture = nullptr;
    wgpuInstanceProcessEvents(m_instance);
}

} // namespace engine

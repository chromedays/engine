#include "engine/gpu.h"
#include "engine/window.h"

#include "glfw_surface.h"

#include <stdio.h>
#include <string.h>

/* Prints a WGPUStringView, which is not necessarily NUL-terminated. */
#define SV_FMT "%.*s"
#define SV_ARG(sv) (int)((sv).data ? ((sv).length == WGPU_STRLEN ? strlen((sv).data) : (sv).length) : 0), (sv).data

typedef struct AdapterRequest {
    WGPUAdapter adapter;
    bool done;
} AdapterRequest;

typedef struct DeviceRequest {
    WGPUDevice device;
    bool done;
} DeviceRequest;

static void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message,
                       void* userdata1, void* userdata2)
{
    (void)userdata2;
    AdapterRequest* req = userdata1;
    if (status == WGPURequestAdapterStatus_Success)
        req->adapter = adapter;
    else
        fprintf(stderr, "[wgpu] request adapter failed: " SV_FMT "\n", SV_ARG(message));
    req->done = true;
}

static void on_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
                      void* userdata1, void* userdata2)
{
    (void)userdata2;
    DeviceRequest* req = userdata1;
    if (status == WGPURequestDeviceStatus_Success)
        req->device = device;
    else
        fprintf(stderr, "[wgpu] request device failed: " SV_FMT "\n", SV_ARG(message));
    req->done = true;
}

static void on_device_lost(WGPUDevice const* device, WGPUDeviceLostReason reason, WGPUStringView message,
                           void* userdata1, void* userdata2)
{
    (void)device, (void)userdata1, (void)userdata2;
    if (reason != WGPUDeviceLostReason_Destroyed && reason != WGPUDeviceLostReason_CallbackCancelled)
        fprintf(stderr, "[wgpu] device lost (%d): " SV_FMT "\n", (int)reason, SV_ARG(message));
}

static void on_uncaptured_error(WGPUDevice const* device, WGPUErrorType type, WGPUStringView message,
                                void* userdata1, void* userdata2)
{
    (void)device, (void)userdata1, (void)userdata2;
    fprintf(stderr, "[wgpu] error (%d): " SV_FMT "\n", (int)type, SV_ARG(message));
}

static WGPUAdapter request_adapter(WGPUInstance instance, WGPUSurface surface)
{
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.compatibleSurface = surface;
    options.powerPreference = WGPUPowerPreference_HighPerformance;

    AdapterRequest req = {0};
    WGPURequestAdapterCallbackInfo cb = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    cb.mode = WGPUCallbackMode_AllowProcessEvents;
    cb.callback = on_adapter;
    cb.userdata1 = &req;

    wgpuInstanceRequestAdapter(instance, &options, cb);
    while (!req.done)
        wgpuInstanceProcessEvents(instance);
    return req.adapter;
}

static WGPUDevice request_device(WGPUInstance instance, WGPUAdapter adapter)
{
    WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    desc.label = (WGPUStringView){"engine device", WGPU_STRLEN};
    desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    desc.deviceLostCallbackInfo.callback = on_device_lost;
    desc.uncapturedErrorCallbackInfo.callback = on_uncaptured_error;

    DeviceRequest req = {0};
    WGPURequestDeviceCallbackInfo cb = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    cb.mode = WGPUCallbackMode_AllowProcessEvents;
    cb.callback = on_device;
    cb.userdata1 = &req;

    wgpuAdapterRequestDevice(adapter, &desc, cb);
    while (!req.done)
        wgpuInstanceProcessEvents(instance);
    return req.device;
}

static void configure_surface(EngGpu* gpu)
{
    if (gpu->width == 0 || gpu->height == 0)
        return;

    WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
    config.device = gpu->device;
    config.format = gpu->surface_format;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = gpu->width;
    config.height = gpu->height;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(gpu->surface, &config);
}

bool eng_gpu_create(EngGpu* gpu, EngWindow* window)
{
    memset(gpu, 0, sizeof(*gpu));
    gpu->window = window;

    gpu->instance = wgpuCreateInstance(NULL);
    if (!gpu->instance) {
        fprintf(stderr, "[engine] wgpuCreateInstance failed\n");
        goto fail;
    }

    gpu->surface = eng_create_glfw_surface(gpu->instance, window->handle);
    if (!gpu->surface) {
        fprintf(stderr, "[engine] failed to create surface\n");
        goto fail;
    }

    gpu->adapter = request_adapter(gpu->instance, gpu->surface);
    if (!gpu->adapter)
        goto fail;

    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(gpu->adapter, &info) == WGPUStatus_Success) {
        printf("[engine] adapter: " SV_FMT " (" SV_FMT ")\n", SV_ARG(info.device), SV_ARG(info.description));
        wgpuAdapterInfoFreeMembers(info);
    }

    gpu->device = request_device(gpu->instance, gpu->adapter);
    if (!gpu->device)
        goto fail;
    gpu->queue = wgpuDeviceGetQueue(gpu->device);

    WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
    wgpuSurfaceGetCapabilities(gpu->surface, gpu->adapter, &caps);
    if (caps.formatCount == 0) {
        fprintf(stderr, "[engine] surface reports no supported formats\n");
        wgpuSurfaceCapabilitiesFreeMembers(caps);
        goto fail;
    }
    gpu->surface_format = caps.formats[0];
    /* Prefer an sRGB format so shader output is gamma-corrected on present. */
    for (size_t i = 0; i < caps.formatCount; ++i) {
        if (caps.formats[i] == WGPUTextureFormat_BGRA8UnormSrgb || caps.formats[i] == WGPUTextureFormat_RGBA8UnormSrgb) {
            gpu->surface_format = caps.formats[i];
            break;
        }
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    eng_window_framebuffer_size(window, &gpu->width, &gpu->height);
    configure_surface(gpu);
    return true;

fail:
    eng_gpu_destroy(gpu);
    return false;
}

void eng_gpu_destroy(EngGpu* gpu)
{
    if (gpu->current_view)
        wgpuTextureViewRelease(gpu->current_view);
    if (gpu->current_texture)
        wgpuTextureRelease(gpu->current_texture);
    if (gpu->surface && gpu->device)
        wgpuSurfaceUnconfigure(gpu->surface);
    if (gpu->queue)
        wgpuQueueRelease(gpu->queue);
    if (gpu->device)
        wgpuDeviceRelease(gpu->device);
    if (gpu->adapter)
        wgpuAdapterRelease(gpu->adapter);
    if (gpu->surface)
        wgpuSurfaceRelease(gpu->surface);
    if (gpu->instance)
        wgpuInstanceRelease(gpu->instance);
    memset(gpu, 0, sizeof(*gpu));
}

void eng_gpu_resize(EngGpu* gpu, uint32_t width, uint32_t height)
{
    if (width == gpu->width && height == gpu->height)
        return;
    gpu->width = width;
    gpu->height = height;
    configure_surface(gpu);
}

WGPUTextureView eng_gpu_begin_frame(EngGpu* gpu)
{
    uint32_t w = 0, h = 0;
    eng_window_framebuffer_size(gpu->window, &w, &h);
    eng_gpu_resize(gpu, w, h);
    if (gpu->width == 0 || gpu->height == 0)
        return NULL; /* minimized */

    WGPUSurfaceTexture surface_texture = WGPU_SURFACE_TEXTURE_INIT;
    wgpuSurfaceGetCurrentTexture(gpu->surface, &surface_texture);

    switch (surface_texture.status) {
    case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
    case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
        break;
    case WGPUSurfaceGetCurrentTextureStatus_Timeout:
    case WGPUSurfaceGetCurrentTextureStatus_Outdated:
    case WGPUSurfaceGetCurrentTextureStatus_Lost:
        if (surface_texture.texture)
            wgpuTextureRelease(surface_texture.texture);
        configure_surface(gpu);
        return NULL;
    default:
        fprintf(stderr, "[engine] wgpuSurfaceGetCurrentTexture failed (%d)\n", (int)surface_texture.status);
        return NULL;
    }

    gpu->current_texture = surface_texture.texture;
    gpu->current_view = wgpuTextureCreateView(gpu->current_texture, NULL);
    return gpu->current_view;
}

void eng_gpu_end_frame(EngGpu* gpu)
{
    wgpuSurfacePresent(gpu->surface);
    wgpuTextureViewRelease(gpu->current_view);
    wgpuTextureRelease(gpu->current_texture);
    gpu->current_view = NULL;
    gpu->current_texture = NULL;
    wgpuInstanceProcessEvents(gpu->instance);
}

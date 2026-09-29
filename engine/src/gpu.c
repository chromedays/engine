#include "nv/gpu.h"
#include "nv/log.h"
#include "nv/window.h"

#include <stdio.h>
#include <string.h>

// Prints a WGPUStringView, which is not necessarily NUL-terminated.
#define SV_FMT "%.*s"
#define SV_ARG(sv) (int)((sv).data ? ((sv).length == WGPU_STRLEN ? strlen((sv).data) : (sv).length) : 0), (sv).data

typedef struct AdapterRequest {
    WGPUAdapter adapter;
} AdapterRequest;

typedef struct DeviceRequest {
    WGPUDevice device;
} DeviceRequest;

internal void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message,
                         void* userdata1, void* userdata2)
{
    (void)userdata2;
    AdapterRequest* request = userdata1;
    if (status == WGPURequestAdapterStatus_Success)
        request->adapter = adapter;
    else
        nv_log(NV_LOG_ERROR, "wgpu", "request adapter failed: " SV_FMT, SV_ARG(message));
}

internal void on_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
                        void* userdata1, void* userdata2)
{
    (void)userdata2;
    DeviceRequest* request = userdata1;
    if (status == WGPURequestDeviceStatus_Success)
        request->device = device;
    else
        nv_log(NV_LOG_ERROR, "wgpu", "request device failed: " SV_FMT, SV_ARG(message));
}

internal void on_device_lost(WGPUDevice const* device, WGPUDeviceLostReason reason, WGPUStringView message,
                             void* userdata1, void* userdata2)
{
    (void)device, (void)userdata1, (void)userdata2;
    if (reason != WGPUDeviceLostReason_Destroyed && reason != WGPUDeviceLostReason_CallbackCancelled)
        nv_log(NV_LOG_ERROR, "wgpu", "device lost (%d): " SV_FMT, (int)reason, SV_ARG(message));
}

internal void on_uncaptured_error(WGPUDevice const* device, WGPUErrorType type, WGPUStringView message,
                                  void* userdata1, void* userdata2)
{
    (void)device, (void)userdata1, (void)userdata2;
    nv_log(NV_LOG_ERROR, "wgpu", "error (%d): " SV_FMT, (int)type, SV_ARG(message));
}

// NOTE: The browser resolves requests on its own event loop. wgpuInstanceWaitAny yields to it
// through Asyncify, so setup reads as straight-line code.
internal void wait_for(WGPUInstance instance, WGPUFuture future)
{
    WGPUFutureWaitInfo wait = WGPU_FUTURE_WAIT_INFO_INIT;
    wait.future = future;
    wgpuInstanceWaitAny(instance, 1, &wait, UINT64_MAX);
}

internal WGPUAdapter request_adapter(WGPUInstance instance, WGPUSurface surface)
{
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.compatibleSurface = surface;
    options.powerPreference = WGPUPowerPreference_HighPerformance;

    AdapterRequest request = {0};
    WGPURequestAdapterCallbackInfo callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_WaitAnyOnly;
    callback.callback = on_adapter;
    callback.userdata1 = &request;

    wait_for(instance, wgpuInstanceRequestAdapter(instance, &options, callback));
    return request.adapter;
}

internal WGPUDevice request_device(WGPUInstance instance, WGPUAdapter adapter, b32* has_timestamps)
{
    WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    desc.label = (WGPUStringView){"nv device", WGPU_STRLEN};
    // Pass timing is optional: many browsers, phones especially, do not offer it.
    local_persist const WGPUFeatureName timestamps = WGPUFeatureName_TimestampQuery;
    *has_timestamps = wgpuAdapterHasFeature(adapter, timestamps);
    if (*has_timestamps) {
        desc.requiredFeatureCount = 1;
        desc.requiredFeatures = &timestamps;
    }
    desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    desc.deviceLostCallbackInfo.callback = on_device_lost;
    desc.uncapturedErrorCallbackInfo.callback = on_uncaptured_error;

    DeviceRequest request = {0};
    WGPURequestDeviceCallbackInfo callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_WaitAnyOnly;
    callback.callback = on_device;
    callback.userdata1 = &request;

    wait_for(instance, wgpuAdapterRequestDevice(adapter, &desc, callback));
    return request.device;
}

internal WGPUTextureFormat srgb_view_format(WGPUTextureFormat format)
{
    switch (format) {
    case WGPUTextureFormat_BGRA8Unorm: return WGPUTextureFormat_BGRA8UnormSrgb;
    case WGPUTextureFormat_RGBA8Unorm: return WGPUTextureFormat_RGBA8UnormSrgb;
    default: return format;
    }
}

internal void configure_surface(NvGpu* gpu)
{
    if (gpu->width == 0 || gpu->height == 0)
        return;

    WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
    config.device = gpu->device;
    config.format = gpu->config_format;
    if (gpu->surface_format != gpu->config_format) {
        config.viewFormatCount = 1;
        config.viewFormats = &gpu->surface_format;
    }
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = gpu->width;
    config.height = gpu->height;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(gpu->surface, &config);
}

internal void release_gpu(NvGpu* gpu)
{
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
    *gpu = (NvGpu){0};
}

b32 nv_gpu_create(NvGpu* gpu, NvWindow* window)
{
    *gpu = (NvGpu){0};
    gpu->window = window;

    local_persist const WGPUInstanceFeatureName instance_features[] = {WGPUInstanceFeatureName_TimedWaitAny};
    WGPUInstanceDescriptor instance_desc = WGPU_INSTANCE_DESCRIPTOR_INIT;
    instance_desc.requiredFeatureCount = NV_ARRAY_COUNT(instance_features);
    instance_desc.requiredFeatures = instance_features;
    gpu->instance = wgpuCreateInstance(&instance_desc);
    if (!gpu->instance) {
        nv_log(NV_LOG_ERROR, "nv", "wgpuCreateInstance failed");
        goto fail;
    }

    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas = WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    canvas.selector = (WGPUStringView){window->canvas_selector, WGPU_STRLEN};
    WGPUSurfaceDescriptor surface_desc = WGPU_SURFACE_DESCRIPTOR_INIT;
    surface_desc.nextInChain = &canvas.chain;
    gpu->surface = wgpuInstanceCreateSurface(gpu->instance, &surface_desc);
    if (!gpu->surface) {
        nv_log(NV_LOG_ERROR, "nv", "failed to create canvas surface");
        goto fail;
    }

    gpu->adapter = request_adapter(gpu->instance, gpu->surface);
    if (!gpu->adapter)
        goto fail;

    gpu->device = request_device(gpu->instance, gpu->adapter, &gpu->has_timestamps);
    if (!gpu->device)
        goto fail;
    gpu->queue = wgpuDeviceGetQueue(gpu->device);

    WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
    wgpuSurfaceGetCapabilities(gpu->surface, gpu->adapter, &caps);
    if (caps.formatCount == 0) {
        nv_log(NV_LOG_ERROR, "nv", "canvas reports no supported formats");
        wgpuSurfaceCapabilitiesFreeMembers(caps);
        goto fail;
    }
    // NOTE: Canvases only offer linear formats, but accept the sRGB twin as a view format.
    // Rendering through that view gamma-corrects shader output.
    gpu->config_format = caps.formats[0];
    gpu->surface_format = srgb_view_format(gpu->config_format);
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    nv_window_framebuffer_size(window, &gpu->width, &gpu->height);
    configure_surface(gpu);
    return 1;

fail:
    release_gpu(gpu);
    return 0;
}

WGPUTextureView nv_gpu_begin_frame(NvGpu* gpu)
{
    NV_ASSERT(!gpu->current_view);

    u32 width = 0;
    u32 height = 0;
    nv_window_framebuffer_size(gpu->window, &width, &height);
    if (width != gpu->width || height != gpu->height) {
        gpu->width = width;
        gpu->height = height;
        configure_surface(gpu);
    }
    if (gpu->width == 0 || gpu->height == 0)
        return NULL; // canvas is hidden or collapsed

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
        nv_log(NV_LOG_ERROR, "nv", "wgpuSurfaceGetCurrentTexture failed (%d)", (int)surface_texture.status);
        return NULL;
    }

    gpu->current_texture = surface_texture.texture;
    WGPUTextureViewDescriptor view_desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    view_desc.format = gpu->surface_format;
    gpu->current_view = wgpuTextureCreateView(gpu->current_texture, &view_desc);
    return gpu->current_view;
}

// NOTE: No wgpuSurfacePresent here: the browser presents the canvas when the animation frame
// callback returns (and emdawnwebgpu aborts if it is called).
void nv_gpu_end_frame(NvGpu* gpu)
{
    NV_ASSERT(gpu->current_view);
    wgpuTextureViewRelease(gpu->current_view);
    wgpuTextureRelease(gpu->current_texture);
    gpu->current_view = NULL;
    gpu->current_texture = NULL;
}

#include "engine/gpu.h"
#include "engine/window.h"

#include "surface.h"

#include <stdio.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
/* The browser resolves requests on its event loop; wgpuInstanceWaitAny yields to it via Asyncify. */
#    define ENG_CALLBACK_MODE WGPUCallbackMode_WaitAnyOnly
#    define ENG_EVENT_CALLBACK_MODE WGPUCallbackMode_AllowSpontaneous
#else
#    define ENG_CALLBACK_MODE WGPUCallbackMode_AllowProcessEvents
#    define ENG_EVENT_CALLBACK_MODE WGPUCallbackMode_AllowProcessEvents
#endif

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

/* Blocks until the callback behind `future` has set `*done`. */
static void wait_for(WGPUInstance instance, WGPUFuture future, const bool* done)
{
#if defined(__EMSCRIPTEN__)
    (void)done;
    WGPUFutureWaitInfo wait = WGPU_FUTURE_WAIT_INFO_INIT;
    wait.future = future;
    wgpuInstanceWaitAny(instance, 1, &wait, UINT64_MAX);
#else
    (void)future;
    while (!*done)
        wgpuInstanceProcessEvents(instance);
#endif
}

static WGPUAdapter request_adapter(WGPUInstance instance, WGPUSurface surface)
{
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.compatibleSurface = surface;
    options.powerPreference = WGPUPowerPreference_HighPerformance;

    AdapterRequest req = {0};
    WGPURequestAdapterCallbackInfo cb = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    cb.mode = ENG_CALLBACK_MODE;
    cb.callback = on_adapter;
    cb.userdata1 = &req;

    wait_for(instance, wgpuInstanceRequestAdapter(instance, &options, cb), &req.done);
    return req.adapter;
}

static WGPUDevice request_device(WGPUInstance instance, WGPUAdapter adapter)
{
    WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    desc.label = (WGPUStringView){"engine device", WGPU_STRLEN};
    desc.deviceLostCallbackInfo.mode = ENG_EVENT_CALLBACK_MODE;
    desc.deviceLostCallbackInfo.callback = on_device_lost;
    desc.uncapturedErrorCallbackInfo.callback = on_uncaptured_error;

    DeviceRequest req = {0};
    WGPURequestDeviceCallbackInfo cb = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    cb.mode = ENG_CALLBACK_MODE;
    cb.callback = on_device;
    cb.userdata1 = &req;

    wait_for(instance, wgpuAdapterRequestDevice(adapter, &desc, cb), &req.done);
    return req.device;
}

static WGPUTextureFormat srgb_view_format(WGPUTextureFormat format)
{
    switch (format) {
    case WGPUTextureFormat_BGRA8Unorm: return WGPUTextureFormat_BGRA8UnormSrgb;
    case WGPUTextureFormat_RGBA8Unorm: return WGPUTextureFormat_RGBA8UnormSrgb;
    default: return format;
    }
}

static void configure_surface(EngGpu* gpu)
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

bool eng_gpu_create(EngGpu* gpu, EngWindow* window)
{
    memset(gpu, 0, sizeof(*gpu));
    gpu->window = window;

    WGPUInstanceDescriptor instance_desc = WGPU_INSTANCE_DESCRIPTOR_INIT;
#if defined(__EMSCRIPTEN__)
    static const WGPUInstanceFeatureName k_instance_features[] = {WGPUInstanceFeatureName_TimedWaitAny};
    instance_desc.requiredFeatureCount = 1;
    instance_desc.requiredFeatures = k_instance_features;
#endif
    gpu->instance = wgpuCreateInstance(&instance_desc);
    if (!gpu->instance) {
        fprintf(stderr, "[engine] wgpuCreateInstance failed\n");
        goto fail;
    }

    gpu->surface = eng_create_surface(gpu->instance, window);
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
    /*
     * Render through an sRGB view so shader output is gamma-corrected on present. Native surfaces
     * usually offer an sRGB format directly; browser canvases only offer the linear format but
     * accept its sRGB twin as a view format.
     */
    gpu->config_format = caps.formats[0];
    for (size_t i = 0; i < caps.formatCount; ++i) {
        if (caps.formats[i] == WGPUTextureFormat_BGRA8UnormSrgb || caps.formats[i] == WGPUTextureFormat_RGBA8UnormSrgb) {
            gpu->config_format = caps.formats[i];
            break;
        }
    }
    gpu->surface_format = srgb_view_format(gpu->config_format);
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
    WGPUTextureViewDescriptor view_desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    view_desc.format = gpu->surface_format;
    gpu->current_view = wgpuTextureCreateView(gpu->current_texture, &view_desc);
    return gpu->current_view;
}

void eng_gpu_end_frame(EngGpu* gpu)
{
#if !defined(__EMSCRIPTEN__)
    /* Browsers present the canvas automatically when the animation frame callback returns. */
    wgpuSurfacePresent(gpu->surface);
#endif
    wgpuTextureViewRelease(gpu->current_view);
    wgpuTextureRelease(gpu->current_texture);
    gpu->current_view = NULL;
    gpu->current_texture = NULL;
    wgpuInstanceProcessEvents(gpu->instance);
}

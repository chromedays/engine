#include "engine/gpu.h"
#include "engine/log.h"
#include "engine/window.h"

#include <emscripten/emscripten.h>
#include <webgpu/webgpu.h>

#include <stdio.h>
#include <string.h>

// The WebGPU backend of engine/gpu.h. The objects of the API are handles into pools of WebGPU objects, kept here.

// Prints a WGPUStringView, which is not necessarily NUL-terminated.
#define SV_FMT "%.*s"
#define SV_ARG(sv) (int)((sv).data ? ((sv).length == WGPU_STRLEN ? strlen((sv).data) : (sv).length) : 0), (sv).data

EM_JS_DEPS(nv_gpu, "$stringToUTF8");

// What the page can tell about the machine. The browser hides the CPU's model, so this is what a
// script sees: logical cores, memory rounded down to a power of two and capped at 8 GB by the
// browser (0 where navigator.deviceMemory does not exist), the screen, and the platform.
EM_JS(void, js_system_info, (int* cores, f32* memory_gb, int* screen_width, int* screen_height, f32* pixel_ratio,
                             char* platform, int platform_size), {
    HEAP32[cores >> 2] = navigator.hardwareConcurrency || 0;
    HEAPF32[memory_gb >> 2] = navigator.deviceMemory || 0;
    HEAP32[screen_width >> 2] = screen.width;
    HEAP32[screen_height >> 2] = screen.height;
    HEAPF32[pixel_ratio >> 2] = window.devicePixelRatio || 1;
    const data = navigator.userAgentData;
    stringToUTF8((data && data.platform) || navigator.platform || "", platform, platform_size);
});

//
// Formats
//

const char* nv_gpu_format_name(NvGpuFormat format)
{
    switch (format) {
    case NV_GPU_FORMAT_BGRA8:      return "BGRA8Unorm";
    case NV_GPU_FORMAT_BGRA8_SRGB: return "BGRA8UnormSrgb";
    case NV_GPU_FORMAT_RGBA8:      return "RGBA8Unorm";
    case NV_GPU_FORMAT_RGBA8_SRGB: return "RGBA8UnormSrgb";
    case NV_GPU_FORMAT_RGBA16F:    return "RGBA16Float";
    case NV_GPU_FORMAT_DEPTH16:    return "Depth16Unorm";
    case NV_GPU_FORMAT_DEPTH32F:   return "Depth32Float";
    default:                       return "other format";
    }
}

internal WGPUTextureFormat wgpu_format(NvGpuFormat format)
{
    switch (format) {
    case NV_GPU_FORMAT_BGRA8:      return WGPUTextureFormat_BGRA8Unorm;
    case NV_GPU_FORMAT_BGRA8_SRGB: return WGPUTextureFormat_BGRA8UnormSrgb;
    case NV_GPU_FORMAT_RGBA8:      return WGPUTextureFormat_RGBA8Unorm;
    case NV_GPU_FORMAT_RGBA8_SRGB: return WGPUTextureFormat_RGBA8UnormSrgb;
    case NV_GPU_FORMAT_RGBA16F:    return WGPUTextureFormat_RGBA16Float;
    case NV_GPU_FORMAT_DEPTH16:    return WGPUTextureFormat_Depth16Unorm;
    case NV_GPU_FORMAT_DEPTH32F:   return WGPUTextureFormat_Depth32Float;
    default:                       return WGPUTextureFormat_Undefined;
    }
}

internal NvGpuFormat format_from_wgpu(WGPUTextureFormat format)
{
    switch (format) {
    case WGPUTextureFormat_BGRA8Unorm:     return NV_GPU_FORMAT_BGRA8;
    case WGPUTextureFormat_BGRA8UnormSrgb: return NV_GPU_FORMAT_BGRA8_SRGB;
    case WGPUTextureFormat_RGBA8Unorm:     return NV_GPU_FORMAT_RGBA8;
    case WGPUTextureFormat_RGBA8UnormSrgb: return NV_GPU_FORMAT_RGBA8_SRGB;
    case WGPUTextureFormat_RGBA16Float:    return NV_GPU_FORMAT_RGBA16F;
    case WGPUTextureFormat_Depth16Unorm:   return NV_GPU_FORMAT_DEPTH16;
    case WGPUTextureFormat_Depth32Float:   return NV_GPU_FORMAT_DEPTH32F;
    default:                               return NV_GPU_FORMAT_NONE;
    }
}

//
// Handle pools
//
// A pool has a fixed number of slots. A slot's generation is odd while it holds an object and even while it is free; a handle
// carries the generation it was made with. Slot 0 is never used, so a zeroed handle is the null handle.
//

typedef struct Pool {
    u32 capacity;
    umm item_size;
    u8* items;
    u32* generations;
    u32* next_free;
    u32 free_head; // index of the first free slot, 0 for none
    u32 top;       // slots from `top` up have never been used
} Pool;

#define DEFINE_POOL(name, Item, count)                       \
    global Item name##_items[count];                         \
    global u32 name##_generations[count];                    \
    global u32 name##_next_free[count];                      \
    global Pool name##_pool = {.capacity = count, .item_size = sizeof(Item), .items = (u8*)name##_items, \
                               .generations = name##_generations, .next_free = name##_next_free, .top = 1}

typedef struct Buffer {
    WGPUBuffer buffer;
    u64 size;
} Buffer;

typedef struct Texture {
    WGPUTexture texture;
    NvGpuTextureDesc desc; // the label is dropped
} Texture;

typedef struct View {
    WGPUTextureView view;
} View;

typedef struct Sampler {
    WGPUSampler sampler;
} Sampler;

typedef struct Layout {
    WGPUBindGroupLayout layout;
} Layout;

typedef struct Group {
    WGPUBindGroup group;
} Group;

typedef struct Pipeline {
    b32 is_compute;
    WGPURenderPipeline render;
    WGPUComputePipeline compute;
} Pipeline;

DEFINE_POOL(buffer, Buffer, 2048);
DEFINE_POOL(texture, Texture, 512);
DEFINE_POOL(view, View, 1024);
DEFINE_POOL(sampler, Sampler, 32);
DEFINE_POOL(layout, Layout, 64);
DEFINE_POOL(group, Group, 1024);
DEFINE_POOL(pipeline, Pipeline, 128);

// Takes a slot; returns its index and, in `generation`, the one a handle to it carries.
internal u32 pool_take(Pool* pool, u32* generation)
{
    u32 index = pool->free_head;
    if (index) {
        pool->free_head = pool->next_free[index];
    } else {
        NV_ASSERT(pool->top < pool->capacity);
        index = pool->top++;
    }
    ++pool->generations[index];
    NV_ASSERT(pool->generations[index] & 1);
    *generation = pool->generations[index];
    memset(pool->items + (umm)index * pool->item_size, 0, pool->item_size);
    return index;
}

// The slot of a handle that is live: asserts on a null, released or reused handle.
internal void* pool_item(const Pool* pool, u32 index, u32 generation)
{
    NV_ASSERT(index > 0 && index < pool->top);
    NV_ASSERT(pool->generations[index] == generation && (generation & 1));
    return pool->items + (umm)index * pool->item_size;
}

internal void pool_give(Pool* pool, u32 index, u32 generation)
{
    pool_item(pool, index, generation);
    ++pool->generations[index];
    pool->next_free[index] = pool->free_head;
    pool->free_head = index;
}

//
// The device
//

typedef enum ReadbackState {
    READBACK_IDLE,    // no request, or the last was taken (the bytes of a taken one stay mapped until the next request)
    READBACK_COPIED,  // the copy is in the frame's commands
    READBACK_MAPPING, // the copy was submitted and the buffer is being mapped
    READBACK_READY,   // mapped, not yet taken
    READBACK_TAKEN,   // taken; still mapped
} ReadbackState;

typedef struct Readback {
    ReadbackState state;
    WGPUBuffer buffer;
    u64 capacity;
    umm size;
    const void* mapped;
} Readback;

typedef struct Backend {
    NvGpu* gpu;
    WGPUInstance instance;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;

    WGPUTexture current_texture;
    WGPUTextureView current_view;
    NvGpuView current_view_handle;

    WGPUCommandEncoder encoder; // the frame's; NULL outside a frame
    WGPURenderPassEncoder render_pass;
    WGPUComputePassEncoder compute_pass;

    // Pass times: the query set, the buffer they resolve into, and the one they are read from.
    WGPUQuerySet timestamp_queries;
    WGPUBuffer timestamp_resolve;
    WGPUBuffer timestamp_readback;
    u32 timestamp_count;          // asked for by this frame; 0 for none
    ReadbackState timestamp_state;
    u64 timestamp_values[NV_GPU_MAX_TIMESTAMPS];

    Readback readbacks[NV_GPU_MAX_READBACKS];

    // The shader modules made so far, by the code they were made from.
    const char* module_codes[16];
    WGPUShaderModule modules[16];
    u32 module_count;
} Backend;

global Backend backend;

// What a request gave; the callbacks fill it in.
typedef struct AdapterRequest {
    b32 ok;
    WGPUAdapter adapter;
} AdapterRequest;

typedef struct DeviceRequest {
    b32 ok;
    WGPUDevice device;
    b32 has_timestamps;
} DeviceRequest;

internal void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message,
                         void* userdata1, void* userdata2)
{
    (void)userdata2;
    AdapterRequest* request = userdata1;
    if (status == WGPURequestAdapterStatus_Success && adapter)
        *request = (AdapterRequest){.ok = 1, .adapter = adapter};
    else
        nv_log(NV_LOG_ERROR, "wgpu", "request adapter failed: " SV_FMT, SV_ARG(message));
}

internal void on_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
                        void* userdata1, void* userdata2)
{
    (void)userdata2;
    DeviceRequest* request = userdata1;
    if (status == WGPURequestDeviceStatus_Success && device) {
        request->ok = 1;
        request->device = device;
    }
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

internal AdapterRequest request_adapter(WGPUInstance instance, WGPUSurface surface)
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
    return request;
}

internal DeviceRequest request_device(WGPUInstance instance, WGPUAdapter adapter)
{
    WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    desc.label = (WGPUStringView){"nv device", WGPU_STRLEN};
    // Pass timing is optional: many browsers, phones especially, do not offer it.
    local_persist const WGPUFeatureName timestamps = WGPUFeatureName_TimestampQuery;
    b32 has_timestamps = wgpuAdapterHasFeature(adapter, timestamps);
    if (has_timestamps) {
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
    if (request.ok)
        request.has_timestamps = has_timestamps;
    return request;
}


internal NvGpuFormat srgb_view_format(NvGpuFormat format)
{
    switch (format) {
    case NV_GPU_FORMAT_BGRA8: return NV_GPU_FORMAT_BGRA8_SRGB;
    case NV_GPU_FORMAT_RGBA8: return NV_GPU_FORMAT_RGBA8_SRGB;
    default: return format;
    }
}

internal void configure_surface(NvGpu* gpu)
{
    if (gpu->width == 0 || gpu->height == 0)
        return;

    WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
    config.device = backend.device;
    config.format = wgpu_format(gpu->config_format);
    WGPUTextureFormat view_format = wgpu_format(gpu->surface_format);
    if (gpu->surface_format != gpu->config_format) {
        config.viewFormatCount = 1;
        config.viewFormats = &view_format;
    }
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = gpu->width;
    config.height = gpu->height;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(backend.surface, &config);
    // The scene and the UI draw straight into the swapchain's view: there is no offscreen color buffer.
    nv_log(NV_LOG_INFO, "nv", "swapchain and color target: %ux%u, canvas format %s, render view %s, present mode Fifo (vsync)",
           gpu->width, gpu->height, nv_gpu_format_name(gpu->config_format), nv_gpu_format_name(gpu->surface_format));
}

internal const char* adapter_backend_name(WGPUBackendType type)
{
    switch (type) {
    case WGPUBackendType_WebGPU:  return "WebGPU";
    case WGPUBackendType_D3D11:   return "D3D11";
    case WGPUBackendType_D3D12:   return "D3D12";
    case WGPUBackendType_Metal:   return "Metal";
    case WGPUBackendType_Vulkan:  return "Vulkan";
    case WGPUBackendType_OpenGL:  return "OpenGL";
    case WGPUBackendType_OpenGLES: return "OpenGLES";
    default:                      return "unknown backend";
    }
}

internal const char* adapter_type_name(WGPUAdapterType type)
{
    switch (type) {
    case WGPUAdapterType_DiscreteGPU:   return "discrete GPU";
    case WGPUAdapterType_IntegratedGPU: return "integrated GPU";
    case WGPUAdapterType_CPU:           return "software (CPU)";
    default:                            return "unknown type";
    }
}

// A string the browser may have left empty (it hides some details), for a log line.
internal int sv_length(WGPUStringView view)
{
    return view.data ? (int)(view.length == WGPU_STRLEN ? strlen(view.data) : view.length) : 0;
}

// The machine, as far as the browser tells: CPU, GPU and display.
internal void log_system(NvGpu* gpu)
{
    int cores = 0;
    f32 memory_gb = 0.0f;
    int screen_width = 0;
    int screen_height = 0;
    f32 pixel_ratio = 1.0f;
    char platform[64];
    js_system_info(&cores, &memory_gb, &screen_width, &screen_height, &pixel_ratio, platform, (int)sizeof(platform));
    char agent[256];
    nv_window_system_text(agent, sizeof(agent));

    nv_log(NV_LOG_INFO, "nv", "CPU: %d logical cores, memory %s%.0f GB (browser-rounded), platform %s, WebAssembly 32-bit; the browser hides the CPU model",
           cores, memory_gb > 0.0f ? "" : "unknown ", memory_gb, platform[0] ? platform : "unknown");
    nv_log(NV_LOG_INFO, "nv", "browser: %s", agent);

    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(backend.adapter, &info) == WGPUStatus_Success) {
        // The browser leaves out what it considers fingerprinting; say so instead of printing blanks.
        nv_log(NV_LOG_INFO, "nv", "GPU: vendor %.*s, architecture %.*s, device %.*s, description %.*s (%s, %s), vendor id 0x%04x, device id 0x%04x",
               sv_length(info.vendor) ? sv_length(info.vendor) : 7, sv_length(info.vendor) ? info.vendor.data : "unknown",
               sv_length(info.architecture) ? sv_length(info.architecture) : 7, sv_length(info.architecture) ? info.architecture.data : "unknown",
               sv_length(info.device) ? sv_length(info.device) : 7, sv_length(info.device) ? info.device.data : "unknown",
               sv_length(info.description) ? sv_length(info.description) : 7, sv_length(info.description) ? info.description.data : "unknown",
               adapter_backend_name(info.backendType), adapter_type_name(info.adapterType), info.vendorID, info.deviceID);
        wgpuAdapterInfoFreeMembers(info);
    }
    WGPULimits limits = WGPU_LIMITS_INIT;
    if (wgpuAdapterGetLimits(backend.adapter, &limits) == WGPUStatus_Success) {
        nv_log(NV_LOG_INFO, "nv", "GPU limits: texture size up to %u, buffer size up to %llu bytes, pass timing (timestamp queries) %s",
               limits.maxTextureDimension2D, (unsigned long long)limits.maxBufferSize, gpu->has_timestamps ? "available" : "not available");
    }

    NvWindow* window = gpu->window;
    f32 ratio = window->pixel_ratio > 0.0f ? window->pixel_ratio : pixel_ratio;
    nv_log(NV_LOG_INFO, "nv", "display: screen %dx%d, canvas %ux%u CSS pixels, device pixel ratio %.2f, framebuffer %ux%u pixels",
           screen_width, screen_height, (u32)((f32)gpu->width / ratio + 0.5f), (u32)((f32)gpu->height / ratio + 0.5f), (f64)ratio,
           gpu->width, gpu->height);
}

internal void release_gpu(NvGpu* gpu)
{
    if (backend.queue)
        wgpuQueueRelease(backend.queue);
    if (backend.device)
        wgpuDeviceRelease(backend.device);
    if (backend.adapter)
        wgpuAdapterRelease(backend.adapter);
    if (backend.surface)
        wgpuSurfaceRelease(backend.surface);
    if (backend.instance)
        wgpuInstanceRelease(backend.instance);
    backend = (Backend){0};
    *gpu = (NvGpu){0};
}

b32 nv_gpu_create(NvGpu* gpu, NvWindow* window)
{
    *gpu = (NvGpu){0};
    backend = (Backend){0};
    gpu->window = window;
    gpu->backend_name = "WebGPU";
    backend.gpu = gpu;

    local_persist const WGPUInstanceFeatureName instance_features[] = {WGPUInstanceFeatureName_TimedWaitAny};
    WGPUInstanceDescriptor instance_desc = WGPU_INSTANCE_DESCRIPTOR_INIT;
    instance_desc.requiredFeatureCount = NV_ARRAY_COUNT(instance_features);
    instance_desc.requiredFeatures = instance_features;
    backend.instance = wgpuCreateInstance(&instance_desc);
    if (!backend.instance) {
        nv_log(NV_LOG_ERROR, "nv", "wgpuCreateInstance failed");
        goto fail;
    }

    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas = WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    canvas.selector = (WGPUStringView){window->canvas_selector, WGPU_STRLEN};
    WGPUSurfaceDescriptor surface_desc = WGPU_SURFACE_DESCRIPTOR_INIT;
    surface_desc.nextInChain = &canvas.chain;
    backend.surface = wgpuInstanceCreateSurface(backend.instance, &surface_desc);
    if (!backend.surface) {
        nv_log(NV_LOG_ERROR, "nv", "failed to create canvas surface");
        goto fail;
    }

    AdapterRequest adapter = request_adapter(backend.instance, backend.surface);
    if (!adapter.ok)
        goto fail;
    backend.adapter = adapter.adapter;

    DeviceRequest device = request_device(backend.instance, backend.adapter);
    if (!device.ok)
        goto fail;
    backend.device = device.device;
    gpu->has_timestamps = device.has_timestamps;
    backend.queue = wgpuDeviceGetQueue(backend.device);

    WGPULimits limits = WGPU_LIMITS_INIT;
    if (wgpuDeviceGetLimits(backend.device, &limits) == WGPUStatus_Success) {
        gpu->max_buffer_bytes = limits.maxBufferSize;
        gpu->max_storage_binding_bytes = limits.maxStorageBufferBindingSize;
    }

    WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
    wgpuSurfaceGetCapabilities(backend.surface, backend.adapter, &caps);
    if (caps.formatCount == 0) {
        nv_log(NV_LOG_ERROR, "nv", "canvas reports no supported formats");
        wgpuSurfaceCapabilitiesFreeMembers(caps);
        goto fail;
    }
    // NOTE: Canvases only offer linear formats, but accept the sRGB twin as a view format.
    // Rendering through that view gamma-corrects shader output.
    gpu->config_format = format_from_wgpu(caps.formats[0]);
    gpu->surface_format = srgb_view_format(gpu->config_format);
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    if (gpu->config_format == NV_GPU_FORMAT_NONE) {
        nv_log(NV_LOG_ERROR, "nv", "canvas format is not one nv uses");
        goto fail;
    }

    if (gpu->has_timestamps) {
        WGPUQuerySetDescriptor query_desc = {.type = WGPUQueryType_Timestamp, .count = NV_GPU_MAX_TIMESTAMPS};
        backend.timestamp_queries = wgpuDeviceCreateQuerySet(backend.device, &query_desc);
        WGPUBufferDescriptor resolve_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        resolve_desc.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        resolve_desc.size = NV_GPU_MAX_TIMESTAMPS * sizeof(u64);
        backend.timestamp_resolve = wgpuDeviceCreateBuffer(backend.device, &resolve_desc);
        WGPUBufferDescriptor readback_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        readback_desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        readback_desc.size = NV_GPU_MAX_TIMESTAMPS * sizeof(u64);
        backend.timestamp_readback = wgpuDeviceCreateBuffer(backend.device, &readback_desc);
    }

    nv_window_framebuffer_size(window, &gpu->width, &gpu->height);
    log_system(gpu);
    configure_surface(gpu);
    return 1;

fail:
    release_gpu(gpu);
    return 0;
}

//
// Buffers
//

internal WGPUBufferUsage wgpu_buffer_usage(u32 usage)
{
    WGPUBufferUsage result = WGPUBufferUsage_CopyDst;
    if (usage & NV_GPU_BUFFER_VERTEX)   result |= WGPUBufferUsage_Vertex;
    if (usage & NV_GPU_BUFFER_INDEX)    result |= WGPUBufferUsage_Index;
    if (usage & NV_GPU_BUFFER_UNIFORM)  result |= WGPUBufferUsage_Uniform;
    if (usage & NV_GPU_BUFFER_STORAGE)  result |= WGPUBufferUsage_Storage;
    if (usage & NV_GPU_BUFFER_INDIRECT) result |= WGPUBufferUsage_Indirect;
    if (usage & NV_GPU_BUFFER_COPY_SRC) result |= WGPUBufferUsage_CopySrc;
    return result;
}

internal WGPUBuffer buffer_of(NvGpuBuffer handle)
{
    return ((Buffer*)pool_item(&buffer_pool, handle.index, handle.generation))->buffer;
}

NvGpuBuffer nv_gpu_create_buffer(NvGpu* gpu, const NvGpuBufferDesc* desc)
{
    (void)gpu;
    WGPUBufferDescriptor wgpu_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    if (desc->label)
        wgpu_desc.label = (WGPUStringView){desc->label, WGPU_STRLEN};
    wgpu_desc.usage = wgpu_buffer_usage(desc->usage);
    wgpu_desc.size = (desc->size + 3) & ~(u64)3;
    u32 generation;
    u32 index = pool_take(&buffer_pool, &generation);
    Buffer* buffer = pool_item(&buffer_pool, index, generation);
    buffer->buffer = wgpuDeviceCreateBuffer(backend.device, &wgpu_desc);
    buffer->size = wgpu_desc.size;
    if (desc->data)
        wgpuQueueWriteBuffer(backend.queue, buffer->buffer, 0, desc->data, (umm)desc->size);
    return (NvGpuBuffer){index, generation};
}

void nv_gpu_release_buffer(NvGpu* gpu, NvGpuBuffer handle)
{
    (void)gpu;
    wgpuBufferRelease(buffer_of(handle));
    pool_give(&buffer_pool, handle.index, handle.generation);
}

void nv_gpu_write_buffer(NvGpu* gpu, NvGpuBuffer handle, u64 offset, const void* bytes, umm size)
{
    (void)gpu;
    NV_ASSERT(size % 4 == 0);
    wgpuQueueWriteBuffer(backend.queue, buffer_of(handle), offset, bytes, size);
}

//
// Textures
//

internal WGPUTextureUsage wgpu_texture_usage(u32 usage)
{
    WGPUTextureUsage result = 0;
    if (usage & NV_GPU_TEXTURE_SAMPLED)       result |= WGPUTextureUsage_TextureBinding;
    if (usage & NV_GPU_TEXTURE_RENDER_TARGET) result |= WGPUTextureUsage_RenderAttachment;
    if (usage & NV_GPU_TEXTURE_COPY_DST)      result |= WGPUTextureUsage_CopyDst;
    return result;
}

internal Texture* texture_of(NvGpuTexture handle)
{
    return pool_item(&texture_pool, handle.index, handle.generation);
}

NvGpuTexture nv_gpu_create_texture(NvGpu* gpu, const NvGpuTextureDesc* desc)
{
    (void)gpu;
    NV_ASSERT(desc->width && desc->height && desc->format != NV_GPU_FORMAT_NONE);
    WGPUTextureDescriptor wgpu_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    if (desc->label)
        wgpu_desc.label = (WGPUStringView){desc->label, WGPU_STRLEN};
    wgpu_desc.usage = wgpu_texture_usage(desc->usage);
    wgpu_desc.size = (WGPUExtent3D){desc->width, desc->height, 1};
    wgpu_desc.format = wgpu_format(desc->format);
    wgpu_desc.mipLevelCount = desc->mip_count ? desc->mip_count : 1;
    wgpu_desc.sampleCount = desc->samples ? desc->samples : 1;
    u32 generation;
    u32 index = pool_take(&texture_pool, &generation);
    Texture* texture = pool_item(&texture_pool, index, generation);
    texture->texture = wgpuDeviceCreateTexture(backend.device, &wgpu_desc);
    texture->desc = *desc;
    texture->desc.label = NULL;
    texture->desc.mip_count = wgpu_desc.mipLevelCount;
    texture->desc.samples = wgpu_desc.sampleCount;
    return (NvGpuTexture){index, generation};
}

void nv_gpu_release_texture(NvGpu* gpu, NvGpuTexture handle)
{
    (void)gpu;
    wgpuTextureRelease(texture_of(handle)->texture);
    pool_give(&texture_pool, handle.index, handle.generation);
}

NvGpuTextureDesc nv_gpu_texture_desc(const NvGpu* gpu, NvGpuTexture handle)
{
    (void)gpu;
    return texture_of(handle)->desc;
}

void nv_gpu_write_texture(NvGpu* gpu, NvGpuTexture handle, u32 mip, u32 x, u32 y, u32 width, u32 height, const void* texels,
                          u32 bytes_per_row)
{
    (void)gpu;
    Texture* texture = texture_of(handle);
    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = texture->texture;
    destination.mipLevel = mip;
    destination.origin = (WGPUOrigin3D){x, y, 0};
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.bytesPerRow = bytes_per_row;
    layout.rowsPerImage = height;
    WGPUExtent3D size = {width, height, 1};
    umm data_size = (umm)bytes_per_row * (height - 1) + (umm)width * nv_gpu_format_bytes(texture->desc.format);
    wgpuQueueWriteTexture(backend.queue, &destination, texels, data_size, &layout, &size);
}

internal WGPUTextureView view_of(NvGpuView handle)
{
    return ((View*)pool_item(&view_pool, handle.index, handle.generation))->view;
}

NvGpuView nv_gpu_create_view(NvGpu* gpu, NvGpuTexture texture, const NvGpuViewDesc* desc)
{
    (void)gpu;
    WGPUTextureViewDescriptor view_desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    if (desc) {
        view_desc.baseMipLevel = desc->base_mip;
        if (desc->mip_count)
            view_desc.mipLevelCount = desc->mip_count;
        if (desc->depth_only)
            view_desc.aspect = WGPUTextureAspect_DepthOnly;
    }
    WGPUTextureView wgpu_view = wgpuTextureCreateView(texture_of(texture)->texture, &view_desc);
    u32 generation;
    u32 index = pool_take(&view_pool, &generation);
    ((View*)pool_item(&view_pool, index, generation))->view = wgpu_view;
    return (NvGpuView){index, generation};
}

void nv_gpu_release_view(NvGpu* gpu, NvGpuView handle)
{
    (void)gpu;
    wgpuTextureViewRelease(view_of(handle));
    pool_give(&view_pool, handle.index, handle.generation);
}

NvGpuSampler nv_gpu_create_sampler(NvGpu* gpu, const NvGpuSamplerDesc* desc)
{
    (void)gpu;
    WGPUSamplerDescriptor sampler_desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sampler_desc.addressModeU = desc->repeat ? WGPUAddressMode_Repeat : WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeV = desc->repeat ? WGPUAddressMode_Repeat : WGPUAddressMode_ClampToEdge;
    sampler_desc.magFilter = WGPUFilterMode_Linear;
    sampler_desc.minFilter = WGPUFilterMode_Linear;
    if (desc->mip_linear)
        sampler_desc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    if (desc->compare)
        sampler_desc.compare = WGPUCompareFunction_LessEqual; // 1 where the point is not behind the map's depth
    u32 generation;
    u32 index = pool_take(&sampler_pool, &generation);
    ((Sampler*)pool_item(&sampler_pool, index, generation))->sampler = wgpuDeviceCreateSampler(backend.device, &sampler_desc);
    return (NvGpuSampler){index, generation};
}

internal WGPUSampler sampler_of(NvGpuSampler handle)
{
    return ((Sampler*)pool_item(&sampler_pool, handle.index, handle.generation))->sampler;
}

void nv_gpu_release_sampler(NvGpu* gpu, NvGpuSampler handle)
{
    (void)gpu;
    wgpuSamplerRelease(sampler_of(handle));
    pool_give(&sampler_pool, handle.index, handle.generation);
}

//
// Bind groups and pipelines
//

internal WGPUShaderStage wgpu_stages(u32 stages)
{
    WGPUShaderStage result = 0;
    if (stages & NV_GPU_STAGE_VERTEX)   result |= WGPUShaderStage_Vertex;
    if (stages & NV_GPU_STAGE_FRAGMENT) result |= WGPUShaderStage_Fragment;
    if (stages & NV_GPU_STAGE_COMPUTE)  result |= WGPUShaderStage_Compute;
    return result;
}

NvGpuLayout nv_gpu_create_layout(NvGpu* gpu, const NvGpuLayoutEntry* entries, u32 count)
{
    (void)gpu;
    WGPUBindGroupLayoutEntry wgpu_entries[NV_GPU_MAX_ENTRIES] = {0};
    NV_ASSERT(count <= NV_GPU_MAX_ENTRIES);
    for (u32 i = 0; i < count; ++i) {
        const NvGpuLayoutEntry* in = &entries[i];
        WGPUBindGroupLayoutEntry* out = &wgpu_entries[i];
        out->binding = in->binding;
        out->visibility = wgpu_stages(in->stages);
        switch (in->kind) {
        case NV_GPU_BINDING_UNIFORM:
            out->buffer.type = WGPUBufferBindingType_Uniform;
            out->buffer.hasDynamicOffset = in->dynamic_offset;
            out->buffer.minBindingSize = in->min_size;
            break;
        case NV_GPU_BINDING_STORAGE_READ:
            out->buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            break;
        case NV_GPU_BINDING_STORAGE_READ_WRITE:
            out->buffer.type = WGPUBufferBindingType_Storage;
            break;
        case NV_GPU_BINDING_TEXTURE_FLOAT:
            out->texture.sampleType = WGPUTextureSampleType_Float;
            out->texture.viewDimension = WGPUTextureViewDimension_2D;
            break;
        case NV_GPU_BINDING_TEXTURE_DEPTH:
            out->texture.sampleType = WGPUTextureSampleType_Depth;
            out->texture.viewDimension = WGPUTextureViewDimension_2D;
            break;
        case NV_GPU_BINDING_TEXTURE_DEPTH_MS:
            out->texture.sampleType = WGPUTextureSampleType_Depth;
            out->texture.viewDimension = WGPUTextureViewDimension_2D;
            out->texture.multisampled = 1;
            break;
        case NV_GPU_BINDING_SAMPLER:
            out->sampler.type = WGPUSamplerBindingType_Filtering;
            break;
        case NV_GPU_BINDING_SAMPLER_COMPARISON:
            out->sampler.type = WGPUSamplerBindingType_Comparison;
            break;
        }
    }
    WGPUBindGroupLayoutDescriptor layout_desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layout_desc.entryCount = count;
    layout_desc.entries = wgpu_entries;
    u32 generation;
    u32 index = pool_take(&layout_pool, &generation);
    ((Layout*)pool_item(&layout_pool, index, generation))->layout = wgpuDeviceCreateBindGroupLayout(backend.device, &layout_desc);
    return (NvGpuLayout){index, generation};
}

internal WGPUBindGroupLayout layout_of(NvGpuLayout handle)
{
    return ((Layout*)pool_item(&layout_pool, handle.index, handle.generation))->layout;
}

void nv_gpu_release_layout(NvGpu* gpu, NvGpuLayout handle)
{
    (void)gpu;
    wgpuBindGroupLayoutRelease(layout_of(handle));
    pool_give(&layout_pool, handle.index, handle.generation);
}

NvGpuGroup nv_gpu_create_group(NvGpu* gpu, NvGpuLayout layout, const NvGpuGroupEntry* entries, u32 count)
{
    (void)gpu;
    WGPUBindGroupEntry wgpu_entries[NV_GPU_MAX_ENTRIES] = {0};
    NV_ASSERT(count <= NV_GPU_MAX_ENTRIES);
    for (u32 i = 0; i < count; ++i) {
        const NvGpuGroupEntry* in = &entries[i];
        WGPUBindGroupEntry* out = &wgpu_entries[i];
        out->binding = in->binding;
        if (in->buffer.index) {
            out->buffer = buffer_of(in->buffer);
            out->size = in->size;
        } else if (in->view.index) {
            out->textureView = view_of(in->view);
        } else {
            out->sampler = sampler_of(in->sampler);
        }
    }
    WGPUBindGroupDescriptor group_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    group_desc.layout = layout_of(layout);
    group_desc.entryCount = count;
    group_desc.entries = wgpu_entries;
    u32 generation;
    u32 index = pool_take(&group_pool, &generation);
    ((Group*)pool_item(&group_pool, index, generation))->group = wgpuDeviceCreateBindGroup(backend.device, &group_desc);
    return (NvGpuGroup){index, generation};
}

internal WGPUBindGroup group_of(NvGpuGroup handle)
{
    return ((Group*)pool_item(&group_pool, handle.index, handle.generation))->group;
}

void nv_gpu_release_group(NvGpu* gpu, NvGpuGroup handle)
{
    (void)gpu;
    wgpuBindGroupRelease(group_of(handle));
    pool_give(&group_pool, handle.index, handle.generation);
}

// The module of `shader`, made the first time it is asked for: every pipeline made from one shader shares it.
internal WGPUShaderModule shader_module(const NvGpuShaderCode* shader)
{
    for (u32 i = 0; i < backend.module_count; ++i) {
        if (backend.module_codes[i] == shader->wgsl)
            return backend.modules[i];
    }
    NV_ASSERT(backend.module_count < NV_ARRAY_COUNT(backend.modules));
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){shader->wgsl, WGPU_STRLEN};
    WGPUShaderModuleDescriptor module_desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    module_desc.nextInChain = &wgsl.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(backend.device, &module_desc);
    backend.module_codes[backend.module_count] = shader->wgsl;
    backend.modules[backend.module_count++] = module;
    return module;
}

internal WGPUPipelineLayout make_pipeline_layout(const NvGpuLayout* layouts, u32 count)
{
    NV_ASSERT(count > 0 && count <= NV_GPU_MAX_LAYOUTS);
    WGPUBindGroupLayout wgpu_layouts[NV_GPU_MAX_LAYOUTS];
    for (u32 i = 0; i < count; ++i)
        wgpu_layouts[i] = layout_of(layouts[i]);
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = count;
    desc.bindGroupLayouts = wgpu_layouts;
    return wgpuDeviceCreatePipelineLayout(backend.device, &desc);
}

internal WGPUVertexFormat wgpu_vertex_format(NvGpuVertexFormat format)
{
    switch (format) {
    case NV_GPU_VERTEX_FLOAT2:   return WGPUVertexFormat_Float32x2;
    case NV_GPU_VERTEX_FLOAT3:   return WGPUVertexFormat_Float32x3;
    case NV_GPU_VERTEX_FLOAT4:   return WGPUVertexFormat_Float32x4;
    case NV_GPU_VERTEX_UINT16X4: return WGPUVertexFormat_Uint16x4;
    case NV_GPU_VERTEX_UNORM8X4: return WGPUVertexFormat_Unorm8x4;
    }
    NV_INVALID_CODE_PATH;
    return WGPUVertexFormat_Float32x4;
}

internal WGPUCompareFunction wgpu_compare(NvGpuCompare compare)
{
    switch (compare) {
    case NV_GPU_COMPARE_ALWAYS:  return WGPUCompareFunction_Always;
    case NV_GPU_COMPARE_LESS:    return WGPUCompareFunction_Less;
    case NV_GPU_COMPARE_GREATER: return WGPUCompareFunction_Greater;
    }
    NV_INVALID_CODE_PATH;
    return WGPUCompareFunction_Always;
}

NvGpuPipeline nv_gpu_create_render_pipeline(NvGpu* gpu, const NvGpuRenderPipelineDesc* desc)
{
    (void)gpu;
    WGPUShaderModule module = shader_module(desc->shader);
    WGPUPipelineLayout layout = make_pipeline_layout(desc->layouts, desc->layout_count);

    WGPUVertexAttribute attributes[NV_GPU_MAX_ATTRIBUTES];
    NV_ASSERT(desc->attribute_count <= NV_GPU_MAX_ATTRIBUTES);
    for (u32 i = 0; i < desc->attribute_count; ++i) {
        attributes[i] = (WGPUVertexAttribute){
            .format = wgpu_vertex_format(desc->attributes[i].format),
            .offset = desc->attributes[i].offset,
            .shaderLocation = desc->attributes[i].location,
        };
    }
    WGPUVertexBufferLayout vertex_layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertex_layout.stepMode = WGPUVertexStepMode_Vertex;
    vertex_layout.arrayStride = desc->vertex_stride;
    vertex_layout.attributeCount = desc->attribute_count;
    vertex_layout.attributes = attributes;

    WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
    switch (desc->blend) {
    case NV_GPU_BLEND_NONE:
        break;
    case NV_GPU_BLEND_ALPHA:
        blend.color = (WGPUBlendComponent){WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha};
        blend.alpha = (WGPUBlendComponent){WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha};
        break;
    case NV_GPU_BLEND_ADD:
        blend.color = (WGPUBlendComponent){WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One};
        blend.alpha = (WGPUBlendComponent){WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One};
        break;
    case NV_GPU_BLEND_ADD_KEEP_ALPHA:
        blend.color = (WGPUBlendComponent){WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One};
        blend.alpha = (WGPUBlendComponent){WGPUBlendOperation_Add, WGPUBlendFactor_Zero, WGPUBlendFactor_One};
        break;
    }
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = wgpu_format(desc->color_format);
    target.blend = desc->blend == NV_GPU_BLEND_NONE ? NULL : &blend;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    if (desc->fragment_entry) {
        fragment.module = module;
        fragment.entryPoint = (WGPUStringView){desc->fragment_entry, WGPU_STRLEN};
        fragment.targetCount = desc->color_format != NV_GPU_FORMAT_NONE ? 1 : 0;
        fragment.targets = &target;
    }

    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = wgpu_format(desc->depth_format);
    depth.depthWriteEnabled = desc->depth_write ? WGPUOptionalBool_True : WGPUOptionalBool_False;
    depth.depthCompare = wgpu_compare(desc->depth_compare);
    depth.depthBias = desc->depth_bias;
    depth.depthBiasSlopeScale = desc->depth_bias_slope;

    WGPURenderPipelineDescriptor pipeline_desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    if (desc->label)
        pipeline_desc.label = (WGPUStringView){desc->label, WGPU_STRLEN};
    pipeline_desc.layout = layout;
    pipeline_desc.vertex.module = module;
    pipeline_desc.vertex.entryPoint = (WGPUStringView){desc->vertex_entry, WGPU_STRLEN};
    if (desc->vertex_stride) {
        pipeline_desc.vertex.bufferCount = 1;
        pipeline_desc.vertex.buffers = &vertex_layout;
    }
    pipeline_desc.primitive.topology = desc->topology == NV_GPU_TOPOLOGY_LINES ? WGPUPrimitiveTopology_LineList : WGPUPrimitiveTopology_TriangleList;
    pipeline_desc.primitive.frontFace = WGPUFrontFace_CCW;
    pipeline_desc.primitive.cullMode = desc->cull == NV_GPU_CULL_BACK ? WGPUCullMode_Back : WGPUCullMode_None;
    if (desc->depth_format != NV_GPU_FORMAT_NONE)
        pipeline_desc.depthStencil = &depth;
    if (desc->samples)
        pipeline_desc.multisample.count = desc->samples;
    if (desc->fragment_entry)
        pipeline_desc.fragment = &fragment;

    u32 generation;
    u32 index = pool_take(&pipeline_pool, &generation);
    Pipeline* pipeline = pool_item(&pipeline_pool, index, generation);
    pipeline->render = wgpuDeviceCreateRenderPipeline(backend.device, &pipeline_desc);
    wgpuPipelineLayoutRelease(layout);
    return (NvGpuPipeline){index, generation};
}

NvGpuPipeline nv_gpu_create_compute_pipeline(NvGpu* gpu, const NvGpuComputePipelineDesc* desc)
{
    (void)gpu;
    WGPUShaderModule module = shader_module(desc->shader);
    WGPUPipelineLayout layout = make_pipeline_layout(desc->layouts, desc->layout_count);
    WGPUComputePipelineDescriptor pipeline_desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    if (desc->label)
        pipeline_desc.label = (WGPUStringView){desc->label, WGPU_STRLEN};
    pipeline_desc.layout = layout;
    pipeline_desc.compute.module = module;
    pipeline_desc.compute.entryPoint = (WGPUStringView){desc->entry, WGPU_STRLEN};
    u32 generation;
    u32 index = pool_take(&pipeline_pool, &generation);
    Pipeline* pipeline = pool_item(&pipeline_pool, index, generation);
    pipeline->is_compute = 1;
    pipeline->compute = wgpuDeviceCreateComputePipeline(backend.device, &pipeline_desc);
    wgpuPipelineLayoutRelease(layout);
    return (NvGpuPipeline){index, generation};
}

internal Pipeline* pipeline_of(NvGpuPipeline handle)
{
    return pool_item(&pipeline_pool, handle.index, handle.generation);
}

void nv_gpu_release_pipeline(NvGpu* gpu, NvGpuPipeline handle)
{
    (void)gpu;
    Pipeline* pipeline = pipeline_of(handle);
    if (pipeline->is_compute)
        wgpuComputePipelineRelease(pipeline->compute);
    else
        wgpuRenderPipelineRelease(pipeline->render);
    pool_give(&pipeline_pool, handle.index, handle.generation);
}

//
// Frames and passes
//

NvGpuFrame nv_gpu_begin_frame(NvGpu* gpu)
{
    NV_ASSERT(!backend.current_view);

    u32 width = 0;
    u32 height = 0;
    nv_window_framebuffer_size(gpu->window, &width, &height);
    if (width != gpu->width || height != gpu->height) {
        gpu->width = width;
        gpu->height = height;
        configure_surface(gpu);
    }
    if (gpu->width == 0 || gpu->height == 0)
        return (NvGpuFrame){0}; // canvas is hidden or collapsed

    WGPUSurfaceTexture surface_texture = WGPU_SURFACE_TEXTURE_INIT;
    wgpuSurfaceGetCurrentTexture(backend.surface, &surface_texture);
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
        return (NvGpuFrame){0};
    default:
        nv_log(NV_LOG_ERROR, "nv", "wgpuSurfaceGetCurrentTexture failed (%d)", (int)surface_texture.status);
        return (NvGpuFrame){0};
    }

    backend.current_texture = surface_texture.texture;
    WGPUTextureViewDescriptor view_desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    view_desc.format = wgpu_format(gpu->surface_format);
    backend.current_view = wgpuTextureCreateView(backend.current_texture, &view_desc);
    u32 generation;
    u32 index = pool_take(&view_pool, &generation);
    ((View*)pool_item(&view_pool, index, generation))->view = backend.current_view;
    backend.current_view_handle = (NvGpuView){index, generation};
    backend.encoder = wgpuDeviceCreateCommandEncoder(backend.device, NULL);
    return (NvGpuFrame){.ok = 1, .view = backend.current_view_handle};
}

internal void readback_map_done(WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void* userdata2);

// Starts mapping the frame's readbacks, once their copies are submitted.
internal void map_readbacks(void)
{
    for (u32 slot = 0; slot < NV_GPU_MAX_READBACKS; ++slot) {
        Readback* readback = &backend.readbacks[slot];
        if (readback->state != READBACK_COPIED)
            continue;
        readback->state = READBACK_MAPPING;
        WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.callback = readback_map_done;
        callback.userdata1 = readback;
        wgpuBufferMapAsync(readback->buffer, WGPUMapMode_Read, 0, readback->size, callback);
    }
}

internal void timestamps_map_done(WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void* userdata2)
{
    (void)message, (void)userdata1, (void)userdata2;
    if (status == WGPUMapAsyncStatus_Success) {
        const u64* ticks = wgpuBufferGetConstMappedRange(backend.timestamp_readback, 0, NV_GPU_MAX_TIMESTAMPS * sizeof(u64));
        if (ticks)
            memcpy(backend.timestamp_values, ticks, sizeof(backend.timestamp_values));
        wgpuBufferUnmap(backend.timestamp_readback);
        backend.timestamp_state = ticks ? READBACK_READY : READBACK_IDLE;
    } else {
        backend.timestamp_state = READBACK_IDLE;
    }
}

void nv_gpu_end_frame(NvGpu* gpu)
{
    (void)gpu;
    NV_ASSERT(backend.current_view && backend.encoder && !backend.render_pass && !backend.compute_pass);
    if (backend.timestamp_count) {
        // A frame without a shadow pass resolves its two queries as 0, which reads as no time.
        wgpuCommandEncoderResolveQuerySet(backend.encoder, backend.timestamp_queries, 0, backend.timestamp_count, backend.timestamp_resolve, 0);
        wgpuCommandEncoderCopyBufferToBuffer(backend.encoder, backend.timestamp_resolve, 0, backend.timestamp_readback, 0,
                                             NV_GPU_MAX_TIMESTAMPS * sizeof(u64));
    }
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(backend.encoder, NULL);
    wgpuQueueSubmit(backend.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(backend.encoder);
    backend.encoder = NULL;

    if (backend.timestamp_count) {
        backend.timestamp_count = 0;
        backend.timestamp_state = READBACK_MAPPING;
        WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.callback = timestamps_map_done;
        wgpuBufferMapAsync(backend.timestamp_readback, WGPUMapMode_Read, 0, NV_GPU_MAX_TIMESTAMPS * sizeof(u64), callback);
    }
    map_readbacks();

    // NOTE: No wgpuSurfacePresent here: the browser presents the canvas when the animation frame
    // callback returns (and emdawnwebgpu aborts if it is called).
    nv_gpu_release_view(gpu, backend.current_view_handle);
    backend.current_view_handle = (NvGpuView){0};
    wgpuTextureRelease(backend.current_texture);
    backend.current_view = NULL;
    backend.current_texture = NULL;
}

internal WGPUPassTimestampWrites pass_timestamps(b32 wanted, u32 begin, u32 end, b32* used)
{
    WGPUPassTimestampWrites writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
    *used = wanted && backend.timestamp_queries;
    if (*used) {
        NV_ASSERT(begin < NV_GPU_MAX_TIMESTAMPS && end < NV_GPU_MAX_TIMESTAMPS);
        writes.querySet = backend.timestamp_queries;
        writes.beginningOfPassWriteIndex = begin;
        writes.endOfPassWriteIndex = end;
    }
    return writes;
}

void nv_gpu_begin_render_pass(NvGpu* gpu, const NvGpuRenderPassDesc* desc)
{
    (void)gpu;
    NV_ASSERT(backend.encoder && !backend.render_pass && !backend.compute_pass);
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    if (desc->label)
        pass_desc.label = (WGPUStringView){desc->label, WGPU_STRLEN};
    if (desc->color.index) {
        color.view = view_of(desc->color);
        if (desc->resolve.index)
            color.resolveTarget = view_of(desc->resolve);
        color.loadOp = desc->color_load == NV_GPU_LOAD_LOAD ? WGPULoadOp_Load : WGPULoadOp_Clear;
        color.storeOp = desc->color_store == NV_GPU_STORE_DISCARD ? WGPUStoreOp_Discard : WGPUStoreOp_Store;
        color.clearValue = (WGPUColor){desc->clear_color[0], desc->clear_color[1], desc->clear_color[2], desc->clear_color[3]};
        pass_desc.colorAttachmentCount = 1;
        pass_desc.colorAttachments = &color;
    }
    if (desc->depth.index) {
        depth.view = view_of(desc->depth);
        depth.depthLoadOp = desc->depth_load == NV_GPU_LOAD_LOAD ? WGPULoadOp_Load : WGPULoadOp_Clear;
        depth.depthStoreOp = desc->depth_store == NV_GPU_STORE_DISCARD ? WGPUStoreOp_Discard : WGPUStoreOp_Store;
        depth.depthClearValue = desc->clear_depth;
        pass_desc.depthStencilAttachment = &depth;
    }
    b32 timed;
    WGPUPassTimestampWrites timestamps = pass_timestamps(desc->timestamps, desc->timestamp_begin, desc->timestamp_end, &timed);
    if (timed)
        pass_desc.timestampWrites = &timestamps;
    backend.render_pass = wgpuCommandEncoderBeginRenderPass(backend.encoder, &pass_desc);
}

void nv_gpu_begin_compute_pass(NvGpu* gpu, const NvGpuComputePassDesc* desc)
{
    (void)gpu;
    NV_ASSERT(backend.encoder && !backend.render_pass && !backend.compute_pass);
    WGPUComputePassDescriptor pass_desc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    if (desc->label)
        pass_desc.label = (WGPUStringView){desc->label, WGPU_STRLEN};
    b32 timed;
    WGPUPassTimestampWrites timestamps = pass_timestamps(desc->timestamps, desc->timestamp_begin, desc->timestamp_end, &timed);
    if (timed)
        pass_desc.timestampWrites = &timestamps;
    backend.compute_pass = wgpuCommandEncoderBeginComputePass(backend.encoder, &pass_desc);
}

void nv_gpu_end_pass(NvGpu* gpu)
{
    (void)gpu;
    if (backend.render_pass) {
        wgpuRenderPassEncoderEnd(backend.render_pass);
        wgpuRenderPassEncoderRelease(backend.render_pass);
        backend.render_pass = NULL;
    } else {
        NV_ASSERT(backend.compute_pass);
        wgpuComputePassEncoderEnd(backend.compute_pass);
        wgpuComputePassEncoderRelease(backend.compute_pass);
        backend.compute_pass = NULL;
    }
}

void nv_gpu_set_pipeline(NvGpu* gpu, NvGpuPipeline handle)
{
    (void)gpu;
    Pipeline* pipeline = pipeline_of(handle);
    if (backend.render_pass) {
        NV_ASSERT(!pipeline->is_compute);
        wgpuRenderPassEncoderSetPipeline(backend.render_pass, pipeline->render);
    } else {
        NV_ASSERT(backend.compute_pass && pipeline->is_compute);
        wgpuComputePassEncoderSetPipeline(backend.compute_pass, pipeline->compute);
    }
}

void nv_gpu_set_group(NvGpu* gpu, u32 index, NvGpuGroup group, const u32* dynamic_offsets, u32 dynamic_count)
{
    (void)gpu;
    if (backend.render_pass) {
        wgpuRenderPassEncoderSetBindGroup(backend.render_pass, index, group_of(group), dynamic_count, dynamic_offsets);
    } else {
        NV_ASSERT(backend.compute_pass);
        wgpuComputePassEncoderSetBindGroup(backend.compute_pass, index, group_of(group), dynamic_count, dynamic_offsets);
    }
}

void nv_gpu_set_vertex_buffer(NvGpu* gpu, NvGpuBuffer buffer)
{
    (void)gpu;
    NV_ASSERT(backend.render_pass);
    wgpuRenderPassEncoderSetVertexBuffer(backend.render_pass, 0, buffer_of(buffer), 0, WGPU_WHOLE_SIZE);
}

void nv_gpu_set_index_buffer(NvGpu* gpu, NvGpuBuffer buffer, b32 is_32_bit)
{
    (void)gpu;
    NV_ASSERT(backend.render_pass);
    wgpuRenderPassEncoderSetIndexBuffer(backend.render_pass, buffer_of(buffer), is_32_bit ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16,
                                        0, WGPU_WHOLE_SIZE);
}

void nv_gpu_set_viewport(NvGpu* gpu, f32 x, f32 y, f32 width, f32 height)
{
    (void)gpu;
    NV_ASSERT(backend.render_pass);
    wgpuRenderPassEncoderSetViewport(backend.render_pass, x, y, width, height, 0.0f, 1.0f);
}

void nv_gpu_set_scissor(NvGpu* gpu, u32 x, u32 y, u32 width, u32 height)
{
    (void)gpu;
    NV_ASSERT(backend.render_pass);
    wgpuRenderPassEncoderSetScissorRect(backend.render_pass, x, y, width, height);
}

void nv_gpu_draw(NvGpu* gpu, u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance)
{
    (void)gpu;
    NV_ASSERT(backend.render_pass);
    wgpuRenderPassEncoderDraw(backend.render_pass, vertex_count, instance_count, first_vertex, first_instance);
}

void nv_gpu_draw_indexed(NvGpu* gpu, u32 index_count, u32 first_index, s32 base_vertex, u32 first_instance)
{
    (void)gpu;
    NV_ASSERT(backend.render_pass);
    wgpuRenderPassEncoderDrawIndexed(backend.render_pass, index_count, 1, first_index, base_vertex, first_instance);
}

void nv_gpu_draw_indirect(NvGpu* gpu, NvGpuBuffer buffer, u64 offset)
{
    (void)gpu;
    NV_ASSERT(backend.render_pass);
    wgpuRenderPassEncoderDrawIndirect(backend.render_pass, buffer_of(buffer), offset);
}

void nv_gpu_dispatch(NvGpu* gpu, u32 x, u32 y, u32 z)
{
    (void)gpu;
    NV_ASSERT(backend.compute_pass);
    wgpuComputePassEncoderDispatchWorkgroups(backend.compute_pass, x, y, z);
}

void nv_gpu_dispatch_indirect(NvGpu* gpu, NvGpuBuffer buffer, u64 offset)
{
    (void)gpu;
    NV_ASSERT(backend.compute_pass);
    wgpuComputePassEncoderDispatchWorkgroupsIndirect(backend.compute_pass, buffer_of(buffer), offset);
}

//
// Reading back
//

b32 nv_gpu_resolve_timestamps(NvGpu* gpu, u32 count)
{
    (void)gpu;
    NV_ASSERT(count > 0 && count <= NV_GPU_MAX_TIMESTAMPS);
    if (!backend.timestamp_queries || backend.timestamp_state != READBACK_IDLE)
        return 0;
    backend.timestamp_count = count;
    return 1;
}

NvGpuTimestamps nv_gpu_poll_timestamps(NvGpu* gpu)
{
    (void)gpu;
    if (backend.timestamp_state != READBACK_READY)
        return (NvGpuTimestamps){0};
    backend.timestamp_state = READBACK_IDLE;
    NvGpuTimestamps result = {.ok = 1};
    memcpy(result.nanoseconds, backend.timestamp_values, sizeof(result.nanoseconds));
    return result;
}

internal void readback_map_done(WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void* userdata2)
{
    (void)message, (void)userdata2;
    Readback* readback = userdata1;
    if (status == WGPUMapAsyncStatus_Success) {
        readback->mapped = wgpuBufferGetConstMappedRange(readback->buffer, 0, readback->size);
        readback->state = readback->mapped ? READBACK_READY : READBACK_IDLE;
    } else {
        nv_log(NV_LOG_ERROR, "wgpu", "reading a buffer back failed (%d)", (int)status);
        readback->state = READBACK_IDLE;
    }
}

b32 nv_gpu_copy_for_readback(NvGpu* gpu, u32 slot, NvGpuBuffer source, u64 offset, umm size)
{
    (void)gpu;
    NV_ASSERT(slot < NV_GPU_MAX_READBACKS && !backend.render_pass && !backend.compute_pass);
    Readback* readback = &backend.readbacks[slot];
    if (readback->state != READBACK_IDLE && readback->state != READBACK_TAKEN)
        return 0;
    if (readback->state == READBACK_TAKEN)
        wgpuBufferUnmap(readback->buffer);
    readback->state = READBACK_IDLE;
    readback->mapped = NULL;
    u64 rounded = ((u64)size + 3u) & ~(u64)3u;
    if (rounded > readback->capacity) {
        if (readback->buffer)
            wgpuBufferRelease(readback->buffer);
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.label = (WGPUStringView){"readback", WGPU_STRLEN};
        desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        desc.size = rounded;
        readback->buffer = wgpuDeviceCreateBuffer(backend.device, &desc);
        readback->capacity = rounded;
    }
    readback->size = (umm)rounded;

    WGPUCommandEncoder encoder = backend.encoder;
    if (!encoder)
        encoder = wgpuDeviceCreateCommandEncoder(backend.device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, buffer_of(source), offset, readback->buffer, 0, rounded);
    readback->state = READBACK_COPIED;
    if (!backend.encoder) {
        // Outside a frame: submit the copy at once.
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuQueueSubmit(backend.queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
        map_readbacks();
    }
    return 1;
}

NvGpuReadback nv_gpu_poll_readback(NvGpu* gpu, u32 slot)
{
    (void)gpu;
    NV_ASSERT(slot < NV_GPU_MAX_READBACKS);
    Readback* readback = &backend.readbacks[slot];
    if (readback->state != READBACK_READY)
        return (NvGpuReadback){0};
    readback->state = READBACK_TAKEN;
    return (NvGpuReadback){.ok = 1, .bytes = readback->mapped, .size = readback->size};
}

#if !defined(NDEBUG)
void nv_gpu_debug_raise_error(NvGpu* gpu)
{
    (void)gpu;
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.size = 16; // no usage: not a valid buffer
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(backend.device, &descriptor);
    if (buffer)
        wgpuBufferRelease(buffer);
}
#endif

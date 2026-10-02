#pragma once

#include "engine/base.h"

#include <webgpu/webgpu.h>

typedef struct NvWindow NvWindow;

// A region of the canvas in framebuffer pixels, with the origin at the top-left corner.
typedef struct NvRect {
    u32 x, y, width, height;
} NvRect;

// NOTE: Owns the WebGPU instance, adapter, device, queue and the canvas surface.
// A zeroed NvGpu is the valid "not created" state.
typedef struct NvGpu {
    NvWindow* window;
    WGPUInstance instance;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat surface_format; // format of the per-frame view; render pipelines target this
    WGPUTextureFormat config_format;  // format the canvas itself is configured with
    u32 width;
    u32 height;
    b32 has_timestamps; // the device can time passes (WGPUFeatureName_TimestampQuery)

    WGPUTexture current_texture;
    WGPUTextureView current_view;
} NvGpu;

// The scene is rendered in linear HDR (docs/specs/vfx.md): colors above 1 are kept until the upscale
// pass multiplies by the exposure, tone maps and writes the canvas. The scene pass has a depth target
// of NV_SCENE_DEPTH_FORMAT, reverse Z.
#define NV_SCENE_FORMAT WGPUTextureFormat_RGBA16Float
#define NV_SCENE_DEPTH_FORMAT WGPUTextureFormat_Depth32Float

// A texture format's name for logs ("BGRA8Unorm"), or its number when it is not one nv uses.
const char* nv_gpu_format_name(WGPUTextureFormat format);

// Bytes per texel of the formats nv uses; 0 for others.
static inline u32 nv_gpu_format_bytes(WGPUTextureFormat format)
{
    switch (format) {
    case WGPUTextureFormat_BGRA8Unorm:
    case WGPUTextureFormat_BGRA8UnormSrgb:
    case WGPUTextureFormat_RGBA8Unorm:
    case WGPUTextureFormat_RGBA8UnormSrgb:
    case WGPUTextureFormat_Depth32Float: return 4;
    case WGPUTextureFormat_RGBA16Float:  return 8;
    case WGPUTextureFormat_Depth16Unorm: return 2;
    default:                             return 0;
    }
}

// Bytes a texture takes: every mip level's texels (each level half the last, at least 1x1).
static inline u64 nv_gpu_texture_bytes(u32 width, u32 height, u32 mip_count, WGPUTextureFormat format)
{
    u64 total = 0;
    for (u32 level = 0; level < mip_count; ++level) {
        total += (u64)width * height * nv_gpu_format_bytes(format);
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    return total;
}

// NOTE: Fails when the browser has no usable WebGPU adapter or device. Logs (info) the CPU, GPU,
// display and swapchain it found; the swapchain again whenever the canvas is resized.
b32 nv_gpu_create(NvGpu* gpu, NvWindow* window);

// NOTE: Acquires this frame's canvas texture view. Returns NULL when the frame should be skipped.
WGPUTextureView nv_gpu_begin_frame(NvGpu* gpu);
void nv_gpu_end_frame(NvGpu* gpu);

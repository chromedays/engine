#pragma once

#include "engine/base.h"
#include "engine/window.h"

#include <webgpu/webgpu.h>

// The texture formats nv uses. Zero is "none". The backend turns them into its API's own formats.
typedef enum NvGpuFormat {
    NV_GPU_FORMAT_NONE,
    NV_GPU_FORMAT_BGRA8,
    NV_GPU_FORMAT_BGRA8_SRGB,
    NV_GPU_FORMAT_RGBA8,
    NV_GPU_FORMAT_RGBA8_SRGB,
    NV_GPU_FORMAT_RGBA16F,
    NV_GPU_FORMAT_DEPTH32F,
    NV_GPU_FORMAT_DEPTH16,
} NvGpuFormat;

// NOTE: Owns the WebGPU instance, adapter, device, queue and the canvas surface.
// A zeroed NvGpu is the valid "not created" state.
typedef struct NvGpu {
    NvWindow* window;
    WGPUInstance instance;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    NvGpuFormat surface_format; // format of the per-frame view; render pipelines target this
    NvGpuFormat config_format;  // format the canvas itself is configured with
    u32 width;
    u32 height;
    b32 has_timestamps; // the device can time passes (WGPUFeatureName_TimestampQuery)

    WGPUTexture current_texture;
    WGPUTextureView current_view;
} NvGpu;

// The scene is rendered in linear HDR (docs/specs/vfx.md): colors above 1 are kept until the upscale
// pass multiplies by the exposure, tone maps and writes the canvas. The scene pass has a depth target
// of NV_SCENE_DEPTH_FORMAT, reverse Z.
#define NV_SCENE_FORMAT NV_GPU_FORMAT_RGBA16F
#define NV_SCENE_DEPTH_FORMAT NV_GPU_FORMAT_DEPTH32F

// A texture format's name for logs ("BGRA8Unorm").
const char* nv_gpu_format_name(NvGpuFormat format);

// The WebGPU format of `format`, and back (NV_GPU_FORMAT_NONE for one nv does not use). They go away from this header when the
// WebGPU calls move into the backend.
WGPUTextureFormat nv_gpu_wgpu_format(NvGpuFormat format);
NvGpuFormat nv_gpu_format_from_wgpu(WGPUTextureFormat format);

// Bytes per texel of the formats nv uses; 0 for none.
static inline u32 nv_gpu_format_bytes(NvGpuFormat format)
{
    switch (format) {
    case NV_GPU_FORMAT_BGRA8:
    case NV_GPU_FORMAT_BGRA8_SRGB:
    case NV_GPU_FORMAT_RGBA8:
    case NV_GPU_FORMAT_RGBA8_SRGB:
    case NV_GPU_FORMAT_DEPTH32F: return 4;
    case NV_GPU_FORMAT_RGBA16F:  return 8;
    case NV_GPU_FORMAT_DEPTH16:  return 2;
    default:                     return 0;
    }
}

// Bytes a texture takes: every mip level's texels (each level half the last, at least 1x1).
static inline u64 nv_gpu_texture_bytes(u32 width, u32 height, u32 mip_count, NvGpuFormat format)
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

// NOTE: Acquires this frame's canvas texture view. Fails when the frame should be skipped.
typedef struct NvGpuFrame {
    b32 ok;
    WGPUTextureView view;
} NvGpuFrame;
NvGpuFrame nv_gpu_begin_frame(NvGpu* gpu);
void nv_gpu_end_frame(NvGpu* gpu);

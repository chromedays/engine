#pragma once

#include "nv/base.h"

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

// NOTE: Fails when the browser has no usable WebGPU adapter or device.
b32 nv_gpu_create(NvGpu* gpu, NvWindow* window);

// NOTE: Acquires this frame's canvas texture view. Returns NULL when the frame should be skipped.
WGPUTextureView nv_gpu_begin_frame(NvGpu* gpu);
void nv_gpu_end_frame(NvGpu* gpu);

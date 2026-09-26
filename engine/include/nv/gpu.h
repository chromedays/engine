#pragma once

#include <stdbool.h>
#include <webgpu/webgpu.h>

typedef struct NvWindow NvWindow;

/* Owns the WebGPU instance, adapter, device, queue and the window surface. */
typedef struct NvGpu {
    NvWindow* window;
    WGPUInstance instance;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat surface_format; /* format of the per-frame view; render pipelines target this */
    WGPUTextureFormat config_format;  /* format the surface itself is configured with */
    uint32_t width;
    uint32_t height;

    WGPUTexture current_texture;
    WGPUTextureView current_view;
} NvGpu;

bool nv_gpu_create(NvGpu* gpu, NvWindow* window);
void nv_gpu_destroy(NvGpu* gpu);

/* Reconfigures the surface when the framebuffer size changed. */
void nv_gpu_resize(NvGpu* gpu, uint32_t width, uint32_t height);

/* Acquires the next swapchain texture view. Returns NULL if the frame should be skipped. */
WGPUTextureView nv_gpu_begin_frame(NvGpu* gpu);
void nv_gpu_end_frame(NvGpu* gpu);

#pragma once

#include "nv/gpu.h"

#include <cimgui.h>

#define NV_IMGUI_MAX_TEXTURES 16
#define NV_IMGUI_MAX_VERTICES 131072
#define NV_IMGUI_MAX_INDICES  262144
#define NV_IMGUI_CLIPBOARD_SIZE 65536

typedef struct NvImguiTexture {
    WGPUTexture texture;
    WGPUTextureView view;
    WGPUBindGroup bind_group;
} NvImguiTexture;

// Dear ImGui on the canvas: browser input in, WebGPU draw calls out. Build UI with the cimgui
// API (ig* functions) between nv_imgui_new_frame and nv_imgui_render.
typedef struct NvImgui {
    NvGpu* gpu;
    NvWindow* window;

    WGPURenderPipeline pipeline;
    WGPUBuffer uniform_buffer;
    WGPUSampler sampler;
    WGPUBuffer vertex_buffer;
    WGPUBuffer index_buffer;

    // Slot 0 is unused so that ImTextureID 0 keeps meaning "no texture".
    NvImguiTexture textures[NV_IMGUI_MAX_TEXTURES];

    // CPU copies of one frame's geometry, uploaded with one write per buffer.
    ImDrawVert* vertices; // [NV_IMGUI_MAX_VERTICES]
    ImDrawIdx* indices;   // [NV_IMGUI_MAX_INDICES]

    // Text ImGui copies, or the browser last pasted; NUL-terminated, truncated to fit.
    char* clipboard; // [NV_IMGUI_CLIPBOARD_SIZE]

    // 1 with a mouse; larger on touch screens so text and hit areas suit fingers.
    f32 ui_scale;
} NvImgui;

// Creates the ImGui context, hooks browser input and creates GPU objects. Staging memory for
// geometry comes from `arena`.
void nv_imgui_init(NvImgui* imgui, NvGpu* gpu, NvWindow* window, NvArena* arena);

void nv_imgui_new_frame(NvImgui* imgui, f32 delta_seconds);

// Finishes the ImGui frame and records a render pass that draws it over `target`.
void nv_imgui_render(NvImgui* imgui, WGPUCommandEncoder encoder, WGPUTextureView target);

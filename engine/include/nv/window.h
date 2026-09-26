#pragma once

#include "nv/base.h"

// NOTE: The window is the page's <canvas>; its size follows the canvas's CSS layout.
typedef struct NvWindow {
    const char* canvas_selector; // CSS selector of the <canvas> element, e.g. "#canvas"
    f32 pixel_ratio;             // device pixels per CSS pixel, updated by nv_window_framebuffer_size
} NvWindow;

void nv_window_create(NvWindow* window, const char* title);

// NOTE: Returns the canvas size in device pixels and resizes its backing store to match.
void nv_window_framebuffer_size(NvWindow* window, u32* width, u32* height);

// Seconds since the page loaded.
f64 nv_time_seconds(void);

typedef void (*NvFrameFn)(void* userdata);

// IMPORTANT: Hands control to the browser's requestAnimationFrame loop and never returns, so any
// state `frame` uses must not live on the caller's stack.
void nv_window_run(NvWindow* window, NvFrameFn frame, void* userdata);

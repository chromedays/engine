#pragma once

#include "engine/base.h"

// NOTE: The window is the page's <canvas>; its size follows the canvas's CSS layout.
typedef struct NvWindow {
    const char* canvas_selector; // CSS selector of the <canvas> element, e.g. "#canvas"
    f32 pixel_ratio;             // device pixels per CSS pixel, updated by nv_window_framebuffer_size
    void (*on_hidden)(void* userdata);
    void* on_hidden_data;
} NvWindow;

void nv_window_create(NvWindow* window, const char* title);

// Device pixels per CSS pixel; 1 until the first nv_window_framebuffer_size has told us.
static inline f32 nv_window_pixel_ratio(const NvWindow* window)
{
    return window->pixel_ratio > 0.0f ? window->pixel_ratio : 1.0f;
}

// NOTE: Returns the canvas size in device pixels and resizes its backing store to match.
void nv_window_framebuffer_size(NvWindow* window, u32* width, u32* height);

// What the page downloaded, for a build label: "3.2 MB downloaded", or "3.2 MB from cache" (the bytes after the browser unpacked
// them). From the page's resource timing entries of the executable's own files (<target>.wasm, .data and .js; the page names
// the target in Module.nvTarget). Empty when the browser does not say.
void nv_window_download_text(char* out, umm capacity);

// Seconds since the page loaded.
f64 nv_time_seconds(void);

// Calls `hidden` when the page is hidden: another tab, the app switcher, the screen turning off,
// or the page closing. Frames stop while it is hidden, so this is the last moment to save.
void nv_window_on_hidden(NvWindow* window, void (*hidden)(void* userdata), void* userdata);

typedef void (*NvFrameFn)(void* userdata);

// IMPORTANT: Hands control to the browser's requestAnimationFrame loop and never returns, so any
// state `frame` uses must not live on the caller's stack.
void nv_window_run(NvWindow* window, NvFrameFn frame, void* userdata);

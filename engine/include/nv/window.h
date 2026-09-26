#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct GLFWwindow GLFWwindow;

typedef struct NvWindow {
#if defined(__EMSCRIPTEN__)
    const char* canvas_selector; /* CSS selector of the <canvas> element, e.g. "#canvas" */
#else
    GLFWwindow* handle;
#endif
} NvWindow;

/* On the web, `title` sets the document title and the size is taken from the canvas's CSS layout. */
bool nv_window_create(NvWindow* window, const char* title, uint32_t width, uint32_t height);
void nv_window_destroy(NvWindow* window);

/* Framebuffer size in pixels (may differ from window size on HiDPI displays). */
void nv_window_framebuffer_size(const NvWindow* window, uint32_t* width, uint32_t* height);

typedef void (*NvFrameFn)(void* userdata);

/*
 * Runs `frame` once per display refresh until the window is closed.
 * Native: returns after the window closes.
 * Web: hands control to the browser's requestAnimationFrame loop and never returns, so any state
 * `frame` uses must not live on the caller's stack.
 */
void nv_window_run(NvWindow* window, NvFrameFn frame, void* userdata);

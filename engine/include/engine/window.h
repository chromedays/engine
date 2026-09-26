#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct GLFWwindow GLFWwindow;

typedef struct EngWindow {
#if defined(__EMSCRIPTEN__)
    const char* canvas_selector; /* CSS selector of the <canvas> element, e.g. "#canvas" */
#else
    GLFWwindow* handle;
#endif
} EngWindow;

/* On the web, `title` sets the document title and the size is taken from the canvas's CSS layout. */
bool eng_window_create(EngWindow* window, const char* title, uint32_t width, uint32_t height);
void eng_window_destroy(EngWindow* window);

/* Framebuffer size in pixels (may differ from window size on HiDPI displays). */
void eng_window_framebuffer_size(const EngWindow* window, uint32_t* width, uint32_t* height);

typedef void (*EngFrameFn)(void* userdata);

/*
 * Runs `frame` once per display refresh until the window is closed.
 * Native: returns after the window closes.
 * Web: hands control to the browser's requestAnimationFrame loop and never returns, so any state
 * `frame` uses must not live on the caller's stack.
 */
void eng_window_run(EngWindow* window, EngFrameFn frame, void* userdata);

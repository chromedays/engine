#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct GLFWwindow GLFWwindow;

typedef struct EngWindow {
    GLFWwindow* handle;
} EngWindow;

bool eng_window_create(EngWindow* window, const char* title, uint32_t width, uint32_t height);
void eng_window_destroy(EngWindow* window);

bool eng_window_should_close(const EngWindow* window);
void eng_window_poll_events(EngWindow* window);

/* Framebuffer size in pixels (may differ from window size on HiDPI displays). */
void eng_window_framebuffer_size(const EngWindow* window, uint32_t* width, uint32_t* height);

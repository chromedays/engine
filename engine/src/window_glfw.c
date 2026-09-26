#include "engine/window.h"

#include <GLFW/glfw3.h>

#include <stdio.h>

static void on_glfw_error(int code, const char* desc)
{
    fprintf(stderr, "[glfw] error %d: %s\n", code, desc);
}

bool eng_window_create(EngWindow* window, const char* title, uint32_t width, uint32_t height)
{
    window->handle = NULL;

    glfwSetErrorCallback(on_glfw_error);
    if (!glfwInit())
        return false;

    /* WebGPU manages the swapchain; GLFW must not create a GL context. */
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window->handle = glfwCreateWindow((int)width, (int)height, title, NULL, NULL);
    if (!window->handle) {
        glfwTerminate();
        return false;
    }
    return true;
}

void eng_window_destroy(EngWindow* window)
{
    if (window->handle) {
        glfwDestroyWindow(window->handle);
        glfwTerminate();
        window->handle = NULL;
    }
}

void eng_window_framebuffer_size(const EngWindow* window, uint32_t* width, uint32_t* height)
{
    int w = 0, h = 0;
    glfwGetFramebufferSize(window->handle, &w, &h);
    *width = (uint32_t)w;
    *height = (uint32_t)h;
}

void eng_window_run(EngWindow* window, EngFrameFn frame, void* userdata)
{
    while (!glfwWindowShouldClose(window->handle)) {
        glfwPollEvents();
        frame(userdata);
    }
}

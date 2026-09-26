#include "engine/window.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <math.h>

bool eng_window_create(EngWindow* window, const char* title, uint32_t width, uint32_t height)
{
    (void)width, (void)height;
    window->canvas_selector = "#canvas";
    emscripten_set_window_title(title);
    return true;
}

void eng_window_destroy(EngWindow* window)
{
    (void)window;
}

void eng_window_framebuffer_size(const EngWindow* window, uint32_t* width, uint32_t* height)
{
    /* Keep the canvas backing store matched to its displayed size in device pixels. */
    double css_w = 0.0, css_h = 0.0;
    emscripten_get_element_css_size(window->canvas_selector, &css_w, &css_h);
    const double dpr = emscripten_get_device_pixel_ratio();
    const int w = (int)lround(css_w * dpr);
    const int h = (int)lround(css_h * dpr);

    int cur_w = 0, cur_h = 0;
    emscripten_get_canvas_element_size(window->canvas_selector, &cur_w, &cur_h);
    if (cur_w != w || cur_h != h)
        emscripten_set_canvas_element_size(window->canvas_selector, w, h);

    *width = (uint32_t)w;
    *height = (uint32_t)h;
}

void eng_window_run(EngWindow* window, EngFrameFn frame, void* userdata)
{
    (void)window;
    emscripten_set_main_loop_arg(frame, userdata, 0, true);
}

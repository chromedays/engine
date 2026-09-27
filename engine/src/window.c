#include "nv/window.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <math.h>

void nv_window_create(NvWindow* window, const char* title)
{
    window->canvas_selector = "#canvas";
    emscripten_set_window_title(title);
}

void nv_window_framebuffer_size(NvWindow* window, u32* width, u32* height)
{
    f64 css_width = 0.0;
    f64 css_height = 0.0;
    emscripten_get_element_css_size(window->canvas_selector, &css_width, &css_height);
    f64 pixel_ratio = emscripten_get_device_pixel_ratio();
    window->pixel_ratio = (f32)pixel_ratio;
    s32 new_width = (s32)lround(css_width * pixel_ratio);
    s32 new_height = (s32)lround(css_height * pixel_ratio);

    s32 old_width = 0;
    s32 old_height = 0;
    emscripten_get_canvas_element_size(window->canvas_selector, &old_width, &old_height);
    if (old_width != new_width || old_height != new_height)
        emscripten_set_canvas_element_size(window->canvas_selector, new_width, new_height);

    *width = (u32)new_width;
    *height = (u32)new_height;
}

f64 nv_time_seconds(void)
{
    return emscripten_get_now() / 1000.0;
}

// Tells the page that loading is over, so it can take down its progress screen.
EM_JS(void, js_notify_running, (void), {
    if (Module["nvRunning"]) Module["nvRunning"]();
});

void nv_window_run(NvWindow* window, NvFrameFn frame, void* userdata)
{
    (void)window;
    js_notify_running();
    emscripten_set_main_loop_arg(frame, userdata, 0, 1);
}

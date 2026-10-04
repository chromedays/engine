#include "engine/window.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <math.h>
#include <stdio.h>

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

// `what` 0 is the bytes after the browser unpacked the files, 1 the bytes that crossed the network (0 from the cache); -1 when
// the browser does not say.
EM_JS(double, js_download_bytes, (int what), {
    const target = Module["nvTarget"];
    const names = [target + ".wasm", target + ".data", target + ".js"];
    let decoded = 0, sent = 0, found = 0;
    for (const entry of performance.getEntriesByType("resource")) {
        const path = new URL(entry.name).pathname;
        if (!names.some((name) => path.endsWith("/" + name))) continue;
        ++found;
        decoded += entry.decodedBodySize;
        sent += entry.transferSize;
    }
    if (!found || !decoded) return -1;
    return what == 0 ? decoded : sent;
});

void nv_window_download_text(char* out, umm capacity)
{
    f64 unpacked = js_download_bytes(0), sent = js_download_bytes(1);
    f64 mb = 1048576.0;
    if (unpacked < 0.0)
        out[0] = 0;
    else if (sent > 0.0)
        snprintf(out, capacity, "%.1f MB downloaded", sent / mb);
    else
        snprintf(out, capacity, "%.1f MB from cache", unpacked / mb);
}

f64 nv_time_seconds(void)
{
    return emscripten_get_now() / 1000.0;
}

internal bool on_visibility(int event_type, const EmscriptenVisibilityChangeEvent* event, void* userdata)
{
    (void)event_type;
    NvWindow* window = userdata;
    if (event->hidden && window->on_hidden)
        window->on_hidden(window->on_hidden_data);
    return 0;
}

void nv_window_on_hidden(NvWindow* window, void (*hidden)(void* userdata), void* userdata)
{
    window->on_hidden = hidden;
    window->on_hidden_data = userdata;
    emscripten_set_visibilitychange_callback(window, 1, on_visibility);
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

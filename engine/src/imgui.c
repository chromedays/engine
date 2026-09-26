#include "nv/imgui.h"
#include "nv/math.h"
#include "nv/window.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <string.h>

// NOTE: imgui.h spells this as a macro that cimgui does not carry over.
#define IMGUI_RESET_RENDER_STATE ((ImDrawCallback)(intptr_t)-8)

// Vertex colors are authored in sRGB, but the canvas view is sRGB-encoding, so they are
// linearized here to avoid applying the curve twice.
global const char* imgui_shader =
    "struct Uniforms {\n"
    "    mvp: mat4x4f,\n"
    "};\n"
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
    "@group(0) @binding(1) var s: sampler;\n"
    "@group(0) @binding(2) var t: texture_2d<f32>;\n"
    "\n"
    "struct VsIn {\n"
    "    @location(0) pos: vec2f,\n"
    "    @location(1) uv: vec2f,\n"
    "    @location(2) col: vec4f,\n"
    "};\n"
    "struct VsOut {\n"
    "    @builtin(position) pos: vec4f,\n"
    "    @location(0) uv: vec2f,\n"
    "    @location(1) col: vec4f,\n"
    "};\n"
    "\n"
    "@vertex\n"
    "fn vs_main(in: VsIn) -> VsOut {\n"
    "    var out: VsOut;\n"
    "    out.pos = u.mvp * vec4f(in.pos, 0.0, 1.0);\n"
    "    out.uv = in.uv;\n"
    "    out.col = vec4f(pow(in.col.rgb, vec3f(2.2)), in.col.a);\n"
    "    return out;\n"
    "}\n"
    "\n"
    "@fragment\n"
    "fn fs_main(in: VsOut) -> @location(0) vec4f {\n"
    "    return in.col * textureSample(t, s, in.uv);\n"
    "}\n";

//
// Browser glue
//

// NOTE: Phones only show their on-screen keyboard for a focused text field, and a canvas is not
// one. A hidden <input> (the "text agent") takes focus while ImGui edits text; what the keyboard
// types arrives as input events and is forwarded to ImGui. The agent always holds one sentinel
// space, so a Backspace on an otherwise empty field is still visible as a shorter value.
EM_JS_DEPS(nv_imgui, "$stringToUTF8,$UTF8ToString");

EM_JS(void, js_setup_text_agent, (char* clipboard, int clipboard_size), {
    const agent = document.createElement("input");
    agent.id = "nv-text-agent";
    agent.type = "text";
    agent.setAttribute("autocomplete", "off");
    agent.setAttribute("autocapitalize", "off");
    agent.setAttribute("autocorrect", "off");
    agent.spellcheck = false;
    // 16px keeps iOS from zooming the page when the field gains focus.
    agent.style.cssText = "position:fixed;left:0;top:0;width:1px;height:1px;opacity:0;border:0;" +
                          "padding:0;font-size:16px;pointer-events:none;";
    document.body.appendChild(agent);

    const reset = () => { agent.value = " "; agent.setSelectionRange(1, 1); };
    const flush = () => {
        const value = agent.value;
        if (value.length < 1) {
            _nv_imgui_js_backspace();
        } else {
            for (const ch of value.slice(1)) _nv_imgui_js_char(ch.codePointAt(0));
        }
        reset();
    };
    let composing = false;
    agent.addEventListener("focus", reset);
    agent.addEventListener("compositionstart", () => { composing = true; });
    agent.addEventListener("compositionend", () => { composing = false; flush(); });
    agent.addEventListener("input", () => { if (!composing) flush(); });

    // Keyboard pastes land here; pastes into the agent (long-press menu) arrive as input instead.
    window.addEventListener("paste", (event) => {
        if (event.target === agent) return;
        const text = event.clipboardData ? event.clipboardData.getData("text/plain") : "";
        stringToUTF8(text, clipboard, clipboard_size);
        event.preventDefault();
        _nv_imgui_js_paste();
    });
});

EM_JS(int, js_text_agent_focused, (void), {
    return document.activeElement === document.getElementById("nv-text-agent") ? 1 : 0;
});

EM_JS(void, js_focus_text_agent, (int focus), {
    const agent = document.getElementById("nv-text-agent");
    if (focus) agent.focus({preventScroll: true});
    else agent.blur();
});

EM_JS(void, js_write_clipboard, (const char* text), {
    if (navigator.clipboard) navigator.clipboard.writeText(UTF8ToString(text)).catch(() => {});
});

EM_JS(int, js_touch_is_primary, (void), {
    return matchMedia("(pointer: coarse)").matches ? 1 : 0;
});

// Entry points for the JavaScript above; not part of the public API.
EMSCRIPTEN_KEEPALIVE void nv_imgui_js_char(u32 codepoint)
{
    ImGuiIO_AddInputCharacter(igGetIO_Nil(), codepoint);
}

EMSCRIPTEN_KEEPALIVE void nv_imgui_js_backspace(void)
{
    ImGuiIO* io = igGetIO_Nil();
    ImGuiIO_AddKeyEvent(io, ImGuiKey_Backspace, 1);
    ImGuiIO_AddKeyEvent(io, ImGuiKey_Backspace, 0);
}

// The pasted text is already in the clipboard buffer; replaying Ctrl+V makes ImGui read it.
EMSCRIPTEN_KEEPALIVE void nv_imgui_js_paste(void)
{
    ImGuiIO* io = igGetIO_Nil();
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Ctrl, 1);
    ImGuiIO_AddKeyEvent(io, ImGuiKey_V, 1);
    ImGuiIO_AddKeyEvent(io, ImGuiKey_V, 0);
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Ctrl, 0);
}

internal const char* get_clipboard(ImGuiContext* context)
{
    (void)context;
    NvImgui* imgui = igGetPlatformIO_Nil()->Platform_ClipboardUserData;
    return imgui->clipboard;
}

internal void set_clipboard(ImGuiContext* context, const char* text)
{
    (void)context;
    NvImgui* imgui = igGetPlatformIO_Nil()->Platform_ClipboardUserData;
    umm length = strlen(text);
    if (length > NV_IMGUI_CLIPBOARD_SIZE - 1)
        length = NV_IMGUI_CLIPBOARD_SIZE - 1;
    memcpy(imgui->clipboard, text, length);
    imgui->clipboard[length] = 0;
    // NOTE: Best effort: browsers may refuse writes that are not close to a user gesture.
    js_write_clipboard(imgui->clipboard);
}

//
// Input
//

// NOTE: Mouse and touch positions use the page's client coordinates, which equal canvas
// coordinates because the page pins the canvas to the top-left corner of the viewport.

typedef struct KeyName {
    const char* code;
    ImGuiKey key;
} KeyName;

global const KeyName named_keys[] = {
    {"Tab", ImGuiKey_Tab},
    {"ArrowLeft", ImGuiKey_LeftArrow},
    {"ArrowRight", ImGuiKey_RightArrow},
    {"ArrowUp", ImGuiKey_UpArrow},
    {"ArrowDown", ImGuiKey_DownArrow},
    {"PageUp", ImGuiKey_PageUp},
    {"PageDown", ImGuiKey_PageDown},
    {"Home", ImGuiKey_Home},
    {"End", ImGuiKey_End},
    {"Insert", ImGuiKey_Insert},
    {"Delete", ImGuiKey_Delete},
    {"Backspace", ImGuiKey_Backspace},
    {"Space", ImGuiKey_Space},
    {"Enter", ImGuiKey_Enter},
    {"NumpadEnter", ImGuiKey_KeypadEnter},
    {"Escape", ImGuiKey_Escape},
    {"ControlLeft", ImGuiKey_LeftCtrl},
    {"ShiftLeft", ImGuiKey_LeftShift},
    {"AltLeft", ImGuiKey_LeftAlt},
    {"MetaLeft", ImGuiKey_LeftSuper},
    {"ControlRight", ImGuiKey_RightCtrl},
    {"ShiftRight", ImGuiKey_RightShift},
    {"AltRight", ImGuiKey_RightAlt},
    {"MetaRight", ImGuiKey_RightSuper},
    {"Quote", ImGuiKey_Apostrophe},
    {"Comma", ImGuiKey_Comma},
    {"Minus", ImGuiKey_Minus},
    {"Period", ImGuiKey_Period},
    {"Slash", ImGuiKey_Slash},
    {"Semicolon", ImGuiKey_Semicolon},
    {"Equal", ImGuiKey_Equal},
    {"BracketLeft", ImGuiKey_LeftBracket},
    {"Backslash", ImGuiKey_Backslash},
    {"BracketRight", ImGuiKey_RightBracket},
    {"Backquote", ImGuiKey_GraveAccent},
};

// Maps a DOM KeyboardEvent.code (the physical key) to an ImGuiKey.
internal ImGuiKey key_from_code(const char* code)
{
    umm length = strlen(code);
    if (length == 4 && memcmp(code, "Key", 3) == 0 && code[3] >= 'A' && code[3] <= 'Z')
        return (ImGuiKey)(ImGuiKey_A + (code[3] - 'A'));
    if (length == 6 && memcmp(code, "Digit", 5) == 0 && code[5] >= '0' && code[5] <= '9')
        return (ImGuiKey)(ImGuiKey_0 + (code[5] - '0'));
    if (length == 7 && memcmp(code, "Numpad", 6) == 0 && code[6] >= '0' && code[6] <= '9')
        return (ImGuiKey)(ImGuiKey_Keypad0 + (code[6] - '0'));
    if ((length == 2 || length == 3) && code[0] == 'F' && code[1] >= '1' && code[1] <= '9') {
        s32 n = (length == 2) ? code[1] - '0' : (code[1] - '0') * 10 + (code[2] - '0');
        if (n >= 1 && n <= 12)
            return (ImGuiKey)(ImGuiKey_F1 + (n - 1));
    }
    for (umm i = 0; i < NV_ARRAY_COUNT(named_keys); ++i) {
        if (strcmp(code, named_keys[i].code) == 0)
            return named_keys[i].key;
    }
    return ImGuiKey_None;
}

// True when `text` is exactly one UTF-8 encoded character, which is how KeyboardEvent.key
// reports printable keys ("a", "A", "é", " "), as opposed to names like "Enter".
internal b32 is_single_character(const char* text)
{
    u8 lead = (u8)text[0];
    umm expected = (lead < 0x80) ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
    return expected && strlen(text) == expected;
}

// NOTE: Cmd (metaKey) is reported as Ctrl so macOS shortcuts (Cmd+C, Cmd+V, Cmd+A) work.
internal void add_modifiers(ImGuiIO* io, const EmscriptenKeyboardEvent* event)
{
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Ctrl, event->ctrlKey || event->metaKey);
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Shift, event->shiftKey);
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Alt, event->altKey);
}

// Browser callbacks return true to stop the browser's default handling. Return values and
// parameter types are fixed by Emscripten's html5.h.
internal bool on_key(int event_type, const EmscriptenKeyboardEvent* event, void* userdata)
{
    (void)userdata;
    ImGuiIO* io = igGetIO_Nil();
    b32 down = (event_type == EMSCRIPTEN_EVENT_KEYDOWN);
    b32 shortcut = event->ctrlKey || event->metaKey;
    add_modifiers(io, event);

    // On-screen keyboards often leave `code` empty, but name special keys in `key`.
    ImGuiKey key = key_from_code(event->code);
    if (key == ImGuiKey_None)
        key = key_from_code(event->key);

    // Leave paste to the browser: it fires a paste event carrying the text (see the glue above).
    if (shortcut && key == ImGuiKey_V)
        return 0;

    // While the text agent has focus, its input events carry text and Backspace; taking them
    // from keydown too would apply them twice. The agent needs the default action to see them.
    if (js_text_agent_focused()) {
        if (key != ImGuiKey_None && key != ImGuiKey_Backspace)
            ImGuiIO_AddKeyEvent(io, key, down);
        return 0;
    }

    if (key != ImGuiKey_None)
        ImGuiIO_AddKeyEvent(io, key, down);

    // NOTE: Text is taken from keydown instead of keypress so keydown can be consumed below
    // (consuming keydown suppresses keypress).
    if (down && !shortcut && is_single_character(event->key))
        ImGuiIO_AddInputCharactersUTF8(io, event->key);

    return io->WantCaptureKeyboard;
}

internal bool on_mouse(int event_type, const EmscriptenMouseEvent* event, void* userdata)
{
    (void)userdata;
    ImGuiIO* io = igGetIO_Nil();
    ImGuiIO_AddMouseSourceEvent(io, ImGuiMouseSource_Mouse);
    ImGuiIO_AddMousePosEvent(io, (f32)event->clientX, (f32)event->clientY);

    if (event_type == EMSCRIPTEN_EVENT_MOUSEDOWN || event_type == EMSCRIPTEN_EVENT_MOUSEUP) {
        // DOM order is left, middle, right; ImGui's is left, right, middle.
        local_persist const s32 buttons[3] = {0, 2, 1};
        if (event->button < 3)
            ImGuiIO_AddMouseButtonEvent(io, buttons[event->button], event_type == EMSCRIPTEN_EVENT_MOUSEDOWN);
    }
    return event_type != EMSCRIPTEN_EVENT_MOUSEMOVE && io->WantCaptureMouse;
}

internal bool on_wheel(int event_type, const EmscriptenWheelEvent* event, void* userdata)
{
    (void)event_type, (void)userdata;
    ImGuiIO* io = igGetIO_Nil();
    // ImGui counts in lines; browsers usually report pixels.
    f32 scale = (event->deltaMode == DOM_DELTA_PIXEL) ? 1.0f / 100.0f : (event->deltaMode == DOM_DELTA_LINE) ? 1.0f / 3.0f : 1.0f;
    ImGuiIO_AddMouseWheelEvent(io, (f32)-event->deltaX * scale, (f32)-event->deltaY * scale);
    return io->WantCaptureMouse;
}

// Touch drives ImGui's mouse with the first finger.
internal bool on_touch(int event_type, const EmscriptenTouchEvent* event, void* userdata)
{
    (void)userdata;
    if (event->numTouches < 1)
        return 0;
    ImGuiIO* io = igGetIO_Nil();
    const EmscriptenTouchPoint* touch = &event->touches[0];
    ImGuiIO_AddMouseSourceEvent(io, ImGuiMouseSource_TouchScreen);
    ImGuiIO_AddMousePosEvent(io, (f32)touch->clientX, (f32)touch->clientY);
    if (event_type == EMSCRIPTEN_EVENT_TOUCHSTART) {
        ImGuiIO_AddMouseButtonEvent(io, 0, 1);
    } else if (event_type == EMSCRIPTEN_EVENT_TOUCHEND || event_type == EMSCRIPTEN_EVENT_TOUCHCANCEL) {
        ImGuiIO_AddMouseButtonEvent(io, 0, 0);
        // IMPORTANT: iOS only opens the keyboard for focus() called inside a touch handler. The
        // press has already been through a frame, so ImGui knows whether a text field took it.
        if (io->WantTextInput)
            js_focus_text_agent(1);
    }
    // Consuming touches stops the browser from also sending emulated mouse events.
    return 1;
}

internal bool on_focus(int event_type, const EmscriptenFocusEvent* event, void* userdata)
{
    (void)event, (void)userdata;
    ImGuiIO_AddFocusEvent(igGetIO_Nil(), event_type == EMSCRIPTEN_EVENT_FOCUS);
    return 0;
}

internal void hook_input(NvWindow* window)
{
    const char* page = EMSCRIPTEN_EVENT_TARGET_WINDOW;
    emscripten_set_keydown_callback(page, NULL, 1, on_key);
    emscripten_set_keyup_callback(page, NULL, 1, on_key);
    emscripten_set_mousedown_callback(window->canvas_selector, NULL, 1, on_mouse);
    // Moves and releases are watched page-wide so a drag keeps working past the canvas edge.
    emscripten_set_mousemove_callback(page, NULL, 1, on_mouse);
    emscripten_set_mouseup_callback(page, NULL, 1, on_mouse);
    emscripten_set_wheel_callback(window->canvas_selector, NULL, 1, on_wheel);
    emscripten_set_touchstart_callback(window->canvas_selector, NULL, 1, on_touch);
    emscripten_set_touchmove_callback(window->canvas_selector, NULL, 1, on_touch);
    emscripten_set_touchend_callback(window->canvas_selector, NULL, 1, on_touch);
    emscripten_set_touchcancel_callback(window->canvas_selector, NULL, 1, on_touch);
    emscripten_set_focus_callback(page, NULL, 1, on_focus);
    emscripten_set_blur_callback(page, NULL, 1, on_focus);
}

//
// Rendering
//

internal WGPURenderPipeline create_pipeline(WGPUDevice device, WGPUTextureFormat format)
{
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){imgui_shader, WGPU_STRLEN};
    WGPUShaderModuleDescriptor module_desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    module_desc.nextInChain = &wgsl.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &module_desc);

    WGPUVertexAttribute attributes[3] = {
        {.format = WGPUVertexFormat_Float32x2, .offset = offsetof(ImDrawVert, pos), .shaderLocation = 0},
        {.format = WGPUVertexFormat_Float32x2, .offset = offsetof(ImDrawVert, uv), .shaderLocation = 1},
        {.format = WGPUVertexFormat_Unorm8x4, .offset = offsetof(ImDrawVert, col), .shaderLocation = 2},
    };
    WGPUVertexBufferLayout vertex_layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertex_layout.stepMode = WGPUVertexStepMode_Vertex;
    vertex_layout.arrayStride = sizeof(ImDrawVert);
    vertex_layout.attributeCount = NV_ARRAY_COUNT(attributes);
    vertex_layout.attributes = attributes;

    WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

    WGPUColorTargetState color_target = WGPU_COLOR_TARGET_STATE_INIT;
    color_target.format = format;
    color_target.blend = &blend;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = (WGPUStringView){"fs_main", WGPU_STRLEN};
    fragment.targetCount = 1;
    fragment.targets = &color_target;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.label = (WGPUStringView){"imgui", WGPU_STRLEN};
    desc.vertex.module = module;
    desc.vertex.entryPoint = (WGPUStringView){"vs_main", WGPU_STRLEN};
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertex_layout;
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.primitive.cullMode = WGPUCullMode_None;
    desc.fragment = &fragment;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &desc);
    wgpuShaderModuleRelease(module);
    return pipeline;
}

internal WGPUBuffer create_buffer(WGPUDevice device, WGPUBufferUsage usage, umm size)
{
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.usage = usage | WGPUBufferUsage_CopyDst;
    desc.size = size;
    return wgpuDeviceCreateBuffer(device, &desc);
}

internal void upload_texture_rect(NvImgui* imgui, ImTextureData* tex, u32 x, u32 y, u32 w, u32 h)
{
    NvImguiTexture* slot = &imgui->textures[ImTextureData_GetTexID(tex)];
    u32 pitch = (u32)ImTextureData_GetPitch(tex);

    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = slot->texture;
    destination.origin = (WGPUOrigin3D){x, y, 0};
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.bytesPerRow = pitch;
    layout.rowsPerImage = h;
    WGPUExtent3D size = {w, h, 1};
    umm data_size = (umm)pitch * (h - 1) + (umm)w * 4;
    wgpuQueueWriteTexture(imgui->gpu->queue, &destination, ImTextureData_GetPixelsAt(tex, (int)x, (int)y),
                          data_size, &layout, &size);
}

// Serves ImGui's texture requests (font atlas creation, glyph uploads, destruction).
internal void update_texture(NvImgui* imgui, ImTextureData* tex)
{
    WGPUDevice device = imgui->gpu->device;

    if (tex->Status == ImTextureStatus_WantCreate) {
        NV_ASSERT(tex->Format == ImTextureFormat_RGBA32);
        u32 index = 1;
        while (index < NV_IMGUI_MAX_TEXTURES && imgui->textures[index].texture)
            ++index;
        NV_ASSERT(index < NV_IMGUI_MAX_TEXTURES);
        NvImguiTexture* slot = &imgui->textures[index];

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.label = (WGPUStringView){"imgui texture", WGPU_STRLEN};
        desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        desc.size = (WGPUExtent3D){(u32)tex->Width, (u32)tex->Height, 1};
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        slot->texture = wgpuDeviceCreateTexture(device, &desc);
        slot->view = wgpuTextureCreateView(slot->texture, NULL);

        WGPUBindGroupEntry entries[3] = {
            {.binding = 0, .buffer = imgui->uniform_buffer, .size = sizeof(NvMat4)},
            {.binding = 1, .sampler = imgui->sampler},
            {.binding = 2, .textureView = slot->view},
        };
        WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(imgui->pipeline, 0);
        WGPUBindGroupDescriptor group_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        group_desc.layout = layout;
        group_desc.entryCount = NV_ARRAY_COUNT(entries);
        group_desc.entries = entries;
        slot->bind_group = wgpuDeviceCreateBindGroup(device, &group_desc);
        wgpuBindGroupLayoutRelease(layout);

        ImTextureData_SetTexID(tex, index);
        upload_texture_rect(imgui, tex, 0, 0, (u32)tex->Width, (u32)tex->Height);
        ImTextureData_SetStatus(tex, ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantUpdates) {
        ImTextureRect r = tex->UpdateRect;
        upload_texture_rect(imgui, tex, r.x, r.y, r.w, r.h);
        ImTextureData_SetStatus(tex, ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
        // Waiting for an unused frame keeps the texture alive while queued draws may use it.
        NvImguiTexture* slot = &imgui->textures[ImTextureData_GetTexID(tex)];
        wgpuBindGroupRelease(slot->bind_group);
        wgpuTextureViewRelease(slot->view);
        wgpuTextureRelease(slot->texture);
        *slot = (NvImguiTexture){0};
        ImTextureData_SetTexID(tex, 0);
        ImTextureData_SetStatus(tex, ImTextureStatus_Destroyed);
    }
}

internal void set_render_state(NvImgui* imgui, WGPURenderPassEncoder pass)
{
    wgpuRenderPassEncoderSetPipeline(pass, imgui->pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, imgui->vertex_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, imgui->index_buffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
}

//
// API
//

void nv_imgui_init(NvImgui* imgui, NvGpu* gpu, NvWindow* window, NvArena* arena)
{
    _Static_assert(sizeof(ImDrawIdx) == 2, "the index buffer is bound as Uint16");
    *imgui = (NvImgui){0};
    imgui->gpu = gpu;
    imgui->window = window;
    imgui->vertices = NV_PUSH_ARRAY(arena, NV_IMGUI_MAX_VERTICES, ImDrawVert);
    imgui->indices = NV_PUSH_ARRAY(arena, NV_IMGUI_MAX_INDICES, ImDrawIdx);
    imgui->clipboard = NV_PUSH_ARRAY(arena, NV_IMGUI_CLIPBOARD_SIZE, char);

    igCreateContext(NULL);
    ImGuiIO* io = igGetIO_Nil();
    io->IniFilename = NULL; // no filesystem to persist window layout to
    io->BackendPlatformName = "nv_html5";
    io->BackendRendererName = "nv_webgpu";
    io->BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;

    ImGuiPlatformIO* platform_io = igGetPlatformIO_Nil();
    platform_io->Platform_ClipboardUserData = imgui;
    platform_io->Platform_GetClipboardTextFn = get_clipboard;
    platform_io->Platform_SetClipboardTextFn = set_clipboard;

    hook_input(window);
    js_setup_text_agent(imgui->clipboard, NV_IMGUI_CLIPBOARD_SIZE);

    // Fingers need bigger text and hit areas than a mouse pointer.
    imgui->ui_scale = js_touch_is_primary() ? 1.5f : 1.0f;
    ImGuiStyle* style = igGetStyle();
    ImGuiStyle_ScaleAllSizes(style, imgui->ui_scale);
    style->FontScaleMain = imgui->ui_scale;

    WGPUDevice device = gpu->device;
    imgui->pipeline = create_pipeline(device, gpu->surface_format);
    imgui->uniform_buffer = create_buffer(device, WGPUBufferUsage_Uniform, sizeof(NvMat4));
    imgui->vertex_buffer = create_buffer(device, WGPUBufferUsage_Vertex, NV_IMGUI_MAX_VERTICES * sizeof(ImDrawVert));
    imgui->index_buffer = create_buffer(device, WGPUBufferUsage_Index, NV_IMGUI_MAX_INDICES * sizeof(ImDrawIdx));

    WGPUSamplerDescriptor sampler_desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sampler_desc.magFilter = WGPUFilterMode_Linear;
    sampler_desc.minFilter = WGPUFilterMode_Linear;
    imgui->sampler = wgpuDeviceCreateSampler(device, &sampler_desc);
}

void nv_imgui_new_frame(NvImgui* imgui, f32 delta_seconds)
{
    ImGuiIO* io = igGetIO_Nil();
    f32 scale = imgui->window->pixel_ratio > 0.0f ? imgui->window->pixel_ratio : 1.0f;
    io->DisplaySize = (ImVec2_c){(f32)imgui->gpu->width / scale, (f32)imgui->gpu->height / scale};
    io->DisplayFramebufferScale = (ImVec2_c){scale, scale};
    io->DeltaTime = delta_seconds > 0.0f ? delta_seconds : 1.0f / 60.0f;

    // Close the on-screen keyboard once ImGui stops editing text (after Enter, or a tap outside).
    if (!io->WantTextInput && js_text_agent_focused())
        js_focus_text_agent(0);

    igNewFrame();
}

NvEditorLayout nv_editor_layout(NvGpu* gpu, f32 viewport_fraction)
{
    NV_ASSERT(viewport_fraction > 0.0f && viewport_fraction <= 1.0f);
    u32 split = (u32)((f32)gpu->height * viewport_fraction);
    NvEditorLayout layout = {
        .viewport = {0, 0, gpu->width, split},
        .panel = {0, split, gpu->width, gpu->height - split},
    };
    return layout;
}

bool nv_imgui_begin_panel(const char* name, NvRect rect)
{
    // ImGui works in CSS pixels; `rect` is in framebuffer pixels.
    f32 scale = igGetIO_Nil()->DisplayFramebufferScale.x;
    igSetNextWindowPos((ImVec2_c){(f32)rect.x / scale, (f32)rect.y / scale}, ImGuiCond_Always, (ImVec2_c){0.0f, 0.0f});
    igSetNextWindowSize((ImVec2_c){(f32)rect.width / scale, (f32)rect.height / scale}, ImGuiCond_Always);
    igSetNextWindowBgAlpha(1.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;
    return igBegin(name, NULL, flags);
}

void nv_imgui_render(NvImgui* imgui, WGPUCommandEncoder encoder, WGPUTextureView target)
{
    igRender();
    ImDrawData* draw_data = igGetDrawData();

    if (draw_data->Textures) {
        for (s32 i = 0; i < draw_data->Textures->Size; ++i) {
            ImTextureData* tex = draw_data->Textures->Data[i];
            if (tex->Status != ImTextureStatus_OK)
                update_texture(imgui, tex);
        }
    }

    ImVec2_c pos = draw_data->DisplayPos;
    ImVec2_c scale = draw_data->FramebufferScale;
    // Clip against the real target size; DisplaySize * scale can round past it.
    f32 fb_width = (f32)imgui->gpu->width;
    f32 fb_height = (f32)imgui->gpu->height;
    if (fb_width <= 0.0f || fb_height <= 0.0f || draw_data->TotalIdxCount == 0)
        return;

    // Gather every list into one vertex and one index upload.
    NV_ASSERT(draw_data->TotalVtxCount <= NV_IMGUI_MAX_VERTICES);
    NV_ASSERT(draw_data->TotalIdxCount <= NV_IMGUI_MAX_INDICES);
    u32 vertex_count = 0;
    u32 index_count = 0;
    for (s32 i = 0; i < draw_data->CmdLists.Size; ++i) {
        ImDrawList* list = draw_data->CmdLists.Data[i];
        memcpy(imgui->vertices + vertex_count, list->VtxBuffer.Data, list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(imgui->indices + index_count, list->IdxBuffer.Data, list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vertex_count += (u32)list->VtxBuffer.Size;
        index_count += (u32)list->IdxBuffer.Size;
    }
    // Buffer writes must be a multiple of 4 bytes; an odd index count gets one padding index.
    umm index_bytes = ((umm)index_count * sizeof(ImDrawIdx) + 3) & ~(umm)3;
    NV_ASSERT(index_bytes <= NV_IMGUI_MAX_INDICES * sizeof(ImDrawIdx));
    WGPUQueue queue = imgui->gpu->queue;
    wgpuQueueWriteBuffer(queue, imgui->vertex_buffer, 0, imgui->vertices, vertex_count * sizeof(ImDrawVert));
    wgpuQueueWriteBuffer(queue, imgui->index_buffer, 0, imgui->indices, index_bytes);

    // Maps ImGui's display rectangle (top-left origin, y down) to clip space.
    f32 l = pos.x;
    f32 r = pos.x + draw_data->DisplaySize.x;
    f32 t = pos.y;
    f32 b = pos.y + draw_data->DisplaySize.y;
    NvMat4 mvp = {{
        2.0f / (r - l), 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f / (t - b), 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        (r + l) / (l - r), (t + b) / (b - t), 0.5f, 1.0f,
    }};
    wgpuQueueWriteBuffer(queue, imgui->uniform_buffer, 0, &mvp, sizeof(mvp));

    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = target;
    color.loadOp = WGPULoadOp_Load;
    color.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_desc.label = (WGPUStringView){"imgui", WGPU_STRLEN};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    set_render_state(imgui, pass);

    u32 vertex_offset = 0;
    u32 index_offset = 0;
    for (s32 i = 0; i < draw_data->CmdLists.Size; ++i) {
        ImDrawList* list = draw_data->CmdLists.Data[i];
        for (s32 c = 0; c < list->CmdBuffer.Size; ++c) {
            ImDrawCmd* cmd = &list->CmdBuffer.Data[c];
            if (cmd->UserCallback) {
                if (cmd->UserCallback == IMGUI_RESET_RENDER_STATE)
                    set_render_state(imgui, pass);
                else
                    cmd->UserCallback(list, cmd);
                continue;
            }

            f32 x0 = (cmd->ClipRect.x - pos.x) * scale.x;
            f32 y0 = (cmd->ClipRect.y - pos.y) * scale.y;
            f32 x1 = (cmd->ClipRect.z - pos.x) * scale.x;
            f32 y1 = (cmd->ClipRect.w - pos.y) * scale.y;
            if (x0 < 0.0f) x0 = 0.0f;
            if (y0 < 0.0f) y0 = 0.0f;
            if (x1 > fb_width) x1 = fb_width;
            if (y1 > fb_height) y1 = fb_height;
            if (x1 <= x0 || y1 <= y0)
                continue;
            wgpuRenderPassEncoderSetScissorRect(pass, (u32)x0, (u32)y0, (u32)(x1 - x0), (u32)(y1 - y0));

            ImTextureID texture = ImDrawCmd_GetTexID(cmd);
            NV_ASSERT(texture > 0 && texture < NV_IMGUI_MAX_TEXTURES && imgui->textures[texture].bind_group);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, imgui->textures[texture].bind_group, 0, NULL);
            wgpuRenderPassEncoderDrawIndexed(pass, cmd->ElemCount, 1, index_offset + cmd->IdxOffset,
                                             (s32)(vertex_offset + cmd->VtxOffset), 0);
        }
        vertex_offset += (u32)list->VtxBuffer.Size;
        index_offset += (u32)list->IdxBuffer.Size;
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

#pragma once

#include "engine/gpu.h"

#include <cimgui.h>

#define NV_IMGUI_MAX_TEXTURES 16
#define NV_IMGUI_MAX_VERTICES 131072
#define NV_IMGUI_MAX_INDICES  262144
#define NV_IMGUI_CLIPBOARD_SIZE 65536
// Textures of the engine drawn in the UI (nv_imgui_preview); their ImTextureIDs follow ImGui's own.
#define NV_IMGUI_MAX_PREVIEWS 64
#define NV_IMGUI_PREVIEW_FRAMES 3 // frames a preview slot outlives its last use, while the GPU may draw it

typedef enum NvTouchGesture {
    NV_TOUCH_NONE,
    NV_TOUCH_UNDECIDED, // finger down, not yet a tap, press or scroll
    NV_TOUCH_PRESS,     // ImGui sees the mouse button down
    NV_TOUCH_SCROLL,    // scrolls the panel under the finger; ImGui sees no button
    NV_TOUCH_VIEW,      // started in the view rect: goes to NvViewInput, not to ImGui
} NvTouchGesture;

// Mouse and touch input that started in NvImgui.view_rect, for moving a camera and picking.
// Distances are CSS pixels; positions are page (= canvas) client coordinates.
typedef struct NvViewInput {
    f32 orbit_x, orbit_y; // left drag, one-finger drag
    f32 pan_x, pan_y;     // right or middle drag, two-finger drag
    f32 dolly;            // log of the distance factor: wheel, pinch; 0 = none, < 0 = closer
    b32 tapped;           // a click or tap that did not move
    f32 tap_x, tap_y;
    u32 tap_mods;         // ImGuiMod_Ctrl / Shift / Alt held when the tap's press went down (Cmd counts as Ctrl)
} NvViewInput;

#define NV_VIEW_MAX_TOUCHES 2

typedef struct NvImguiTexture {
    WGPUTexture texture;
    WGPUTextureView view;
    WGPUBindGroup bind_group;
} NvImguiTexture;

// How nv_imgui_preview shows a texture (docs/specs/textures.md).
typedef enum NvImguiPreviewMode {
    NV_IMGUI_PREVIEW_RGBA,
    NV_IMGUI_PREVIEW_RGB, // alpha ignored
    NV_IMGUI_PREVIEW_R,   // one channel as gray
    NV_IMGUI_PREVIEW_G,
    NV_IMGUI_PREVIEW_B,
    NV_IMGUI_PREVIEW_A,
    NV_IMGUI_PREVIEW_DEPTH, // a depth texture as gray over a range (NvImguiPreview.range_*)
} NvImguiPreviewMode;

// How a depth texture's values become the range's units.
typedef enum NvImguiDepth {
    NV_IMGUI_DEPTH_RAW,                  // the stored 0..1 value
    NV_IMGUI_DEPTH_REVERSE_PERSPECTIVE,  // reverse Z, perspective: distance from the camera
    NV_IMGUI_DEPTH_REVERSE_ORTHOGRAPHIC, // reverse Z, orthographic: distance from the camera
} NvImguiDepth;

// What to show of a texture. Zero is the whole texture's first mip level as RGBA.
typedef struct NvImguiPreview {
    WGPUTexture texture; // needs WGPUTextureUsage_TextureBinding
    u32 mip;
    NvImguiPreviewMode mode;
    NvImguiDepth depth;
    f32 range_min, range_max; // DEPTH: the values shown black and white
    f32 near_z, far_z;        // DEPTH, reverse Z: the camera's planes
} NvImguiPreview;

typedef struct NvImguiPreviewSlot {
    NvImguiPreview key;
    WGPUTextureView view; // the key's mip level
    WGPUBuffer params;
    WGPUBindGroup bind_group;
    b32 depth;       // drawn by the depth pipeline
    b32 multisampled; // a depth texture with more than one sample: the multisampled depth pipeline
    u64 used_frame;  // the last frame it was asked for
} NvImguiPreviewSlot;

// Dear ImGui on the canvas: browser input in, WebGPU draw calls out. Build UI with the cimgui
// API (ig* functions) between nv_imgui_new_frame and nv_imgui_render.
typedef struct NvImgui {
    NvGpu* gpu;
    NvWindow* window;

    WGPURenderPipeline pipeline;
    WGPUBuffer uniform_buffer;
    WGPUSampler sampler;
    WGPUBuffer vertex_buffer;
    WGPUBuffer index_buffer;

    // Slot 0 is unused so that ImTextureID 0 keeps meaning "no texture".
    NvImguiTexture textures[NV_IMGUI_MAX_TEXTURES];

    // Engine textures shown in the UI: ImTextureID NV_IMGUI_MAX_TEXTURES + i is previews[i]. A slot
    // holds a reference on its texture, so a released texture cannot come back at the same address
    // while a slot is keyed by it.
    WGPURenderPipeline preview_pipeline; // color textures
    WGPURenderPipeline depth_pipeline;   // depth textures, read without a sampler
    WGPURenderPipeline depth_ms_pipeline; // multisampled depth textures (sample 0)
    NvImguiPreviewSlot previews[NV_IMGUI_MAX_PREVIEWS];
    u64 frame; // counts nv_imgui_new_frame calls

    // CPU copies of one frame's geometry, uploaded with one write per buffer.
    ImDrawVert* vertices; // [NV_IMGUI_MAX_VERTICES]
    ImDrawIdx* indices;   // [NV_IMGUI_MAX_INDICES]

    // Text ImGui copies, or the browser last pasted; NUL-terminated, truncated to fit.
    char* clipboard; // [NV_IMGUI_CLIPBOARD_SIZE]

    // 1 with a mouse; larger on touch screens so text and hit areas suit fingers.
    f32 ui_scale;

    // A touch reaches ImGui only once it is known to be a tap or a press, so a finger that scrolls
    // a panel never clicks what it started on (see on_touch in imgui.c).
    NvTouchGesture touch_gesture;
    f32 touch_start_x, touch_start_y; // CSS pixels
    f32 touch_x, touch_y;
    f64 touch_start_time; // seconds
    f32 touch_scroll_pending; // pixels scrolled since the last frame
    f32 touch_scroll;         // this frame's scroll, applied by nv_imgui_begin_panel
    u32 text_agent_grace;     // frames to keep the keyboard up while a tapped field activates

    // The scene viewport, in framebuffer pixels; set by the app every frame. Input that starts
    // there (while no popup is open) skips ImGui and lands in `view`, which holds this frame's.
    NvRect view_rect;
    NvViewInput view;
    NvViewInput view_pending; // gathered by the event handlers until the next frame
    s32 view_mouse_button;    // DOM button dragging in the view, + 1; 0 = none
    b32 view_moved;           // the current press moved too far to be a tap
    f32 view_press_x, view_press_y;
    f32 view_mouse_x, view_mouse_y;
    u32 view_touch_count;
    b32 view_touch_multi; // two fingers were down during this gesture
    s32 view_touch_id[NV_VIEW_MAX_TOUCHES];
    f32 view_touch_x[NV_VIEW_MAX_TOUCHES];
    f32 view_touch_y[NV_VIEW_MAX_TOUCHES];

    // Lets the app take a press in the view for something it draws there (a gizmo handle). A left
    // press or a one-finger touch waits two frames, so ImGui has seen the pointer where it went
    // down, then `view_grab` decides: true hands the press to ImGui as a left button, false keeps
    // it in `view`. NULL keeps every press in the view without waiting.
    b32 (*view_grab)(void* data);
    void* view_grab_data;
    u32 view_grab_wait; // frames until the pending press is decided; 0 = none pending
    b32 view_tap_held;  // the pending press was released as a tap before it was decided
    b32 view_grab_touch; // the pending press is a touch
    u32 view_press_mods; // the modifiers held when the view's press went down (NvViewInput.tap_mods)

    // Lets the app keep a key for itself: asked on every key event with the key and the modifiers
    // held (an ImGuiKeyChord); true consumes the event, so the browser's own action for it (Ctrl+S
    // opens "Save page") does not happen. NULL claims nothing. Runs inside the browser's event
    // handler, so it must not call into WebAssembly-side ImGui frames.
    b32 (*claims_key)(void* data, ImGuiKeyChord chord);
    void* claims_key_data;
} NvImgui;

// Creates the ImGui context, hooks browser input and creates GPU objects. Staging memory for
// geometry comes from `arena`.
void nv_imgui_init(NvImgui* imgui, NvGpu* gpu, NvWindow* window, NvArena* arena);

// Makes the TrueType font in `ttf` (`size` bytes, which must outlive the ImGui context) the font of
// every window, at `pixel_size` CSS pixels (before the touch scale). `fallback` (NULL for none) is
// merged behind it: it draws the characters `ttf` lacks (docs/specs/korean.md). Call before the
// first frame; returns false, keeping the built-in font, if `ttf` is no font.
bool nv_imgui_set_font(NvImgui* imgui, void* ttf, u32 size, void* fallback, u32 fallback_size, f32 pixel_size);

void nv_imgui_new_frame(NvImgui* imgui, f32 delta_seconds);

// Begins an ImGui window that fills `rect` (framebuffer pixels), with no title bar, and cannot be
// moved, resized or collapsed. `name` only identifies the window. A vertical touch drag scrolls
// it. Like igBegin, always pair it with igEnd.
bool nv_imgui_begin_panel(NvImgui* imgui, const char* name, NvRect rect);

// The same with extra ImGuiWindowFlags (a menu bar, no scrollbar, no background).
bool nv_imgui_begin_panel_ex(NvImgui* imgui, const char* name, NvRect rect, ImGuiWindowFlags extra_flags);

// Applies this frame's touch scroll to the current window if the finger is on it. The panel does
// this itself; call it right after igBeginChild_Str for a child window that scrolls on its own, since
// the finger counts as on the child, not on the panel around it.
void nv_imgui_touch_scroll(NvImgui* imgui);

// The ImTextureID that draws `preview` this frame (with igImage). Slots are kept while in use and
// released NV_IMGUI_PREVIEW_FRAMES frames after their last one; more than NV_IMGUI_MAX_PREVIEWS in
// use at once asserts.
ImTextureID nv_imgui_preview(NvImgui* imgui, const NvImguiPreview* preview);

// Preview slots in use, for tests.
u32 nv_imgui_preview_count(NvImgui* imgui);

// Finishes the ImGui frame and records a render pass that draws it over `target`.
void nv_imgui_render(NvImgui* imgui, WGPUCommandEncoder encoder, WGPUTextureView target);

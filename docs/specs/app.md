# One app spec

Status: draft (2026-09-26). Changes to this spec are agreed first.

## Goal

Merge the three examples (triangle, scene, character) into one app at one address. The editor
panel picks which sample is shown; the viewport above it draws that sample.

## Decisions

| Topic | Decision |
|---|---|
| Structure | One executable. Each former example becomes a **sample**: a C file behind a small function table |
| Location | `app/` (`app/main.c` is the shell, `app/samples/*.c` the samples); `examples/` is removed |
| Address | Pages root: `https://chromedays.github.io/engine/`. `#triangle`, `#scene`, `#character` pick the sample |
| Old addresses | `/engine/character/` and the others stay as tiny pages that redirect to `/engine/#character` |
| Default sample | `character` |
| Loading | A sample is set up the first time it is shown and then stays loaded; switching back keeps its state |
| Updating | Only the shown sample updates and draws; the others are paused |
| Assets | One preloaded `app.data` with everything (1.3 MB today). Loading per sample waits until assets grow |
| Engine state | One `NvGpu`, `NvRenderer`, `NvImgui` and nv_anim instance, shared by all samples |
| Third-party | None needed; this is app structure |

## Sample interface

```c
// app/sample.h
typedef struct NvSampleContext {
    NvGpu* gpu;
    NvRenderer* renderer;
    NvImgui* imgui;
    NvArena* permanent; // lives as long as the app
    NvArena* scratch;   // reset by the shell after every call
} NvSampleContext;

typedef struct NvSample {
    const char* name;                                // "character"; also the URL hash
    void* (*init)(NvSampleContext* ctx);             // first time shown; returns the sample's state
    void (*enter)(void* state, NvSampleContext* ctx); // every time it becomes the shown sample
    void (*update)(void* state, NvSampleContext* ctx, f32 dt);
    void (*ui)(void* state, NvSampleContext* ctx);    // inside the editor panel
    void (*draw)(void* state, NvSampleContext* ctx, WGPUCommandEncoder encoder,
                 WGPUTextureView target, NvRect viewport);
} NvSample;
```

- `init` pushes the sample's state (its own `NvScene`, ids, UI values) from `permanent`, so state
  stays off the stack (`nv_window_run` never returns) and there is no malloc.
- `enter` sets what the renderer shares between samples, such as `clear_color` and `ambient`.
- `draw` must stay inside `viewport`; the shell clears the rest and draws ImGui over it.

## Shell (`app/main.c`)

Every frame:

1. `nv_gpu_begin_frame`, `nv_editor_layout`, `nv_imgui_new_frame`.
2. If the URL hash changed (see below), switch to that sample.
3. Open the panel with `nv_imgui_begin_panel`. Its first row is a sample selector (a combo or
   tabs) and the FPS readout; the shown sample's `ui` fills the rest.
4. `update` then `draw` for the shown sample, then `nv_imgui_render`, submit, `nv_gpu_end_frame`.

Switching sets the hash with `history.replaceState`, so a link or a reload opens the same
sample. A `hashchange` listener handles the browser back button and edited URLs.

## Engine changes

- **Shared renderer.** Meshes, materials and textures of every sample live in one renderer. The
  totals (under 40 meshes, 20 materials, 10 textures today) fit the current limits of 256. The
  renderer draws the scene it is given, so each sample passes its own `NvScene`.
- **Debug lines.** Queued lines belong to the frame's shown sample; the queue is cleared on switch
  so a hidden sample's lines never show.
- **Triangle.** It keeps its own pipeline (the "raw WebGPU" sample) and draws into the viewport
  with `wgpuRenderPassEncoderSetViewport`, clearing only the viewport.
- **Memory.** One set of arenas: permanent 64 MB, scratch 32 MB, anim 48 MB (today's largest,
  character, uses 48/32/48). These are zero-filled globals, so they do not grow the download.

## Build and deploy

- `app/CMakeLists.txt`: `add_executable(app ...)` and
  `nv_setup_executable(app ASSETS assets/quaternius)`, installed at the package root rather than
  in a subfolder.
- `web/landing.html` is removed; `index.html` is the app. `web/redirect.html.in` produces the three
  old-address pages.
- CI keeps the same steps; only the installed layout changes.

## Phases

1. **Shell:** `app/`, the sample interface, hash routing, and triangle and scene ported. Remove
   those two from `examples/`.
2. **Character:** port character, move its assets into `app.data`, remove `examples/`.
3. **Deploy:** install at the root, old-address redirects, update `AGENTS.md`, `README.md` and
   CI. Check Release and Debug in headless Chromium: every sample, switching back and forth with
   state kept, hash links, reload, and phone size.

## Open questions

1. Directory and target name: `app` (proposed), `editor` or keep `examples`?
2. Keep redirect pages for the old addresses (proposed), or drop them?
3. Default sample: `character` (proposed)?
4. Sample selector: a combo (proposed; fits a phone) or a tab row?

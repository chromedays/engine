# Texture viewer spec

Status: implemented (2026-09-30). Changes to this spec are agreed first.

## Goal

See the textures the engine is using, inside the app: the character's base color maps, the shadow
map, the scene's depth target and ImGui's font atlas. A **Textures** tab lists them with a
thumbnail, size, format, mip levels, memory and what uses them; picking one shows it large, with
mip level, channel and depth-range controls. The Inspector's Mesh section shows its material's
texture too.

Out of scope: reading texel values back to the CPU (a value under the pointer), editing or
replacing textures, and textures of past frames (no capture, as RenderDoc does natively).

## What there is today

| Texture | Where | Format | Usage | Can ImGui sample it today |
|---|---|---|---|---|
| Material textures (glTF base color: `T_Hair_1_BaseColor`, `T_Eye_Brown`, `T_Superhero_Male_Dark`) and the 1×1 white default (slot 0) | `NvRenderer.textures[256]`, `nv_renderer_add_texture` | `RGBA8UnormSrgb` (white: `RGBA8Unorm`), full mip chain | `TextureBinding`, `CopyDst` | Yes, through a new bind group; but the renderer keeps no name, size, format or mip count |
| Shadow map | `NvRenderer.shadow_texture` | `Depth32Float` or `Depth16Unorm`, 512² to 2048², or a 1×1 placeholder when off | `RenderAttachment`, `TextureBinding` | No: ImGui's shader takes `texture_2d<f32>` with a filtering sampler, and a depth format cannot be filtered |
| Scene depth target | `NvRenderer.depth_texture` | `Depth32Float`, canvas size, reverse Z | `RenderAttachment` only | No: not created for sampling |
| Swapchain (the canvas) | `NvGpu.current_texture` | `BGRA8Unorm` with a `BGRA8UnormSrgb` view (per browser) | `RenderAttachment` | No, and it cannot be: the ImGui pass is drawing into it |
| ImGui font atlas | `NvImgui.textures[16]` | `RGBA8Unorm` | `TextureBinding`, `CopyDst` | Yes (it is ImGui's own) |

## Approaches

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **Our own viewer on `igImage`, with a preview pipeline in `imgui.c`** (recommended) | The tab draws textures with ImGui's `igImage`. `imgui.c` gains preview slots: a bind group per (texture, mip, mode) made on demand, drawn by a small second pipeline that can show one channel, or a depth texture as gray over a chosen range | C17, fixed slots, no new dependency; about 400 lines (engine and app) | Ours to write. No texel readback |
| imgui_tex_inspect (andyborrell, C++, MIT) | An ImGui texture inspector: zoom, pan, per-texel value annotations, channel masks | The closest match in features | C++ with no C API (no cimgui binding), and backends only for OpenGL and DirectX 11: its WebGPU backend (a shader and texel readback) would be ours to write anyway, plus a C++ wrapper like `anim.cpp` |
| Dear ImGui alone (`igImage`, `igImageWithBg`) | The widgets, with the current pipeline | Already here | Enough for color textures, but shows no depth texture, no channel, no mip level |
| WebGPU Inspector (Brendan Duncan, browser extension, MIT) | Captures a frame and lists every GPU object, textures included | Nothing to add to the engine | Outside the app: a desktop browser with the extension, not a phone. A good tool alongside, not the feature asked for |

Recommendation: write it ourselves on `igImage`. No third-party library.

## Decisions

| Topic | Decision |
|---|---|
| Tab | Desktop UI (`layout.md`): a **Textures** tab in the right dock, after Inspector and View; the right dock is tall enough for a texture, where the bottom dock is not. Phone UI: a tab between View and Console, and the tab bar uses ImGui's shrink fitting (`ImGuiTabBarFlags_FittingPolicyShrink`), so all five tabs stay visible with their labels cut short instead of a scrolling tab bar |
| List and picked texture | Where the tab is wide (700 px times `ui_scale` or more), the list and the picked texture sit side by side. Otherwise (the right dock, a phone) the tab shows one or the other: picking a row shows the texture, and **< Textures** goes back to the list |
| Groups | Three collapsing sections: **Materials** (the renderer's textures), **Render targets** (shadow map, depth target, swapchain), **UI** (ImGui's font atlas) |
| "In use" | A material texture is in use when a mesh node of the shown scene has a material with it as `base_color_texture`; the white default counts for materials with none. An **In use only** checkbox (on by default) hides the rest. Render targets are in use while they are drawn to (the shadow map while shadows are on) |
| Row | A thumbnail (48 px times `ui_scale`, aspect kept, on a checkerboard so alpha shows), then three short lines: the name; size and format; mip levels, memory and users ("11 mips, 5.3 MB, 1 user"). Only rows on screen make thumbnails (`igIsItemVisible`), so the preview slots stay few however long the list |
| Memory | Width × height × bytes per texel, × 4/3 with a mip chain. The section headers show their sums, and the tab's top line the total |
| Names | Material textures take the glTF image's name (`T_Eye_Brown`), or `image N` when it has none; the default is `white`. Also set as the WebGPU label, so browser tools show the same name |
| Detail | The texture fits the panel's width and the height left (at least 160 px times `ui_scale`). **Zoom** (1× to 16×, a slider) and a sideways drag on the image pan it (a vertical drag keeps scrolling the panel). **Mip** (a slider over its levels, with that level's size). **Channels**: RGBA, RGB (alpha ignored), R, G, B, A as gray, as a row of radio buttons (one tap each, where a combo takes two). **Checkerboard** behind, on by default. The pointer's texel coordinates and UV under the image, on hover or while pressed |
| Used by | Under the detail, the nodes using it; tapping one selects it (and opens the Inspector, as the Scene tab does) |
| Inspector | The Mesh section replaces "Multiplied with a texture." with the texture's thumbnail and name; tapping it opens the Textures tab with it selected |
| Depth display | Depth is shown as gray, mapped from a **range**: the shadow map raw 0..1 (it is orthographic, so already linear); the depth target turned back into view distance with the camera's near and far planes (reverse Z: 1 is near), black at the camera and white at **White at** (twice the orbit camera's distance by default, a slider up to the far plane). The depth target shows the scene viewport's part of it. Mip and channel controls are hidden for depth |
| Depth target sampling | The depth target is made with `TextureBinding` only while the Textures tab is shown (`NvRenderer.depth_sampled`, set by the app; the renderer recreates the texture when it changes). Otherwise it keeps `RenderAttachment` alone, so a shipped frame never pays for it (GPU notes below). While samplable, the scene pass also stores its depth (`WGPUStoreOp_Store`); otherwise it discards it, as nothing reads it after the pass |
| Swapchain | Listed with size, formats and present mode, without a preview: the ImGui pass renders into it, and a pass cannot sample its own attachment. A copy to preview it would cost a full-screen copy every frame for little gain |
| Preview slots | `imgui.c` keeps 64 preview slots beside ImGui's 16. A slot is found by (texture, mip, mode, depth range) or made (a texture view of that mip, a small uniform buffer, a bind group), and released after 3 frames unused, since the GPU may still draw with it. The slot's view holds its texture, so a texture the renderer recreates (the shadow map on a setting change) cannot come back at the same address while a slot is still keyed by it. More than 64 at once asserts: the clipper keeps the visible thumbnails far below that |
| Preview pipeline | A second pipeline with the same vertex layout. Its fragment shader applies the slot's mode: a channel as gray, alpha forced to 1 for RGB, or depth (`texture_depth_2d` read with `textureLoad` at the pixel's texel, no sampler, then the range mapping). Color modes sample with the same linear sampler. Draw commands pick the pipeline by their texture id's range, so ImGui's own draws are unchanged |
| sRGB | sRGB textures are sampled as linear values and the canvas view encodes back to sRGB, so they show as stored; a channel shown as gray is the stored channel value |
| Saved | No. What the tab shows (section states, selection, zoom, mip, channels, range) is view state, like the Console's filters |
| Undo | Nothing here is undoable, and nothing here changes what undo compares |
| Third-party | None |

## Engine API changes

- **`nv/renderer.h`**
  - `NvRenderTexture` gains `char name[64]`, `u32 width, height, mip_count`, `WGPUTextureFormat format`.
  - `nv_renderer_add_texture(renderer, name, width, height, rgba, srgb, scratch)`: the new `name`
    parameter (NULL = `texture N`); `gltf.c` passes the image's name.
  - `NvRenderer.depth_sampled` (b32): make the depth target with `TextureBinding` too.
  - `nv_gpu_format_bytes(WGPUTextureFormat)` next to `nv_gpu_format_name` in `nv/gpu.h`, for the
    memory column.
- **`nv/imgui.h`**
  - `typedef enum NvImguiPreview { NV_IMGUI_PREVIEW_RGBA, NV_IMGUI_PREVIEW_RGB, NV_IMGUI_PREVIEW_R, ..._G, ..._B, ..._A, NV_IMGUI_PREVIEW_DEPTH }`.
  - `ImTextureID nv_imgui_preview(NvImgui* imgui, WGPUTexture texture, u32 mip, NvImguiPreview mode, f32 range_min, f32 range_max, f32 near, f32 far)`:
    the id to pass to `igImage` this frame. `near` and `far` turn reverse-Z depth into distance;
    0 and 0 read depth raw.
  - `ImTextureID nv_imgui_font_atlas(NvImgui*)`, so the UI group can show it.

## App changes

- `app/textures.c` (new): the Textures tab, its state (`TextureView` in `App`), the "in use" and
  "used by" walk over the shown scene's nodes, and the checkerboard.
- `app/ui.c`: the tab, shrink fitting on narrow panels, and the Inspector's Mesh thumbnail.
- `app/main.c`: `renderer.depth_sampled` follows whether the Textures tab was shown last frame.

## GPU notes

Per the user preferences, driver behavior is described with Mesa's RADV (the open-source AMD
Vulkan driver) as the reference; browsers reach Vulkan on Linux and Android.

- **A sampled depth target can cost compression.** RADV decides per image whether depth gets
  HTILE (the hierarchical depth metadata that speeds up depth tests and clears) and whether it is
  "TC-compatible", meaning the texture unit can read the compressed data directly. When a depth
  image also has the sampled usage and TC-compatible HTILE cannot be used (older GCN chips, some
  format and sample-count cases), reading it needs a decompress pass first, or HTILE is left off.
  Browsers pass WebGPU's `TextureBinding` down as that sampled usage. Adding it only while the
  viewer shows the depth target keeps the normal path untouched on every chip.
- **Reading depth with `textureLoad`.** Depth formats cannot be filtered in WebGPU; a load
  instruction without a sampler is what the hardware does for this anyway (an image load on AMD),
  and the shader picks the texel from the fragment's UV.
- **Depth is stored only while it is read.** The scene pass ends with `WGPUStoreOp_Discard` for
  depth unless the viewer samples it: a tiler (phone GPUs) then never writes depth to memory, and
  a desktop driver may skip decompressing it at the end of the pass.
- **Preview views are cheap.** A texture view for one mip level is a descriptor, not a copy; the
  cost of the viewer is the bind groups and the extra draw calls for visible thumbnails.

## Tests

- **Node**: `nv_gpu_format_bytes` and the memory sum (with and without a mip chain).
- **Playwright**, Release and Debug, desktop mouse and phone touch sizes. Debug builds export the
  texture count, the preview slots in use, the selected texture, and where rows and controls were
  drawn (as the Console tab does).
  - The showcase lists four material textures (three glTF and white), with the glTF names, sizes and
    mip counts; "In use only" shows the ones in use; the stress scene lists the same (its crowd uses the
    character's textures).
  - A thumbnail's pixels match the texture (a screenshot at the thumbnail against the decoded image,
    within a tolerance); channel R of `T_Eye_Brown` shows gray.
  - The shadow map previews at each size and format, and after a format change; the depth target
    previews only while the tab is shown, and `depth_sampled` goes back off when another tab is
    picked; turning shadows off shows the 1×1 placeholder line without a preview.
  - Mip slider: the shown level's size halves each step.
  - Tapping a "used by" node selects it and opens the Inspector; the Inspector's thumbnail opens the
    tab with its texture selected.
  - Scrolling the list up and down keeps preview slots under 64, and they return to the idle count
    after leaving the tab.
  - No WebGPU errors in the Console tab through all of the above; the save round trip and the undo
    step count are unchanged.

## Phases

1. **Engine:** texture names and sizes, `nv_gpu_format_bytes`, preview slots and the preview
   pipeline (color, channels, depth), `depth_sampled`. Checked with a Debug export that draws the
   shadow map and a material texture in a test window (done with the tab itself instead).
2. **Tab:** the three groups, rows with the clipper, detail with zoom, pan, mip, channels and depth
   range, "used by", the Inspector thumbnail, shrink fitting on phones.
3. **Edge cases and docs:** shadow settings changing while previewed, a resize while the depth
   target is previewed, the stress scene, the slot count after long scrolling; `AGENTS.md`, README
   and the Dependencies note that none was added.

Every phase is checked in Release and Debug in headless Chromium, with the mouse at desktop size
and with touch at phone size.

# Scene resolution spec

Status: implemented (2026-10-01). Changes to this spec are agreed first.

## Goal

Let the scene render at a resolution of its own and show it in the viewport with the
nearest-pixel filter, in one of two modes:

- **Scale:** a whole fraction of the viewport's pixels (1/1, 1/2, 1/3, 1/4), filling the viewport.
  Below 1/1 it trades sharpness for speed where the GPU is the limit, which the stress benchmark
  found on phones: a phone with a 3× pixel ratio renders its viewport at about 1170 × 1000 pixels
  today, nine pixels per CSS pixel.
- **Fixed:** an exact size (e.g. 1280 × 720) whatever the window, shown at its own aspect ratio,
  centered, with black bars on the sides the image does not fill (letterbox above and below,
  pillarbox left and right). What a game would show at that resolution, independent of the docks
  and the window.

Either way every rendered pixel shows as an even square block of screen pixels. The editor UI (the
docks, the gizmo, the build label) stays at the canvas's full resolution: only the scene changes.

## Today

The scene pass draws straight into the canvas, inside the viewport rectangle (viewport and
scissor), and with MSAA it draws into a 4-sample texture of the whole canvas's size and resolves it
into the canvas (`msaa.md`). The depth target is the canvas's size too. So the scene's resolution
is always the canvas's, its aspect ratio is the viewport's, and its targets cover the docks and
panels it never draws.

## Approaches

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **An offscreen scene target and an upscale pass** (recommended) | The scene pass renders into its own color texture at the scene resolution (MSAA resolves into it); a small pass then draws that texture into its rectangle of the canvas with the nearest-pixel filter, before the ImGui pass | One path for both modes and every size; the targets shrink to the scene's own size, which also does what `msaa.md` left for later (no 4-sample texture under the docks). The scene color becomes a sampled texture the Textures tab can show | One more pass, a full-viewport read and write, even at 1/1 |
| Render straight into the canvas at 1/1, offscreen only otherwise | Two paths | Saves the extra pass at 1/1 | Two code paths to keep right; at 1/1 the targets stay canvas-sized |
| Lower the canvas's own resolution (fewer framebuffer pixels per CSS pixel) | The whole page renders at a lower pixel ratio | No new pass | Blurs the UI text too, which is what we must not touch; no fixed size |
| AMD FidelityFX Super Resolution 1 (FSR 1: EASU upscale + RCAS sharpen; MIT; GLSL/HLSL shaders) | A spatial upscaler, smoother than nearest | Ported to WGSL, it would replace the nearest pass | Two passes and a port to maintain. A later option |
| Dynamic resolution (the scale follows the frame time) | Lowers the scale when frames run long | Steady frame rate | Needs reliable GPU timings, which SwiftShader cannot give for tests; a later step on top of this one |

Recommendation: an offscreen scene target and a nearest-pixel upscale pass, written by us. FSR 1 and
dynamic resolution are left for later and need no change to this design: they replace the upscale
pass and set the scale.

## Decisions

### Both modes

| Topic | Decision |
|---|---|
| Setting | A **Resolution** section in the View tab: **Mode** (Scale, Fixed), then the mode's own controls (below). It shows what the scene renders at ("585 × 497") and how large a rendered pixel is on screen ("2 × 2 screen pixels, 1.5 per CSS pixel") |
| Default | Scale on both UIs: 1/1 on the desktop UI, 1/2 on the phone UI (where the GPU is the limit; a 3× screen still gets 1.5 pixels per CSS pixel). The setting changes either |
| Upscale filter | **Nearest only**: each rendered pixel becomes a block of whole screen pixels, crisp and exactly what was rendered. So scales are whole fractions and fixed sizes are shown at whole multiples where they fit: a share like 67% would mix 1- and 2-pixel-wide blocks across the image. Nothing is rendered larger than it is shown in Scale mode: with nearest filtering, extra pixels would be dropped, not averaged |
| Scene target | A single-sample color texture at the scene resolution, sampled by the upscale pass. Made like the canvas (its format with the sRGB view format, rendered and sampled through the sRGB view), so the MSAA resolve writes the same colors it does today (`msaa.md` found a texture created directly in the sRGB format resolved too dark) |
| MSAA and depth | The 4-sample color texture and the depth texture are the scene resolution too, not the canvas's. MSAA resolves into the scene target |
| Sizes and resizing | In Scale mode the targets are allocated rounded up to multiples of 64 pixels and kept while they are large enough, so a splitter drag or a window resize does not remake them every frame; they are remade smaller when they hold more than twice the pixels needed. The scene renders into the top-left part (viewport and scissor), and the upscale pass reads that part. In Fixed mode the size does not follow the window at all |
| Limits | The scene resolution is clamped to the device's `maxTextureDimension2D` (8192 in WebGPU's defaults) and to at least 1 × 1 |
| Upscale pass | A full-screen triangle with the viewport and scissor of the image's rectangle, sampling the scene target with a clamp-to-edge sampler. It clears the canvas to black first (the scene pass no longer touches the canvas): that black is the bars in Fixed mode, and the docks drawn later cover the rest |
| Image rectangle | The app computes where the image goes inside the viewport (`App.layout.image`, framebuffer pixels): the whole viewport in Scale mode, the centered fit in Fixed mode. The renderer, picking, the gizmo and panning use it; the viewport stays the area that takes input |
| Debug lines, selection boxes | Drawn in the scene pass, so they scale with the scene (1 pixel wide at the scene resolution). The gizmo and the build label are ImGui and stay sharp |
| Textures tab | Lists the **scene color** target with a preview (it is sampled anyway), and the depth and MSAA targets at their new size. The depth target no longer needs "the scene's part" cut out: it is the scene |
| Cost shown | The Stress tab shows the resolution ("585 × 497, scale 1/2" or "1280 × 720 fixed, shown ×1") and the GPU time of the upscale pass (a third timestamp pair); the benchmark records them |
| Third-party | None now; FSR 1 (MIT) is the candidate when a smoother upscale is wanted |

### Scale mode

| Topic | Decision |
|---|---|
| Presets | 1/1, 1/2, 1/3 and 1/4 of the viewport's framebuffer pixels per side. The image fills the viewport; its aspect ratio is the viewport's, as today |
| Rounding | The scene size is the viewport's divided and rounded up, and the image rectangle is the scene size times the divisor, cut to the viewport, so the blocks at the right and bottom edges are the only ones that can be partial |

### Fixed mode

| Topic | Decision |
|---|---|
| Sizes | Presets 640 × 360, 1280 × 720, 1920 × 1080 (16:9 landscape), 360 × 640, 720 × 1280 (9:16 portrait, for phones), and **Custom**: width and height fields, 16 to 4096 each (and within the device limit) |
| Aspect ratio | The camera's projection uses the fixed size's aspect ratio, not the viewport's: the vertical field of view stays, so a portrait size shows a narrower view. What is visible therefore does not change when the docks or the window move |
| Fit | The image is shown at the largest **whole** multiple of its size that fits in the viewport (×1, ×2, ...), centered, so every block is the same size. When even ×1 does not fit (a size larger than the viewport), it is shrunk to fit, keeping its aspect ratio, and the View tab says so ("larger than the viewport: shown at 0.62×, some pixels dropped") |
| Bars | Black, drawn by the upscale pass's clear. They take input like the rest of the viewport |
| Input on the bars | Drags orbit and pan, and the wheel or a pinch zooms, anywhere in the viewport, bars included. A tap on a bar picks nothing and keeps the selection (a tap on the image's empty background still clears it, as today). The build label's badge stays in the viewport's top-left corner, over the bar if there is one |
| Picking and gizmo | Use the image rectangle and the fixed aspect ratio (`nv_renderer_view_ray`, `nv_renderer_camera_matrices` and `ImGuizmo_SetRect` get the image rectangle). The gizmo is drawn by ImGui at screen resolution, so it may reach over the bars |
| Panning | One pixel of drag moves the orbit point by the height the view covers at that distance divided by the **image's** height, so the scene still follows the finger |
| Memory | The targets are the fixed size, so a large fixed size costs the same whatever the window (1920 × 1080 with MSAA 4×: about 71 MB) |

### Saved

| Tag | Type | What | Missing or invalid |
|---|---|---|---|
| `RSMD` | u32 | mode: 0 Scale, 1 Fixed | 0 |
| `RSCL` | u32 | Scale mode's divisor: 1, 2, 3 or 4 | the device's default |
| `RSFW`, `RSFH` | u32 | Fixed mode's width and height, clamped to 16..4096 | 1280 × 720 |

The phone's and the desktop's defaults differ, so a save written on one device and loaded on
another keeps the user's choice, not the other device's default.

## Memory

With MSAA 4× at 1/2 on a 3× phone (viewport about 1170 × 1000), the scene's targets hold about
585 × 500 pixels: color 4.5 MB (4×), depth 4.5 MB (4×) and the scene target 1.1 MB, against
about 90 MB for the canvas-sized MSAA targets today. At 1/1 on a 1280 × 800 desktop with the
default docks (viewport 680 × 552), about 13 MB against 32 MB.

## Changes

- **Renderer (`nv/renderer.h`, `engine/src/renderer.c`).** `nv_renderer_draw` takes the scene's
  resolution and the image rectangle instead of one viewport rectangle; the projection's aspect
  ratio is the resolution's. The scene color target and the MSAA and depth targets, sized by
  `update_scene_targets` (with the rounding above); the upscale pipeline (a full-screen triangle and
  a nearest sampler); the scene pass into the scene target, then the upscale pass into the canvas;
  a third timestamp pair for the upscale pass. `nv_renderer_view_ray` and
  `nv_renderer_camera_matrices` take the image rectangle and the resolution.
- **App.** The resolution settings in `App`; `app_layout` computing the scene size and
  `App.layout.image` from the mode; picking, panning and the gizmo on the image rectangle, and a
  tap on a bar ignored (`app/main.c`); the View tab's Resolution section (`app/ui.c`); the defaults
  by device; the four tags (`app/save.c`, `save.md`); the Textures tab's entries (`app/textures.c`);
  the Stress tab line and the benchmark columns (`app/stress.c`). Debug exports for the scene size
  and the image rectangle.
- **Docs.** `msaa.md` (targets sized to the scene), `viewport.md` and `gizmo.md` (the image
  rectangle), `textures.md`, `stress.md`, `AGENTS.md`, README.

## Phases

1. **Offscreen scene:** the scene target, the upscale pass, the targets sized to the viewport with
   the rounding, MSAA resolving into the scene target, still at 1/1. Checked: a still frame is the
   same as before the change (pixel comparison: at 1/1 the nearest pass is a copy), with MSAA on
   and off; resizing the window and dragging a splitter do not remake the targets every frame (the
   log); picking and the gizmo work.
2. **Scale mode:** the divisors, the View tab section, the defaults, `RSMD` and `RSCL`, the
   Textures tab entries. Checked: each divisor's scene size (debug exports), 1/2 shows even 2 × 2
   blocks and 1/4 even 4 × 4 blocks, picking and the gizmo at 1/3, a reload keeps the setting, an
   old save gets the device's default.
3. **Fixed mode:** the sizes and Custom, the fixed aspect ratio, the whole-multiple fit and the
   shrink, the black bars, the image rectangle for picking, panning and the gizmo, taps on the bars,
   `RSFW` and `RSFH`. Checked: 1280 × 720 in the desktop viewport (pillarboxed or letterboxed as the
   docks move, the visible scene unchanged while a splitter is dragged), a portrait size on the
   phone, a size larger than the viewport (shrunk, the note shown), a tap on a bar keeps the
   selection, a tap on the character picks it, the gizmo drags in a letterboxed image.
4. **Cost and docs:** the Stress tab line, the upscale pass's GPU time, the benchmark columns, a
   phone-size check, the documents.

Every phase is checked in Release and Debug in headless Chromium, at desktop and phone size.
SwiftShader's timings mean nothing; real costs come from devices.

As built:

- The Fixed mode's code (the size, the whole-multiple fit, the shrink, the image rectangle) and the
  save tags came with phases 1 and 2, where the scene output was first computed; phase 3 added its
  View tab controls and checked it; the upscale pass's timestamp pair came with phase 1.
- The targets stay allocated while they are large enough and at most twice the pixels needed, so a
  fixed 1280 × 720 can sit in targets left from a larger viewport (the log lines say what is held).
- The upscale shader reads the scene color target with `textureLoad` (the texel under the screen
  pixel, counted from the image's corner in blocks of `pixel_size`), not a sampler: that is exact
  nearest filtering, and also what lets a shrunk fixed size drop pixels evenly.
- The scene color target is made like the canvas (`BGRA8Unorm` with the sRGB view format), and the
  Textures tab previews it through the same path as other `Unorm` textures (their bytes are
  display-encoded), which came out right.
- Checked in a Debug build at 1280 × 800, 1920 × 1080 and 390 × 664 (touch): the scene size for each
  divisor; even 2 × 2 and 4 × 4 blocks at 1/2 and 1/4; the first frame at 1/1 equal to the
  previous build's in the static regions, with MSAA on and off; 1280 × 720 pillarboxed and
  letterboxed in a 1320 × 832 viewport with black bars; 640 × 360 shown at ×2 as even blocks; a
  portrait size; 4096 × 2160 shrunk to 0.32 keeping its aspect ratio; the scene inside the image
  identical after a splitter drag; a tap picking the character, a tap on a bar keeping the
  selection, a tap on the empty background clearing it, the gizmo dragging and a drag from a bar
  orbiting; a reload keeping both modes; a bad divisor and a missing tag; the phone's default of
  1/2; the Stress tab's resolution line.
- Not measured: the real cost of the upscale pass and of each scale on a GPU; SwiftShader's numbers
  say nothing about it.

## Out of scope

- FSR 1 or any smoothing upscaler, and dynamic resolution (later steps on this design).
- Scaling the UI.
- Bars in another color or with a pattern.

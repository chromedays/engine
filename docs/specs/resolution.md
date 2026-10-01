# Scene resolution spec

Status: proposed (2026-10-01). Changes to this spec are agreed first.

## Goal

Let the scene render at a resolution of its own, set as a share of the viewport's pixels, and
scale it up (or down) into the viewport. Below 100% it trades sharpness for speed where the GPU is
the limit, which the stress benchmark found on phones: a phone with a 3× pixel ratio renders its
viewport at about 1170 × 1000 pixels today, nine pixels per CSS pixel. The scene is scaled up with
the nearest-pixel filter only, so every rendered pixel shows as an even square block.

The editor UI (the docks, the gizmo, the build label) stays at the canvas's full resolution: only
the scene changes.

## Today

The scene pass draws straight into the canvas, inside the viewport rectangle (viewport and
scissor), and with MSAA it draws into a 4-sample texture of the whole canvas's size and resolves it
into the canvas (`msaa.md`). The depth target is the canvas's size too. So the scene's resolution
is always the canvas's, and its targets cover the docks and panels it never draws.

## Approaches

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **An offscreen scene target and an upscale pass** (recommended) | The scene pass renders into its own color texture at the scene resolution (MSAA resolves into it); a small pass then draws that texture into the viewport rectangle of the canvas with the nearest-pixel filter, before the ImGui pass | One path for every scale; the targets shrink to the scene's own size, which also does what `msaa.md` left for later (no 4-sample texture under the docks). The scene color becomes a sampled texture the Textures tab can show | One more pass, a full-viewport read and write, even at 100% |
| Render straight into the canvas at 100%, offscreen only when scaled | Two paths | Saves the extra pass at 100% | Two code paths to keep right; at 100% the targets stay canvas-sized |
| Lower the canvas's own resolution (fewer framebuffer pixels per CSS pixel) | The whole page renders at a lower pixel ratio | No new pass | Blurs the UI text too, which is what we must not touch |
| AMD FidelityFX Super Resolution 1 (FSR 1: EASU upscale + RCAS sharpen; MIT; GLSL/HLSL shaders) | A spatial upscaler, smoother than nearest at 50–77% | Ported to WGSL, it would replace the nearest pass | Two passes and a port to maintain; worth it only once low scales are used. A later option (below) |
| Dynamic resolution (the scale follows the frame time) | Lowers the scale when frames run long | Steady frame rate | Needs reliable GPU timings, which SwiftShader cannot give for tests; a later step on top of this one |

Recommendation: an offscreen scene target and a nearest-pixel upscale pass, written by us. FSR 1 and
dynamic resolution are left for later and need no change to this design: they replace the upscale
pass and set the scale.

## Decisions

| Topic | Decision |
|---|---|
| Setting | **Resolution** in the View tab, a combo of presets: 100%, 50%, 33% and 25% of the viewport's framebuffer pixels per side (1/1, 1/2, 1/3, 1/4). Each preset shows the pixels it gives ("50% · 585 × 497") and how many per CSS pixel ("1.5 px per CSS px") |
| Default | 100% on the desktop UI. 50% on the phone UI, where the GPU is the limit and a 3× screen still gets 1.5 pixels per CSS pixel. The setting changes either |
| Upscale filter | **Nearest only**: each rendered pixel becomes a block of whole screen pixels, crisp and exactly what was rendered. That is why the presets are whole fractions: at 1/2 every block is 2 × 2, while a share like 67% or 75% would mix 1- and 2-pixel-wide blocks across the image. Nothing above 100%: with nearest filtering, extra pixels would be dropped, not averaged, so they would cost without showing |
| Scene target | A single-sample color texture at the scene resolution, sampled by the upscale pass. Made like the canvas (its format with the sRGB view format, rendered and sampled through the sRGB view), so the MSAA resolve writes the same colors it does today (`msaa.md` found a texture created directly in the sRGB format resolved too dark) |
| MSAA and depth | The 4-sample color texture and the depth texture are the scene resolution too, not the canvas's. MSAA resolves into the scene target |
| Sizes and resizing | The targets are allocated rounded up to multiples of 64 pixels and kept while they are large enough, so a splitter drag or a window resize does not remake them every frame; they are remade smaller when they hold more than twice the pixels needed. The scene renders into the top-left part (viewport and scissor), and the upscale pass reads that part |
| Limits | The scene resolution is clamped to the device's `maxTextureDimension2D` (8192 in WebGPU's defaults) and to at least 1 × 1 |
| Upscale pass | A full-screen triangle with the viewport and scissor of the viewport rectangle, sampling the scene target with a clamp-to-edge sampler; it clears the canvas first (the scene pass no longer touches the canvas), and the docks drawn later cover the rest |
| Input | Unchanged: taps, orbit, picking and the gizmo work in the viewport's CSS pixels and in normalized device coordinates, which do not depend on the scene resolution |
| Debug lines, selection boxes | Drawn in the scene pass, so they scale with the scene (1 pixel wide at the scene resolution). The gizmo and the build label are ImGui and stay sharp |
| Saved | `EDIT` tag `RSCL` (u32, the scale in percent: 100, 50, 33 or 25; anything else loads as the device's default). The phone's and the desktop's defaults differ, so a save written on one device and loaded on another keeps the user's choice, not the other device's default |
| Textures tab | Lists the **scene color** target with a preview (it is sampled anyway), and the depth and MSAA targets at their new size. The depth target no longer needs "the scene's part" cut out: it is the scene |
| Cost shown | The Stress tab shows the scene resolution ("585 × 497, 50%") and the GPU time of the upscale pass (a third timestamp pair); the benchmark records the scale |
| Third-party | None now; FSR 1 (MIT) is the candidate when a sharper upscale is wanted |

## Memory

With MSAA 4× at 50% on a 3× phone (viewport about 1170 × 1000), the scene's targets hold about
585 × 500 pixels: color 4.5 MB (4×), depth 4.5 MB (4×) and the scene target 1.1 MB, against
about 90 MB for the canvas-sized MSAA targets today. At 100% on a 1280 × 800 desktop with the
default docks (viewport 680 × 552), about 13 MB against 32 MB.

## Changes

- **Renderer (`nv/renderer.h`, `engine/src/renderer.c`).** `NvRenderer.render_scale` (percent) and
  the scene color target, and the MSAA and depth targets sized by
  `update_scene_targets` from the viewport and the scale (with the rounding above); the upscale
  pipeline (a full-screen triangle and two samplers); the scene pass into the scene target, then the
  upscale pass into the canvas; `NvRenderer.scene_width` and `scene_height` for the UI; a third
  timestamp pair for the upscale pass.
- **App.** The View tab's Resolution section (`app/ui.c`), the defaults by device, the `RSCL` tag
  (`app/save.c`, `save.md`), the Textures tab's entries (`app/textures.c`), the Stress
  tab line and the benchmark columns (`app/stress.c`).
- **Docs.** `msaa.md` (targets sized to the scene), `textures.md`, `stress.md`, `AGENTS.md`, README.

## Phases

1. **Offscreen scene:** the scene target, the upscale pass, the targets sized to the viewport with
   the rounding, MSAA resolving into the scene target, still at 100%. Checked: a still frame is the
   same as before the change (pixel comparison: at 100% the nearest pass is a copy), with MSAA on
   and off; resizing the window and dragging a splitter do not remake the targets every frame (the
   log); picking and the gizmo work.
2. **The setting:** the scale, the View tab section, the defaults, the save tags,
   the Textures tab entries. Checked: each preset's scene size (debug exports), 50% shows
   even 2 × 2 blocks and 25% even 4 × 4 blocks, a reload keeps the setting, an
   old save gets the device's default.
3. **Cost and docs:** the Stress tab line, the upscale pass's GPU time, the benchmark columns, a
   phone-size check, the documents.

Every phase is checked in Release and Debug in headless Chromium, at desktop and phone size.
SwiftShader's timings mean nothing; real costs come from devices.

## Out of scope

- FSR 1 or any sharpening upscaler, and dynamic resolution (later steps on this design).
- A fixed output resolution (e.g. 1920 × 1080 whatever the window) with letterboxing.
- Scaling the UI.

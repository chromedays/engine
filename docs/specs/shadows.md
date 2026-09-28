# Directional shadows spec

Status: implemented (2026-09-28). Changes to this spec are agreed first.

## Goal

The sun casts shadows: the character, the planet and the stress scene's cubes darken what is
behind them from the light, and the character stands on the ground instead of floating over it.
It has to stay affordable on a phone, where the benchmark already found the GPU to be the limit.

Today the renderer has one scene pass (`engine/src/renderer.c`): every mesh node is one draw, and
the fragment shader lights it with the first directional light (Lambert plus ambient), with no
shadows.

## Approaches

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **One shadow map, fitted to the view** (recommended first step) | Render depth from the light into one texture that covers the camera's view up to a shadow distance; the scene pass compares against it | One extra depth-only pass; works for both scenes; simple to get stable | Resolution is spread over the whole shadow distance: sharp near a small scene, soft over the stress grid |
| Cascaded shadow maps (2 to 4 cascades) | Several maps, each covering a slice of the view distance | The standard for large outdoor views | One pass per cascade (the draws multiply again), and blending between cascades. A later step if the single map is too soft |
| A fixed map around the scene | One map over fixed world bounds | Simplest | The stress grid is about 150 m wide: far too coarse there |
| Ray-traced or screen-space shadows | | | Not available in WebGPU, or far too costly on phones |
| A library | None fits: shadows are the renderer's own passes and shaders | | |

Recommendation: one map fitted to the view, with the code shaped so cascades can be added later
(the light matrix and the map are per cascade from the start, with one cascade).

## Decisions

| Topic | Decision |
|---|---|
| Light | The first directional light, as today. Other light types cast no shadows |
| Map | 2048² by default, 1024² on touch screens (`NvImgui.ui_scale` > 1), settable to 512, 1024, 2048 or off |
| Format | Both `depth32float` (the default) and `depth16unorm`, chosen in the settings. Changing it remakes the map and the shadow pipelines (the depth format is part of a pipeline). `depth16unorm` halves the map's memory and bandwidth; `depth32float` keeps more precision if the light's depth range grows. The Stress tab's shadow pass time shows what the choice costs on a device |
| Fitting | The camera's view from its near plane to the **shadow distance** (30 m by default) is enclosed in a sphere; the light's orthographic box is that sphere's square, pulled back toward the light by 50 m so casters behind the view still cast. The sphere's radius does not change as the camera turns, and its center is snapped to whole shadow-map texels, so shadows do not shimmer or crawl while orbiting |
| Casters | Every mesh node, static and skinned (the skinned vertex shader's skinning is reused), except those whose world box is outside the light's box (a CPU test with the boxes picking already uses). Double-sided materials cast from both faces |
| Receivers | Every mesh node. The shadow darkens only the directional light's term; the ambient term stays |
| Filtering | A comparison sampler with linear filtering, so each lookup is a 2×2 percentage-closer filter in hardware. **High**: 3×3 such lookups (a soft 4×4-texel edge). **Low**: one lookup. High by default, Low on touch screens |
| Acne and peter-panning | A slope-scaled depth bias in the shadow pipeline, and a normal offset in the scene shader (the lookup moves along the surface normal by about one texel). The constant bias means different things per format (below), so each format has its own tuned bias values, set with its pipelines; the slope scale and the normal offset are shared. All are tuned in the showcase and the stress scene |
| Beyond the distance | Unshadowed, faded out over the last 10% so the edge is not a line |
| Settings | A **Shadows** section in the View tab: size (off, 512, 1024, 2048), format (32-bit float, 16-bit), filter (Low, High), distance (5 to 100 m), and "Show light box" (the fitted box as debug lines). Saved with the editor settings (new `EDIT` tags, per `save.md`) |
| Cost shown | The Stress tab gains shadow draws, and the GPU time of the shadow pass (a second timestamp pair). The benchmark table gains the shadow settings it ran with (size, format, filter) |
| Third-party | None |

## Notes on the GPU side

These hold for WebGPU as specified, whatever GPU or driver the browser runs on.

- **A depth-only pass is cheap.** The shadow pipelines have no fragment stage and never discard,
  so the pass writes depth only and does no pixel shading.
- **Hardware PCF.** A sampler with a comparison function and linear filtering makes one
  `textureSampleCompare` compare the four nearest texels and blend the results: a 2×2
  percentage-closer filter for the cost of one lookup. The High filter is nine such lookups.
- **The two formats.** Every WebGPU implementation can render to both and sample both with a
  comparison sampler, so both are always offered.
  - `depth32float` needs no depth range tuning: precision stays ample whatever the shadow
    distance and the 50 m pulled toward the light. It is 16 MB at 2048².
  - `depth16unorm` has 65,536 even steps over the light's depth range (the orthographic
    projection spreads depth linearly), about 1.5 mm over 100 m. It is 8 MB at 2048², and half
    the bandwidth to write and read.
- **Depth bias depends on the format.** The pipeline's constant `depthBias` counts the depth
  format's smallest step. That step is fixed for a normalized format like `depth16unorm`
  (1/65,536 of the range), but for `depth32float` it depends on the triangle's own depth. So one
  constant cannot suit both: each format's pipelines carry their own tuned value.
- **One pass reads what the other wrote.** The shadow map is written in the shadow pass and
  sampled in the scene pass of the same command buffer; WebGPU orders the two passes, so no
  explicit barrier is needed.

## Engine changes

- **Renderer (`nv/renderer.h`, `engine/src/renderer.c`).**
  - `NvShadowSettings` (size, format, filter, distance) and `nv_renderer_set_shadows`.
  - The shadow map texture and its views, remade when the size or the format changes; a
    comparison sampler.
  - `FrameUniforms` gains the light's view-projection matrix, the map's texel size, the filter
    and the distance; the frame bind group gains the map and the sampler.
  - Depth-only pipelines (static and skinned, each with the double-sided variant) for the chosen
    format, with that format's bias, remade when the format changes. They use new vertex entry
    points that transform by the light's matrix.
  - `nv_renderer_draw` records the shadow pass first, then the scene pass; the fragment shader
    applies the shadow to the directional term.
  - The light box as debug lines on request; stats for shadow draws and the shadow pass's GPU
    time.
- **App.** The View tab's Shadows section, its save tags, the touch-screen defaults, and the
  benchmark's shadow column.

## Phases

1. **Shadows:** the map (`depth32float`), the fitted light box, the depth-only pipelines for static and skinned
   meshes, one hardware PCF lookup with a slope-scaled bias, and on/off in the View tab. Checked:
   the character's and the planet's shadows land on the ground in the showcase; the stress scene
   draws without asserts at its maximum workloads.
2. **Quality:** the 3×3 filter, the normal offset, texel snapping (no shimmer while orbiting,
   compared frame to frame in screenshots), the distance and its fade, caster culling, the light
   box lines, the `depth16unorm` option with its own bias (no acne or peter-panning in either
   format, compared in screenshots), the settings saved and their touch defaults.
3. **Cost and docs:** the shadow stats and GPU time, the benchmark column, a phone-size check,
   `AGENTS.md` and README.

As built, the engine side of phase 2 (the 3×3 filter, the normal offset, texel snapping, the fade
and the light box) arrived with phase 1, since they are a few lines each in the same code; phase 2
then added caster culling, the saved settings and the `depth16unorm` checks. At phone size the
touch defaults (1024, Low) give visibly stepped shadow edges; High smooths them at nine lookups.

Every phase is checked in Release and Debug in headless Chromium, at desktop and phone size.
SwiftShader's timings mean nothing; real costs come from devices.

# MSAA spec

Status: proposed (2026-10-01). Changes to this spec are agreed first.

## Goal

Smooth the scene's triangle edges with 4× multisample anti-aliasing (MSAA). Today the scene pass
draws straight into the canvas with one sample per pixel, so every silhouette (the character,
the cubes, the ground's horizon) and every debug line is stair-stepped, most visibly on a phone.

The engine is a forward renderer, the case MSAA suits: the fragment shader still runs once per
pixel per triangle; only coverage and depth are kept per sample, and the samples are averaged
(resolved) into the canvas at the end of the pass.

## Approaches

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **MSAA 4× in the scene pass** (recommended) | The scene pass renders into a 4-sample color texture and a 4-sample depth texture; WebGPU resolves the color into the canvas at the pass's end (`resolveTarget`) | Built into WebGPU (`sampleCount` 4 is the one count every implementation supports). Shading cost stays the same inside triangles; only edge pixels shade more than once. A few dozen lines in `renderer.c` | Memory: the two textures are 4× the size of a single-sample one (below). It does nothing for aliasing made inside a shader (sharp specular, texture detail), which this renderer barely has today |
| FXAA or SMAA (a post pass) | Finds edges in the finished image and blurs along them | One cheap full-screen pass; also softens shader aliasing | Needs the scene in an offscreen texture first (there is none: the scene draws into the canvas), blurs text-like detail and thin lines, and crawls in motion. SMAA also needs lookup-table textures |
| TAA (temporal) | Jitters the camera every frame and blends with past frames | Best quality per cost in large engines | Needs motion vectors, history textures and ghosting fixes; far too much for this renderer now |
| Supersampling | Renders at 2× size and scales down | Simplest to reason about | 4× the shading cost everywhere; the phone is already GPU-bound |
| A library | None: MSAA is the API's own, and the post-process filters are shader code we would port | | |

Recommendation: MSAA 4×, with Off kept as a setting. No third-party library.

## Decisions

| Topic | Decision |
|---|---|
| Sample counts | Off (1) and 4×. WebGPU guarantees only 1 and 4 for render attachments, so 2× and 8× are not offered |
| Default | 4× on every device. The Stress tab shows what it costs on each one (below), and the setting turns it off |
| Setting | An **Anti-aliasing** combo in the View tab (Off, MSAA 4×), beside the Shadows section, on both UIs |
| Saved | A new `EDIT` tag, `MSAA` (u32: 1 or 4; anything else loads as 4). Missing: the default |
| Engine API | `NvRenderer.msaa` (the sample count), set by the app like `NvRenderer.shadows`. `nv_renderer_draw` keeps its signature: the canvas view it is given becomes the resolve target |
| Color target | A 4-sample texture of the canvas's size and its render format (the sRGB view format, so samples are averaged in linear space and edges are not darkened). Store op Discard: only the resolved canvas is kept. Remade when the canvas size, the format or the sample count changes, like the depth buffer |
| Depth target | The existing `depth32float` target becomes 4-sample too (a pass's attachments share one sample count). Reverse Z is unchanged. WebGPU cannot resolve depth, so it stays Discard, unless the Textures tab keeps it (below) |
| Pipelines | Every pipeline used in the scene pass (the meshes, static and skinned, single- and double-sided, and the debug lines) gets `multisample.count` = the setting, and is remade when it changes. The shadow pass and the ImGui pass stay single-sample: they have their own attachments |
| Viewport | The scene pass already sets a viewport and scissor inside the canvas; the resolve writes the whole canvas, which is what the pass's clear does today, and the editor's windows draw over the rest afterwards |
| Alpha | No change. Nothing uses alpha testing (`discard`), so alpha to coverage is not needed. The blended debug lines blend per sample |
| Textures tab | It shows the depth target as a 4-sample texture: a second depth preview pipeline reads `texture_depth_multisampled_2d` with `textureLoad(…, sample 0)`. The 4-sample color texture is not listed (it is discarded every frame) |
| Picking, gizmo, shadows | Unaffected: picking is a CPU ray, the gizmo draws in the ImGui pass, the shadow map has its own pass |
| Cost shown | The startup log's color and depth target lines give the sample count and the memory of each. The Stress tab gains an "MSAA" line, and the benchmark table records the setting it ran with |
| Third-party | None |

## Memory

Each 4-sample target is four times a single-sample one: 4 bytes × 4 samples per pixel for both
`bgra8unorm-srgb` and `depth32float`.

| Canvas (framebuffer pixels) | Color, 4× | Depth, 4× (today 1×) | Added by MSAA |
|---|---|---|---|
| 1280 × 800 (desktop) | 16 MB | 16 MB (4 MB) | 28 MB |
| 1920 × 1080 | 32 MB | 32 MB (8 MB) | 56 MB |
| 1170 × 2532 (a phone at 3× pixel ratio) | 45 MB | 45 MB (11 MB) | 79 MB |

Mobile GPUs keep a pass's samples in on-chip tile memory and write out only the resolved pixels,
so the bandwidth cost there is small, but the memory is still allocated: browsers expose no
"transient attachment" to WebGPU pages. Sizing the targets to the scene viewport instead of the
whole canvas (60% of a phone's height) would save that share, at the cost of a copy into the
canvas after the resolve; it is left for later, once the Stress tab shows whether it matters.

## Changes

- **Renderer (`nv/renderer.h`, `engine/src/renderer.c`).** `NvRenderer.msaa`; the 4-sample color
  texture and its view; `update_depth_buffer` creating the depth target with the sample count;
  the scene pipelines created with it (remade on change); the scene pass's color attachment
  pointing at the 4-sample view with the canvas as `resolveTarget`, or straight at the canvas when
  Off; log lines with the sample count and the sizes.
- **ImGui previews (`engine/src/imgui.c`).** A multisampled depth preview pipeline and bind group
  layout, chosen when the previewed depth texture has more than one sample.
- **App.** The View tab's Anti-aliasing combo (`app/ui.c`), the `MSAA` tag (`app/save.c` and
  `save.md`), the Stress tab's line and the benchmark's column (`app/stress.c`).
- **Docs.** `textures.md` (the multisampled depth), `stress.md`, `AGENTS.md` and README.

## Phases

1. **MSAA:** the setting in the renderer, the 4-sample color and depth targets, the scene
   pipelines and the resolve, the View tab combo with 4× by default. Checked: edges in a still
   frame have blended pixels with 4× and none with Off (screenshots compared along the
   character's silhouette and a cube's edge), the inside of triangles is unchanged, switching at
   runtime and resizing the window raise no WebGPU errors, picking and the gizmo still work.
2. **Around it:** the `MSAA` save tag (a reload keeps the setting; an old save gets 4×), the
   Textures tab's multisampled depth preview, the log lines with the memory.
3. **Cost and docs:** the Stress tab line and the benchmark column (the GPU scene pass time with
   Off and with 4× at the stress scene's largest workloads), a phone-size check, and the
   documents above.

Every phase is checked in Release and Debug in headless Chromium, at desktop and phone size.
SwiftShader's timings mean nothing; real costs come from devices.

## Out of scope

- 2× and 8× (not guaranteed by WebGPU), FXAA, SMAA and TAA.
- Targets sized to the viewport (see Memory).
- Alpha to coverage, until a material uses alpha testing.

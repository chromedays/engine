# Stress scene spec

Status: implemented (2026-09-27). Changes to this spec are agreed first.

## Goal

A second scene in the app that loads the engine with many objects. It measures where the frame
time goes as counts grow, so we know what to optimize next (culling, instancing, draw sorting)
and can compare devices and commits.

Today every node is one draw call, nothing is culled or instanced, and the whole object buffer is
uploaded every frame. None of that has been measured yet.

## Decisions

| Topic | Decision |
|---|---|
| Where | A second scene in the same app. The View tab switches between Showcase and Stress. (`#stress` in the URL opened it directly until the autosave spec removed it; the app now always starts on the showcase) |
| Limits | Raised after the first phone results, where every step still ran at 60 fps: `NV_MAX_NODES` 16384 (was 4096), `NV_MAX_ANIMATORS` 256 (was 64), `NV_MAX_DEBUG_LINES` 16384 (was 8192); `NV_MAX_MATERIALS` stays 256 |
| Benchmark | A button runs fixed steps and shows a table that can be copied |
| Timing | Measured in the app with `emscripten_get_now`; GPU time only where the browser offers `timestamp-query` |
| Third-party | None. Tracy (C++, BSD) was considered as a profiler, but its wasm support is weak |

## Workloads

Each workload has an on/off toggle and a count slider. The sliders are clamped so that all
workloads together stay under `NV_MAX_NODES`.

| Workload | What it builds | Max | Measures |
|---|---|---|---|
| Cube grid | N static cubes on a square grid | 16000 | draw calls, scene update, object upload |
| Deep hierarchy | a chain of N small cubes, each the child of the last, all turning | 1000 | transform propagation |
| Crowd | N characters in rows, each on its own clip and start time | 200 | ozz sampling, skin matrix upload, skinning shader |
| Material variety | the grid uses one material, or up to 256 distinct colors | 256 | bind group switches |
| Churn | K grid cubes removed and added again every frame | 256/frame | free list, generation ids |
| Bone overlay | bone lines for every crowd character | ~12800 lines | debug lines |

The stress scene also has a camera, a sun and a ground. Big workloads sit under group nodes
("grid", "chain", "crowd") that start collapsed in the Scene tab. Otherwise the tree would draw
thousands of rows every frame and distort the measurements.

## Engine changes

- **Animators belong to a scene.**
  - `nv_anim_create_animator(skeleton, scene, owner)` records the scene.
  - `nv_anim_update_scene(scene, dt)` updates only that scene's animators, so the hidden scene's
    characters pause instead of asserting on a node from the other scene.
- **Instantiating a loaded model.**
  - `nv_gltf_instantiate(const NvGltfModel* model, NvScene* scene, NvNodeId parent, NvGltfModel* out)`
    copies the model's node tree into `scene` with a new root under `parent`.
  - It reuses the same meshes, materials and skeleton, and creates a new animator. Without it,
    each crowd character would load its own meshes and textures and run past `NV_MAX_MESHES`.
- **Draw statistics.**
  - `NvRenderer.stats` (`NvRenderStats`: draws, triangles, skinned draws, pipeline and bind group
    changes) is filled by `nv_renderer_draw` every frame.

## App

- **Scenes:**
  - `App` holds the showcase and stress scenes and the one that is shown.
  - The Scene, Inspector and View tabs work on the shown scene. Each scene keeps its own
    selection and camera.
  - The stress scene is built the first time it is shown.
- **Switching:**
  - A combo box in the View tab switches scenes. (It also set a `#stress` URL hash, read at
    startup, until `save.md` removed it.)
- **Stress tab** (shown while the stress scene is):
  - the workload toggles and sliders;
  - live stats: FPS; frame time (average and max over the last second); CPU time for animation
    update, scene update, draw recording and ImGui; node, draw, triangle and skinned counts;
    GPU time when available.
- **Benchmark:** "Run benchmark" steps through fixed setups and records 3 seconds of each, after
  1 second of warm-up.
  - Cubes: 250, 1000, 4000, 8000 and 16000, the crowd off.
  - Characters: 8, 32, 60, 120 and 200, the grid off.
  - The table also shows the load: the larger of the CPU stage total and the GPU pass, as a
    share of the frame time. It keeps rising while vsync holds the frame rate.
  - The table shows average and worst frame time and the CPU stage times per step.
  - "Copy" puts the table on the clipboard as text, with the browser's user agent and the commit
    hash.
  - Touching the workload controls stops a run.

## Phases

1. **Engine:** scene-owned animators, `nv_gltf_instantiate` and render stats. Check that the
   showcase scene behaves exactly as before.
2. **Scene:** the stress scene, its workloads, scene switching and `#stress`.
3. **Measurement:** the stats in the Stress tab, the benchmark and the copyable table, then GPU
   timestamps where supported.

Every phase is checked in Release and Debug in headless Chromium at desktop and phone size:

- every workload at its maximum without asserts;
- switching scenes back and forth with state kept;
- `#stress` opening directly;
- a full benchmark run finishing.

The numbers from SwiftShader are not meaningful; real measurements come from real devices.

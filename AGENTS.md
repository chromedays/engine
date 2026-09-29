# AGENTS.md

Guidance for AI coding agents working in this repository.

## Communication

- Answer in both Korean and English in every response. Write the Korean version first, then the
  English version with the same content.
- Commit messages, code comments and documentation stay in English.

## Project

nv ("night view") is a C17 rendering engine on WebGPU that runs in the browser. It compiles to
WebAssembly with Emscripten, and WebGPU calls go to the browser through the `emdawnwebgpu` port.
There is no native build.

```
engine/include/nv/         public API: base.h (types, asserts, arenas), math.h, scene.h, window.h, gpu.h,
                           imgui.h (Dear ImGui, ImGuizmo), renderer.h (meshes, materials, skinning, debug lines),
                           gltf.h (cgltf loading), anim.h (skeletal animation over ozz-animation),
                           chunk.h (tagged binary files), storage.h (files kept in IndexedDB)
engine/src/                window.c, gpu.c, scene.c, imgui.c, renderer.c, gltf.c, chunk.c, storage.c,
                           anim.cpp (the ozz wrapper; our only C++ file)
app/                       the app: main.c (showcase scene, frame), stress.c (stress scene and
                           benchmark, picked in the View tab), ui.c (editor panel), save.c (autosave),
                           undo.c (undo and redo),
                           app.h (shared state)
assets/                    binary assets (Git LFS); assets/quaternius/ is built by tools/trim_assets.sh
tools/                     offline asset scripts (run with npx; nothing installed into the repo)
tests/                     tests that need no browser, built for Node and run with ctest
web/                       index.html.in (the page: downloads the app with a progress bar, then
                           starts it), manifest.cmake (file sizes for that progress bar)
docs/CODING_STANDARD.md    coding standard (read before writing code)
docs/specs/                feature specs (read the relevant one before working on a feature)
.github/workflows/build.yml  CI: Release and Debug web builds + GitHub Pages deploy
```

## Build and run

Requires the Emscripten SDK (tested with 6.0.10), CMake 3.30+ (ozz-animation needs it) and Git LFS. Cloud sessions may not have Git LFS;
install it with `apt-get install -y git-lfs && git lfs install --local && git lfs pull`.

```sh
emcmake cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
python3 -m http.server -d build/app 8000   # then open http://localhost:8000/
```

## Conventions

Follow `docs/CODING_STANDARD.md`. The web-specific rules:

- `wgpuSurfacePresent` must not be called; the browser presents the canvas.
- Blocking waits use `wgpuInstanceWaitAny`, which works through Asyncify.
- `nv_window_run` never returns, so app state must not live on `main`'s stack.
- ImGui input reads page client coordinates, which match canvas coordinates only because the page
  pins the canvas to the top-left corner. Keep that true in `web/index.html.in`.
- UI code uses the cimgui API (`ig*` functions, `ImGuiIO_*`), fetched by CMake. Call it between
  `nv_imgui_new_frame` and `nv_imgui_render`.
- On touch screens ImGui text arrives through a hidden `<input id="nv-text-agent">` so phones
  show their keyboard; clipboard pastes arrive through the page's `paste` event. Both are set up
  in `engine/src/imgui.c`.
- `NvImgui.ui_scale` is 1.5 on touch screens; size ImGui windows with it.
- The app uses the editor layout: `nv_editor_layout` splits the canvas into the
  scene viewport (top 60%, passed to `nv_renderer_draw`) and the editor panel (bottom 40%, filled
  with `nv_imgui_begin_panel`, which scrolls on a vertical touch drag). Keep editor UI inside the
  panel. The exceptions are the build label in the viewport's top-left corner and the transform
  gizmo on the selection.
- Mouse and touch input that starts in the viewport (`NvImgui.view_rect`) skips ImGui and arrives in
  `NvImgui.view` (orbit, pan, dolly, tap); the app turns it into camera moves and picking
  (`nv_renderer_view_ray`, `nv_renderer_pick`). Playwright drives it with mouse drags, the wheel and
  CDP `Input.dispatchTouchEvent` for multi-touch.
- The transform gizmo is ImGuizmo through cimguizmo (`#include <cimguizmo.h>`, `ImGuizmo_*`).
  A press on its handle reaches ImGui through `NvImgui.view_grab`: a left press or one-finger touch
  in the viewport waits two frames before the hook decides. Tests must hold the press longer than
  that before moving. `ImGuizmo_BeginFrame` runs before the panel is built, so the gizmo's window
  stays behind the panel.
- The app autosaves its state (the showcase and the editor settings, not the stress scene) to
  `/nv-save/state.nvs` in IndexedDB (`docs/specs/save.md`). A new piece of editable state gets a
  tag in `app/save.c` and a row in the spec's tables. Never change what an existing tag means
  without raising `SAVE_VERSION`: old saves must keep loading. Debug builds check at start that
  save, load, save gives the same bytes; tests call `Module._app_debug_save_round_trip()` after
  editing.
- The showcase has Edit and Play modes (`docs/specs/play.md`). In Edit mode nothing may move the
  scene by itself: anything time-driven (spins, walking, sweeps, jumps) goes in the
  `app->playing` branch of `update_showcase`, and the animation only previews in place. Play
  snapshots the showcase in the save format and Stop loads it back (`SAVE_PART_SCENE`), so
  anything Stop must restore has to be in the save. The autosave writes that snapshot while
  playing, and undo is off. Debug builds expose `Module._app_debug_save_crc()` to compare the
  state before Play and after Stop.
- Undo (`docs/specs/undo.md`) needs no code at edit sites: whenever no widget, gizmo or popup is in
  use, `app/undo.c` compares the selected node, the character and the scene settings (written by
  `save_write_scope`) with the last commit. So a new editable value is undoable once it is saved in
  its scope; a value the app changes every frame in Edit mode must be left out there
  (`driven_fields` in `app/save.c`, now only the orbit camera), or every frame becomes a step. Debug builds expose `Module._app_debug_undo_steps()`
  and `_app_debug_undo_done()` for tests.
- The first directional light casts shadows through one shadow map fitted to the view
  (`docs/specs/shadows.md`); the app sets `NvRenderer.shadows` and the renderer remakes the map and
  its depth-only pipelines when the size or format changes. The shadow pass binds
  `shadow_frame_group`, which leaves the map out: a pass cannot sample the texture it renders to.
  A new mesh vertex layout needs a matching entry point in `shadow_shader` too.
- The scene pass uses reverse Z: a `depth32float` buffer cleared to 0, compare `Greater`, and clip z
  turned into w - z by `reverse_depth` in `renderer.c`, so depth runs 1 (near) to 0 (far). The
  camera matrices the API returns (`nv_renderer_camera_matrices`, `nv_renderer_view_ray`) stay
  standard 0..1. The shadow map keeps standard depth, since its projection is orthographic.
- In `engine/src/anim.cpp`, ozz headers are included before nv headers: `nv/base.h` defines
  `internal` as a macro, which breaks ozz's `internal::` namespace.
- Joint names are case-sensitive and come from the asset (the Quaternius rig has `Head`, `hand_r`).
  Assert on `nv_anim_find_joint` results.
- Animation runs through the scene: `nv_anim_update_scene` (before `nv_scene_update`) updates
  every animator, moves its `owner` node by root motion, aims look-at at `target_node` and fills
  `NvNode.attach` for nodes that follow a joint. Do not move those nodes by hand.
- `nv_gltf_load_model` creates the skeleton and animator for a skinned model; call `nv_anim_init`
  first.
- There is one executable, `app`. Its assets are packaged with
  `nv_setup_executable(app ROOT ASSETS <dir>)` and read from `/assets/...` with `fopen`. CI installs
  the Release build to `release/` and the Debug build to `debug/` on Pages (the site root has
  nothing). Debug builds keep their DWARF in `app.debug.wasm`, which only browser developer tools
  download.

## Assets

- Binary assets live under `assets/` and are stored with Git LFS. `.gitattributes` lists the
  tracked types; add a pattern there before committing a new binary type.
- Check `git lfs ls-files` after adding assets, so a binary never lands in plain Git history.
- Only commit assets whose license allows redistribution in a public repository, and record the
  source and license next to them.

## Third-party libraries

- When implementing a feature or designing a spec, first look for third-party libraries that
  would help. Present the candidates to the user (what each does, language, license, how it fits
  this engine and the coding standard, trade-offs) with a recommendation, including "write it
  ourselves" when that is the better fit.
- Do not add a library until the user confirms it. Then record the reason in the Dependencies
  section of `docs/CODING_STANDARD.md`.

## Testing without a display

There is no physical display in cloud sessions. Headless Chromium renders WebGPU with
`--enable-unsafe-webgpu --enable-features=Vulkan --use-vulkan=swiftshader
--use-webgpu-adapter=swiftshader --use-angle=swiftshader --disable-vulkan-fallback-to-gl-for-testing`.
Test both a Release build and a Debug build (the Debug build enables `NV_ASSERT`).
When driving ImGui with Playwright, hold clicks for about 100 ms (`mouse.down`, wait, `mouse.up`);
an instant click can land between frames and be missed, which real users never trigger.
`ctest --test-dir build --output-on-failure` runs `tests/` under Node (CI runs it for both builds).
Each Playwright browser launch starts with empty storage, and `page.reload()` keeps it, which is
how autosave is tested. Do not delete the IndexedDB database while the page is open: the deletion
waits for the page to close and then removes the save. The page's `FS` is a global, so tests can
read and damage `/nv-save/state.nvs` (then `FS.syncfs(false, ...)`).

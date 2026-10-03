# AGENTS.md

Guidance for AI coding agents working in this repository.

## Communication

- Answer in both Korean and English in every response. Write the Korean version first, then the
  English version with the same content.
- Commit messages and code comments stay in English.
- Every document is written in both Korean and English in one file: the Korean version first, then
  the English version with the same content.

## Project

nv ("night view") is a C17 rendering engine on WebGPU that runs in the browser. It compiles to
WebAssembly with Emscripten, and WebGPU calls go to the browser through the `emdawnwebgpu` port.
There is no native build.

```
engine/                    each module's header and source side by side, included as <engine/name.h>:
                           base.h (types, asserts, arenas), math.h, scene.c/.h, window.c/.h, gpu.c/.h,
                           imgui.c/.h (Dear ImGui, ImGuizmo), renderer.c/.h (meshes, materials, skinning, debug lines),
                           gltf.c/.h (cgltf loading), anim.cpp/.h (skeletal animation over ozz-animation; our only
                           C++ file), chunk.c/.h (tagged binary files), storage.c/.h (files kept in IndexedDB),
                           log.c/.h (the log ring the Console tab shows),
                           vfx.c/.h (effects: compute particles, trails, beams, decals) and vfx_cpu.c/.h (the
                           GPU-free parts tests run; engine-private: app code does not include vfx_cpu.h),
                           mesh.c/.h (vertex types; box, plane, sphere, cylinder, cone, capsule and torus primitives;
                           docs/specs/mesh.md), camera.c/.h (orbit camera math),
                           strings.c/.h (T, TL and the language; each executable brings its own table),
                           renderer_cpu.c (the renderer's GPU-free parts: scene resolution, camera matrices, view rays)
app/                       the app: main.c (showcase scene, frame), stress.c (stress scene and
                           benchmark, picked in the View tab), ui.c (editor panel), save.c (autosave),
                           undo.c (undo and redo), console.c (the Console tab), textures.c (the Textures tab),
                           effects.c (the effect definitions and the Effects workload),
                           ui_desktop.c and ui_phone.c (the two editor UIs), shortcuts.c (desktop shortcuts and the
                           palette's actions), search.c (panel search boxes, command palette), selection.c (the
                           multiple selection), app.h (shared state)
autobattler/               the auto-battler, a second executable with its own page (docs/specs/battle.md): battle.c/.h (the rules and
                           the 30 Hz tick; no GPU), defs.c (reads data/units.txt and stage.txt), main.c (window, frame loop),
                           battle_view.c (drawing, deployment input, the Battle panel), game.h (its state), strings.c (its Korean table)
assets/                    binary assets (Git LFS); assets/quaternius/ is built by tools/trim_assets.sh,
                           assets/fonts/ holds the UI font (Pretendard: English and Korean in one file)
tools/                     offline asset scripts (run with npx; nothing installed into the repo)
tests/                     tests that need no browser, built for Node and run with ctest
web/                       index.html.in (the page: downloads the app with a progress bar, then
                           starts it), manifest.cmake (file sizes for that progress bar)
docs/CODING_STANDARD.md    coding standard (read before writing code)
docs/specs/                feature specs (read the relevant one before working on a feature)
.github/workflows/build.yml  CI: Release and Debug web builds, staged for Pages in the gh-pages branch
.github/workflows/pages.yml  deploys gh-pages to GitHub Pages after each build
```

Where code goes: `engine/` holds what any app would need (rendering, GPU, window, scene, glTF,
animation, chunks, storage, log, ImGui's platform glue) and never refers to `App`; `app/` holds what
only this editor needs (its scenes, panels, search, shortcuts, saved state, undo, strings). Ask "would
another app use this?": yes goes in the engine, no in the app. Dependencies point one way, `app` to
`engine`; `autobattler/` is a second app on the same terms and uses nothing from `app/`.

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
  in `engine/imgui.c`.
- `NvImgui.ui_scale` is 1.3 on touch screens; size ImGui windows with it. The UI font is Pretendard
  Regular (`assets/fonts/`, 14 px, `nv_imgui_load_ui_font`; `docs/specs/fonts.md`), which has Latin and every Hangul
  syllable, not ImGui's built-in one.
- The app has two editor UIs, chosen once at start from the primary pointer (`App.ui_mode`; touch
  gives the phone UI, anything else the desktop UI; `docs/specs/layout.md`). `app_layout` fills
  `App.layout` (framebuffer pixels) each frame before `nv_imgui_new_frame`: the **desktop** has a top
  bar (menus, Play / Stop), left, right and bottom docks with draggable splitters, and the viewport
  in the middle (`app/ui_desktop.c`); the **phone** has a top bar (Undo, Play / Stop, Redo), the
  viewport (60%) and one tabbed panel (40%) (`app/ui_phone.c`). Neither file branches on the other's
  device; they share only the section functions in `app/ui.c` (`ui_scene_tab`, `ui_inspector_tab`,
  `ui_view_tab`, `console_tab`, `stress_ui`). The viewport is any rectangle, not the top-left corner:
  pass `App.layout.viewport` to the renderer, picking and the gizmo. Play / Stop is at the top center
  in both UIs, its center on the canvas's. Each dock is a window from `nv_imgui_begin_panel`, which
  scrolls on a vertical touch drag. Keep editor UI inside the docks and panel. The exceptions are the
  build label (with its badge) in the viewport's top-left corner and the transform gizmo on the
  selection. A splitter must not use `nv_imgui_begin_panel`: its NoBringToFrontOnFocus puts a new
  window behind the earlier ones. Debug builds export `Module._app_debug_layout(region, component)`,
  `_app_debug_ui_mode`, `_app_debug_dock`, `_app_debug_view` and `_app_debug_playing` for tests.
- Mouse and touch input that starts in the viewport (`NvImgui.view_rect`) skips ImGui and arrives in
  `NvImgui.view` (orbit, pan, dolly, tap); the app turns it into camera moves and picking
  (`nv_renderer_tap_ray`, `nv_renderer_pick`; the orbit camera is `NvOrbitCamera` in `engine/camera.h`, kept in
  `SceneView.orbit`, with the limits `ORBIT_LIMITS` set where a view is made). Playwright drives it with mouse drags,
  the wheel and CDP `Input.dispatchTouchEvent` for multi-touch.
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
- The selection (`docs/specs/selection.md`) is `SceneView.selected`, the primary node (picked last: the
  Inspector edits it, the camera follows it), plus up to 255 `others`. Change it only through
  `app/selection.c` (`selection_set`, `selection_add`, `selection_toggle`, `selection_keep_primary`),
  never by assigning `selected`, so the others and the Shift+click anchor stay consistent; read it with
  `selection_count` / `selection_get` / `selection_has`. Ctrl or Shift+click (viewport and Scene tab)
  and the phone's Multi toggle add and remove; Shift+click in the Scene tab selects a range of last
  frame's rows. A viewport tap's modifiers are `NvViewInput.tap_mods`, the keys held at the press, not
  the keys at the frame the tap arrives. `selection_prune` drops nodes that no longer exist each frame.
  The gizmo moves every selected node except the active camera and nodes under a selected node,
  around their center, applying its change since the drag started to each node's starting world
  matrix; undo's Node scope holds every selected node, so such a drag is one step. The save keeps the
  others as `SELO`. Debug builds export `_app_debug_selection_count`, `_app_debug_selection(i)`,
  `_app_debug_select(index, mode)`, `_app_debug_set_multi`, `_app_debug_tree_row`,
  `_app_debug_find_node`, `_app_debug_node` and `_app_debug_set_grid` for tests.
- Desktop keyboard shortcuts are rows of one table in `app/shortcuts.c` (`docs/specs/shortcuts.md`):
  add a key there, never a `igIsKeyPressed` at the action, so the menus' labels (`shortcut_label`),
  the help window (`?`) and the browser claim (`NvImgui.claims_key`: a bound key does not reach the
  browser, so Ctrl+S is not "Save page") stay in step. Nothing fires while a text field is being
  edited, a widget is held, a popup is open or the gizmo is dragged. The phone UI has no shortcuts.
  Tests press keys with Playwright and read `Module._app_debug_gizmo`, `_app_debug_state` and the
  other debug exports.
- Every widget of a searchable panel (Scene, Inspector, View, Textures and Stress tabs; the Console has
  its own filters) goes through `search_row` (one widget carrying its label), `search_group` (several
  widgets or a button) or, for text that is no setting, `if (search_plain(app))`; sections start with
  `search_section(app, "Heading")`, which draws the heading with its first visible row
  (`docs/specs/search.md`). A panel's content sits between `search_panel_begin` and `search_panel_end`
  (the box above, a scrolling child below). The same calls make the palette's list of settings: it runs
  the panels in collect mode, where `search_row` notes the row and draws nothing. A new setting is
  searchable and in the palette by being written that way. Commands (actions the palette lists and the
  desktop's keys) are rows of the table in `app/shortcuts.c`; a row without keys is palette only. Debug
  builds export `Module._app_debug_search_buffer`, `_app_debug_search_set(panel)`,
  `_app_debug_search_rows(panel)`, `_app_debug_palette(n)`, `_app_debug_palette_result(i, k)` and
  `_app_debug_palette_query(k)` for tests.
- UI text is written in English in the code and goes through `T("text")` (a printf format keeps
  its conversions) or `TL("label")` (a widget label: it keeps its ImGui id in both languages),
  from `engine/strings.h`; each executable keeps its own table of Korean rows (the app's is `app/strings.c`, handed to
  the engine by `app_strings_init`) and each string needs a row there (`docs/specs/korean.md`).
  `tests/strings_test.mjs` (ctest) fails on a missing row or on a row that changes the printf
  conversions. Combo item arrays are built at the call with `T()`, not `local_persist`. Console log
  rows, node, clip and texture names and the benchmark's copied table stay English. Text copied into a fixed
  buffer ends with `nv_utf8_trim`, and cut with `nv_utf8_fit` (`engine/base.h`), never in the middle of a character. The desktop types
  through the text agent too (an input method composes there); a key that types no text is kept from it
  in `on_key`.
- Report through `nv_log(level, source, format, ...)` (`engine/log.h`), not `fprintf(stderr, ...)`
  (`docs/specs/console.md`). It writes to the browser console and to one fixed log ring that the
  Console tab shows; an equal message in a row is one row with a count. The page's own output
  (`Module.print`, `printErr`, uncaught errors, `Module.nvLog` for `EM_JS` code) is queued in
  `web/index.html.in` and `nv_log_pump()` moves it in at the start of each frame, since a hook
  must not call into WebAssembly. The ring is not saved and not undoable. Every `EM_JS` message goes
  through `Module.nvLog(level, source, text)` (0 info, 1 warning, 2 error), not `console.*`. Rows
  the Console tab lists are indices into the ring, so nothing may add or clear messages while it
  draws (Clear is done after the list). Debug builds export `Module._app_debug_log(level, n)`,
  `_app_debug_log_count`, `_app_debug_wgpu_error` and the `_app_debug_console_*` functions (item
  rects, rows, scroll, selection) for tests.
- The build label in the viewport's top-left corner shows the build type, the commit's short hash
  (`+` when the tree has uncommitted changes) and, under it, the commit's subject line, cut to the
  viewport's width. `cmake/version.cmake` writes them into `nv_version.h` (`NV_GIT_COMMIT`,
  `NV_GIT_SUBJECT`) on every build, through the `<target>_version` target that `nv_add_version(<target>)`
  makes, so they are never those of an older configure. Beside the build it shows what the page downloaded
  (`App.download_text`, from `nv_window_download_text`: the Resource Timing entries of `<target>.wasm`, `.data` and
  `.js`, the target being `Module.nvTarget`: "3.2 MB downloaded", or "from cache"). The label also gets a badge (a dot and the count of warnings and
  errors that arrived while the Console tab was not shown); a tap on it opens the Console tab
  instead of picking (`pick` in `app/main.c`, through `App.badge_box`).
- The Textures tab (`docs/specs/textures.md`, `app/textures.c`) draws engine textures with
  `nv_imgui_preview` (preview slots in `imgui.c`, ImTextureIDs from `NV_IMGUI_MAX_TEXTURES` on; a
  color pipeline for channels, a depth pipeline reading with `textureLoad`). The depth target is
  samplable (`NvRenderer.depth_sampled`) and its depth stored only while the tab is shown. A new
  kind of texture belongs in its list (`gather`), and a texture it shows needs
  `WGPUTextureUsage_TextureBinding`. Renderer textures carry a name (`nv_renderer_add_texture`).
  Debug builds export `Module._app_debug_textures_*`, `_app_debug_preview_slots` and
  `_app_debug_depth_sampled` for tests.
- The first directional light casts shadows through one shadow map fitted to the view
  (`docs/specs/shadows.md`); the app sets `NvRenderer.shadows` and the renderer remakes the map and
  its depth-only pipelines when the size or format changes. The shadow pass binds
  `shadow_frame_group`, which leaves the map out: a pass cannot sample the texture it renders to.
  A new mesh vertex layout needs a matching entry point in `shadow_shader` too.
- The scene renders at a resolution of its own (`docs/specs/resolution.md`): `nv_renderer_draw`
  takes an `NvSceneOutput` (the scene's width and height, and the image rectangle of the canvas it
  is shown in, with `pixel_size` screen pixels per scene pixel), draws into the renderer's own color
  (`scene_color`), MSAA and depth targets (allocated rounded up to 64 and kept while large enough),
  then an upscale pass clears the canvas to black and shows it with the nearest filter
  (`textureLoad`). The app computes it each frame in `app_layout` from `App.resolution` (Scale: a
  divisor 1 to 4 of the viewport; Fixed: an exact size, by default at the largest whole multiple
  that fits, centered, with black bars, or fitted or stretched to the viewport: `NvFixedFit`; `NvResolution` and
  `nv_renderer_scene_output` are the engine's):
  `App.layout.scene`. Picking, panning and the gizmo use it
  (`nv_renderer_view_ray` and `nv_renderer_camera_matrices` take it), and a tap outside the image
  does nothing. Do not size a target by the canvas or the viewport; use the scene's resolution.
  Debug builds export `_app_debug_scene`, `_app_debug_set_resolution` and `_app_debug_project`.
- The scene renders in linear HDR (`docs/specs/vfx.md`): the scene targets are `NV_SCENE_FORMAT`
  (`RGBA16Float`) and every pipeline of the scene pass targets it. Exposure, tone mapping (`NvPostSettings.tone`:
  Clamp, PBR Neutral, ACES) and the bloom composite happen in the upscale pass; the canvas's sRGB view encodes at the
  end. A shader writes linear light that may exceed 1; never apply a transfer curve in a scene shader. The MSAA resolve
  averages in linear space. Bloom (`renderer.c`: `update_bloom`, `record_bloom`) is a 6-level chain between the scene
  pass and the upscale pass, remade only when the scene's size changes.
- Effects (`engine/vfx.h`, `engine/vfx.c`, `docs/specs/vfx.md`): particles live on the GPU only, simulated by compute
  passes recorded in `nv_renderer_draw` (emit, simulate, prepare; the free list, two alive lists and a visible list).
  The app registers effects once (`nv_vfx_add_effect`, see `app/effects.c`), calls `nv_vfx_update(dt)` with the game
  clock's dt every frame (0 pauses them) and `nv_vfx_burst`, `nv_vfx_emit`, `nv_vfx_trail`, `nv_vfx_beam` and
  `nv_vfx_decal` where things happen; `NvRenderer.vfx` makes the renderer run and draw them. Draw order in the scene
  pass: opaque meshes, decals, debug lines, segments, alpha particles, additive particles; none write depth. A pass
  binds only the buffers it uses (an indirect-argument buffer cannot be bound where it is also read as arguments).
  Segments and decals are rings the CPU stages and uploads in runs; their shaders drop an expired slot by moving it
  outside the clip volume. Alpha particles are unsorted on purpose (`// TODO:` in `app/effects.c`). Stats come back
  a frame or two late. Debug builds export `Module._app_debug_vfx(n)`, `_app_debug_vfx_fire(effect)`,
  `_app_debug_set_effects(...)`, `_app_debug_post` and `_app_debug_set_post(...)`; the scene's post settings are saved
  (`TONE`, `EXPO`, `BLOM`, `BLMI`).
- The scene pass is multisampled by default (`docs/specs/msaa.md`): the app sets `NvRenderer.msaa`
  (1 or 4; the View tab's Anti-aliasing combo, saved as `MSAA`), and `update_msaa` makes the
  4-sample color target (made like the canvas: its own format with the sRGB view format, rendered
  through the sRGB view; a texture created directly in the sRGB format resolved too dark) and the
  scene pipelines, and `update_depth_buffer` the 4-sample depth; the canvas view passed to
  `nv_renderer_draw` is the resolve target. A pipeline used in the scene pass takes
  `renderer->scene_samples`; the shadow and ImGui passes stay single-sample. A multisampled depth
  texture is previewed through `depth_ms_pipeline`. Debug builds export `_app_debug_msaa` and
  `_app_debug_set_msaa`.
- The scene pass uses reverse Z: a `depth32float` buffer cleared to 0, compare `Greater`, and clip z
  turned into w - z by `reverse_depth` in `renderer.c`, so depth runs 1 (near) to 0 (far). The
  camera matrices the API returns (`nv_renderer_camera_matrices`, `nv_renderer_view_ray`) stay
  standard 0..1. The shadow map keeps standard depth, since its projection is orthographic.
- In `engine/anim.cpp`, ozz headers are included before engine headers: `engine/base.h` defines
  `internal` as a macro, which breaks ozz's `internal::` namespace.
- Joint names are case-sensitive and come from the asset (the Quaternius rig has `Head`, `hand_r`).
  Assert on `nv_anim_find_joint` results.
- Animation runs through the scene: `nv_anim_update_scene` (before `nv_scene_update`) updates
  every animator, moves its `owner` node by root motion, aims look-at at `target_node` and fills
  `NvNode.attach` for nodes that follow a joint. Do not move those nodes by hand.
- `nv_gltf_load_model` creates the skeleton and animator for a skinned model; call `nv_anim_init`
  first.
- There are two executables, `app` (the editor) and `autobattler` (`docs/specs/battle.md`), each with its own page.
  `nv_setup_executable(<target> [ROOT] [COMPONENT <name>] [ASSETS <dir>] [PRELOAD <dir>@<path> ...])` packs the files a program reads
  into `<target>.data`, read with `fopen`: `app` takes the whole `assets/` directory (`ASSETS`, at `/assets`), `autobattler` only the
  font folder and its `data/` (`PRELOAD`), and its own install `COMPONENT`. CI installs the editor's Release build to `release/` and
  Debug build to `debug/` on Pages (the site root has nothing), and the auto-battler's to `autobattler/release/` and
  `autobattler/debug/`; every other pushed branch publishes only its Debug builds, to `<branch>/` and `autobattler/<branch>/` (`/`
  becomes `-`; a branch named `release`, `debug` or `autobattler` is not published). Builds are staged in the `gh-pages` branch (one
  commit, rewritten by CI) and deployed by `.github/workflows/pages.yml`. Debug builds keep their DWARF in `<target>.debug.wasm`,
  which only browser developer tools download.
- The auto-battler (`docs/specs/battle.md`): `battle.c` is the rules and reads no GPU, ImGui or clock, so `tests/battle_test.c` runs whole
  rounds; the view (`battle_view.c`) steps it with `battle_tick` at a fixed 30 Hz and only reads it, changing it through `battle_place`,
  `battle_remove`, `battle_start` and `battle_retry`. Units, weapons and the stage are the text files in `autobattler/data/`, read by
  `defs.c` (a bad file blocks Start and is reported with its line); a new key goes in `defs.c`'s field tables and the spec's. Its
  viewport and its panel must not overlap (input that starts in the viewport skips ImGui). UI text goes through `T()`/`TL()` with rows in
  `autobattler/strings.c`, like the app's. Debug builds export `Module._battle_debug(n)`, `_battle_debug_deploy`, `_battle_debug_start`,
  `_battle_debug_run`, `_battle_debug_layout` and `_battle_debug_project` for tests. A touch on a panel widget takes a few frames to
  count as a tap: tests that touch buttons wait about two seconds.

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
`ctest --test-dir build --output-on-failure` runs `tests/` under Node (CI runs it for both builds); `battle_test` reads the
auto-battler's `autobattler/data/` directly.
Each Playwright browser launch starts with empty storage, and `page.reload()` keeps it, which is
how autosave is tested. Do not delete the IndexedDB database while the page is open: the deletion
waits for the page to close and then removes the save. The page's `FS` is a global, so tests can
read and damage `/nv-save/state.nvs` (then `FS.syncfs(false, ...)`).

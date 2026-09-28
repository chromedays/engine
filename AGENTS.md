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
                           imgui.h (Dear ImGui), renderer.h (meshes, materials, skinning, debug lines),
                           gltf.h (cgltf loading), anim.h (skeletal animation over ozz-animation)
engine/src/                window.c, gpu.c, scene.c, imgui.c, renderer.c, gltf.c,
                           anim.cpp (the ozz wrapper; our only C++ file)
app/                       the app: main.c (showcase scene, frame), stress.c (stress scene and
                           benchmark, opened with #stress), ui.c (editor panel), app.h (shared state)
assets/                    binary assets (Git LFS); assets/quaternius/ is built by tools/trim_assets.sh
tools/                     offline asset scripts (run with npx; nothing installed into the repo)
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
  panel; the one exception is the build label in the viewport's top-left corner.
- Mouse and touch input that starts in the viewport (`NvImgui.view_rect`) skips ImGui and arrives in
  `NvImgui.view` (orbit, pan, dolly, tap); the app turns it into camera moves and picking
  (`nv_renderer_view_ray`, `nv_renderer_pick`). Playwright drives it with mouse drags, the wheel and
  CDP `Input.dispatchTouchEvent` for multi-touch.
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

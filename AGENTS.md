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
                           imgui.h (Dear ImGui input and rendering)
engine/src/                window.c (canvas), gpu.c (WebGPU setup and frames), scene.c, imgui.c
examples/                  triangle (minimal), scene (scene graph, depth, lighting, ImGui inspector)
web/                       index.html.in (per-example page), landing.html (Pages index)
docs/CODING_STANDARD.md    coding standard (read before writing code)
.github/workflows/build.yml  CI: web build + GitHub Pages deploy
```

## Build and run

Requires the Emscripten SDK (tested with 6.0.10).

```sh
emcmake cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
python3 -m http.server -d build/examples/scene 8000
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
- New examples call `nv_setup_executable(<target>)` so they get an HTML page and are packaged,
  are added with `add_subdirectory` in the top-level `CMakeLists.txt`, and get a link in
  `web/landing.html`.

## Testing without a display

There is no physical display in cloud sessions. Headless Chromium renders WebGPU with
`--enable-unsafe-webgpu --enable-features=Vulkan --use-vulkan=swiftshader
--use-webgpu-adapter=swiftshader --use-angle=swiftshader --disable-vulkan-fallback-to-gl-for-testing`.
Test both a Release build and a Debug build (the Debug build enables `NV_ASSERT`).

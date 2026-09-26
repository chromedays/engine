# AGENTS.md

Guidance for AI coding agents working in this repository.

## Communication

- Answer in both Korean and English in every response. Write the Korean version first, then the
  English version with the same content.
- Commit messages, code comments and documentation stay in English.

## Project

A C17 rendering engine on WebGPU (`webgpu.h`) that builds natively and for the web from the same
sources.

- Native: wgpu-native (prebuilt, fetched by CMake) + GLFW.
- Web: Emscripten + the `emdawnwebgpu` port, drawing into a `<canvas>`.

```
cmake/Dependencies.cmake   dependency setup; defines the `webgpu` target and engine_setup_executable()
engine/include/engine/     public API: window.h (EngWindow), gpu.h (EngGpu)
engine/src/                window_glfw.c / window_web.c, gpu.c, surface.c, surface_metal.m (macOS)
examples/triangle/         example app
web/                       index.html.in (per-example page), landing.html (Pages index)
.github/workflows/build.yml  CI: Linux x86_64/aarch64, Windows x86_64, macOS arm64, web
```

## Build and run

```sh
# Native
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/examples/triangle/triangle

# Web (requires the Emscripten SDK, tested with 6.0.10)
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
python3 -m http.server -d build-web/examples/triangle 8000
```

Linux builds need the X11/Wayland dev packages listed in README.md.

## Conventions

- Pure C17 (Objective-C only for the macOS Metal layer). Compile warning-free with `-Wall -Wextra`.
- Public API uses the `eng_` prefix for functions and `Eng` for types; setup functions return
  `bool` and clean up after themselves on failure.
- Use the `WGPU_*_INIT` macros to initialize WebGPU structs, and `WGPUStringView` with
  `WGPU_STRLEN` for strings.
- Platform differences go behind `#if defined(__EMSCRIPTEN__)` and friends inside the engine, not in
  examples.
- Web constraints: `wgpuSurfacePresent` must not be called (the browser presents); blocking waits
  use `wgpuInstanceWaitAny` with Asyncify; `eng_window_run` never returns on the web, so app state
  must not live on `main`'s stack.
- New examples call `engine_setup_executable(<target>)` so they get packaged for native and web.

## Testing without a display

There is no physical display in cloud sessions. Run native builds under Xvfb (Mesa's software
Vulkan driver) and capture screenshots with `xwd`. For the web build, headless Chromium renders
WebGPU with `--enable-unsafe-webgpu --enable-features=Vulkan --use-vulkan=swiftshader
--use-webgpu-adapter=swiftshader --use-angle=swiftshader --disable-vulkan-fallback-to-gl-for-testing`.

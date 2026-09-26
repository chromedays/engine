# engine

A C17 rendering engine built on native WebGPU (`webgpu.h`), using
[wgpu-native](https://github.com/gfx-rs/wgpu-native) as the implementation and
[GLFW](https://www.glfw.org/) for windowing.

## Layout

```
cmake/Dependencies.cmake   fetches GLFW (source) and wgpu-native (prebuilt release)
engine/                    static library: window, gpu context, GLFW -> WGPUSurface glue
examples/triangle/         draws a single colored triangle
```

## Build

Requirements: CMake 3.24+, a C17 compiler, and a Vulkan (Linux/Windows),
D3D12 (Windows) or Metal (macOS) capable driver.

Linux also needs the X11/Wayland development packages GLFW builds against, e.g. on Ubuntu:

```sh
sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
                 libwayland-dev libxkbcommon-dev wayland-protocols
```

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/examples/triangle/triangle
```

The wgpu-native release is chosen from the host OS and CPU architecture
(x86_64 or aarch64/arm64); override the version with `-DWGPU_NATIVE_VERSION=<tag>`.

## Prebuilt binaries

GitHub Actions (`.github/workflows/build.yml`) builds every push for Linux x86_64,
Linux aarch64, Windows x86_64 and macOS aarch64 (Apple Silicon). Each build is uploaded as a
workflow artifact: open the run on the repository's **Actions** tab and download
`engine-<platform>`. Unzip it and run `triangle` (or `triangle.exe`); the WebGPU library sits
next to the executable.

Pushing a tag like `v0.1.0` also publishes the zips as a GitHub Release.

The macOS build is unsigned, so macOS blocks it the first time; clear the quarantine flag with
`xattr -dr com.apple.quarantine <folder>`.

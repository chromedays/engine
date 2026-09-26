# engine

A C17 rendering engine built on WebGPU (`webgpu.h`) that runs both natively and in the browser
from the same source:

- **Native:** [wgpu-native](https://github.com/gfx-rs/wgpu-native) (Vulkan, D3D12, Metal) with
  [GLFW](https://www.glfw.org/) for windowing.
- **Web:** compiled to WebAssembly with [Emscripten](https://emscripten.org/); WebGPU calls go
  to the browser through Emscripten's `emdawnwebgpu` port, and the engine draws into a `<canvas>`.

## Layout

```
cmake/Dependencies.cmake   native: fetches GLFW and wgpu-native; web: sets up emdawnwebgpu
engine/                    static library: window (GLFW or canvas), gpu context, surface creation
examples/triangle/         draws a single colored triangle
web/                       HTML page template for web builds
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

### Web build

Install the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
(tested with 6.0.10), then:

```sh
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
python3 -m http.server -d build-web/examples/triangle 8000   # open http://localhost:8000
```

The page must be served over HTTP(S) (not opened as a file) in a browser with WebGPU:
recent Chrome/Edge, Safari 26+, or Firefox 141+ on Windows.

### Native build details

The wgpu-native release is chosen from the host OS and CPU architecture
(x86_64 or aarch64/arm64); override the version with `-DWGPU_NATIVE_VERSION=<tag>`.

## Prebuilt binaries

GitHub Actions (`.github/workflows/build.yml`) builds every push for Linux x86_64,
Linux aarch64, Windows x86_64 and macOS aarch64 (Apple Silicon). Each build is uploaded as a
workflow artifact: open the run on the repository's **Actions** tab and download
`engine-<platform>`. Unzip it and run `triangle` (or `triangle.exe`); the WebGPU library sits
next to the executable.

CI also builds the web version (`engine-web` artifact) and deploys it to GitHub Pages on every push
to the default branch. Enable Pages once under **Settings > Pages > Source: GitHub Actions**; the
site is then at `https://<owner>.github.io/<repo>/`. On the free GitHub plan the repository must be
public for Pages to work.

Pushing a tag like `v0.1.0` also publishes the zips as a GitHub Release.

The macOS build is unsigned, so macOS blocks it the first time; clear the quarantine flag with
`xattr -dr com.apple.quarantine <folder>`.

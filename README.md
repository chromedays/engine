# nv

nv ("night view") is a C17 rendering engine on WebGPU that runs in the browser. It compiles to
WebAssembly with [Emscripten](https://emscripten.org/); WebGPU calls go to the browser through
Emscripten's `emdawnwebgpu` port, and the engine draws into a `<canvas>`. Tool UI uses
[Dear ImGui](https://github.com/ocornut/imgui) through [cimgui](https://github.com/cimgui/cimgui).

Live: https://chromedays.github.io/engine/

## Layout

```
engine/include/nv/   public API: base.h (types, asserts, arenas), math.h, scene.h, window.h, gpu.h,
                     imgui.h (Dear ImGui input and rendering)
engine/src/          window.c (canvas), gpu.c (WebGPU setup and frames), scene.c, imgui.c
examples/            triangle (minimal), scene (scene graph, depth, lighting, ImGui inspector)
web/                 HTML page template for examples, and the Pages landing page
docs/                coding standard
```

## Build

Install the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
(tested with 6.0.10), then:

```sh
emcmake cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
python3 -m http.server -d build/examples/scene 8000   # open http://localhost:8000
```

The page must be served over HTTP(S) (not opened as a file) in a browser with WebGPU:
recent Chrome/Edge, Safari 26+, or Firefox 141+ on Windows.

## CI and deployment

GitHub Actions (`.github/workflows/build.yml`) builds the web version on every push (downloadable
as the `engine-web` artifact) and deploys it to GitHub Pages on every push to the default branch.
Enable Pages once under **Settings > Pages > Source: GitHub Actions**. On the free GitHub plan the
repository must be public for Pages to work.

Pushing a tag like `v0.1.0` also publishes the web build as a zip on a GitHub Release.

## Contributing

See [docs/CODING_STANDARD.md](docs/CODING_STANDARD.md).

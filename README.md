# nv

nv ("night view") is a C17 rendering engine on WebGPU that runs in the browser. It compiles to
WebAssembly with [Emscripten](https://emscripten.org/); WebGPU calls go to the browser through
Emscripten's `emdawnwebgpu` port, and the engine draws into a `<canvas>`. Tool UI uses
[Dear ImGui](https://github.com/ocornut/imgui) through [cimgui](https://github.com/cimgui/cimgui),
with [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) (through
[cimguizmo](https://github.com/cimgui/cimguizmo)) for the transform gizmo;
models load with [cgltf](https://github.com/jkuhlmann/cgltf) and skeletal animation runs on
[ozz-animation](https://github.com/guillaumeblanc/ozz-animation). The character and animations
are Quaternius's CC0 packs (see `assets/quaternius/LICENSE.txt`).

Live:
- Release: https://chromedays.github.io/engine/release/
- Debug (`NV_ASSERT` on, crashes show their stack on the page): https://chromedays.github.io/engine/debug/

## Layout

```
engine/include/nv/   public API: base.h, math.h, scene.h, window.h, gpu.h, imgui.h, renderer.h,
                     gltf.h, anim.h, chunk.h, storage.h
engine/src/          implementation (anim.cpp wraps ozz-animation; everything else is C)
app/                 the app: a showcase scene (a planet and moon, an animated character with a
                     sword), a stress scene with a benchmark (picked in the View tab), and an
                     editor panel (node tree, inspector, view settings, stress workloads). It
                     autosaves the showcase to the browser (IndexedDB)
assets/              binary assets (Git LFS)
tests/               tests that need no browser (run with ctest under Node)
tools/               offline asset scripts
web/                 HTML page template
docs/                coding standard and feature specs
```

## Build

Binary assets (models, textures, audio) are stored with [Git LFS](https://git-lfs.com/). Install it
before cloning, or run `git lfs install && git lfs pull` in an existing clone.

Install the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
(tested with 6.0.10) and CMake 3.30 or newer, then:

```sh
emcmake cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
python3 -m http.server -d build/app 8000   # open http://localhost:8000/
ctest --test-dir build --output-on-failure # tests/, run under Node
```

The page must be served over HTTP(S) (not opened as a file) in a browser with WebGPU:
recent Chrome/Edge, Safari 26+, or Firefox 141+ on Windows.

## CI and deployment

GitHub Actions (`.github/workflows/build.yml`) builds a Release and a Debug web version on every
push (downloadable together as the `engine-web` artifact) and deploys them to GitHub Pages, under
`release/` and `debug/`, on every push to the default branch.
Enable Pages once under **Settings > Pages > Source: GitHub Actions**. On the free GitHub plan the
repository must be public for Pages to work.

Pushing a tag like `v0.1.0` also publishes the web build as a zip on a GitHub Release.

## Contributing

See [docs/CODING_STANDARD.md](docs/CODING_STANDARD.md).

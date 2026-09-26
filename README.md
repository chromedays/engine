# nv

nv ("night view") is a C17 rendering engine on WebGPU that runs in the browser. It compiles to
WebAssembly with [Emscripten](https://emscripten.org/); WebGPU calls go to the browser through
Emscripten's `emdawnwebgpu` port, and the engine draws into a `<canvas>`. Tool UI uses
[Dear ImGui](https://github.com/ocornut/imgui) through [cimgui](https://github.com/cimgui/cimgui);
models load with [cgltf](https://github.com/jkuhlmann/cgltf) and skeletal animation runs on
[ozz-animation](https://github.com/guillaumeblanc/ozz-animation). The character and animations
are Quaternius's CC0 packs (see `assets/quaternius/LICENSE.txt`).

Live: https://chromedays.github.io/engine/

## Layout

```
engine/include/nv/   public API: base.h, math.h, scene.h, window.h, gpu.h, imgui.h, renderer.h,
                     gltf.h, anim.h
engine/src/          implementation (anim.cpp wraps ozz-animation; everything else is C)
examples/            triangle (minimal), scene (scene graph, ImGui inspector),
                     character (skeletal animation: clips, crossfades, blending, root motion, IK)
assets/              binary assets (Git LFS)
tools/               offline asset scripts
web/                 HTML page template for examples, and the Pages landing page
docs/                coding standard
```

## Build

Binary assets (models, textures, audio) are stored with [Git LFS](https://git-lfs.com/). Install it
before cloning, or run `git lfs install && git lfs pull` in an existing clone.

Install the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
(tested with 6.0.10) and CMake 3.30 or newer, then:

```sh
emcmake cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
python3 -m http.server -d build/examples 8000   # open http://localhost:8000/character/
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

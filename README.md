# nv

- [한국어](#한국어)
- [English](#english)

## 한국어

nv ("night view")는 브라우저에서 도는 WebGPU 위의 C17 렌더링 엔진이다. [Emscripten](https://emscripten.org/)으로
WebAssembly로 컴파일된다; WebGPU 호출은 Emscripten의 `emdawnwebgpu` 포트를 통해 브라우저로 가고, 엔진은 `<canvas>`에
그린다. 도구 UI는 [cimgui](https://github.com/cimgui/cimgui)를 통한 [Dear ImGui](https://github.com/ocornut/imgui)를
쓰고, 트랜스폼 기즈모에는 ([cimguizmo](https://github.com/cimgui/cimguizmo)를 통한)
[ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo)를 쓴다; 모델은 [cgltf](https://github.com/jkuhlmann/cgltf)로
불러오고 스켈레탈 애니메이션은 [ozz-animation](https://github.com/guillaumeblanc/ozz-animation) 위에서 돈다. 캐릭터와
애니메이션은 Quaternius의 CC0 팩이다 (`assets/quaternius/LICENSE.txt` 참고). UI 글꼴은
[Inter](https://github.com/rsms/inter)이고, 한글은 [Pretendard](https://github.com/orioncactus/pretendard)의 서브셋에서
오며, 둘 다 SIL Open Font License를 따른다 (`assets/fonts/*-LICENSE.txt`).

실행:
- Release: https://chromedays.github.io/engine/release/
- Debug (`NV_ASSERT` 켜짐, 크래시가 페이지에 스택을 보여 줌): https://chromedays.github.io/engine/debug/

### 구성

```
engine/include/nv/   public API: base.h, math.h, scene.h, window.h, gpu.h, imgui.h, renderer.h,
                     gltf.h, anim.h, chunk.h, storage.h, log.h
engine/src/          구현 (anim.cpp는 ozz-animation을 감싼다; 나머지는 모두 C)
app/                 앱: 쇼케이스 씬 (행성과 달, 검을 든 애니메이션 캐릭터), 벤치마크가 있는 스트레스 씬
                     (View 탭에서 고름), 그리고 에디터 (노드 트리, 인스펙터, 뷰 설정, 스트레스 워크로드),
                     데스크톱 UI (뷰포트 둘레의 도크)와 폰 UI (탭 패널 하나), 둘 다 위쪽 가운데에 Play / Stop.
                     쇼케이스를 브라우저 (IndexedDB)에 자동 저장하고, undo와 redo, Edit와 Play 모드
                     (Play는 쇼케이스를 실행하고 Stop은 되돌림)가 있다. 태양이 그림자를 드리우고 (그림자 맵
                     하나; 크기, 형식, 필터는 View 탭에서), 가장자리는 4x MSAA로 부드럽게 한다 (View 탭
                     설정). 씬은 뷰포트 픽셀의 분수로, 또는 검은 띠가 있는 고정 크기로 렌더링할 수 있다.
                     Console 탭은 엔진과 페이지가 보고하는 것 (경고, 오류)을 나열하고, Textures 탭은 사용
                     중인 텍스처 (머티리얼 맵, 그림자 맵, 깊이 타깃)를 밉과 채널과 함께 보여 준다
assets/              바이너리 에셋 (Git LFS)
tests/               브라우저가 필요 없는 테스트 (Node에서 ctest로 실행)
tools/               오프라인 에셋 스크립트
web/                 HTML 페이지 템플릿
docs/                코딩 표준과 기능 스펙
```

### 빌드

바이너리 에셋 (모델, 텍스처, 오디오)은 [Git LFS](https://git-lfs.com/)로 저장된다. 클론하기 전에 설치하거나, 기존
클론에서 `git lfs install && git lfs pull`을 실행한다.

[Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) (6.0.10으로 테스트)와 CMake 3.30 이상을
설치한 다음:

```sh
emcmake cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
python3 -m http.server -d build/app 8000   # open http://localhost:8000/
ctest --test-dir build --output-on-failure # tests/, run under Node
```

페이지는 WebGPU가 있는 브라우저에서 (파일로 열지 말고) HTTP(S)로 제공해야 한다: 최신 Chrome/Edge, Safari 26+, 또는
Windows의 Firefox 141+.

### CI와 배포

GitHub Actions (`.github/workflows/build.yml`)가 모든 push에서 Release와 Debug 웹 버전을 빌드하고 (`engine-web`
아티팩트로 함께 내려받을 수 있음), 기본 브랜치로의 모든 push에서 그것을 GitHub Pages의 `release/`와 `debug/` 아래에
배포한다. **Settings > Pages > Source: GitHub Actions**에서 Pages를 한 번 켠다. 무료 GitHub 플랜에서는 Pages가
동작하려면 저장소가 공개여야 한다.

`v0.1.0` 같은 태그를 push하면 웹 빌드가 GitHub Release에 zip으로도 게시된다.

### 기여

[docs/CODING_STANDARD.md](docs/CODING_STANDARD.md) 참고.

## English

nv ("night view") is a C17 rendering engine on WebGPU that runs in the browser. It compiles to
WebAssembly with [Emscripten](https://emscripten.org/); WebGPU calls go to the browser through
Emscripten's `emdawnwebgpu` port, and the engine draws into a `<canvas>`. Tool UI uses
[Dear ImGui](https://github.com/ocornut/imgui) through [cimgui](https://github.com/cimgui/cimgui),
with [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) (through
[cimguizmo](https://github.com/cimgui/cimguizmo)) for the transform gizmo;
models load with [cgltf](https://github.com/jkuhlmann/cgltf) and skeletal animation runs on
[ozz-animation](https://github.com/guillaumeblanc/ozz-animation). The character and animations
are Quaternius's CC0 packs (see `assets/quaternius/LICENSE.txt`). The UI font is
[Inter](https://github.com/rsms/inter), with Hangul from a subset of
[Pretendard](https://github.com/orioncactus/pretendard), both under the SIL Open Font License
(`assets/fonts/*-LICENSE.txt`).

Live:
- Release: https://chromedays.github.io/engine/release/
- Debug (`NV_ASSERT` on, crashes show their stack on the page): https://chromedays.github.io/engine/debug/

### Layout

```
engine/include/nv/   public API: base.h, math.h, scene.h, window.h, gpu.h, imgui.h, renderer.h,
                     gltf.h, anim.h, chunk.h, storage.h, log.h
engine/src/          implementation (anim.cpp wraps ozz-animation; everything else is C)
app/                 the app: a showcase scene (a planet and moon, an animated character with a
                     sword), a stress scene with a benchmark (picked in the View tab), and an
                     editor (node tree, inspector, view settings, stress workloads), with a
                     desktop UI (docks around the viewport) and a phone UI (one tabbed panel),
                     Play / Stop at the top center of both. It
                     autosaves the showcase to the browser (IndexedDB), has undo and redo, and
                     Edit and Play modes (Play runs the showcase; Stop puts it back). The sun
                     casts shadows (one shadow map; size, format and filter in the View tab), and edges are
                     smoothed with 4x MSAA (a View tab setting). The scene can render at a
                     fraction of the viewport's pixels or at a fixed size with black bars. A
                     Console tab lists what the engine and the page report (warnings, errors),
                     and a Textures tab shows the textures in use (material maps, shadow map,
                     depth target) with their mips and channels
assets/              binary assets (Git LFS)
tests/               tests that need no browser (run with ctest under Node)
tools/               offline asset scripts
web/                 HTML page template
docs/                coding standard and feature specs
```

### Build

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

### CI and deployment

GitHub Actions (`.github/workflows/build.yml`) builds a Release and a Debug web version on every
push (downloadable together as the `engine-web` artifact) and deploys them to GitHub Pages, under
`release/` and `debug/`, on every push to the default branch.
Enable Pages once under **Settings > Pages > Source: GitHub Actions**. On the free GitHub plan the
repository must be public for Pages to work.

Pushing a tag like `v0.1.0` also publishes the web build as a zip on a GitHub Release.

### Contributing

See [docs/CODING_STANDARD.md](docs/CODING_STANDARD.md).

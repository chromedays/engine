# Shared code spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨, 1–7단계 (2026-10-02). 이 스펙의 변경은 먼저 합의한다. 만든 것이 스펙과 다른 곳은 "구현 결과"에 있다. 선행 작업인 엔진 구조 평탄화(`engine/include/nv/`와
`engine/src/`를 `engine/` 하나로)는 끝났다(`c3e0de4`).

### 목표

`docs/specs/battle.md`의 게임(`autobattler/`, 엔진만 링크하는 두 번째 실행 파일)을 만들기 전에, `app/`에 있는 코드 중 그 게임도
쓸 것을 엔진으로 옮긴다. 리팩터링일 뿐이다: 에디터 앱의 동작, 화면, 저장 형식은 바뀌지 않는다.

### 원칙

- **옮기는 기준은 `AGENTS.md`의 "다른 앱도 쓸까?"이고, 그 다른 앱은 `battle.md`다.** 코딩 표준의 "두 번째로 필요할 때
  뽑는다"에 따라, 게임이 지금 쓸 것만 옮긴다. 언젠가 쓸지도 모르는 것은 남긴다.
- **동작을 바꾸지 않는다.** 저장 태그와 그 값, `SAVE_VERSION`, 디버그 내보내기, 화면의 픽셀이 그대로다(예외 하나: 해결된
  질문 4). 열거형의 값 순서를
  지켜서, 저장된 값이 같은 뜻으로 읽히게 한다.
- **엔진은 `App`을 모른다.** 옮긴 코드는 `App`, `SceneView`, 검색, 저장을 참조하지 않는다. 앱에 남는 쪽이 엔진 함수를 부른다.
- **새 서드파티는 없다.**
- **파일 배치는 평평한 엔진을 따른다.** 새 모듈은 `engine/<이름>.h`와 `engine/<이름>.c`가 나란히 있고
  `#include <engine/<이름>.h>`로 포함한다. 소스는 `engine/CMakeLists.txt`의 목록에 더한다. 테스트는 소스를
  `${PROJECT_SOURCE_DIR}/engine/<이름>.c`로 빌드하고 include 경로는 저장소 루트다. GPU 없는 모듈의 헤더는 WebGPU 헤더를
  끌어오지 않게 해서, 테스트가 포트 없이 빌드되게 한다.

### 조사 결과

| 파일 | 내용 | 판단 |
|---|---|---|
| `app/strings.c`, `strings.h` | `T()`, `TL()`, 언어, 한국어 표 | **옮김** (조회와 언어). 표는 실행 파일마다 남는다 |
| `app/main.c`: `append_box`, `app_box_mesh`, `create_ground_mesh` | 상자와 바닥 메시 | **옮김**: 게임의 유닛, 지형지물, 전장 |
| `app/main.c`: `read_asset`, `load_font` | UI 글꼴 읽기 | **옮김**: 같은 글꼴을 쓴다 |
| `app/main.c`: `js_download_bytes`, `read_download_size` | 내려받은 크기 글 | **옮김**: 파일 이름을 실행 파일 이름에서 얻도록 |
| `app/main.c`: `apply_view_input`, `update_camera` | 궤도 카메라(회전, 줌, 팬, 배치) | **일부 옮김**: 수학은 엔진, 선택 따라가기와 홈은 앱 |
| `app/main.c`: `pick` | 탭이 이미지 안인지, CSS 픽셀에서 광선 | **일부 옮김**: 탭에서 광선까지. 배지와 선택은 앱 |
| `app/ui.c`: `scene_output`, `Resolution`, `ui_rect` | 씬 해상도와 이미지 사각형 | **옮김**: 게임도 폰에서 해상도를 낮춘다 |
| `app/ui.c`: `fit_text` | 폭에 맞춰 자르고 "..." | **옮김**: 빌드 표시 |
| `app/CMakeLists.txt`: `app_version`, `NV_BUILD_NAME` | 커밋 해시와 제목 헤더 | **옮김** (CMake 함수): 게임 페이지도 어느 커밋인지 보여 준다 |
| 중복된 작은 도우미 | `clampf`(ui_desktop.c), `clamp`(save.c), `clamp_u32`(ui.c), FNV-1a 세 곳(strings.c, save.c, ui.c), xorshift(effects.c), `pixel_ratio > 0 ? … : 1` 일곱 곳 | **옮김**: 게임은 FNV-1a 해시와 시드 난수가 필요하다 |
| `app/ui.c`: `ui_build_label` | 빌드 표시와 콘솔 배지 | 남김: 배지가 콘솔 탭과 묶여 있다. 게임은 `nv_imgui_fit_text`로 제 표시를 그린다 |
| `app/effects.c` | 폭발, 불꽃, 연기, 미사일 궤적의 정의 | 남김: 내용(content)이다. 게임은 무기마다 제 이펙트를 정의한다 (해결된 질문 1) |
| `app/console.c` | Console 탭 | 남김: 게임 스펙에 없다. 로그 링은 이미 엔진에 있다 (해결된 질문 2) |
| `app/save.c`, `undo.c`, `selection.c`, `search.c`, `shortcuts.c`, `ui_desktop.c`, `ui_phone.c`, `textures.c`, `stress.c` | 저장, undo, 선택, 검색과 팔레트, 단축키, 도크, 폰 패널, Textures 탭, 스트레스 씬 | 남김: 에디터만의 것 |
| `app/ui.c`: `msaa_ui`, `post_ui`, `shadow_ui`, `resolution_ui` | 렌더러 설정 위젯 | 남김: `search_row`에 묶여 있고 게임 스펙에 없다 |
| `app/main.c`: `frame`, `main`의 초기화, `FrameTimes` | 프레임 순서, 아레나, 기본값, 시간 재기 | 남김: 실행 파일마다 짧고 다르다. 프레임워크로 만들지 않는다 (해결된 질문 3) |

### 옮기는 것

#### 1. UI 문자열 (`engine/strings.h`)

`battle.md`의 "UI 문자열" 절에서 이리로 옮긴다. 정한 대로다:

```c
typedef enum NvLanguage { NV_LANGUAGE_EN, NV_LANGUAGE_KO, NV_LANGUAGE_COUNT } NvLanguage;
typedef struct NvStringPair { const char* english; const char* korean; } NvStringPair;

#define NV_STRINGS_MAX 4096 // pairs per executable

void nv_strings_set_table(const NvStringPair* pairs, u32 count); // kept, not copied; once, before any T()
NvLanguage nv_strings_language(void);
void nv_strings_set_language(NvLanguage language);
NvLanguage nv_strings_browser_language(void);
const char* nv_strings_find_korean(const char* english);
const char* nv_strings_text(const char* english);
const char* nv_strings_label(const char* english);

// NOTE: Short on purpose, since they wrap every UI text: the one exception to the naming rules.
#define T(english)  nv_strings_text(english)
#define TL(english) nv_strings_label(english)
```

- `engine/strings.c`: 지금 `app/strings.c`의 해시 표, `T`, `TL`, 언어. 상태는 파일 전역이다.
- `app/strings.c`는 표(`NvStringPair` 배열)만 남기고, `main`이 `nv_strings_set_table`을 부른다. `app/strings.h`는 없어지고
  `app/app.h`가 `#include <engine/strings.h>`를 한다. 엔진 헤더는 언제나 폴더와 함께 포함하므로, 앱의 `strings.c`나 POSIX의
  `<strings.h>`와 섞이지 않는다.
- 저장 태그 `LANG`의 값(0 영어, 1 한국어)은 `NV_LANGUAGE_*`의 값과 같다.
- `tests/strings_test.mjs`는 폴더 목록을 받아 폴더마다 자기 `strings.c`에 대해 검사한다. 지금은 `app/` 하나이고, 게임이
  `autobattler/`를 더한다. (`tools/subset_hangul.sh`도 표 목록을 받았지만, 2026-10-03에 글꼴이 Pretendard 하나로 바뀌며 없어졌다.)

#### 2. 메시 기본형 (`engine/mesh.h`)

```c
// A mesh being built in arrays the caller owns; the primitives append to it and assert that they fit.
typedef struct NvMeshBuilder {
    NvVertex* vertices;
    u32 vertex_count, vertex_capacity;
    u32* indices;
    u32 index_count, index_capacity;
} NvMeshBuilder;
NvMeshData nv_mesh_builder_data(const NvMeshBuilder* mesh); // what nv_renderer_add_mesh takes

void nv_mesh_append_box(NvMeshBuilder* mesh, NvVec3 center, NvVec3 half);
void nv_mesh_append_plane(NvMeshBuilder* mesh, f32 half_x, f32 half_z); // y = 0, facing +Y
```

- `engine/mesh.h`가 `NvVertex`, `NvSkinnedVertex`, `NvMeshData`를 `engine/renderer.h`에서 넘겨받고, `renderer.h`는
  `mesh.h`를 포함한다. 그래서 `mesh.h`와 `engine/mesh.c`는 GPU가 없고, ctest가 포트 없이 검사한다. 함수는 `NvMeshData`의
  count를 늘린다.
- 앱: `app_box_mesh`, `create_sword_mesh`, `create_ground_mesh`가 이것을 쓴다. 꼭짓점 순서와 값이 같으므로 화면이 같다.
- 게임: 유닛 큐브, 지형지물 상자, 64 × 96 m 전장.

#### 3. UI 글꼴 (`engine/imgui.h`)

```c
// The UI font from /assets/fonts/ (docs/specs/fonts.md; Pretendard since 2026-10-03), read into `arena`
// (ImGui keeps pointing at the bytes). Logs a warning and keeps the built-in font when a file is missing.
b32 nv_imgui_load_ui_font(NvImgui* imgui, NvArena* arena);

// `text` cut to `room` pixels with "..." at the end, never inside a UTF-8 character.
void nv_imgui_fit_text(const char* text, f32 room, char* out, umm capacity);
```

- 경로와 크기(14 px)는 `fonts.md`가 정한 UI 글꼴이므로 엔진에 둔다. 앱의 `read_asset`, `load_font`, `fit_text`는 없어진다.
- 로그 출처는 `"imgui"`가 된다(지금은 `"app"`). 테스트가 출처로 찾는 것은 없다.

#### 4. 씬 해상도 (`engine/renderer.h`)

```c
typedef enum NvResolutionMode { NV_RESOLUTION_SCALE, NV_RESOLUTION_FIXED } NvResolutionMode;
typedef enum NvFixedFit { NV_FIT_WHOLE, NV_FIT_VIEWPORT, NV_FIT_STRETCH } NvFixedFit;

typedef struct NvResolution {
    NvResolutionMode mode;
    NvFixedFit fixed_fit;
    u32 divisor;                   // SCALE: 1 to 4
    u32 fixed_width, fixed_height; // FIXED: NV_RESOLUTION_MIN..NV_RESOLUTION_MAX
} NvResolution;

#define NV_RESOLUTION_MIN 16
#define NV_RESOLUTION_MAX 4096

// The scene's size and where its image goes in `viewport` (docs/specs/resolution.md).
NvSceneOutput nv_renderer_scene_output(const NvResolution* resolution, NvRect viewport);

// A ray through a tap given in CSS pixels; not ok when the tap is outside the image (on a black bar).
NvTapRay nv_renderer_tap_ray(NvScene* scene, NvSceneOutput output, f32 tap_x, f32 tap_y, f32 pixel_ratio);

// The view-projection the scene pass draws with (reverse Z): `renderer.c` calls it, so it is declared too.
NvMat4 nv_renderer_camera_view_proj(NvNode* camera_node, f32 aspect);
```

- `engine/renderer_cpu.c`: GPU 없는 렌더러 코드(`engine/vfx_cpu.c`와 같은 방식). `nv_renderer_scene_output`과
  `nv_renderer_view_ray`가 여기로 와서 ctest로 검사한다. `nv_renderer_camera_matrices`도 GPU를 쓰지 않으면 함께 온다.
  선언은 `engine/renderer.h`에 남으므로 따로 헤더가 없다. `renderer.h`는 `engine/gpu.h`(WebGPU 헤더)를 포함하므로, 테스트는
  `gpu_format_test`처럼 `--use-port=emdawnwebgpu`로 헤더만 받고 아무것도 링크하지 않는다.
- 열거형의 값 순서가 지금과 같으므로 저장 태그 `RSMD`, `RSFT`의 값이 같은 뜻이다.
- 앱: `Resolution`, `ResolutionMode`, `FixedFit`, `RESOLUTION_MIN/MAX`, `scene_output`이 없어진다. `pick`은
  `nv_renderer_tap_ray`로 시작한다. `ui_rect`는 앱에 남는다(도크 배치).
- 게임: 폰에서 divisor 2, 배치 입력의 탭.

#### 5. 궤도 카메라 (`engine/camera.h`)

```c
typedef struct NvOrbitCamera {
    NvVec3 target; // the point it orbits
    f32 yaw, pitch, distance;
    f32 min_pitch, max_pitch, min_distance, max_distance;
} NvOrbitCamera;

// Turns by yaw and pitch (radians) and dollies by `dolly` (the log of the distance factor), within the limits.
void nv_orbit_camera_turn(NvOrbitCamera* camera, f32 yaw, f32 pitch, f32 dolly);
// The world move for a pan of (pan_x, pan_y) pixels on an image `image_height` pixels high: one pixel is the
// height the view covers at the target divided by the image's height, along the camera's right and up axes.
NvVec3 nv_orbit_camera_pan(const NvOrbitCamera* camera, const NvNode* camera_node, f32 image_height, f32 pan_x, f32 pan_y);
// Sets the camera node's position, rotation and world matrix (a top-level node).
void nv_orbit_camera_place(const NvOrbitCamera* camera, NvNode* camera_node);
```

- `engine/camera.h`는 `engine/scene.h`만 포함한다. 입력(`NvViewInput`, `engine/imgui.h`)과 해상도(`NvSceneOutput`,
  `engine/renderer.h`)는 호출하는 쪽이 숫자로 바꿔 넘긴다: 앱은 `orbit_x * ORBIT_RADIANS_PER_PIXEL`, 이미지 높이는
  `scene_output.height * pixel_height / pixel_ratio`. 그래서 `engine/camera.c`는 GPU가 없고 ctest가 포트 없이 검사한다.
- 앱: `SceneView`의 `camera_yaw`, `camera_pitch`, `camera_distance`, `orbit_point`가 `NvOrbitCamera orbit` 하나가 된다.
  선택 따라가기, `pan`, 홈, 초점, 기즈모 중 멈춤은 앱에 남는다. `CAMERA_*` 한계는 `orbit`의 필드가 된다.
- 저장: 태그는 그대로이고, 쓰고 읽는 필드 경로만 바뀐다(`view->orbit.yaw`).
- 게임: 피치와 요를 고정하고, 팬 결과를 지면에 투영(y를 0으로)해 전장 안으로 제한한다.

#### 6. 내려받은 크기 (`engine/window.h`)

```c
// "3.2 MB downloaded" or "3.2 MB from cache" for this page's own files; empty when the browser does not say.
void nv_window_download_text(char* out, umm capacity);
```

- 파일 이름은 `app.wasm`처럼 고정돼 있었다. `web/index.html.in`이 `Module.nvTarget = "@NV_WEB_TARGET@"`를 두고,
  `EM_JS`가 `<target>.wasm`, `.data`, `.js`를 찾는다.

#### 7. 빌드 버전 (CMake)

- 루트 `CMakeLists.txt`에 `nv_add_version(<target>)`: `<target>_version` 타깃, 그 타깃 폴더의 `nv_version.h`, include 경로,
  `NV_BUILD_NAME`. `app/CMakeLists.txt`의 같은 코드가 이 한 줄이 된다. `cmake/version.cmake`는 그대로다.

#### 8. 작은 도우미 (`engine/base.h`, `engine/math.h`, `engine/window.h`)

```c
// engine/base.h: FNV-1a, chainable: nv_fnv1a(NV_FNV1A_SEED, bytes, size).
#define NV_FNV1A_SEED  2166136261u
#define NV_FNV1A_PRIME 16777619u
static inline u32 nv_fnv1a(u32 hash, const void* bytes, umm size);

// engine/math.h
static inline f32 nv_clamp_f32(f32 value, f32 lo, f32 hi);
static inline u32 nv_clamp_u32(u32 value, u32 lo, u32 hi);

// engine/math.h: PCG32 (O'Neill, pcg-random.org; the formula is cited in a comment). A zeroed NvRandom is a valid
// generator (the step uses increment | 1); nv_random_seed picks a sequence.
typedef struct NvRandom { u64 state, increment; } NvRandom;
static inline void nv_random_seed(NvRandom* random, u64 seed, u64 sequence);
static inline u32 nv_random_u32(NvRandom* random);
static inline f32 nv_random_f32(NvRandom* random); // [0, 1)

// engine/window.h: CSS pixels to framebuffer pixels, 1 before the browser says.
static inline f32 nv_window_pixel_ratio(const NvWindow* window);
```

- 앱의 `clampf`, `clamp`, `clamp_u32`, 세 FNV-1a 루프, 일곱 `pixel_ratio` 식이 이것을 쓴다. `save.c`의 장면 배치 해시는
  같은 값을 내야 한다(저장의 노드가 그 해시로 맞춰지므로): 테스트가 지금 값과 비교한다.
- `effects.c`의 xorshift는 `NvRandom`이 된다. 스트레스 씬 이펙트의 무작위 순서가 바뀌지만 저장되지도, 값으로 검사되지도
  않는다 (해결된 질문 4).
- 게임: 결정론의 해시와 시드 난수(`battle.md`의 "결정론").
- `nv_fnv1a`의 구현은 `engine/base.c`가 없으므로 `static inline`으로 헤더에 둔다.

### 바뀌지 않는 것

- 저장 형식: 모든 태그, 그 값, `SAVE_VERSION`. 이 리팩터링 전에 쓴 저장이 그대로 읽힌다.
- 디버그 내보내기(`Module._app_debug_*`)의 이름과 결과.
- 화면: Edit 모드 쇼케이스의 스크린샷이 픽셀 단위로 같다(SwiftShader는 결정적이다).
- 로그 메시지 글. 출처만 `"app"`에서 `"imgui"`로 바뀌는 것이 있다(글꼴).

### 테스트

ctest(Node), 새 파일. `tests/CMakeLists.txt`가 각 테스트를 `${PROJECT_SOURCE_DIR}/engine/<이름>.c`와 함께 빌드한다:

- `tests/mesh_test.c`: 상자 꼭짓점 24개와 인덱스 36개, 면마다 법선과 반시계 감기, 용량 assert(디버그), 평면.
- `tests/resolution_test.c`: `nv_renderer_scene_output`의 Scale 1–4, Fixed의 세 맞춤, 너무 작은 뷰포트, 0 크기;
  `nv_renderer_tap_ray`의 검은 띠.
- `tests/camera_test.c`: 한계, 팬의 픽셀당 거리, 배치한 노드의 위치와 회전.
- `tests/base_test.c`: FNV-1a 알려진 값, PCG32 첫 값들(참조 구현의 출력), 0 상태의 `NvRandom`.
- `strings_test.mjs`: 폴더 목록 형식으로.

브라우저(헤드리스 Chromium, Release와 Debug, 데스크톱과 폰 크기). 리팩터링 전 빌드와 후 빌드를 같은 절차로 돌려 비교한다:

- 첫 방문의 `_app_debug_save_crc()`가 같다. 전 빌드가 쓴 저장을 후 빌드가 읽고, 다시 쓰면 같은 바이트다.
- Edit 모드 쇼케이스의 스크린샷이 같다. 한국어로 바꾼 패널도 같다.
- `_app_debug_scene`, `_app_debug_project`, `_app_debug_view`, `_app_debug_language`가 같다. 검은 띠를 탭하면 선택이
  바뀌지 않는다.
- 내려받은 크기 글이 보인다. 콘솔에 글꼴 경고가 없다.

### 단계

각 단계 끝마다 앱이 전과 같은지 위 검사로 확인했다. 2–6단계는 서로 같은 파일을 바꿔서 커밋 하나에 담았다.

1. **도우미:** `nv_fnv1a`, `nv_clamp_*`, `NvRandom`, `nv_window_pixel_ratio`, `base_test.c`.
2. **메시:** `engine/mesh.h`, `mesh_test.c`.
3. **해상도와 탭:** `NvResolution`, `renderer_cpu.c`, `nv_renderer_tap_ray`, `resolution_test.c`.
4. **카메라:** `engine/camera.h`, `camera_test.c`, `SceneView`의 변경.
5. **페이지와 글꼴:** `nv_imgui_load_ui_font`, `nv_imgui_fit_text`, `nv_window_download_text`, `nv_add_version`.
6. **문자열:** `engine/strings.h`, `strings_test.mjs`, `subset_hangul.sh`.
7. **문서:** `AGENTS.md`(`engine/` 목록의 새 모듈 `mesh.c/.h`, `camera.c/.h`, `strings.c/.h`, `renderer_cpu.c`; UI 문자열 줄), `CODING_STANDARD.md`(`T`/`TL` 예외), `korean.md`,
   `resolution.md`, `fonts.md`의 이름과 경로. `battle.md`는 이 스펙을 선행 작업으로 가리킨다.

### 구현 결과

스펙과 다르게 만든 곳:

- **도우미:** `nv_random_*`와 `nv_window_pixel_ratio`는 `static inline`이다(`engine/math.c`가 없다). `base.h`에 `NV_FNV1A_PRIME`이
  더 있다: `save.c`의 장면 배치 해시는 깊이를 바이트가 아니라 한 단어로 섞으므로(`(hash ^ depth) * prime`), 바이트 단위
  `nv_fnv1a`로는 깊이 256부터 다른 값이 나온다. 이름만 `nv_fnv1a`로 돌리고 깊이 줄은 그대로 두었다. `engine/imgui.c`의 두 곳도
  `nv_window_pixel_ratio`를 쓴다. `engine/gpu.c`의 것은 대체 값이 달라서 그대로다.
- **메시:** `NvMeshData`는 const 포인터를 가지므로 쓸 수 없다. 그래서 `NvMeshBuilder`(배열, 개수, 용량)에 덧붙이고
  `nv_mesh_builder_data`로 `NvMeshData`를 얻는다. 용량 초과는 assert(trap)라 같은 프로세스에서 검사하지 못한다.
- **해상도:** `nv_renderer_camera_view_proj`(그리기가 쓰는 reverse Z 행렬)도 `renderer_cpu.c`로 옮겨 선언했다. `renderer.c`가
  부르기 때문이다. `nv_renderer_camera_matrices`와 `nv_renderer_view_ray`도 같이 옮겼다.
- **카메라:** `SceneView.home`은 `NvOrbitCamera` 통째의 복사다. 제한은 필드이고 0이 아니어야 하므로, 시야를 만드는 두 곳이
  `ORBIT_LIMITS`를 넣는다.
- **문자열:** 앱은 `app_strings_init()`으로 표를 넘긴다. 해시 표는 표를 정할 때 바로 만든다(전에는 처음 쓸 때). 해시 표 크기는
  8192칸(`u16`, 16 KB)이다.
- **브라우저 확인:** 리팩터링 전 빌드(`3f6f8c7`)와 단계마다의 빌드를 같은 Playwright 절차로 돌려 비교했다. 절차: 첫 방문, 클릭으로
  고르기(땅, 큐브, 캐릭터), 궤도, 줌, 팬, 고정 해상도와 그 띠 탭, Scale 2, 한국어, 폰 UI, 저장 파일 읽기, 전 빌드가 쓴 저장 불러오기.
  JSON(CRC, 해상도, 시점, 선택, 언어, 저장 파일의 SHA-256)은 정확히 같고, 스크린샷은 독(dock)이 거의 같고(0.1% 이하) 뷰포트는
  15% 이하로 같았다. 뷰포트에는 Edit 모드에서도 시간으로 움직이는 캐릭터의 애니메이션과 그림자가 있어서 픽셀 단위로 같을 수
  없다. 스트레스 씬과 이펙트는 Debug에서 시도해 assert 없이 돌았다.

### 해결된 질문

모두 2026-10-02에 추천대로 정했다.

1. **이펙트 정의:** `app/effects.c`의 폭발, 불꽃, 연기, 미사일 궤적은 앱에 남는다. 내용이므로 게임은 무기마다 제 이펙트를
   정의한다.
2. **Console 탭:** 지금은 옮기지 않는다. 게임이 원할 때 옮긴다. 로그 링은 이미 엔진에 있다.
3. **프레임과 초기화:** 실행 파일마다 `main`과 `frame`을 직접 쓴다. 공통 시작 코드를 엔진 함수로 묶지 않는다.
4. **난수:** `effects.c`는 `NvRandom`을 쓴다. 스트레스 씬 이펙트의 무작위 순서가 바뀌는 것은 "동작을 바꾸지 않는다"의 유일한
   예외다: 저장되지도, 값으로 검사되지도 않는다.
5. **카메라 모듈:** 새 모듈 `engine/camera.h`, `engine/camera.c`.

## English

Status: built, all seven phases (2026-10-02). Changes to this spec are agreed first. Where the build differs from the spec, see "As built". The work it needs first, flattening the
engine (`engine/include/nv/` and `engine/src/` into one `engine/`), is done (`c3e0de4`).

### Goal

Before building the game of `docs/specs/battle.md` (`autobattler/`, a second executable that links only the engine), move
the code in `app/` that the game will also use into the engine. It is a refactoring only: the editor app's behavior, screen
and save format do not change.

### Principles

- **The test for moving is `AGENTS.md`'s "would another app use this?", and the other app is `battle.md`.** Following the
  coding standard's "pull it out the second time it is needed", only what the game uses now moves. What it might use some
  day stays.
- **Behavior does not change.** Save tags and their values, `SAVE_VERSION`, debug exports and the pixels on screen stay the
  same (one exception: resolved question 4). Enum value order is kept, so saved values read with the same meaning.
- **The engine does not know `App`.** Moved code refers to no `App`, `SceneView`, search or save. The side that stays in the
  app calls the engine functions.
- **No new third-party code.**
- **Files follow the flat engine.** A new module is `engine/<name>.h` and `engine/<name>.c` side by side, included as
  `#include <engine/<name>.h>`; its source joins the list in `engine/CMakeLists.txt`. Tests build sources as
  `${PROJECT_SOURCE_DIR}/engine/<name>.c` with the repository root as the include path. A GPU-free module's header pulls in
  no WebGPU header, so its test builds without the port.

### Survey

| File | Contents | Verdict |
|---|---|---|
| `app/strings.c`, `strings.h` | `T()`, `TL()`, the language, the Korean table | **Moves** (lookup and language). Each executable keeps its own table |
| `app/main.c`: `append_box`, `app_box_mesh`, `create_ground_mesh` | Box and ground meshes | **Moves**: the game's units, props and field |
| `app/main.c`: `read_asset`, `load_font` | Reading the UI font | **Moves**: the game uses the same font |
| `app/main.c`: `js_download_bytes`, `read_download_size` | The downloaded-size text | **Moves**, with file names taken from the executable's name |
| `app/main.c`: `apply_view_input`, `update_camera` | The orbit camera (turn, zoom, pan, placement) | **Partly moves**: the math goes to the engine; following the selection and home stay in the app |
| `app/main.c`: `pick` | Whether a tap is inside the image, a ray from CSS pixels | **Partly moves**: from the tap to the ray. The badge and the selection stay |
| `app/ui.c`: `scene_output`, `Resolution`, `ui_rect` | The scene resolution and the image rectangle | **Moves**: the game also lowers the resolution on phones |
| `app/ui.c`: `fit_text` | Cut to a width with "..." | **Moves**: for a build label |
| `app/CMakeLists.txt`: `app_version`, `NV_BUILD_NAME` | The commit hash and subject header | **Moves** (a CMake function): the game's page also shows its commit |
| Repeated small helpers | `clampf` (ui_desktop.c), `clamp` (save.c), `clamp_u32` (ui.c), FNV-1a in three places (strings.c, save.c, ui.c), xorshift (effects.c), `pixel_ratio > 0 ? … : 1` in seven places | **Move**: the game needs an FNV-1a hash and seeded random numbers |
| `app/ui.c`: `ui_build_label` | The build label and the Console badge | Stays: the badge is tied to the Console tab. The game draws its own label with `nv_imgui_fit_text` |
| `app/effects.c` | What an explosion, sparks, smoke and a missile trail are made of | Stays: it is content. The game defines its own effects per weapon (resolved question 1) |
| `app/console.c` | The Console tab | Stays: not in the game's spec. The log ring is already in the engine (resolved question 2) |
| `app/save.c`, `undo.c`, `selection.c`, `search.c`, `shortcuts.c`, `ui_desktop.c`, `ui_phone.c`, `textures.c`, `stress.c` | Saving, undo, selection, search and palette, shortcuts, docks, the phone panel, the Textures tab, the stress scene | Stay: editor only |
| `app/ui.c`: `msaa_ui`, `post_ui`, `shadow_ui`, `resolution_ui` | Renderer setting widgets | Stay: tied to `search_row`, and not in the game's spec |
| `app/main.c`: `frame`, the setup in `main`, `FrameTimes` | Frame order, arenas, defaults, timing | Stay: short and different in each executable. No framework (resolved question 3) |

### What moves

#### 1. UI strings (`engine/strings.h`)

Moved here from the "UI strings" section of `battle.md`, as decided:

```c
typedef enum NvLanguage { NV_LANGUAGE_EN, NV_LANGUAGE_KO, NV_LANGUAGE_COUNT } NvLanguage;
typedef struct NvStringPair { const char* english; const char* korean; } NvStringPair;

#define NV_STRINGS_MAX 4096 // pairs per executable

void nv_strings_set_table(const NvStringPair* pairs, u32 count); // kept, not copied; once, before any T()
NvLanguage nv_strings_language(void);
void nv_strings_set_language(NvLanguage language);
NvLanguage nv_strings_browser_language(void);
const char* nv_strings_find_korean(const char* english);
const char* nv_strings_text(const char* english);
const char* nv_strings_label(const char* english);

// NOTE: Short on purpose, since they wrap every UI text: the one exception to the naming rules.
#define T(english)  nv_strings_text(english)
#define TL(english) nv_strings_label(english)
```

- `engine/strings.c`: the hash table, `T`, `TL` and the language now in `app/strings.c`. Its state is file-global.
- `app/strings.c` keeps only its table (an `NvStringPair` array), and `main` calls `nv_strings_set_table`. `app/strings.h`
  goes, and `app/app.h` does `#include <engine/strings.h>`. Engine headers are always included with their folder, so it is
  never confused with the app's `strings.c` or POSIX's `<strings.h>`.
- The `LANG` save tag's values (0 English, 1 Korean) equal those of `NV_LANGUAGE_*`.
- `tests/strings_test.mjs` takes a list of folders and checks each against its own `strings.c`. For now that is `app/`;
  the game adds `autobattler/`. (`tools/subset_hangul.sh` took a list of tables too, until the font became Pretendard alone on 2026-10-03 and it went.)

#### 2. Mesh primitives (`engine/mesh.h`)

```c
// A mesh being built in arrays the caller owns; the primitives append to it and assert that they fit.
typedef struct NvMeshBuilder {
    NvVertex* vertices;
    u32 vertex_count, vertex_capacity;
    u32* indices;
    u32 index_count, index_capacity;
} NvMeshBuilder;
NvMeshData nv_mesh_builder_data(const NvMeshBuilder* mesh); // what nv_renderer_add_mesh takes

void nv_mesh_append_box(NvMeshBuilder* mesh, NvVec3 center, NvVec3 half);
void nv_mesh_append_plane(NvMeshBuilder* mesh, f32 half_x, f32 half_z); // y = 0, facing +Y
```

- `engine/mesh.h` takes `NvVertex`, `NvSkinnedVertex` and `NvMeshData` over from `engine/renderer.h`, which includes
  `mesh.h`. So `mesh.h` and `engine/mesh.c` have no GPU, and ctest checks them without the port. The functions grow the
  counts in `NvMeshData`.
- App: `app_box_mesh`, `create_sword_mesh` and `create_ground_mesh` use it. Vertex order and values are the same, so the
  screen is too.
- Game: unit cubes, prop boxes, the 64 × 96 m field.

#### 3. UI font (`engine/imgui.h`)

```c
// The UI font from /assets/fonts/ (docs/specs/fonts.md; Pretendard since 2026-10-03), read into `arena`
// (ImGui keeps pointing at the bytes). Logs a warning and keeps the built-in font when a file is missing.
b32 nv_imgui_load_ui_font(NvImgui* imgui, NvArena* arena);

// `text` cut to `room` pixels with "..." at the end, never inside a UTF-8 character.
void nv_imgui_fit_text(const char* text, f32 room, char* out, umm capacity);
```

- The paths and size (14 px) are the UI font `fonts.md` decided, so they live in the engine. The app's `read_asset`,
  `load_font` and `fit_text` go.
- The log source becomes `"imgui"` (now `"app"`). No test looks messages up by source.

#### 4. Scene resolution (`engine/renderer.h`)

```c
typedef enum NvResolutionMode { NV_RESOLUTION_SCALE, NV_RESOLUTION_FIXED } NvResolutionMode;
typedef enum NvFixedFit { NV_FIT_WHOLE, NV_FIT_VIEWPORT, NV_FIT_STRETCH } NvFixedFit;

typedef struct NvResolution {
    NvResolutionMode mode;
    NvFixedFit fixed_fit;
    u32 divisor;                   // SCALE: 1 to 4
    u32 fixed_width, fixed_height; // FIXED: NV_RESOLUTION_MIN..NV_RESOLUTION_MAX
} NvResolution;

#define NV_RESOLUTION_MIN 16
#define NV_RESOLUTION_MAX 4096

// The scene's size and where its image goes in `viewport` (docs/specs/resolution.md).
NvSceneOutput nv_renderer_scene_output(const NvResolution* resolution, NvRect viewport);

// A ray through a tap given in CSS pixels; not ok when the tap is outside the image (on a black bar).
NvTapRay nv_renderer_tap_ray(NvScene* scene, NvSceneOutput output, f32 tap_x, f32 tap_y, f32 pixel_ratio);

// The view-projection the scene pass draws with (reverse Z): `renderer.c` calls it, so it is declared too.
NvMat4 nv_renderer_camera_view_proj(NvNode* camera_node, f32 aspect);
```

- `engine/renderer_cpu.c`: the renderer's GPU-free code (the way `engine/vfx_cpu.c` works). `nv_renderer_scene_output`
  and `nv_renderer_view_ray` move there so ctest can check them; `nv_renderer_camera_matrices` comes too if it uses no GPU.
  The declarations stay in `engine/renderer.h`, so it has no header of its own. `renderer.h` includes `engine/gpu.h` (the
  WebGPU header), so the test, like `gpu_format_test`, takes the headers with `--use-port=emdawnwebgpu` and links nothing.
- The enum value order is unchanged, so the `RSMD` and `RSFT` save tags keep their meaning.
- App: `Resolution`, `ResolutionMode`, `FixedFit`, `RESOLUTION_MIN/MAX` and `scene_output` go. `pick` starts with
  `nv_renderer_tap_ray`. `ui_rect` stays in the app (dock layout).
- Game: divisor 2 on phones, taps for deployment.

#### 5. Orbit camera (`engine/camera.h`)

```c
typedef struct NvOrbitCamera {
    NvVec3 target; // the point it orbits
    f32 yaw, pitch, distance;
    f32 min_pitch, max_pitch, min_distance, max_distance;
} NvOrbitCamera;

// Turns by yaw and pitch (radians) and dollies by `dolly` (the log of the distance factor), within the limits.
void nv_orbit_camera_turn(NvOrbitCamera* camera, f32 yaw, f32 pitch, f32 dolly);
// The world move for a pan of (pan_x, pan_y) pixels on an image `image_height` pixels high: one pixel is the
// height the view covers at the target divided by the image's height, along the camera's right and up axes.
NvVec3 nv_orbit_camera_pan(const NvOrbitCamera* camera, const NvNode* camera_node, f32 image_height, f32 pan_x, f32 pan_y);
// Sets the camera node's position, rotation and world matrix (a top-level node).
void nv_orbit_camera_place(const NvOrbitCamera* camera, NvNode* camera_node);
```

- `engine/camera.h` includes only `engine/scene.h`. The caller turns input (`NvViewInput`, `engine/imgui.h`) and the
  resolution (`NvSceneOutput`, `engine/renderer.h`) into numbers: the app passes `orbit_x * ORBIT_RADIANS_PER_PIXEL`, and
  the image height as `scene_output.height * pixel_height / pixel_ratio`. So `engine/camera.c` has no GPU, and ctest checks
  it without the port.
- App: `SceneView`'s `camera_yaw`, `camera_pitch`, `camera_distance` and `orbit_point` become one `NvOrbitCamera orbit`.
  Following the selection, `pan`, home, focus and holding still during a gizmo drag stay in the app. The `CAMERA_*` limits
  become fields of `orbit`.
- Save: the tags stay; only the field paths written and read change (`view->orbit.yaw`).
- Game: pitch and yaw fixed, the pan projected onto the ground (y set to 0) and kept over the field.

#### 6. Downloaded size (`engine/window.h`)

```c
// "3.2 MB downloaded" or "3.2 MB from cache" for this page's own files; empty when the browser does not say.
void nv_window_download_text(char* out, umm capacity);
```

- The file names were fixed (`app.wasm`). `web/index.html.in` sets `Module.nvTarget = "@NV_WEB_TARGET@"`, and the `EM_JS`
  looks for `<target>.wasm`, `.data` and `.js`.

#### 7. Build version (CMake)

- `nv_add_version(<target>)` in the root `CMakeLists.txt`: a `<target>_version` target, `nv_version.h` in the target's
  folder, the include path and `NV_BUILD_NAME`. The same code in `app/CMakeLists.txt` becomes this one line.
  `cmake/version.cmake` stays as it is.

#### 8. Small helpers (`engine/base.h`, `engine/math.h`, `engine/window.h`)

```c
// engine/base.h: FNV-1a, chainable: nv_fnv1a(NV_FNV1A_SEED, bytes, size).
#define NV_FNV1A_SEED  2166136261u
#define NV_FNV1A_PRIME 16777619u
static inline u32 nv_fnv1a(u32 hash, const void* bytes, umm size);

// engine/math.h
static inline f32 nv_clamp_f32(f32 value, f32 lo, f32 hi);
static inline u32 nv_clamp_u32(u32 value, u32 lo, u32 hi);

// engine/math.h: PCG32 (O'Neill, pcg-random.org; the formula is cited in a comment). A zeroed NvRandom is a valid
// generator (the step uses increment | 1); nv_random_seed picks a sequence.
typedef struct NvRandom { u64 state, increment; } NvRandom;
static inline void nv_random_seed(NvRandom* random, u64 seed, u64 sequence);
static inline u32 nv_random_u32(NvRandom* random);
static inline f32 nv_random_f32(NvRandom* random); // [0, 1)

// engine/window.h: CSS pixels to framebuffer pixels, 1 before the browser says.
static inline f32 nv_window_pixel_ratio(const NvWindow* window);
```

- The app's `clampf`, `clamp`, `clamp_u32`, the three FNV-1a loops and the seven `pixel_ratio` expressions use these. The
  scene layout hash in `save.c` must give the same value (saved nodes are matched by it): a test compares it with today's.
- `effects.c`'s xorshift becomes `NvRandom`. The random order of the stress scene's effects changes, but it is neither saved
  nor checked by value (resolved question 4).
- Game: the hash and seeded random numbers of determinism (`battle.md`, "Determinism").
- There is no `engine/base.c`, so `nv_fnv1a` is `static inline` in the header.

### What does not change

- The save format: every tag, its values, `SAVE_VERSION`. Saves written before this refactoring load as they are.
- Debug exports (`Module._app_debug_*`): names and results.
- The screen: a screenshot of the showcase in Edit mode is the same, pixel for pixel (SwiftShader is deterministic).
- Log message texts. Only one source changes, from `"app"` to `"imgui"` (the font).

### Tests

ctest (Node), new files. `tests/CMakeLists.txt` builds each test with `${PROJECT_SOURCE_DIR}/engine/<name>.c`:

- `tests/mesh_test.c`: a box's 24 vertices and 36 indices, each face's normal and counter-clockwise winding, the capacity
  assert (Debug), the plane.
- `tests/resolution_test.c`: `nv_renderer_scene_output` for Scale 1–4, Fixed's three fits, a viewport too small, zero size;
  `nv_renderer_tap_ray` on a black bar.
- `tests/camera_test.c`: limits, the pan's distance per pixel, the placed node's position and rotation.
- `tests/base_test.c`: FNV-1a known values, PCG32's first outputs (from the reference implementation), a zeroed `NvRandom`.
- `strings_test.mjs`: in its folder-list form.

Browser (headless Chromium, Release and Debug, desktop and phone sizes). The build before the refactoring and the one after
run the same steps and are compared:

- `_app_debug_save_crc()` on a first visit is the same. A save written by the old build loads in the new one, and writing it
  again gives the same bytes.
- Screenshots of the showcase in Edit mode are the same, and so is the panel switched to Korean.
- `_app_debug_scene`, `_app_debug_project`, `_app_debug_view` and `_app_debug_language` are the same. A tap on a black bar
  does not change the selection.
- The downloaded-size text shows, and the Console has no font warning.

### Phases

Each phase ended by checking with the steps above that the app is as before. Phases 2 to 6 change the same files, so they share one commit.

1. **Helpers:** `nv_fnv1a`, `nv_clamp_*`, `NvRandom`, `nv_window_pixel_ratio`, `base_test.c`.
2. **Meshes:** `engine/mesh.h`, `mesh_test.c`.
3. **Resolution and taps:** `NvResolution`, `renderer_cpu.c`, `nv_renderer_tap_ray`, `resolution_test.c`.
4. **Camera:** `engine/camera.h`, `camera_test.c`, the `SceneView` change.
5. **Page and font:** `nv_imgui_load_ui_font`, `nv_imgui_fit_text`, `nv_window_download_text`, `nv_add_version`.
6. **Strings:** `engine/strings.h`, `strings_test.mjs`, `subset_hangul.sh`.
7. **Docs:** `AGENTS.md` (the new modules in the `engine/` list: `mesh.c/.h`, `camera.c/.h`, `strings.c/.h`,
   `renderer_cpu.c`; the UI strings line), `CODING_STANDARD.md` (the `T`/`TL`
   exception), and the names and paths in `korean.md`, `resolution.md` and `fonts.md`. `battle.md` points to this spec as
   the work that comes first.

### As built

Where the build differs from the spec:

- **Helpers:** `nv_random_*` and `nv_window_pixel_ratio` are `static inline` (there is no `engine/math.c`). `base.h` has one more,
  `NV_FNV1A_PRIME`: the scene layout hash in `save.c` mixes the depth in as a word, not a byte (`(hash ^ depth) * prime`), so a
  byte-wise `nv_fnv1a` would give another value from depth 256 on. Only the name loop became `nv_fnv1a`, and the depth line
  stayed. The two places in `engine/imgui.c` use `nv_window_pixel_ratio` too; the one in `engine/gpu.c` has another fallback and
  stays.
- **Meshes:** `NvMeshData` holds const pointers, so it cannot be written to. The primitives append to an `NvMeshBuilder`
  (arrays, counts, capacities) instead, and `nv_mesh_builder_data` gives the `NvMeshData`. Running out of room is an assert (a
  trap), which cannot be tested in the same process.
- **Resolution:** `nv_renderer_camera_view_proj` (the reverse-Z matrix drawing uses) moved to `renderer_cpu.c` and is declared
  too, since `renderer.c` calls it. `nv_renderer_camera_matrices` and `nv_renderer_view_ray` moved with it.
- **Camera:** `SceneView.home` is a copy of a whole `NvOrbitCamera`. The limits are fields and must not be zero, so the two
  places that make a view put in `ORBIT_LIMITS`.
- **Strings:** the app hands its table over with `app_strings_init()`. The hash table is built when the table is set (it was on
  first use), and has 8192 slots (`u16`, 16 KB).
- **Browser checks:** the build from before the refactoring (`3f6f8c7`) and the build of each phase ran the same Playwright steps
  and were compared. The steps: a first visit, picking by click (the ground, a cube, the character), orbit, zoom, pan, a fixed
  resolution and a tap on its bar, Scale 2, Korean, the phone UI, reading the save file, and loading a save the old build wrote.
  The JSON (CRC, resolution, view, selection, language, the save file's SHA-256) is exactly the same. In screenshots the docks match
  (within 0.1%) and the viewport within 15%: it has the character's animation and shadow, which move with time even in Edit mode,
  so it cannot be the same pixel for pixel. The stress scene and the effects ran in Debug without an assert.

### Resolved questions

All settled as recommended on 2026-10-02.

1. **Effect definitions:** `app/effects.c`'s explosion, sparks, smoke and missile trail stay in the app. They are content, so
   the game defines its own effects per weapon.
2. **Console tab:** not moved now; it moves when the game wants it. The log ring is already in the engine.
3. **Frame and setup:** each executable writes its own `main` and `frame`. The common start is not bundled into an engine
   function.
4. **Random numbers:** `effects.c` uses `NvRandom`. The changed random order of the stress scene's effects is the one exception
   to "behavior does not change": it is neither saved nor checked by value.
5. **Camera module:** a new module, `engine/camera.h` and `engine/camera.c`.

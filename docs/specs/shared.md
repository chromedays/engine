# Shared code spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 초안 (2026-10-02). 구현 전에 "열린 질문"을 합의한다.

### 목표

`docs/specs/battle.md`의 게임(`autobattler/`, 엔진만 링크하는 두 번째 실행 파일)을 만들기 전에, `app/`에 있는 코드 중 그 게임도
쓸 것을 엔진으로 옮긴다. 리팩터링일 뿐이다: 에디터 앱의 동작, 화면, 저장 형식은 바뀌지 않는다.

### 원칙

- **옮기는 기준은 `AGENTS.md`의 "다른 앱도 쓸까?"이고, 그 다른 앱은 `battle.md`다.** 코딩 표준의 "두 번째로 필요할 때
  뽑는다"에 따라, 게임이 지금 쓸 것만 옮긴다. 언젠가 쓸지도 모르는 것은 남긴다.
- **동작을 바꾸지 않는다.** 저장 태그와 그 값, `SAVE_VERSION`, 디버그 내보내기, 화면의 픽셀이 그대로다. 열거형의 값 순서를
  지켜서, 저장된 값이 같은 뜻으로 읽히게 한다.
- **엔진은 `App`을 모른다.** 옮긴 코드는 `App`, `SceneView`, 검색, 저장을 참조하지 않는다. 앱에 남는 쪽이 엔진 함수를 부른다.
- **새 서드파티는 없다.**

### 조사 결과

| 파일 | 내용 | 판단 |
|---|---|---|
| `app/strings.c`, `strings.h` | `T()`, `TL()`, 언어, 한국어 표 | **옮김** (조회와 언어). 표는 실행 파일마다 남는다 |
| `app/main.c`: `append_box`, `app_box_mesh`, `create_ground_mesh` | 상자와 바닥 메시 | **옮김**: 게임의 유닛, 지형지물, 전장 |
| `app/main.c`: `read_asset`, `load_font` | UI 글꼴(Inter와 한글 서브셋) 읽기 | **옮김**: 같은 글꼴을 쓴다 |
| `app/main.c`: `js_download_bytes`, `read_download_size` | 내려받은 크기 글 | **옮김**: 파일 이름을 실행 파일 이름에서 얻도록 |
| `app/main.c`: `apply_view_input`, `update_camera` | 궤도 카메라(회전, 줌, 팬, 배치) | **일부 옮김**: 수학은 엔진, 선택 따라가기와 홈은 앱 |
| `app/main.c`: `pick` | 탭이 이미지 안인지, CSS 픽셀에서 광선 | **일부 옮김**: 탭에서 광선까지. 배지와 선택은 앱 |
| `app/ui.c`: `scene_output`, `Resolution`, `ui_rect` | 씬 해상도와 이미지 사각형 | **옮김**: 게임도 폰에서 해상도를 낮춘다 |
| `app/ui.c`: `fit_text` | 폭에 맞춰 자르고 "..." | **옮김**: 빌드 표시 |
| `app/CMakeLists.txt`: `app_version`, `NV_BUILD_NAME` | 커밋 해시와 제목 헤더 | **옮김** (CMake 함수): 게임 페이지도 어느 커밋인지 보여 준다 |
| 중복된 작은 도우미 | `clampf`(ui_desktop.c), `clamp`(save.c), `clamp_u32`(ui.c), FNV-1a 세 곳(strings.c, save.c, ui.c), xorshift(effects.c), `pixel_ratio > 0 ? … : 1` 일곱 곳 | **옮김**: 게임은 FNV-1a 해시와 시드 난수가 필요하다 |
| `app/ui.c`: `ui_build_label` | 빌드 표시와 콘솔 배지 | 남김: 배지가 콘솔 탭과 묶여 있다. 게임은 `nv_imgui_fit_text`로 제 표시를 그린다 |
| `app/effects.c` | 폭발, 불꽃, 연기, 미사일 궤적의 정의 | 남김: 내용(content)이다. 게임은 무기마다 제 이펙트를 정의한다 (열린 질문 1) |
| `app/console.c` | Console 탭 | 남김: 게임 스펙에 없다. 로그 링은 이미 엔진에 있다 (열린 질문 2) |
| `app/save.c`, `undo.c`, `selection.c`, `search.c`, `shortcuts.c`, `ui_desktop.c`, `ui_phone.c`, `textures.c`, `stress.c` | 저장, undo, 선택, 검색과 팔레트, 단축키, 도크, 폰 패널, Textures 탭, 스트레스 씬 | 남김: 에디터만의 것 |
| `app/ui.c`: `msaa_ui`, `post_ui`, `shadow_ui`, `resolution_ui` | 렌더러 설정 위젯 | 남김: `search_row`에 묶여 있고 게임 스펙에 없다 |
| `app/main.c`: `frame`, `main`의 초기화, `FrameTimes` | 프레임 순서, 아레나, 기본값, 시간 재기 | 남김: 실행 파일마다 짧고 다르다. 프레임워크로 만들지 않는다 (열린 질문 3) |

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
- `app/strings.c`는 표(`NvStringPair` 배열)만 남기고, `main`이 `nv_strings_set_table`을 부른다. `app/strings.h`는 없어진다.
- 저장 태그 `LANG`의 값(0 영어, 1 한국어)은 `NV_LANGUAGE_*`의 값과 같다.
- `tests/strings_test.mjs`는 폴더 목록을 받아 폴더마다 자기 `strings.c`에 대해 검사한다. 지금은 `app/` 하나이고, 게임이
  `autobattler/`를 더한다. `tools/subset_hangul.sh`도 표 목록을 받는다.

#### 2. 메시 기본형 (`engine/mesh.h`)

```c
// Appends to `data`, whose arrays hold `vertex_capacity` and `index_capacity`; asserts on overflow.
void nv_mesh_append_box(NvMeshData* data, u32 vertex_capacity, u32 index_capacity, NvVec3 center, NvVec3 half);
void nv_mesh_append_plane(NvMeshData* data, u32 vertex_capacity, u32 index_capacity, f32 half_x, f32 half_z); // y = 0, facing +Y
```

- `engine/mesh.c`: GPU 없는 코드라 ctest로 검사한다. `NvMeshData`의 count를 늘린다.
- 앱: `app_box_mesh`, `create_sword_mesh`, `create_ground_mesh`가 이것을 쓴다. 꼭짓점 순서와 값이 같으므로 화면이 같다.
- 게임: 유닛 큐브, 지형지물 상자, 64 × 96 m 전장.

#### 3. UI 글꼴 (`engine/imgui.h`)

```c
// Inter with the Hangul subset behind it, from /assets/fonts/ (docs/specs/fonts.md), read into `arena`
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

// A ray through a tap given in CSS pixels; false when the tap is outside the image (on a black bar).
b32 nv_renderer_tap_ray(NvScene* scene, NvSceneOutput output, f32 tap_x, f32 tap_y, f32 pixel_ratio, NvRay* ray);
```

- `engine/renderer_cpu.c`: GPU 없는 렌더러 코드(`vfx_cpu.c`와 같은 방식). `nv_renderer_scene_output`과
  `nv_renderer_view_ray`가 여기로 와서 ctest로 검사한다. `nv_renderer_camera_matrices`도 GPU를 쓰지 않으면 함께 온다.
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

// Turns by orbit_x and orbit_y (pixels times `radians_per_pixel`) and dollies by `dolly`, within the limits.
void nv_orbit_camera_turn(NvOrbitCamera* camera, const NvViewInput* input, f32 radians_per_pixel);
// The world move for a pan of (pan_x, pan_y) CSS pixels: one pixel is the height the view covers at
// the target divided by the image's height, along the camera's right and up axes.
NvVec3 nv_orbit_camera_pan(const NvOrbitCamera* camera, const NvNode* camera_node, NvSceneOutput output,
                           f32 pixel_ratio, f32 pan_x, f32 pan_y);
// Sets the camera node's position, rotation and world matrix (a top-level node).
void nv_orbit_camera_place(const NvOrbitCamera* camera, NvNode* camera_node);
```

- `engine/camera.c`, GPU 없음, ctest로 검사한다.
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
#define NV_FNV1A_SEED 2166136261u
static inline u32 nv_fnv1a(u32 hash, const void* bytes, umm size);

// engine/math.h
static inline f32 nv_clamp_f32(f32 value, f32 lo, f32 hi);
static inline u32 nv_clamp_u32(u32 value, u32 lo, u32 hi);

// engine/math.h: PCG32 (O'Neill, pcg-random.org; the formula is cited in a comment). A zeroed NvRandom is a valid
// generator (the step uses increment | 1); nv_random_seed picks a sequence.
typedef struct NvRandom { u64 state, increment; } NvRandom;
void nv_random_seed(NvRandom* random, u64 seed, u64 sequence);
u32 nv_random_u32(NvRandom* random);
f32 nv_random_f32(NvRandom* random); // [0, 1)

// engine/window.h: CSS pixels to framebuffer pixels, 1 before the browser says.
f32 nv_window_pixel_ratio(const NvWindow* window);
```

- 앱의 `clampf`, `clamp`, `clamp_u32`, 세 FNV-1a 루프, 일곱 `pixel_ratio` 식이 이것을 쓴다. `save.c`의 장면 배치 해시는
  같은 값을 내야 한다(저장의 노드가 그 해시로 맞춰지므로): 테스트가 지금 값과 비교한다.
- `effects.c`의 xorshift는 `NvRandom`이 된다. 스트레스 씬 이펙트의 무작위 순서가 바뀌지만 저장되지도, 값으로 검사되지도
  않는다 (열린 질문 4).
- 게임: 결정론의 해시와 시드 난수(`battle.md`의 "결정론").
- `nv_fnv1a`의 구현은 `engine/base.c`가 없으므로 `static inline`으로 헤더에 둔다.

### 바뀌지 않는 것

- 저장 형식: 모든 태그, 그 값, `SAVE_VERSION`. 이 리팩터링 전에 쓴 저장이 그대로 읽힌다.
- 디버그 내보내기(`Module._app_debug_*`)의 이름과 결과.
- 화면: Edit 모드 쇼케이스의 스크린샷이 픽셀 단위로 같다(SwiftShader는 결정적이다).
- 로그 메시지 글. 출처만 `"app"`에서 `"imgui"`로 바뀌는 것이 있다(글꼴).

### 테스트

ctest(Node), 새 파일:

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

각 단계는 커밋 하나이고, 끝마다 앱이 전과 같은지 위 검사로 확인한다.

1. **도우미:** `nv_fnv1a`, `nv_clamp_*`, `NvRandom`, `nv_window_pixel_ratio`, `base_test.c`.
2. **메시:** `engine/mesh.h`, `mesh_test.c`.
3. **해상도와 탭:** `NvResolution`, `renderer_cpu.c`, `nv_renderer_tap_ray`, `resolution_test.c`.
4. **카메라:** `engine/camera.h`, `camera_test.c`, `SceneView`의 변경.
5. **페이지와 글꼴:** `nv_imgui_load_ui_font`, `nv_imgui_fit_text`, `nv_window_download_text`, `nv_add_version`.
6. **문자열:** `engine/strings.h`, `strings_test.mjs`, `subset_hangul.sh`.
7. **문서:** `AGENTS.md`(구조 목록의 새 헤더, UI 문자열 줄), `CODING_STANDARD.md`(`T`/`TL` 예외), `korean.md`,
   `resolution.md`, `fonts.md`의 이름과 경로. `battle.md`는 이 스펙을 선행 작업으로 가리킨다.

### 열린 질문

1. **이펙트 정의:** `effects.c`의 폭발, 불꽃, 연기, 미사일 궤적은 앱에 남기고 게임이 제 것을 정의한다(추천). 아니면 엔진의
   기본 이펙트 모음으로 옮길까?
2. **Console 탭:** 게임에는 없다(`battle.md`). 폰에서 디버깅하려면 필요할 수 있다. 나중에 게임이 원할 때 옮긴다(추천)?
3. **프레임과 초기화:** 실행 파일마다 `main`과 `frame`을 직접 쓴다(추천). 아니면 공통 시작 코드(창, GPU, 렌더러, ImGui,
   글꼴, 기본값)를 엔진 함수 하나로 묶을까?
4. **난수:** `effects.c`를 `NvRandom`으로 바꿔 스트레스 이펙트의 무작위 순서가 바뀌는 것이 괜찮은가? 아니면 xorshift를
   그대로 둘까?
5. **카메라 모듈:** 새 모듈 `engine/camera.h`(추천)인가, `engine/scene.h`에 둘까?

## English

Status: draft (2026-10-02). The "Open questions" are agreed before it is built.

### Goal

Before building the game of `docs/specs/battle.md` (`autobattler/`, a second executable that links only the engine), move
the code in `app/` that the game will also use into the engine. It is a refactoring only: the editor app's behavior, screen
and save format do not change.

### Principles

- **The test for moving is `AGENTS.md`'s "would another app use this?", and the other app is `battle.md`.** Following the
  coding standard's "pull it out the second time it is needed", only what the game uses now moves. What it might use some
  day stays.
- **Behavior does not change.** Save tags and their values, `SAVE_VERSION`, debug exports and the pixels on screen stay the
  same. Enum value order is kept, so saved values read with the same meaning.
- **The engine does not know `App`.** Moved code refers to no `App`, `SceneView`, search or save. The side that stays in the
  app calls the engine functions.
- **No new third-party code.**

### Survey

| File | Contents | Verdict |
|---|---|---|
| `app/strings.c`, `strings.h` | `T()`, `TL()`, the language, the Korean table | **Moves** (lookup and language). Each executable keeps its own table |
| `app/main.c`: `append_box`, `app_box_mesh`, `create_ground_mesh` | Box and ground meshes | **Moves**: the game's units, props and field |
| `app/main.c`: `read_asset`, `load_font` | Reading the UI font (Inter and the Hangul subset) | **Moves**: the game uses the same font |
| `app/main.c`: `js_download_bytes`, `read_download_size` | The downloaded-size text | **Moves**, with file names taken from the executable's name |
| `app/main.c`: `apply_view_input`, `update_camera` | The orbit camera (turn, zoom, pan, placement) | **Partly moves**: the math goes to the engine; following the selection and home stay in the app |
| `app/main.c`: `pick` | Whether a tap is inside the image, a ray from CSS pixels | **Partly moves**: from the tap to the ray. The badge and the selection stay |
| `app/ui.c`: `scene_output`, `Resolution`, `ui_rect` | The scene resolution and the image rectangle | **Moves**: the game also lowers the resolution on phones |
| `app/ui.c`: `fit_text` | Cut to a width with "..." | **Moves**: for a build label |
| `app/CMakeLists.txt`: `app_version`, `NV_BUILD_NAME` | The commit hash and subject header | **Moves** (a CMake function): the game's page also shows its commit |
| Repeated small helpers | `clampf` (ui_desktop.c), `clamp` (save.c), `clamp_u32` (ui.c), FNV-1a in three places (strings.c, save.c, ui.c), xorshift (effects.c), `pixel_ratio > 0 ? … : 1` in seven places | **Move**: the game needs an FNV-1a hash and seeded random numbers |
| `app/ui.c`: `ui_build_label` | The build label and the Console badge | Stays: the badge is tied to the Console tab. The game draws its own label with `nv_imgui_fit_text` |
| `app/effects.c` | What an explosion, sparks, smoke and a missile trail are made of | Stays: it is content. The game defines its own effects per weapon (open question 1) |
| `app/console.c` | The Console tab | Stays: not in the game's spec. The log ring is already in the engine (open question 2) |
| `app/save.c`, `undo.c`, `selection.c`, `search.c`, `shortcuts.c`, `ui_desktop.c`, `ui_phone.c`, `textures.c`, `stress.c` | Saving, undo, selection, search and palette, shortcuts, docks, the phone panel, the Textures tab, the stress scene | Stay: editor only |
| `app/ui.c`: `msaa_ui`, `post_ui`, `shadow_ui`, `resolution_ui` | Renderer setting widgets | Stay: tied to `search_row`, and not in the game's spec |
| `app/main.c`: `frame`, the setup in `main`, `FrameTimes` | Frame order, arenas, defaults, timing | Stay: short and different in each executable. No framework (open question 3) |

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
  goes.
- The `LANG` save tag's values (0 English, 1 Korean) equal those of `NV_LANGUAGE_*`.
- `tests/strings_test.mjs` takes a list of folders and checks each against its own `strings.c`. For now that is `app/`;
  the game adds `autobattler/`. `tools/subset_hangul.sh` takes a list of tables too.

#### 2. Mesh primitives (`engine/mesh.h`)

```c
// Appends to `data`, whose arrays hold `vertex_capacity` and `index_capacity`; asserts on overflow.
void nv_mesh_append_box(NvMeshData* data, u32 vertex_capacity, u32 index_capacity, NvVec3 center, NvVec3 half);
void nv_mesh_append_plane(NvMeshData* data, u32 vertex_capacity, u32 index_capacity, f32 half_x, f32 half_z); // y = 0, facing +Y
```

- `engine/mesh.c`: GPU-free, so ctest checks it. It grows the counts in `NvMeshData`.
- App: `app_box_mesh`, `create_sword_mesh` and `create_ground_mesh` use it. Vertex order and values are the same, so the
  screen is too.
- Game: unit cubes, prop boxes, the 64 × 96 m field.

#### 3. UI font (`engine/imgui.h`)

```c
// Inter with the Hangul subset behind it, from /assets/fonts/ (docs/specs/fonts.md), read into `arena`
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

// A ray through a tap given in CSS pixels; false when the tap is outside the image (on a black bar).
b32 nv_renderer_tap_ray(NvScene* scene, NvSceneOutput output, f32 tap_x, f32 tap_y, f32 pixel_ratio, NvRay* ray);
```

- `engine/renderer_cpu.c`: the renderer's GPU-free code (the way `vfx_cpu.c` works). `nv_renderer_scene_output` and
  `nv_renderer_view_ray` move there so ctest can check them; `nv_renderer_camera_matrices` comes too if it uses no GPU.
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

// Turns by orbit_x and orbit_y (pixels times `radians_per_pixel`) and dollies by `dolly`, within the limits.
void nv_orbit_camera_turn(NvOrbitCamera* camera, const NvViewInput* input, f32 radians_per_pixel);
// The world move for a pan of (pan_x, pan_y) CSS pixels: one pixel is the height the view covers at
// the target divided by the image's height, along the camera's right and up axes.
NvVec3 nv_orbit_camera_pan(const NvOrbitCamera* camera, const NvNode* camera_node, NvSceneOutput output,
                           f32 pixel_ratio, f32 pan_x, f32 pan_y);
// Sets the camera node's position, rotation and world matrix (a top-level node).
void nv_orbit_camera_place(const NvOrbitCamera* camera, NvNode* camera_node);
```

- `engine/camera.c`, GPU-free, checked by ctest.
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
#define NV_FNV1A_SEED 2166136261u
static inline u32 nv_fnv1a(u32 hash, const void* bytes, umm size);

// engine/math.h
static inline f32 nv_clamp_f32(f32 value, f32 lo, f32 hi);
static inline u32 nv_clamp_u32(u32 value, u32 lo, u32 hi);

// engine/math.h: PCG32 (O'Neill, pcg-random.org; the formula is cited in a comment). A zeroed NvRandom is a valid
// generator (the step uses increment | 1); nv_random_seed picks a sequence.
typedef struct NvRandom { u64 state, increment; } NvRandom;
void nv_random_seed(NvRandom* random, u64 seed, u64 sequence);
u32 nv_random_u32(NvRandom* random);
f32 nv_random_f32(NvRandom* random); // [0, 1)

// engine/window.h: CSS pixels to framebuffer pixels, 1 before the browser says.
f32 nv_window_pixel_ratio(const NvWindow* window);
```

- The app's `clampf`, `clamp`, `clamp_u32`, the three FNV-1a loops and the seven `pixel_ratio` expressions use these. The
  scene layout hash in `save.c` must give the same value (saved nodes are matched by it): a test compares it with today's.
- `effects.c`'s xorshift becomes `NvRandom`. The random order of the stress scene's effects changes, but it is neither saved
  nor checked by value (open question 4).
- Game: the hash and seeded random numbers of determinism (`battle.md`, "Determinism").
- There is no `engine/base.c`, so `nv_fnv1a` is `static inline` in the header.

### What does not change

- The save format: every tag, its values, `SAVE_VERSION`. Saves written before this refactoring load as they are.
- Debug exports (`Module._app_debug_*`): names and results.
- The screen: a screenshot of the showcase in Edit mode is the same, pixel for pixel (SwiftShader is deterministic).
- Log message texts. Only one source changes, from `"app"` to `"imgui"` (the font).

### Tests

ctest (Node), new files:

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

Each phase is one commit, and each ends by checking with the steps above that the app is as before.

1. **Helpers:** `nv_fnv1a`, `nv_clamp_*`, `NvRandom`, `nv_window_pixel_ratio`, `base_test.c`.
2. **Meshes:** `engine/mesh.h`, `mesh_test.c`.
3. **Resolution and taps:** `NvResolution`, `renderer_cpu.c`, `nv_renderer_tap_ray`, `resolution_test.c`.
4. **Camera:** `engine/camera.h`, `camera_test.c`, the `SceneView` change.
5. **Page and font:** `nv_imgui_load_ui_font`, `nv_imgui_fit_text`, `nv_window_download_text`, `nv_add_version`.
6. **Strings:** `engine/strings.h`, `strings_test.mjs`, `subset_hangul.sh`.
7. **Docs:** `AGENTS.md` (the new headers in the layout list, the UI strings line), `CODING_STANDARD.md` (the `T`/`TL`
   exception), and the names and paths in `korean.md`, `resolution.md` and `fonts.md`. `battle.md` points to this spec as
   the work that comes first.

### Open questions

1. **Effect definitions:** keep `effects.c`'s explosion, sparks, smoke and missile trail in the app and let the game define
   its own (recommended), or move them to the engine as a set of default effects?
2. **Console tab:** the game has none (`battle.md`). Debugging on a phone may need it. Move it later, when the game wants it
   (recommended)?
3. **Frame and setup:** each executable writes its own `main` and `frame` (recommended), or bundle the common start (window,
   GPU, renderer, ImGui, font, defaults) into one engine function?
4. **Random numbers:** is it fine that switching `effects.c` to `NvRandom` changes the random order of the stress effects, or
   keep its xorshift?
5. **Camera module:** a new module, `engine/camera.h` (recommended), or in `engine/scene.h`?

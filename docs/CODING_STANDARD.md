# Coding standard

- [한국어](#한국어)
- [English](#english)

## 한국어

nv는 Handmade Hero 철학을 따른다. 일을 하는 코드를 쓰고, 데이터는 단순하고 보이게 두고, 메모리는 직접
관리하고, 추상화는 반복이 요구할 때만 더한다.

### 원칙

- **사용 코드를 먼저 쓴다.** 코드는 두 번째로 필요해질 때 함수로 뽑는다. 그 전에는 뽑지 않는다.
  "나중을 위한" 추측성 계층, 인터페이스, 옵션은 두지 않는다.
- **단순한 데이터.** 필드가 공개된 구조체. 직접 호출로 되는 곳에 getter/setter, opaque 타입, vtable,
  콜백을 쓰지 않는다.
- **메모리를 직접 관리한다.** 메모리는 한 번 예약하고 아레나에서 나눠 준다. 할당은 수명별로 묶고 묶음
  단위로 한 번에 버린다.
- **기계가 하는 일을 안다.** 영리한 구조보다 평평한 배열과 곧은 루프를 쓴다. 고정 용량은 괜찮다. 용량을
  넘는 것은 assert로 멈추는 버그다.

### 언어와 파일

- C17 규칙은 우리가 쓰는 코드에만 적용된다. 외부 라이브러리는 C나 C++일 수 있고(Dear ImGui와
  ozz-animation은 C++), 경고를 끈 채 있는 그대로 컴파일한다.
- 우리 코드는 C17이고 `-Wall -Wextra`에서 경고 없이 컴파일된다. 유일한 예외는 C API가 없는 C++
  라이브러리의 래퍼다. 그 파일은 C++이고, C 헤더를 통해 `extern "C"` 함수만 내보내며, 가능한 한 얇게
  유지한다(현재는 ozz-animation 래퍼).
- 엔진 모듈은 헤더 하나와 소스 하나이고, 둘 다 `engine/`에 나란히 둔다(`engine/scene.h`, `engine/scene.c`). include 경로는
  저장소 루트이므로 `#include <engine/scene.h>`로 포함한다. 엔진 안에서만 쓰는 헤더는 맨 위 주석에 그렇게 적고(지금은
  `engine/vfx_cpu.h`), 앱은 포함하지 않는다.
- 모든 파일이 `engine/base.h`를 (직접 또는 다른 엔진 헤더를 통해) 포함한다.

### 이름 짓기

| 종류 | 스타일 | 예 |
|---|---|---|
| 공개 함수 | `nv_module_verb` | `nv_gpu_begin_frame` |
| 공개 타입 | `NvPascalCase` | `NvGpu`, `NvArena` |
| 공개 매크로 / 상수 | `NV_UPPER_CASE` | `NV_ASSERT`, `NV_PUSH_ARRAY` |
| 파일 내부 함수 / 타입 | `snake_case` / `PascalCase`, 접두사 없음 | `configure_surface`, `AdapterRequest` |
| 변수와 필드 | `snake_case`, 줄이지 않은 단어 | `surface_format`, `new_width` |

예외 하나: UI 문자열을 감싸는 `T("text")`와 `TL("label")`(`engine/strings.h`)은 모든 UI 텍스트에 붙으므로 짧은 이름의 매크로다.

위 표는 이름의 모양을 정한다. 이름이 말해야 하는 것은 아래와 같다.

- **함수 이름은 무엇을 돌려주는지, 무엇을 하는지 말한다.** 값을 돌려주는 함수는 그 값으로 짓고(`battle_cell_center`,
  `nv_mat4_identity`), 무언가를 바꾸는 함수는 동사로 짓는다(`configure_surface`). 헷갈릴 수 있는 단위나 공간(CSS 픽셀과 프레임버퍼
  픽셀, 초와 틱, 로컬과 월드, 라디안과 도)은 이름에 쓰고, 변환이면 무엇에서 무엇으로인지 쓴다(`framebuffer_rect_from_css`,
  `seconds_to_ticks`). 길어도 된다: 본문을 열어 봐야 뜻을 아는 짧은 이름보다, 호출하는 곳에서 뜻이 읽히는 긴 이름이 낫다.
- **변수와 필드는 같은 양이 두 단위로 섞일 때 단위를 이름에 쓴다.** 필드와 매개변수의 단위는 선언의 주석으로 충분하다
  (`f32 cooldown; // seconds`). 다만 같은 양을 다른 단위로 가진 이름이 함께 쓰이면 이름에 단위를 쓴다(`WeaponDef.cooldown`은 초,
  `Unit.cooldown_ticks`는 틱). 지역 변수는 한 함수 안에서 같은 양을 두 단위로 다룰 때 단위를 쓴다(`width_css`와 `gpu.width`); 하나뿐이면
  쓰지 않는다.

### 타입과 키워드

- `engine/base.h`의 별칭을 쓴다: `u8`–`u64`, `s8`–`s64`, `f32`, `f64`, `b32`(true/false), `umm`(메모리 크기와
  메모리 인덱스). `int`나 `bool`은 외부 API가 요구하는 곳(Emscripten 콜백 시그니처, ImGui의 `bool*`
  매개변수)에서만 쓴다.
- `static`은 의도에 따라 다르게 쓴다:
  - 파일 내부 함수에는 `internal`,
  - 파일 범위 변수에는 `global`,
  - 함수 안의 static 변수에는 `local_persist`.

### 메모리

- 엔진은 `malloc`/`free`를 부르지 않는다. 메모리는 시작할 때 한 번 예약한 블록 위의 `NvArena`에서
  `NV_PUSH_STRUCT` / `NV_PUSH_ARRAY`로 얻는다.
- 아레나는 리셋할 뿐 조각 단위로 해제하지 않는다. 프레임마다 쓰는 임시 데이터는 매 프레임 리셋되는 자체
  아레나에 둔다.
- 배열은 미리 정한 고정 용량을 가진다. 늘어나는 것은 기본이 아니다.

### 초기화

- **0이 초기화다.** 모든 구조체를 `{0}`이 유효한 상태가 되도록 설계하고, init 함수보다 그것을 선호한다.
  아레나 push는 0으로 채운 메모리를 돌려준다.
- 핸들과 인덱스는 0을 "없음"으로 예약하므로, 0으로 채운 핸들은 null 핸들이다.

### 오류

- 프로그래머 실수(잘못된 핸들, 용량 초과, 깨진 불변식)는 `NV_ASSERT`에 걸려 멈춘다. 오류 코드로 바꾸지
  않는다. `NV_INVALID_CODE_PATH`는 실행되면 안 되는 분기를 표시한다.
- 실행 중에 실제로 일어나는 실패(브라우저에 WebGPU가 없음, 파일이 없음, 저장할 내용이 버퍼에 맞지 않음)는 그것에
  대해 무언가 할 수 있는 호출자가 처리한다. 실패할 수 있는 함수는 실패를 결과와 따로 알린다.
  - 성공 여부 말고 돌려줄 결과가 없으면 `b32`를 돌려준다.
  - 결과가 있으면 **결과 구조체**를 값으로 돌려준다. 첫 필드가 `b32 ok`이고 결과가 그 뒤에 온다. 실패하면 `ok`도
    결과도 0이다. 호출자가 실패 이유를 알아야 하면 그 이유를 필드로 둔다(`NvChunkFile.status`, `SaveLoad.error`).
    결과를 포인터 매개변수로 내보내지 않고, 실패를 결과의 특별한 값(NULL, 0, -1)으로 알리지 않는다.

    ```c
    typedef struct NvFileData {
        b32 ok;
        u8* bytes;
        umm size;
    } NvFileData;
    NvFileData nv_file_read(NvArena* arena, const char* path);

    NvFileData units = nv_file_read(arena, "/data/units.txt");
    if (!units.ok)
        ...
    ```
  - 실패는 아니어도 입력에 따라 결과가 없을 수 있는 함수도 같다: 이미지 밖을 누른 탭(`NvTapRay`), 맞지 않는
    검색어, 마지막 줄 다음.
- 다음은 이 규칙에 들지 않는다.
  - 찾는 것이 없을 수 있는 조회는 결과 하나를 돌려주고, 없으면 그 타입의 "없음"을 돌려준다: 핸들은 0, 포인터는
    NULL, 배열 안의 위치는 -1(`nv_anim_find_joint`).
  - 함수가 다루는 대상은 결과가 아니다: 함수가 설정하거나 바꾸는 객체(`nv_gpu_create`의 `gpu`,
    `defs_read_units`의 `defs`), 호출 사이에 이어지는 커서(`nv_chunk_next`의 `child`), 값이 있을 때만 덮어쓰는
    자리(`nv_chunk_read_u32s`의 `out`), 호출자가 크기와 함께 넘기는 채울 버퍼(`char* out, umm capacity`).
  - JavaScript 함수(`EM_JS`)는 숫자만 주고받으므로 포인터 매개변수를 쓴다. 그것을 감싸는 C 함수가 규칙을 따른다.

### 의존성

- 플랫폼이 요구하는 것: Emscripten과 그 WebGPU 포트(`emdawnwebgpu`).
- 디버그와 도구 UI에는 cimgui(C API)를 통한 Dear ImGui: 즉시 모드 UI 라이브러리를 만드는 것은 이
  프로젝트의 목적이 아니다. 플랫폼과 렌더러 백엔드는 우리가 C로 작성한다(`engine/imgui.c`).
- 뷰포트의 트랜스폼 기즈모에는 cimguizmo(C API)를 통한 ImGuizmo: ImGui의 draw list로 그리므로 렌더러
  작업이 필요 없고, C API 덕분에 `anim.cpp`가 우리의 유일한 C++ 파일로 남는다. 두 라이브러리가 ImGui
  컨텍스트 하나를 공유하도록 cimgui 라이브러리 안에 함께 빌드한다.
- 모델과 애니메이션 로딩에는 단일 헤더 C glTF 파서인 cgltf: glTF가 에셋 형식이고, 그것을(JSON 포함)
  파싱하는 것은 이 프로젝트의 목적이 아니다.
- 실행 중 스켈레탈 애니메이션(샘플링, 블렌딩, 스키닝 행렬)에는 ozz-animation: 우리가 다시 만들 것을
  다루는 성숙한 데이터 지향 라이브러리다. API가 C++이므로 엔진은 우리 C 래퍼를 통해서만 사용한다.
- UI 글꼴에는 Pretendard Regular(SIL OFL 1.1, `assets/fonts/`): 영어와 한국어를 한 파일로 그린다. 라틴은 Inter를 바탕으로
  해서 영어가 화면용 산세리프로 보이고, 한글 11,172 음절이 모두 있어 서브셋 도구가 필요 없다. 대가는 크기(약 2.6 MB)다.
- 그 밖의 것은 (단일 헤더 라이브러리라도) 이유를 글로 쓰고 먼저 합의해야 한다. 수학, 컨테이너, 문자열은
  여기서 작성한다.

### 주석

- 주석은 왜를 설명하고, 코드는 무엇을 말한다. `//` 주석을 쓴다.
- 독자가 놓치면 안 되는 것에는 태그를 단다: `// TODO:`(알려진 빠진 작업), `// NOTE:`(명백하지 않은
  사실), `// IMPORTANT:`(무시하면 망가짐).

## English

nv follows the Handmade Hero philosophy: write the code that does the work, keep data plain and
visible, own your memory, and add abstraction only when repetition asks for it.

### Principles

- **Write the usage code first.** Pull code out into a function the second time it is needed,
  not before. No speculative layers, interfaces or options "for later".
- **Plain data.** Structs with public fields. No getters/setters, opaque types, vtables or
  callbacks where a direct call works.
- **Own the memory.** Reserve memory once, then hand it out from arenas. Group allocations by
  lifetime and drop each group at once.
- **Know what the machine does.** Prefer flat arrays and straight loops over clever structures.
  Fixed capacities are fine; exceeding one is a bug that asserts.

### Language and files

- The C17 rule covers only the code we write. External libraries may be written in C or C++
  (Dear ImGui and ozz-animation are C++); they are compiled as they come, with their warnings off.
- Our code is C17 and compiles warning-free with `-Wall -Wextra`. The one exception is the wrapper
  around a C++ library that has no C API: that file is C++, exposes only `extern "C"` functions
  through a C header, and stays as thin as possible (currently the ozz-animation wrapper).
- An engine module is one header and one source, side by side in `engine/` (`engine/scene.h`, `engine/scene.c`). The
  include path is the repository root, so they are included as `#include <engine/scene.h>`. A header only the engine uses
  says so in its top comment (now `engine/vfx_cpu.h`), and the app does not include it.
- `engine/base.h` is included (directly or through another engine header) by every file.

### Naming

| Kind | Style | Example |
|---|---|---|
| Public function | `nv_module_verb` | `nv_gpu_begin_frame` |
| Public type | `NvPascalCase` | `NvGpu`, `NvArena` |
| Public macro / constant | `NV_UPPER_CASE` | `NV_ASSERT`, `NV_PUSH_ARRAY` |
| File-local function / type | `snake_case` / `PascalCase`, no prefix | `configure_surface`, `AdapterRequest` |
| Variables and fields | `snake_case`, full words | `surface_format`, `new_width` |

One exception: `T("text")` and `TL("label")` (`engine/strings.h`), which wrap UI strings, are macros with short names, since they
go around every UI text.

The table sets the shape of names. What a name has to say:

- **A function's name says what it gives or does.** A function that returns a value is named for that value (`battle_cell_center`,
  `nv_mat4_identity`); one that changes something takes a verb (`configure_surface`). A unit or space that could be mistaken (CSS
  pixels or framebuffer pixels, seconds or ticks, local or world, radians or degrees) goes in the name, and a conversion names both
  sides (`framebuffer_rect_from_css`, `seconds_to_ticks`). Longer is fine: a name whose meaning reads at the call beats a short one
  the reader has to open the body to understand.
- **A variable or field names its unit when the same quantity appears in two units.** A comment at the declaration is enough for a
  field's or a parameter's unit (`f32 cooldown; // seconds`). But when another name holds the same quantity in a different unit, the
  unit goes in the name (`WeaponDef.cooldown` in seconds, `Unit.cooldown_ticks` in ticks). A local variable names its unit when one
  function handles the same quantity in two units (`width_css` beside `gpu.width`); with only one, it does not.

### Types and keywords

- Use the aliases from `engine/base.h`: `u8`–`u64`, `s8`–`s64`, `f32`, `f64`, `b32` (true/false),
  `umm` (memory sizes and indices into memory). Use `int` or `bool` only where an external API
  demands it (Emscripten callback signatures, ImGui's `bool*` parameters).
- `static` is spelled by intent:
  - `internal` for file-local functions,
  - `global` for file-scope variables,
  - `local_persist` for static variables inside a function.

### Memory

- The engine does not call `malloc`/`free`. Memory comes from an `NvArena` over a block reserved
  once at startup, via `NV_PUSH_STRUCT` / `NV_PUSH_ARRAY`.
- Arenas are reset, never freed piece by piece. Per-frame scratch data lives in its own arena that
  is reset every frame.
- Arrays have fixed capacities chosen up front. Growth is not the default.

### Initialization

- **Zero is initialization.** Design every struct so that `{0}` is a valid state, and prefer that
  over init functions. Arena pushes return zeroed memory.
- Handles and indices reserve 0 for "none", so a zeroed handle is a null handle.

### Errors

- Programmer mistakes (bad handle, capacity exceeded, broken invariant) hit `NV_ASSERT` and stop.
  They are not turned into error codes. `NV_INVALID_CODE_PATH` marks branches that must not run.
- Failures that really happen at runtime (no WebGPU in the browser, a missing file, a save that does not fit its
  buffer) are handled by the caller that can do something about them. A function that can fail reports the failure apart
  from its results.
  - With nothing to give back but whether it worked, it returns `b32`.
  - With results, it returns a **result struct** by value: its first field is `b32 ok`, and the results follow. On
    failure `ok` and the results are all 0. When the caller needs to know why it failed, the reason is a field too
    (`NvChunkFile.status`, `SaveLoad.error`). Results never go out through pointer parameters, and failure is never a
    special value of a result (NULL, 0, -1).

    ```c
    typedef struct NvFileData {
        b32 ok;
        u8* bytes;
        umm size;
    } NvFileData;
    NvFileData nv_file_read(NvArena* arena, const char* path);

    NvFileData units = nv_file_read(arena, "/data/units.txt");
    if (!units.ok)
        ...
    ```
  - The same holds for a function that may have no result for its input without anything failing: a tap outside the
    image (`NvTapRay`), a search query that does not match, the line after the last.
- These are outside the rule:
  - A lookup that may find nothing returns its one result, or that type's "none" when there is nothing: 0 for a handle,
    NULL for a pointer, -1 for a position in an array (`nv_anim_find_joint`).
  - What a function works on is not a result: the object it sets up or changes (`gpu` of `nv_gpu_create`, `defs` of
    `defs_read_units`), a cursor carried from call to call (`child` of `nv_chunk_next`), a place it overwrites only when
    there is a value (`out` of `nv_chunk_read_u32s`), and a buffer the caller hands in to be filled, with its size
    (`char* out, umm capacity`).
  - JavaScript functions (`EM_JS`) pass only numbers, so they use pointer parameters; the C function around one follows
    the rule.

### Dependencies

- What the platform requires: Emscripten and its WebGPU port (`emdawnwebgpu`).
- Dear ImGui, through cimgui (its C API), for debug and tool UI: writing an immediate-mode UI
  library is not the point of this project. Its platform and renderer backends are ours
  (`engine/imgui.c`), in C.
- ImGuizmo, through cimguizmo (its C API), for the transform gizmo in the viewport: it draws with
  ImGui's draw lists, so it needs no renderer work, and its C API keeps `anim.cpp` our only C++
  file. It is built into the cimgui library so both share one ImGui context.
- cgltf, a single-header C glTF parser, for loading models and animations: glTF is the asset
  format, and parsing it (JSON included) is not the point of this project.
- ozz-animation for skeletal animation at runtime (sampling, blending, skinning matrices): a
  mature, data-oriented library that covers what we would otherwise rebuild. Its API is C++, so
  the engine talks to it only through our own C wrapper.
- Pretendard Regular (SIL OFL 1.1, `assets/fonts/`) for the UI font: one file draws English and Korean. Its Latin is
  based on Inter, so English reads as a screen sans, and it has all 11,172 Hangul syllables, so no subsetting tool is
  needed. The cost is its size (about 2.6 MB).
- Anything else (even a single-header library) needs a written reason and agreement first. Math,
  containers and strings are written here.

### Comments

- Comments explain why; the code says what. Use `//` comments.
- Tags for things a reader must not miss: `// TODO:` (known missing work), `// NOTE:` (a
  non-obvious fact), `// IMPORTANT:` (breaks things if ignored).

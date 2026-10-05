# Console spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-09-29). 이 스펙의 변경은 먼저 합의한다.

### 목표

엔진과 페이지가 보고하는 것을 샌드박스 안에서 본다: WebGPU 검증 오류, 불러오지 못한 glTF, 쓰지 못한 저장,
ozz-animation의 경고. 지금 이것들은 `fprintf(stderr, ...)`나 `console.warn`으로 가므로 브라우저 개발자
도구를 열어야만 보이는데, 폰에는 그것이 없다. 에디터 패널의 **Console** 탭이 레벨, 필터, 복사와 함께
이것들을 나열하고, 다른 탭을 보는 동안 경고나 오류가 오면 뷰포트의 빌드 라벨에 붙은 배지가 알려 준다.

범위 밖: 명령 입력 (REPL), 파일이나 서버로의 로깅, 크래시 이후의 메시지 (페이지의 크래시 화면이 이미
스택을 보여 준다).

### 접근법

| 접근법 | 무엇인가 | 맞음 | 장단점 |
|---|---|---|---|
| **자체 로그 링과 Console 탭** (추천) | `engine/log.h`: 정적 메모리의 고정 메시지 링 하나, `nv_log`가 씀; `sandbox/console.c`가 Dear ImGui 자체 `ExampleAppLog` 데모의 패턴을 따라 ImGui로 그린다 | 순수 C17, 고정 용량, 할당 없음; 약 300줄. ImGui 데모가 UI 부품을 보여 준다 (`ImGuiListClipper`, `ImGuiTextFilter`, 맨 아래 붙는 스크롤) | 우리가 쓰고 테스트해야 한다 |
| Dear ImGui의 `ExampleAppLog` / `ExampleAppConsole` (`imgui_demo.cpp`, MIT) | 라이브러리가 아닌 데모 코드 | UI 참고 자료로 따른다 | C++이고, `ImGuiTextBuffer`가 힙에서 무한히 자란다. 그대로는 못 쓴다 |
| rxi/log.c (C99, MIT) | 아주 작은 로깅 라이브러리: 레벨, `stderr` 출력, 콜백 | C, 작음 | 이력을 남기지 않는데 그것이 여기서 핵심이다; 어차피 우리가 쓸 링 위에 콜백 계층을 더할 뿐. 의존성으로 둘 가치가 없다 |
| `ringbuffer_sink`가 있는 spdlog (C++, MIT) | 완전한 로깅 라이브러리 | 메시지 링이 있다 | 엔진 핵심에 C++, 힙 할당, 작은 일에 큰 의존성 |
| GitHub의 ImGui 콘솔 위젯 (예: `imgui-console`, C++) | 명령과 이력이 있는 콘솔 창 | 비슷해 보인다 | C++, 힙, 원하지 않는 명령, 패널 탭 대신 창 |

추천: 직접 작성한다. 서드파티 라이브러리 없음.

### 결정

| 주제 | 결정 |
|---|---|
| 레벨 | Info, Warning, Error |
| 출처 | 메시지마다 붙는 짧은 태그: `nv` (엔진), `wgpu` (WebGPU 콜백), `sandbox`, `stdout`, `stderr`, `js` (페이지 오류). 자체 열에 보이고 텍스트 필터로 거를 수 있다 |
| 저장 공간 | `engine/log.c`의 엔진 전역 링 하나, 정적 메모리: 최대 1024개 메시지, 텍스트는 128 KB 텍스트 링을 나눠 쓴다. 어느 쪽이든 가득 차면 가장 오래된 메시지를 버린다. 4 KB보다 긴 메시지는 잘리고 `...`로 끝난다 |
| 전역 하나인 이유 | 메시지는 샌드박스가 손에 없는 곳(WebGPU 콜백, glTF 로더, `anim.cpp`)에서 온다; 페이지 하나에 로그 하나다 |
| 반복 | 가장 최근 메시지와 같은 메시지 (같은 레벨, 출처, 텍스트)는 새로 추가되지 않고 그 메시지의 반복 횟수와 시간을 올린다. 그래서 매 프레임 나는 오류는 한 행을 차지한다 ("x240") |
| 브라우저 콘솔 | `nv_log`는 레벨에 따라 `console.log` / `console.warn` / `console.error`에도 `[source] text`로 쓴다. 그래서 개발자 도구와 Playwright의 `page.on("console")`이 계속 모두를 본다. `stderr`를 거치지 않으므로 두 번 잡히지 않는다 |
| 기존 출력 | `engine/`과 `sandbox/`의 모든 `fprintf(stderr, ...)`는 `nv_log`가 된다. 우리 `EM_JS` 코드의 `console.warn` / `console.error`는 `Module.nvLog`가 된다 (아래) |
| 우리가 쓰지 않는 출력 | 페이지의 `Module.print`와 `Module.printErr` (Emscripten의 `stdout`과 `stderr`: ozz-animation의 로그, Emscripten의 경고)는 브라우저 콘솔에 계속 쓰고, Info (`stdout`)나 Error (`stderr`) 메시지도 추가한다. 페이지의 `error`와 `unhandledrejection` 핸들러 (`js`)도 크래시 화면 전에 똑같이 한다 |
| JS 메시지 | 페이지는 `Module.nvLog(level, source, text)`를 정의한다. 이것은 콘솔에 쓰고 메시지를 시간과 함께 큐에 넣는다. 각 프레임 시작에 불리는 `nv_log_pump()`가 큐를 링으로 옮긴다. 큐는 512개를 담는다; 페이지가 한 프레임보다 앞서가면 가장 오래된 것을 버리고, 다음 pump가 몇 개인지 알리는 경고를 추가한다. 이 훅들에서는 아무것도 WebAssembly를 부르지 않는다: `printErr`는 WebAssembly 호출 안에서 돌고, 재진입 호출은 Asyncify 대기에 걸릴 수 있다 |
| 시간 | 페이지 시작 이후 초 (`performance.now`, `nv_time_seconds`가 쓰는 시계), `12.345`로 표시 |
| 저장 | 안 함. 로그는 저장에 없고, 새로고침하면 빈 상태로 시작한다. Console 탭의 설정 (레벨 필터, 자동 스크롤)도 저장하지 않는다: 텍스트 필터처럼 보기 상태다 |
| Undo | undo 불가이며, undo가 비교하는 어떤 것도 건드리지 않는다: 로그 지우기는 한 단계가 아니다 |
| 탭 | 패널 탭 바에서 View 다음의 **Console** 탭 (데스크톱 UI, `layout.md`에서는 하단 도크의 첫 탭; 배지를 탭하면 접힌 도크가 열린다). 보이지 않는 동안 라벨은 마지막으로 보인 뒤 도착한 경고와 오류 수를 반복 포함해 센다: `Console (3)`, 하나라도 오류면 빨강, 경고만이면 노랑 (폰처럼 좁은 패널에서는 색만: 숫자 때문에 탭 바가 스크롤되고, 배지가 센다). Info는 세지 않는다. 라벨의 ImGui id는 고정 (`###console`)이라 탭이 제자리를 지킨다 |
| 배지 | 뷰포트 왼쪽 위 빌드 라벨 (`ui.c`의 `ui_draw_build_label`)의 텍스트 뒤에 같은 수: 점과 숫자, `Release build 1a2b3c · ● 3`, 하나라도 오류면 빨강, 경고만이면 노랑. 수가 없으면 배지도 없다. 라벨처럼 foreground draw list에 그리므로 ImGui 아이템이 아니다. 빌드 라벨은 이미 "에디터 UI는 패널 안에" 규칙의 예외이고, 배지는 새 예외를 더하지 않는다 |
| 배지 탭 | 배지가 보이는 동안 빌드 라벨 상자에 떨어진 탭이나 클릭은 picking 대신 Console 탭을 연다 (수가 지워진다). 뷰포트 입력은 ImGui를 건너뛰므로 (`NvImgui.view`), 샌드박스는 `pick`이 돌기 전에 `view.tap_x` / `tap_y` (CSS 픽셀, 라벨을 그리는 좌표)를 상자와 비교한다. 상자는 손가락이 맞출 수 있게 최소 32 × 32 CSS 픽셀 × `NvImgui.ui_scale`로 키운다. 그 위에서 시작한 드래그는 여전히 카메라를 공전시킨다 |
| 툴바 | Info, Warning, Error 체크박스, 각각 링이 담은 수와 함께 ("Error 2"; 좁은 패널은 툴바를 두 줄로 유지하려고 "Warn"에 수 없음); 텍스트 필터 (`ImGuiTextFilter`, 출처와 텍스트에 맞춤); **Clear**; **Copy** (보이는 메시지를 텍스트로, 브라우저까지 닿는 ImGui 클립보드로); **Auto-scroll** (좁은 패널에서는 "Auto"). 좁은 패널에서는 아이템이 다음 줄로 넘어간다 (`ui.c`의 `ui_same_line_if_fits`). 행이 링의 인덱스이므로 Clear는 목록을 그린 뒤에 실행한다 |
| 목록 | 상세 상자 위에서 탭을 채우는 자식 창. 메시지마다 한 줄짜리 행: 시간, 레벨 (색), 출처, 반복 횟수 (둘 이상일 때 `x3`), 첫 줄바꿈까지의 텍스트 (최대 512 바이트). 더 긴 텍스트는 `...`로 끝난다. 좁은 패널 (44글자 미만)은 시간을 빼고 상세에서 보여 준다. 행은 필터를 통과한 인덱스 (매 프레임 scratch 아레나에 만듦)에 대해 `ImGuiListClipper`로 그리므로, 1024개 메시지라도 보이는 행만큼만 든다 |
| 자동 스크롤 | 기본으로 켜짐. 목록이 맨 아래에 있는 동안 새 메시지를 따라간다; 위로 스크롤하면 다시 맨 아래로 올 때까지 따라가지 않는다 |
| 상세 | 행을 탭하면 선택된다; 목록 아래 상자가 그 전체 텍스트를 줄바꿈해서 읽기 전용 여러 줄 텍스트 필드에 보여 주므로, 폰에서도 선택하고 복사할 수 있다. 상자 위에는 레벨, 출처, 시간, 반복 횟수, 그리고 **Copy message** 버튼 (폰에는 필드를 복사할 키보드가 없다). 선택된 행을 다시 탭하면 선택이 풀린다. 선택이 없으면 상자를 숨긴다. 새로 고른 메시지는 상세가 보이도록 패널을 스크롤한다 (폰에서는 화면 아래에 있다); 좁은 패널은 3줄 상자와 최소 4개의 목록 행을, 넓은 패널은 5줄과 6행을 보여 준다. 선택된 메시지가 링에서 버려지면 선택이 지워진다 |
| 터치 | 세로 드래그는 패널처럼 목록을 스크롤한다. `nv_imgui_begin_panel`은 마우스가 올라간 패널만 스크롤하고 그 자식 창은 하지 않으므로, 스크롤 적용을 `nv_imgui_touch_scroll`로 옮겨 패널과 목록의 자식 창이 둘 다 부른다. 손가락이 목록을 스크롤한 프레임은 새 메시지를 따라 맨 아래로 가지 않는다. ImGui가 스크롤을 다음 `Begin`에서 적용하므로 따라가기가 그것을 덮어쓰기 때문이다 |
| 테스트 | 링을 위한 Node 테스트; Playwright용 Debug export (아래) |
| 서드파티 | 없음 |
| 시작 정보 | Info 메시지가 엔진이 무엇 위에서 도는지 말한다: **CPU** (논리 코어, 메모리, 플랫폼; 브라우저는 모델을 숨긴다), **브라우저** user agent, **GPU** (vendor, architecture, device, description, backend와 type, id; 브라우저가 일부를 비워 두면 "unknown"으로 표시)와 그 limits, **디스플레이** (화면, CSS 픽셀 단위 캔버스, device pixel ratio, 프레임버퍼), **스왑체인과 색 타깃** (크기, 캔버스와 렌더 뷰 형식, present mode; 씬과 UI는 스왑체인 뷰에 바로 그리며 오프스크린 색 버퍼는 없다), **깊이 타깃** (크기, 형식)과 **그림자 맵**, 그리고 에디터 패널과 함께 **씬 뷰포트**. 스왑체인, 깊이 타깃, 뷰포트 줄은 리사이즈로 바뀔 때마다 다시 나온다. `nv_gpu_create`, `configure_surface`, 렌더러, `frame`이 쓴다. Info는 배지에 세지 않는다 |

### 엔진 API

**`engine/log.h`** (새로 생김; `anim.cpp`의 `extern "C"` 블록 안에서 쓸 수 있음):

```c
typedef enum NvLogLevel {
    NV_LOG_INFO,
    NV_LOG_WARNING,
    NV_LOG_ERROR,
    NV_LOG_LEVEL_COUNT,
} NvLogLevel;

#define NV_LOG_MAX_MESSAGES     1024
#define NV_LOG_TEXT_SIZE        NV_KILOBYTES(128)
#define NV_LOG_MAX_MESSAGE_SIZE NV_KILOBYTES(4)
#define NV_LOG_SOURCE_SIZE      8

typedef struct NvLogMessage {
    NvLogLevel level;
    char source[NV_LOG_SOURCE_SIZE]; // "wgpu"
    u32 repeat;      // times it arrived in a row; 1 = once
    f64 time;        // seconds since the page started, of the latest repeat
    u32 text_offset; // into NvLog.text; a message's text never wraps around the ring's end
    u32 text_size;   // without a terminator
} NvLogMessage;

typedef struct NvLog {
    NvLogMessage messages[NV_LOG_MAX_MESSAGES]; // a ring
    u32 first;                                  // the oldest message's slot
    u32 count;
    u32 level_counts[NV_LOG_LEVEL_COUNT]; // messages held, by level
    u64 arrived[NV_LOG_LEVEL_COUNT];      // messages and repeats ever added, by level; never cleared
    u64 added; // messages ever added (not repeats): the i-th held is number added - count + i
    char text[NV_LOG_TEXT_SIZE];
    u32 text_end; // where the next message's text goes
} NvLog;

// The page's one log. Zero is an empty log, so it is valid before anything runs.
extern NvLog nv_log_ring;

// Adds a message and writes it to the browser console. `source` is cut to 7 characters.
void nv_log(NvLogLevel level, const char* source, const char* format, ...)
    __attribute__((format(printf, 3, 4)));

// Moves messages queued by the page (Module.nvLog) into the ring. Call once per frame.
void nv_log_pump(void);

// Drops every message held; `arrived` and `added` keep counting.
void nv_log_clear(void);

// The i-th message held, 0 = the oldest.
NvLogMessage* nv_log_message(u32 i);
```

메시지의 텍스트는 `nv_log_ring.text + message->text_offset`이고, 길이는 `text_size` 바이트다.

**`engine/imgui.h`**: `void nv_imgui_touch_scroll(NvImgui* imgui);`는 이번 프레임의 터치 스크롤을 현재 창에
마우스가 올라가 있으면 적용한다. `nv_imgui_begin_panel`은 그것을 직접 하는 대신 이 함수를 부른다.

### 샌드박스 변경

- `sandbox/console.c` (새로 생김): Console 탭 (툴바, 목록, 상세)과 그 상태, `Sandbox` 안의 `Console` 구조체: 레벨
  필터, 자동 스크롤, `ImGuiTextFilter`, 선택된 메시지 (링이 움직여도 유효한 번호로), 탭이 마지막으로 보인
  때의 `arrived` 수.
- `sandbox/ui.c`: 탭과 그 라벨; `build_label`이 배지를 그리고 라벨의 상자를 기록한다 (`Sandbox.build_label_box`,
  CSS 픽셀; 배지가 없는 동안은 0).
- `sandbox/main.c`: `frame` 시작에 `nv_log_pump()`; `pick`은 먼저 탭을 `build_label_box`와 비교하고, 맞으면
  Console 탭을 열고 (`open_inspector`처럼 `sandbox->open_console`) 선택을 바꾸지 않고 돌아간다.
- `main.c`의 `fprintf(stderr, ...)`는 `nv_log(NV_LOG_ERROR, "sandbox", ...)`가 된다. 저장 알림
  (`sandbox->save_notice`)은 View 탭의 제자리에 남고 경고로도 기록된다.
- `web/index.html.in`: `Module.nvLog`, 그 큐, 그리고 그것을 채우는 `print`, `printErr`, `error`,
  `unhandledrejection` 훅.

### 테스트

- **Node** (`tests/log_test.c`, ctest가 실행): 링이 1024개에서 돈다; 텍스트 링이 차면 긴 텍스트가 오래된
  메시지를 밀어낸다; 링 끝 전에 들어가지 않는 텍스트는 0에서 시작한다; 5 KB 메시지는 `...`와 함께 4 KB로
  잘린다; 반복은 접힌다; 레벨 수는 버림과 지움을 따른다; 0은 유효한 빈 로그다.
- **Playwright**, Release와 Debug, 데스크톱 마우스와 폰 터치 크기. Debug 빌드는
  `Module._sandbox_debug_log(level, n)` (`sandbox` 메시지 "test message n"을 추가), `_sandbox_debug_log_lines`,
  `_sandbox_debug_log_count`, `_sandbox_debug_log_repeat`, `_sandbox_debug_wgpu_error`, `_sandbox_debug_console_*` 함수들
  (안 본 수, 행, 스크롤, 선택, 각 아이템이 그려진 곳, 그래서 테스트가 클릭할 수 있음)을 export한다;
  페이지의 문자열은 `Module.nvLog`를 거친다. 스크립트는 Playwright로 손으로 돌린다 (저장소에는 아무것도
  없다); Release 빌드에는 export가 없으므로, Debug 실행에서 기록한 배치대로 클릭하고 클립보드로 확인한다.
  - 실제 WebGPU 오류 (잘못된 버퍼를 만드는 Debug export)가 `wgpu` Error로 보인다; 1초 동안 매 프레임 하나씩
    나도 반복 횟수가 붙은 한 행으로 남는다.
  - `Module.printErr("x")`와 `setTimeout` 안에서 던진 오류가 `stderr`와 `js` Error로 보인다.
  - 다른 탭을 보는 동안 탭 라벨과 빌드 라벨 배지가 세고, Console 탭을 열면 지워진다. Info 메시지는 배지를
    띄우지 않는다.
  - 배지를 클릭 (데스크톱)하거나 탭 (폰)하면 Console 탭이 열리고 선택은 유지된다; 배지 없는 라벨을 클릭하면
    전처럼 picking한다; 배지에서 시작한 드래그는 공전한다.
  - 레벨 체크박스와 텍스트 필터가 행을 숨긴다; Copy는 보이는 행을 클립보드에 넣는다.
  - 2000개 메시지는 최신 1024개를 남긴다; 자동 스크롤 중에는 목록이 맨 아래에 머물고, 위로 스크롤한 뒤에는
    그 자리에 머문다. UI 시간 (`shown_average.ui`)은 빈 로그 (약 0.3 ms)의 몇 배 안에 머문다.
  - 행을 탭하면 전체 텍스트가 보인다; 터치 드래그는 목록을 스크롤한다.
  - 저장 왕복 (`_sandbox_debug_save_round_trip`)과 undo 단계 수는 탭에서 무엇을 해도 바뀌지 않는다.

### 단계

1. **로그:** `engine/log.h`, `log.c`, Node 테스트, 그리고 기존의 모든 `fprintf(stderr, ...)`와 `console.*` 호출을
   그리로 옮김. 확인: 브라우저 콘솔이 전과 같은 메시지를 보여 준다.
2. **Console 탭:** 탭, 툴바, clipper가 있는 목록, 상세 상자, 탭 수, 빌드 라벨 배지와 그 탭,
   `nv_imgui_touch_scroll`, 페이지의 훅.
3. **경계 사례와 문서:** 폭주 (반복, 꽉 찬 링), 폰 배치, 꽉 찬 로그에서의 UI 비용; `AGENTS.md` (로그 모듈,
   Console 탭, 빌드 라벨 예외 안의 배지)와 README.

모든 단계는 헤드리스 Chromium에서 Release와 Debug로, 데스크톱 크기의 마우스와 폰 크기의 터치로 확인한다.

## English

Status: implemented (2026-09-29). Changes to this spec are agreed first.

### Goal

See what the engine and the page report, inside the sandbox: WebGPU validation errors, a glTF that
failed to load, a save that could not be written, a warning from ozz-animation. Today these go to
`fprintf(stderr, ...)` or `console.warn`, so they are only seen with the browser's developer tools
open, which phones do not have. A **Console** tab in the editor panel lists them, with levels,
filters and copy, and a badge on the viewport's build label says when a warning or error arrived
while another tab is shown.

Out of scope: typing commands (a REPL), logging to a file or a server, and messages after a crash
(the page's crash screen already shows the stack).

### Approaches

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **Our own log ring and Console tab** (recommended) | `engine/log.h`: one fixed ring of messages in static memory, written by `nv_log`; `sandbox/console.c` draws it with ImGui, following the pattern of Dear ImGui's own `ExampleAppLog` demo | Plain C17, fixed capacities, no allocation; about 300 lines. The ImGui demo shows the UI parts (`ImGuiListClipper`, `ImGuiTextFilter`, stick-to-bottom scrolling) | Ours to write and test |
| Dear ImGui's `ExampleAppLog` / `ExampleAppConsole` (`imgui_demo.cpp`, MIT) | Demo code, not a library | A reference for the UI, which we follow | C++, and its `ImGuiTextBuffer` grows on the heap without limit. Not usable as it is |
| rxi/log.c (C99, MIT) | A tiny logging library: levels, `stderr` output, callbacks | C, small | Keeps no history, which is the whole point here; would add a callback layer over a ring we still write. Not worth a dependency |
| spdlog with its `ringbuffer_sink` (C++, MIT) | A full logging library | Has a ring of messages | C++ in the engine's core, heap allocation, a large dependency for a small job |
| ImGui console widgets on GitHub (e.g. `imgui-console`, C++) | A console window with commands and history | Looks close | C++, heap, commands we do not want, and a window instead of a panel tab |

Recommendation: write it ourselves. No third-party library.

### Decisions

| Topic | Decision |
|---|---|
| Levels | Info, Warning, Error |
| Source | A short tag kept with each message: `nv` (engine), `wgpu` (WebGPU callbacks), `sandbox`, `stdout`, `stderr`, `js` (page errors). Shown in its own column and filterable by the text filter |
| Storage | One engine-wide ring in `engine/log.c`, in static memory: up to 1024 messages whose text shares a 128 KB text ring. When either is full, the oldest messages are dropped. A message longer than 4 KB is cut and ends in `...` |
| Why one global | Messages come from places with no sandbox at hand (WebGPU callbacks, the glTF loader, `anim.cpp`); there is one page and one log |
| Repeats | A message equal to the newest one (same level, source and text) raises that message's repeat count and time instead of adding a new one, so an error raised every frame takes one row ("x240") |
| Browser console | `nv_log` also writes to `console.log` / `console.warn` / `console.error` by level, as `[source] text`, so developer tools and Playwright's `page.on("console")` keep seeing everything. It does not go through `stderr`, so it is not captured twice |
| Existing output | Every `fprintf(stderr, ...)` in `engine/` and `sandbox/` becomes `nv_log`. `console.warn` / `console.error` in our `EM_JS` code become `Module.nvLog` (below) |
| Output we do not write | The page's `Module.print` and `Module.printErr` (Emscripten's `stdout` and `stderr`: ozz-animation's logs, Emscripten's warnings) keep writing to the browser console and also add an Info (`stdout`) or Error (`stderr`) message. So do the page's `error` and `unhandledrejection` handlers (`js`), before the crash screen |
| JS messages | The page defines `Module.nvLog(level, source, text)`, which writes to the console and queues the message with its time. `nv_log_pump()`, called at the start of each frame, moves the queue into the ring. The queue holds 512 messages; when the page outruns a frame, the oldest are dropped and the next pump adds a warning saying how many. Nothing calls into WebAssembly from those hooks: `printErr` runs inside a WebAssembly call, and a re-entrant call could land in an Asyncify wait |
| Time | Seconds since the page started (`performance.now`, the clock `nv_time_seconds` uses), shown as `12.345` |
| Saved | No. The log is not in the save, and a reload starts empty. The Console tab's settings (level filters, auto-scroll) are not saved either: they are view state, like the text filter |
| Undo | Not undoable, and touching nothing undo compares: clearing the log is not a step |
| Tab | A **Console** tab after View in the panel's tab bar (on the desktop UI, `layout.md`, the first tab of the bottom dock; a badge tap opens the dock if it is collapsed). While it is not shown, its label counts the warnings and errors that arrived since it was last shown, repeats included: `Console (3)`, red when one is an error, yellow for warnings only (on a narrow panel, a phone's, only the color: the number would make the tab bar scroll, and the badge counts). Info does not count. The label's ImGui id stays fixed (`###console`), so the tab keeps its place |
| Badge | The same count on the build label in the viewport's top-left corner (`ui.c`'s `ui_draw_build_label`), after its text: a dot and the number, `Release build 1a2b3c · ● 3`, red when one is an error, yellow for warnings only. No count, no badge. It is drawn on the foreground draw list like the label, so it is not an ImGui item. The build label is already an exception to "editor UI stays in the panel"; the badge adds no new one |
| Badge tap | A tap or click that lands on the build label's box while the badge shows opens the Console tab (which clears the count) instead of picking. Input in the viewport skips ImGui (`NvImgui.view`), so the sandbox tests `view.tap_x` / `tap_y` (CSS pixels, the coordinates the label is drawn in) against the box before `pick` runs. The box is grown to at least 32 × 32 CSS pixels times `NvImgui.ui_scale`, so a finger can hit it. Drags that start on it still orbit the camera |
| Toolbar | Info, Warning and Error checkboxes, each with the count the ring holds ("Error 2"; a narrow panel gets "Warn" and no counts, to keep the toolbar to two lines); a text filter (`ImGuiTextFilter`, matching the source and the text); **Clear**; **Copy** (the shown messages as text, through ImGui's clipboard, which reaches the browser's); **Auto-scroll** ("Auto" on a narrow panel). Items wrap to the next line on a narrow panel (`ui_same_line_if_fits` in `ui.c`). Clear runs after the list is drawn, since the rows are indices into the ring |
| List | A child window filling the tab above the detail box. One row per message, one line each: time, level (colored), source, the repeat count (`x3`, when more than one), and the text up to its first line break (at most 512 bytes). Longer text ends in `...`. A narrow panel (under 44 characters wide) leaves out the time; the detail shows it. Rows are drawn with `ImGuiListClipper` over the indices that pass the filters (built in the scratch arena each frame), so 1024 messages cost only the visible rows |
| Auto-scroll | On by default. The list follows new messages while it is scrolled to the bottom; scrolling up stops following until it is back at the bottom |
| Detail | Tapping a row selects it; the box under the list shows its full text, wrapped, in a read-only multi-line text field, so it can be selected and copied on phones too. Above the box, its level, source, time and repeat count, and a **Copy message** button (a phone has no keyboard to copy the field with). Tapping the selected row again deselects it. Nothing selected: the box is hidden. A newly picked message scrolls the panel so its detail is in view (on a phone it is below the fold); a narrow panel shows a 3-line box and at least 4 list rows, a wide one 5 and 6. A selected message that is dropped from the ring clears the selection |
| Touch | A vertical drag scrolls the list like the panel. `nv_imgui_begin_panel` scrolls only a hovered panel, not its child windows, so the scroll it applies moves into `nv_imgui_touch_scroll`, which the panel and the list's child window both call. A frame in which a finger scrolled the list does not follow new messages to the bottom, since ImGui applies the scroll at the next `Begin` and the follow would overwrite it |
| Tests | A Node test for the ring; Debug exports for Playwright (below) |
| Third-party | None |
| Startup info | Info messages say what the engine runs on: **CPU** (logical cores, memory, platform; the browser hides the model), the **browser** user agent, the **GPU** (vendor, architecture, device, description, backend and type, ids; the browser leaves some empty, shown as "unknown") and its limits, the **display** (screen, canvas in CSS pixels, device pixel ratio, framebuffer), the **swapchain and color target** (size, canvas and render view formats, present mode; the scene and the UI draw straight into the swapchain view, there is no offscreen color buffer), the **depth target** (size, format) and the **shadow map**, and the **scene viewport** with the editor panel. The swapchain, depth target and viewport lines come again whenever a resize changes them. Written by `nv_gpu_create`, `configure_surface`, the renderer and `frame`. Info does not count toward the badge |

### Engine API

**`engine/log.h`** (new; usable from `anim.cpp` inside its `extern "C"` block):

```c
typedef enum NvLogLevel {
    NV_LOG_INFO,
    NV_LOG_WARNING,
    NV_LOG_ERROR,
    NV_LOG_LEVEL_COUNT,
} NvLogLevel;

#define NV_LOG_MAX_MESSAGES     1024
#define NV_LOG_TEXT_SIZE        NV_KILOBYTES(128)
#define NV_LOG_MAX_MESSAGE_SIZE NV_KILOBYTES(4)
#define NV_LOG_SOURCE_SIZE      8

typedef struct NvLogMessage {
    NvLogLevel level;
    char source[NV_LOG_SOURCE_SIZE]; // "wgpu"
    u32 repeat;      // times it arrived in a row; 1 = once
    f64 time;        // seconds since the page started, of the latest repeat
    u32 text_offset; // into NvLog.text; a message's text never wraps around the ring's end
    u32 text_size;   // without a terminator
} NvLogMessage;

typedef struct NvLog {
    NvLogMessage messages[NV_LOG_MAX_MESSAGES]; // a ring
    u32 first;                                  // the oldest message's slot
    u32 count;
    u32 level_counts[NV_LOG_LEVEL_COUNT]; // messages held, by level
    u64 arrived[NV_LOG_LEVEL_COUNT];      // messages and repeats ever added, by level; never cleared
    u64 added; // messages ever added (not repeats): the i-th held is number added - count + i
    char text[NV_LOG_TEXT_SIZE];
    u32 text_end; // where the next message's text goes
} NvLog;

// The page's one log. Zero is an empty log, so it is valid before anything runs.
extern NvLog nv_log_ring;

// Adds a message and writes it to the browser console. `source` is cut to 7 characters.
void nv_log(NvLogLevel level, const char* source, const char* format, ...)
    __attribute__((format(printf, 3, 4)));

// Moves messages queued by the page (Module.nvLog) into the ring. Call once per frame.
void nv_log_pump(void);

// Drops every message held; `arrived` and `added` keep counting.
void nv_log_clear(void);

// The i-th message held, 0 = the oldest.
NvLogMessage* nv_log_message(u32 i);
```

A message's text is `nv_log_ring.text + message->text_offset`, `text_size` bytes long.

**`engine/imgui.h`**: `void nv_imgui_touch_scroll(NvImgui* imgui);` applies this frame's touch scroll to
the current window if it is hovered. `nv_imgui_begin_panel` calls it instead of doing it inline.

### Sandbox changes

- `sandbox/console.c` (new): the Console tab (toolbar, list, detail) and its state, a `Console` struct
  in `Sandbox`: level filters, auto-scroll, the `ImGuiTextFilter`, the selected message (by its
  number, which stays valid as the ring moves), and the `arrived` counts when the tab was
  last shown.
- `sandbox/ui.c`: the tab and its label; `build_label` draws the badge and keeps the label's box
  (`Sandbox.build_label_box`, CSS pixels; zero while no badge shows).
- `sandbox/main.c`: `nv_log_pump()` at the start of `frame`; `pick` first checks the tap against
  `build_label_box` and, on a hit, opens the Console tab (`sandbox->open_console`, like
  `open_inspector`) and returns without changing the selection.
- `fprintf(stderr, ...)` in `main.c` becomes `nv_log(NV_LOG_ERROR, "sandbox", ...)`. The save's
  notices (`sandbox->save_notice`) stay where they are in the View tab and are also logged as warnings.
- `web/index.html.in`: `Module.nvLog`, its queue, and the `print`, `printErr`, `error` and
  `unhandledrejection` hooks feeding it.

### Tests

- **Node** (`tests/log_test.c`, run by ctest): the ring wraps at 1024 messages; long texts drop
  older messages when the text ring fills; a text that does not fit before the ring's end starts at
  0; a 5 KB message is cut to 4 KB with `...`; repeats collapse; level counts follow drops and
  clears; zero is a valid empty log.
- **Playwright**, Release and Debug, desktop mouse and phone touch sizes. Debug builds export
  `Module._sandbox_debug_log(level, n)` (adds the `sandbox` message "test message n"), `_sandbox_debug_log_lines`,
  `_sandbox_debug_log_count`, `_sandbox_debug_log_repeat`, `_sandbox_debug_wgpu_error` and the
  `_sandbox_debug_console_*` functions (unseen count, rows, scroll, selection, and where each item was
  drawn, so a test can click it); strings from the page go through `Module.nvLog`. The scripts are
  run by hand with Playwright (nothing of them is in the repo); the Release build has no exports, so
  its run clicks by the layout recorded from the Debug run and checks through the clipboard.
  - A real WebGPU error (a Debug export that makes an invalid buffer) shows as a `wgpu` Error; one
    per frame for a second stays one row with a repeat count.
  - `Module.printErr("x")` and a thrown error in a `setTimeout` show as `stderr` and `js` Errors.
  - The tab label and the build-label badge count while another tab is shown, and clear when the
    Console tab is opened. Info messages show no badge.
  - A click (desktop) and a tap (phone) on the badge open the Console tab and keep the selection; a
    click on the label without a badge picks as before; a drag starting on the badge orbits.
  - Level checkboxes and the text filter hide rows; Copy puts the shown rows on the clipboard.
  - 2000 messages keep the newest 1024; the list stays at the bottom while auto-scrolling, and stays
    put after scrolling up. The UI time (`shown_average.ui`) stays within a few times of an empty log's (about 0.3 ms).
  - Tapping a row shows its full text; a touch drag scrolls the list.
  - The save round trip (`_sandbox_debug_save_round_trip`) and the undo step count are unchanged by
    anything done in the tab.

### Phases

1. **Log:** `engine/log.h`, `log.c`, the Node test, and every existing `fprintf(stderr, ...)` and
   `console.*` call moved to it. Checked: the browser console shows the same messages as before.
2. **Console tab:** the tab, toolbar, list with the clipper, detail box, tab count, the build-label
   badge and its tap, `nv_imgui_touch_scroll`, and the page's hooks.
3. **Edge cases and docs:** floods (repeats, a full ring), the phone layout, the UI cost with a full
   log; `AGENTS.md` (the log module, the Console tab, and
   the badge in the build-label exception) and README.

Every phase is checked in Release and Debug in headless Chromium, with the mouse at desktop size
and with touch at phone size.

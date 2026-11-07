# Linux Wayland 빌드 스펙 (입력, 창, 샌드박스 실행)

상태: 초안 (2026-11-07). 범위(포인터, 키보드, 창 크기와 정수 배율)와 의존성(libxkbcommon)은 합의했다(2026-11-07). 이미지 디코딩은
`image_decoding.md`가 했다. 이 스펙의 변경은 먼저 합의한다.

### 목표

Linux의 Wayland 창 빌드(`NV_HEADLESS` 꺼짐, 기본)에서 샌드박스를 마우스와 키보드로 쓸 수 있게 한다: 데스크톱 UI를 다루고, 창 크기를
바꾸고, 정수 배율 화면에서 바른 크기로 보이고, 저장이 다음 실행까지 남는다. 지금 이 창은 시각 회귀 테스트(`visual_regression.md`)가
필요한 만큼만 한다: 크기가 고정이고 입력이 없다.

- **규칙은 웹, Windows와 같다.** 씬 파일, 저장 형식, 실행 취소, Edit/Play, 단축키 표, 데스크톱 UI가 같다. 입력을 ImGui와 뷰포트로
  나누는 규칙(`nv_imgui_view_mouse`, `NvImgui.view_rect`, `view_grab`)은 공통 코드(`engine/imgui.c`)에 있으므로 그대로 쓴다.
- **플랫폼 코드는 Windows의 나누기를 따른다**(`d3d12_port.md`, "플랫폼 계층"): 창은 `window_linux.c`, 입력은 새 `imgui_linux.c`.
- **Wayland는 코어 프로토콜과 xdg-shell만 쓴다.** 새 프로토콜 XML도, `wayland-scanner`로 만들 코드도 늘지 않는다.
- **헤드리스 빌드(`NV_HEADLESS` 켬)는 바뀌지 않는다:** 입력이 없고(`imgui_headless.c`), 샌드박스를 빌드하지 않는다.
- **시각 회귀 테스트의 그림은 바뀌지 않는다:** 고정 크기와 배율 1을 지킨다(아래 "창").

범위 밖:

- 나중 단계로 미룬 것: 클립보드, 분수 배율, 커서 모양, 한글 IME, 서버 쪽 창 장식(아래 "나중"). 이번 범위에서 Linux로는 한글을
  칠 수 없다.
- X11. Wayland 컴포지터가 없으면 지금처럼 오류를 남기고 끝난다.
- 터치(`wl_touch`), 태블릿, 폰 UI. Linux는 언제나 데스크톱 UI다(`Sandbox.ui_mode`).
- 파일 대화 상자(`local_file`). 지금처럼 실패를 알리되, 메시지만 "Linux에서는 아직 안 된다"로 고친다.
- 끌어다 놓기, primary selection(가운데 클릭 붙여 넣기), 데드 키와 조합 키(xkb compose).
- CI. 스크립트 실행(`--script`)은 Linux로 옮기지 않는다: 사용자가 다른 작업에서 없앨 계획이다(2026-11-07). Linux의 ctest는 이미 켰다(2026-11-07): 창 빌드와 헤드리스 빌드 모두 `tests/`를 빌드하고 돌린다.
- 창 여럿, seat 여럿. 첫 `wl_seat`만 쓴다.

### 지금 상태 (2026-11-07, `master`의 `9dc89da`)

`NV_HEADLESS` 커밋(`08ec6a4`) 위에서 `CMakeLists.txt`의 Linux `set(NV_BUILD_SANDBOX OFF)`만 빼고 빌드해 보았다(커밋하지 않음). 그 뒤
이미지 디코딩(`image_decoding.md`), Linux의 ctest와 `just` 레시피(`just build`, `just test`)가 더해졌다(PR #27). 아래 표에서 이미지 밖의 줄은 지금도 그대로다.

- 샌드박스는 오류 없이 컴파일되고 링크된다. `nv_setup_executable`이 `assets/`와 `data/`를 실행 파일 옆에 복사한다.
- 실행하면 바로 끝난다: `cannot read the scene file /data/showcase.nvscene`. UI 글꼴도 읽지 못한다.

| 영역 | 파일 | 없는 것 |
|---|---|---|
| 빌드 | `CMakeLists.txt` | Linux에서 `NV_BUILD_SANDBOX`를 강제로 끈다 |
| 에셋 경로 | `engine/file.c` | `/assets`, `/data`를 실행 파일 폴더로 바꾸는 것은 Windows만 한다. Linux는 파일 시스템 루트의 `/data`를 연다 |
| 저장 | `engine/storage_linux.c` | 받은 경로(`/nv-save`)를 그대로 만든다. 루트라 실패하고 저장이 꺼진다. `nv_storage_flush_then_reload`는 아무것도 하지 않는다 |
| 이미지 | `engine/image_linux.c` | 디코딩이 없었다(텍스처 없이 단색, 이미지마다 오류). `image_decoding.md`가 고쳤다: stb_image, 모든 할당은 아레나에서 |
| 창 | `engine/window_linux.c` | 크기 고정(1280x720), 배율 1, 숨김 콜백은 끝날 때만 |
| 입력 | `engine/imgui_headless.c` | 포인터와 키보드가 없다 |

### 합의한 것

AGENTS.md의 "Third-party libraries" 규칙에 따라 후보를 비교했다. 받아들인 것은 실제로 더하는 단계에서 `docs/CODING_STANDARD.md`의
의존성 절에 까닭과 함께 적는다.

| 항목 | 정한 것 | 까닭 |
|---|---|---|
| 범위 | 포인터, 키보드, 창 크기와 정수 배율(합의함, 2026-11-07) | Wayland는 코어가 작고 나머지가 확장 프로토콜이라, 다 하면 Windows보다 몇 배 크다. 샌드박스를 쓰는 데 꼭 필요한 것만 한다 |
| 창과 입력 라이브러리 | 쓰지 않고 직접 작성(합의함, 2026-11-07) | SDL3는 다 하지만 한 플랫폼에만 쓰기에 크다. GLFW는 Wayland IME가 없다. 작은 것은 Wayland를 제대로 하지 않는다: RGFW는 스스로 "very experimental and broken"이라 하고, sokol_app은 원본에 Wayland가 없다(머지되지 않은 PR #425의 포크뿐) |
| 키맵 | **libxkbcommon** (MIT, 합의함, 2026-11-07) | Wayland는 키를 evdev 키 코드로 보내고, 해석하는 법(자판 배열, 모디파이어, 글자)은 컴포지터가 보내는 XKB 키맵 파일에 있다. 그 파일을 읽는 것은 수천 줄이라 직접 쓸 일이 아니다. evdev 표를 직접 쓰면 글자가 자판 배열을 따르지 않는다. GLFW, SDL, GTK, Qt가 쓰는 시스템 패키지(`xkbcommon.pc`)다 |
| 이미지 디코딩 | 이 스펙의 일이 아니다: `image_decoding.md`가 정하고 했다 | stb_image를 Linux에서만 쓰고, 그 할당은 모두 호출자의 아레나에서 한다. 후보 비교와 웹에서의 측정도 그 스펙에 있다 |

### 창 (`engine/window_linux.c`)

| 무엇 | 어떻게 |
|---|---|
| 바인드 버전 | `xdg_wm_base`는 v6까지(`suspended` 상태가 v6에서 왔다), `wl_compositor`는 v6까지(`wl_surface.preferred_buffer_scale`가 v6에서 왔다). 지금 코드는 각각 v2와 v4까지다(`window_linux.c`의 `registry_global`). 컴포지터가 낮은 버전만 주면 그 기능 없이 간다: suspended 없이 그리고, 배율은 1이다 |
| 크기 | `xdg_toplevel.configure`의 크기(논리 픽셀, 곧 "CSS 픽셀")를 따른다. 0이면 앱이 정한다: 처음은 1600x900(Windows와 같다), 그 뒤로는 직전 크기 |
| 고정 크기 | 엔진 내부 함수 `nv_window_linux_set_fixed_size(window, width, height)`(`window_linux.h`, 합의함, 2026-11-07): 그 뒤로 프레임버퍼는 그 크기, 배율은 1이고, configure와 배율을 따르지 않는다. `visual_test`가 `nv_window_create` 바로 뒤에 1280x720으로 부른다. NOTE: 시각 테스트를 어떻게 실행해도(`just visual`이든 직접이든) 그림이 컴포지터와 화면 배율에 닿지 않게 하는 방법이다. 환경 변수에 기대면 직접 실행한 테스트가 타일링된 크기로 찍힌다. `NV_WINDOW_WIDTH`와 `NV_WINDOW_HEIGHT`는 손으로 고정할 때 쓰고, 있으면 같은 고정이 된다. Windows의 `nv_window_win32_set_fixed_size`와 같은 뜻이지만 그것에 기대지 않는다 |
| 배율 | 정수만. `wl_surface.preferred_buffer_scale`(`wl_compositor` v6)를 받아 `wl_surface_set_buffer_scale`로 같은 값을 알리고, 프레임버퍼는 논리 크기 × 배율이다. `pixel_ratio`가 그 배율이다. v6이 없으면 1. 분수 배율의 모니터(예: 1.5)에서는 컴포지터가 보통 2를 알리고 줄여서 보여 준다 |
| 적용 | `nv_window_framebuffer_size`가 새 크기를 돌려주면 `gpu_webgpu.c`가 이미 서피스를 다시 구성한다(`nv_gpu_begin_frame`). 버퍼 배율은 그 프레임의 present가 하는 commit에 함께 실린다 |
| 숨김 콜백 | 웹과 Windows처럼 저장할 마지막 기회다. toplevel이 `activated` 상태를 잃을 때(Windows의 `WM_ACTIVATEAPP`), `suspended` 상태가 될 때(`xdg_wm_base` v6: 다른 작업 공간, 최소화), 닫힐 때 부른다 |
| suspended | 그리지 않고 이벤트를 기다린다(`poll`을 막히게). Windows의 최소화와 같다 |
| 루프와 닫기 | 지금처럼 `xdg_toplevel.close`나 `nv_quit` 뒤에 `nv_window_run`이 돌아오고, 숨김 콜백을 부른다. 샌드박스의 `main`은 그 뒤 `return 0`이다 |

창 코드는 입력을 모른다. `imgui_linux.c`에는 지금처럼 `nv_window_linux_wayland`로 `wl_display`와 `wl_surface`만 준다.

### 입력 (`engine/imgui_linux.c`)

창 빌드에서 `imgui_headless.c` 대신 컴파일한다. `nv_window_linux_wayland`로 받은 `wl_display`에 자기 레지스트리를 만들어 `wl_seat`를
바인드한다. 이벤트는 창의 `pump_events`가 같은 큐에서 디스패치한다. Windows는 창 프로시저가 하나라 메시지 훅이 필요했지만,
Wayland는 객체마다 리스너를 따로 달 수 있어서 창과 입력 사이에 훅이 없다.

| 무엇 | 어떻게 |
|---|---|
| 바인드 버전 | `wl_seat`는 v8까지(`wl_pointer.axis_value120`이 v8에서 왔다). 낮으면 아래 "휠"의 다음 방법으로 간다 |
| 좌표 | `wl_pointer`의 surface 좌표가 곧 CSS 픽셀(논리 픽셀)이다. 배율로 나누지 않는다 |
| 포인터 | `enter`, `motion`: `ImGuiIO_AddMousePosEvent`와 `nv_imgui_view_mouse(NV_MOUSE_MOVE)`. `leave`: 눌린 버튼이 없으면 위치를 `-FLT_MAX`로. `button`: `BTN_LEFT`, `BTN_MIDDLE`, `BTN_RIGHT`를 DOM 번호 0, 1, 2(ImGui는 0, 2, 1)로 옮기고, 먼저 `nv_imgui_view_mouse`에 준 뒤 뷰가 갖지 않으면 ImGui에 준다(`imgui_win32.c`와 같다). 버튼을 누른 채 창 밖으로 나가도 이벤트가 계속 오는 것은 Wayland의 암묵적 grab이므로 `SetCapture` 같은 일은 없다 |
| 휠 | `axis_value120`(`wl_seat` v8)이 있으면 120이 한 칸, 없으면 `axis_discrete`, 그것도 없으면(터치패드) `axis` 값 / 10을 칸으로 본다. Wayland는 아래쪽이 양수라 부호를 뒤집는다. 세로 휠은 먼저 `nv_imgui_view_wheel(칸 × 100)`, 뷰가 갖지 않으면 ImGui 휠 |
| 커서 | 컴포지터의 기본 커서 그대로다. `nv_imgui_platform_set_cursor`는 아무것도 하지 않는다("나중") |
| 모디파이어 | `wl_keyboard.modifiers`마다 xkb 상태를 갱신하고 Ctrl, Shift, Alt, Logo를 읽는다. `ImGuiMod_*` 키 이벤트와 `nv_imgui_view_mouse`의 `mods`(`NvViewInput.tap_mods`)가 그것을 쓴다 |
| 키 | `wl_keyboard.key`의 키 코드 + 8이 xkb 키 코드다. ImGui 키는 모디파이어가 없는 첫 단계의 keysym으로 정한다(Windows의 가상 키처럼 자판 배열을 따른다: `XKB_KEY_a`가 `ImGuiKey_A`). 왼쪽/오른쪽 Shift, Ctrl, Alt, Super와 키패드도 구별한다 |
| 글자 | 키를 누를 때 `xkb_state_key_get_utf8`. Ctrl이나 Alt가 눌려 있으면 글자로 보내지 않는다(단축키다). 제어 문자(0x20 미만, 0x7F)는 보내지 않는다 |
| 반복 | Wayland는 키 반복을 보내지 않는다(클라이언트의 일). 키 이벤트는 한 번만 보낸다: Backspace, 화살표 같은 편집 키의 반복은 ImGui가 스스로 한다(`io.KeyRepeatDelay`). 글자는 `wl_keyboard.repeat_info`의 지연과 속도로 `nv_imgui_platform_update`에서 반복한다(속도 0이면 반복하지 않는다). 키를 떼거나 포커스를 잃으면 멈춘다 |
| 포커스 | `wl_keyboard.enter`/`leave`를 `ImGuiIO_AddFocusEvent`로. 포커스를 잃으면 ImGui가 눌린 키를 모두 뗀다 |
| 클립보드 | 앱 안에서만 된다: ImGui의 기본 동작(자기 버퍼)을 그대로 둔다. 다른 앱과는 주고받지 않는다("나중") |

### 샌드박스 실행

| 무엇 | 어떻게 |
|---|---|
| 빌드 | Linux 창 빌드에서는 `NV_BUILD_SANDBOX`를 끄지 않는다(최상위 프로젝트일 때 기본 켬, 다른 플랫폼과 같다). 헤드리스 빌드에서는 지금처럼 끈다. `NV_BUILD_TESTS`는 이미 켜져 있다(다른 플랫폼과 같다) |
| 에셋 경로 | `file.c`의 `nv_file_resolve_path`가 맨 앞의 `/`를 실행 파일 폴더로 바꾼다(`readlink("/proc/self/exe")`). Windows 분기와 같은 규칙이다 |
| 저장 | `$XDG_DATA_HOME/nv/<target>/`, 없으면 `~/.local/share/nv/<target>/`(합의함, 2026-11-07). `<target>`은 실행 파일 이름이다(Windows의 `%LOCALAPPDATA%\nv\<target>\`). `NV_STORAGE_DIR`는 Windows와 같은 뜻이다(테스트가 빈 저장에서 시작). `~/.local/state`(`$XDG_STATE_HOME`)도 보았으나, 저장에는 편집 상태만이 아니라 씬을 편집한 내용이 들어 있어 데이터 쪽이 맞다 |
| 다시 시작 | `nv_storage_flush_then_reload`는 Windows처럼 자기 자신을 다시 실행한다(`/proc/self/exe`를 같은 인자로 `execv`) |
| 이미지 | 했다(`image_decoding.md`): `image_linux.c`가 stb_image로 디코딩하고, stb의 할당은 모두 호출자의 아레나에서 한다. 샌드박스의 scratch 아레나(128 MB)에 chess.glb와 함께 들어간다 |
| 파일 대화 상자 | 메시지를 "file dialogs are not available headless"에서 "file dialogs are not implemented on Linux"로 바꾼다 |
| 빌드 라벨, 언어 | 이미 된다: 내려받은 크기 자리에 백엔드 이름(`NvGpu.backend_name`), 언어는 `LC_ALL`/`LANG` |

### 빌드

- `engine/CMakeLists.txt`의 창 빌드: `pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)`을 더하고 `PkgConfig::XKBCOMMON`을
  링크한다. `imgui_headless.c` 대신 `imgui_linux.c`를 쓴다.
- `wayland-protocols`의 요구 버전은 바뀌지 않는다(xdg-shell만 쓴다).

### 검증

Linux에는 자동으로 입력을 넣을 수단이 없다(Wayland는 다른 앱의 창에 입력을 넣지 못하게 한다. `--script`는 옮기지 않는다: "범위 밖"). 그래서
단계마다 개발 PC(Hyprland)에서 손으로 확인하고, 확인한 것을 "찾은 것"에 적는다. 단계마다 함께 확인하는 것:

- `just visual`이 합격한다(그림이 바뀌지 않는다).
- Linux 샌드박스 Release와 Debug의 로그에 오류가 없다(Dawn의 검증 오류 포함). Debug의 시작 때 저장 왕복 검사가 통과한다.
- 헤드리스 Linux 빌드가 빌드된다.
- 공통 파일(`engine/file.c` 등)을 바꾼 단계는 웹 빌드와 Windows 빌드도 빌드한다(AGENTS.md). Windows는 사용자가 한다.

### 단계

**작업은 `chromedays/linux-wayland-input-support` 브랜치에서 한다.** 단계마다 한 커밋 이상이고, 각 커밋은 그 단계의 검증을 통과한 뒤 올린다.
`master`에 합치거나 PR을 만드는 것은 사용자가 말할 때만이다.

1. **샌드박스가 뜬다:** 빌드 켜기, 에셋 경로, 저장 폴더, 다시 시작, 파일 대화 상자 메시지(이미지 디코딩은 `image_decoding.md`가 했다).
   확인: showcase 씬이 텍스처와 함께 그려지고 UI 글꼴이 Pretendard다. 저장 파일(`~/.local/share/nv/sandbox/state.nvs`)이 생기고, Debug의
   시작 때 저장 왕복 검사가 통과한다. 입력이 없어 상태를 바꿀 수 없으므로, 다시 실행해 상태가 돌아오는지와 다른 씬은 단계 3에서 본다.
2. **창:** 크기 따르기, 정수 배율, 숨김 콜백, suspended, 고정 크기(`nv_window_linux_set_fixed_size`, `visual_test`가 부른다). 확인: 떠 있는 창과 타일링 창의 크기 바꾸기, 모니터
   배율 1과 2에서 UI 크기, 다른 작업 공간으로 옮겼다 돌아오기, `just visual` 합격.
3. **포인터:** 확인: 뷰포트의 궤도, 팬, 줌; 탭으로 선택, Ctrl과 Shift 클릭; 기즈모 끌기; 스플리터; 메뉴; 패널 휠 스크롤; 창 밖까지
   끌기; 세 씬 모두 바꿔 보기(chess의 텍스처); 무엇을 옮기고 끝낸 뒤 다시 실행하면 그 상태가 돌아온다(저장).
4. **키보드:** xkb, 모디파이어, 글자, 반복, 포커스. 확인: 단축키 표 전체(`?` 창), 이름 바꾸기, 검색 상자, 팔레트, 실행 취소,
   Backspace와 글자 길게 누르기, 다른 자판 배열(예: `de`)에서 글자, 앱 안의 복사와 붙여 넣기.
5. **정리:** AGENTS.md(구조도, Linux 빌드와 실행 방법, "There is no build for any other platform" 고치기), `docs/CODING_STANDARD.md`의
   의존성, `visual_regression.md`(범위 밖의 입력과 창 크기 줄, 검증의 "옵션 기본값이 꺼짐", 고정 크기를 정하는 방법), 이 스펙의 "만든 결과".

### 나중

이번 범위 밖이다. 하나씩 따로 더할 수 있고, 어느 것도 새 라이브러리가 들지 않는다: 모두 `wayland-protocols`(이미 의존성)의 XML에서
`wayland-scanner`로 만드는 프로토콜이다(xdg-shell과 같은 방식).

| 무엇 | 필요한 것 |
|---|---|
| 클립보드(다른 앱과) | 코어 `wl_data_device`. 복사는 `wl_data_source`가 UTF-8 텍스트를 내놓고, 붙여 넣기는 offer를 파이프로 받는다(다른 앱을 기다리므로 제한 시간이 든다) |
| 커서 모양 | `cursor-shape-v1`(staging; 코드를 만들려면 `tablet-v2`도 필요). 모양 이름만 보내고 컴포지터가 그린다 |
| 분수 배율 | `fractional-scale-v1` + `viewporter`. 1.25, 1.5 배율에서 흐리지 않게 한다 |
| 한글 IME | `text-input-unstable-v3`. 조합 중인 글자를 앱이 그려야 한다: 웹의 오버레이처럼 캐럿 위치에 전경 그리기 목록으로, `igRender` 바로 앞의 플랫폼 훅에서 |
| 서버 쪽 창 장식 | `xdg-decoration-unstable-v1`(KDE, sway). GNOME은 서버 장식이 없어서 libdecor가 든다 |

### 위험

| 위험 | 대응 |
|---|---|
| Dawn(Vulkan)의 FIFO present가 창이 보이지 않을 때(다른 작업 공간) 막힐 수 있다 | suspended 동안에는 그리지 않는다. 그래도 막히면 "찾은 것"에 적고 present 모드를 본다 |
| 컴포지터마다 다르다(GNOME에는 서버 장식이 없어 제목 표시줄이 없다) | 코어와 xdg-shell만 쓰므로 차이가 작다. 확인은 개발 PC의 Hyprland에서만 한다 |
| 손으로만 확인해서 회귀를 놓친다 | 단계마다 확인 목록을 "찾은 것"에 남긴다. 입력이 없는 부분은 ctest와 `just visual`이 지킨다 |

### 열린 질문

- 파일 대화 상자: xdg-desktop-portal(D-Bus: sd-bus나 libdbus), 또는 `zenity`/`kdialog` 실행. 별도 스펙에서 정한다.

### 찾은 것

없음.

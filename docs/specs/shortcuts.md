# Desktop keyboard shortcuts spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-09-30). 이 스펙의 변경은 먼저 합의한다.

### 목표

데스크톱 UI (`layout.md`)에 키보드 단축키를 빠짐없이 주고, 키, 메뉴의 단축키 라벨, 도움말 창을 움직이는 표 하나에
둔다, 그래서 단축키는 한 번 정의되고 그 액션이 나오는 곳마다 보인다.

지금 몇몇 키는 액션이 있는 곳에서 각자의 검사로 처리된다: `app/undo.c`의 Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y, `draw_gizmo`의
W, E, R (포인터가 뷰포트 위에 있을 때만), `app/ui_desktop.c`의 Space와 F. Edit 메뉴는 "Ctrl+Z"를 리터럴로 쓴다. 그리고
페이지가 소비하지 않은 키는 브라우저로 넘어간다: 엔진은 ImGui가 키보드를 원하는 동안 (텍스트 필드)에만 키를 소비하므로,
Ctrl+S가 브라우저의 "페이지 저장" 대화 상자를 연다.

### 접근법

| 접근법 | 무엇인가 | 맞음 | 장단점 |
|---|---|---|---|
| **자체 표, Dear ImGui의 `Shortcut()`으로 발동** (추천) | `app/shortcuts.c`가 단축키마다 한 행을 담는다: 키 조합, 이름, 적용 조건, 부를 함수. 매 프레임 데스크톱 UI가 모든 행에 대해 ImGui의 `igShortcut_Nil(chord, RouteGlobal)`에 묻는다 | ImGui 1.92는 이미 조합을 라우팅한다: 편집 중인 텍스트 필드는 쓰는 키를 소유하고 (Ctrl+Z는 필드의 텍스트를 undo하고, 입력한 W는 W로 남는다), 수식키가 정확히 맞춰진다 (Ctrl+Z는 Ctrl+Shift+Z가 아니다). 표가 메뉴와 도움말 창에 같은 라벨을 준다. 순수 C, 고정 배열 | 표, 도움말 창, 브라우저 훅 (아래)을 위한 자체 코드 |
| 각 액션에서 키 검사 유지 | 지금처럼 | 쓸 것이 없다 | 검사가 서로 어긋나고 (W/E/R은 포인터가 뷰포트에 있어야 하고 Space는 아니다), 라벨을 두 번 입력하고, 키를 나열하는 것이 없다 |
| 웹 단축키 라이브러리 (hotkeys-js, Mousetrap; JavaScript, MIT) | 페이지에서 키를 바인딩한다 | 브라우저 쪽을 처리한다 | 키가 ImGui 입력 밖에서 앱에 닿으므로 텍스트 필드가 키를 지킬 수 없다; `imgui.c` 옆의 두 번째 입력 경로. 의존성으로 둘 가치가 없다 |
| ImGui의 키보드 내비게이션 (`NavEnableKeyboard`) | 화살표 키와 Enter가 위젯 사이를 움직인다 | 내장 | 단축키가 아니다; 창에 포커스가 있을 때마다 ImGui가 키보드를 원하게 만들어 뷰포트에서 키를 빼앗는다. 꺼 둔다 |

추천: `igShortcut_Nil`로 발동하는 자체 표. 서드파티 라이브러리 없음.

### 결정

| 주제 | 결정 |
|---|---|
| 어디서 | 데스크톱 UI만. `layout.md`가 말하듯 폰 UI에는 단축키가 없다: Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y가 `undo_update`에서 표로 옮겨 가므로, 폰 (또는 폰 UI를 받는 키보드 달린 태블릿)은 더 이상 그것을 갖지 않는다 |
| 표 하나 | `app/shortcuts.c`: `Shortcut {ShortcutId id; ImGuiKeyChord chord; const char* name; u32 when; void (*run)(App*); b32 repeat;}`. 행은 두 번째 조합을 가질 수 있다 (Ctrl+Shift+Z 옆의 Ctrl+Y). 메뉴는 오른쪽 텍스트로 `shortcut_label(id)`를 부른다; 도움말 창은 행을 나열한다 |
| 언제 발동하나 | 도크를 만든 뒤 프레임마다 한 번 `igShortcut_Nil(chord, ImGuiInputFlags_RouteGlobal)`로. 텍스트 필드를 편집하는 중 (`io.WantTextInput`), 팝업이나 모달이 열린 중 (Escape는 그들의 것), 기즈모를 끄는 중에는 결코 아니다. 행은 추가로 쇼케이스 (`WHEN_SHOWCASE`), Edit 모드 (`WHEN_EDITING`), 선택 (`WHEN_SELECTION`)을 요구할 수 있다. 지금과 달리 W, E, R은 더 이상 포인터가 뷰포트 위에 있을 필요가 없다: 입력을 안전하게 지키는 것은 텍스트 필드 규칙이다 |
| 반복 | Undo와 Redo만 누르고 있는 동안 반복한다; 나머지는 누를 때마다 한 번 발동한다 |
| 브라우저 자체 키 | 엔진에 모든 keydown에서 묻는 훅 `NvImgui.claims_key(data, key, mods)`가 생긴다: 그것이 예라고 하면 ImGui가 키보드를 원하지 않아도 `on_key`가 이벤트를 소비한다 (`preventDefault`). 앱이 표로 답하므로, Ctrl+S는 웹 페이지가 아니라 앱을 저장한다. 브라우저가 페이지에 결코 주지 않는 키는 바인딩하지 않는다: Ctrl+W, Ctrl+T, Ctrl+N, Ctrl+Tab, Ctrl+Shift+W/T/N, Ctrl+1–Ctrl+9, Ctrl+Q, F11. 새로고침 (F5, Ctrl+R)과 개발자 도구 (F12, Ctrl+Shift+I)는 일부러 브라우저에 남긴다 |
| 위치로 키 | 글자는 `imgui.c`가 이미 읽듯 물리적 위치 (`KeyboardEvent.code`)로 맞추므로, W, E, R은 어떤 배열에서든 함께 있다. 라벨은 US 배열의 키를 이름 붙인다. `?`도 위치로 Shift+Slash다 |
| macOS | Cmd는 이미 Ctrl로 도착한다 (`imgui.c`). 라벨은 macOS (`navigator.platform`으로)에서 "Ctrl" 대신 "Cmd"라고 하며, 기본 글꼴에 ⌘ 글리프가 없으므로 글자로 쓴다 |
| 도움말 | **Help > Keyboard shortcuts**와 `?` 키가 모든 행을 그룹별로, 키와 함께, 적용되지 않는 동안은 회색으로 나열하는 창을 연다. Escape나 창의 닫기 버튼이 닫는다 |
| 테스트 | Debug 빌드는 기존 `_app_debug_playing`, `_app_debug_view`, `_app_debug_undo_done` 옆에 `Module._app_debug_gizmo(n)` (0 작업, 1 로컬, 2 스냅), `_app_debug_docks_shown(n)`, `_app_debug_selected()`를 export한다. Playwright가 키를 누르고 그것들을 확인하며, 자체 리스너로 가져온 키의 `defaultPrevented`를 확인한다 |
| 서드파티 | 없음 |

### 단축키

기존 것은 표시했다; 나머지는 새것이다.

| 그룹 | 키 | 하는 일 | 언제 |
|---|---|---|---|
| File | Ctrl+S | 지금 저장 (File 메뉴의 항목) | 저장소 사용 가능 |
| Edit | Ctrl+Z (기존) | Undo | 쇼케이스, Edit 모드; 반복 |
| Edit | Ctrl+Shift+Z, Ctrl+Y (기존) | Redo | 쇼케이스, Edit 모드; 반복 |
| Edit | Escape | 선택 지우기 | 선택 있음; 팝업 없음 |
| Play | Space (기존) | Play 또는 Stop | 쇼케이스 |
| Gizmo | W, E, R (기존, 이제 전역) | 이동, 회전, 크기 | |
| Gizmo | X | Local과 World 축 바꾸기 (Unity의 키) | |
| Gizmo | Ctrl, 드래그 중 누르고 있기 | 이 드래그에 스냅 (누르는 동안 Snap 상자의 반대) | 기즈모를 끄는 중 |
| View | F (기존) | 공전 점을 선택으로 옮기기 | 선택 있음 |
| View | Shift+F | "Camera follows selection" 켜기 또는 끄기 | |
| View | Home | 씬의 시작 뷰로 (yaw, pitch, 거리, 공전 점) | |
| Docks | Ctrl+B | Scene 도크 (왼쪽) 보이기 또는 숨기기 | |
| Docks | Ctrl+I | Inspector 도크 (오른쪽) 보이기 또는 숨기기 | |
| Docks | \` (백쿼트) | Console 도크 보이기 또는 숨기기; 접혀 있으면 연다 | |
| Help | ? (Shift+/) | Keyboard shortcuts 창 열기 | |

나중 작업을 위해 비워 둠: 씬 편집을 위한 Delete, Ctrl+D (복제), F2 (이름 바꾸기). Ctrl+D는 브라우저의 북마크 키다:
복제가 생기면 가져오기 훅이 그것을 가져갈 것이다.

`search.md`가 더한 것:

| 그룹 | 키 | 하는 일 | 언제 |
|---|---|---|---|
| Find | Ctrl+Shift+P, F1 | 명령 팔레트 열기 (Firefox는 Ctrl+Shift+P를 비공개 창에 쓰므로, F1이 거기서의 방법이다) | |
| Find | Ctrl+F | 포인터 아래 패널의 검색 상자, 아니면 오른쪽 도크의 현재 탭의 검색 상자에 포커스 | |

표에서 키가 없는 행 (Show save, Reset everything..., Showcase / Stress 씬 보이기, Textures 탭 열기, 콘솔 지우기)은 팔레트
전용이다: 팔레트는 실행할 것이 있는 모든 행을, 도움말 창은 키가 있는 행만 나열한다.

### 변경

- **엔진 (`engine/imgui.h`, `engine/imgui.c`).** `NvImgui.claims_key`와 `claims_key_data`; 훅이 키를 가져가면 `on_key`가
  true를 돌려준다.
- **앱.**
  - `app/shortcuts.c` (새로 생김): 표, `shortcuts_update` (행을 발동, 데스크톱만), `shortcut_label`, `shortcuts_claim`
    (훅), `shortcuts_help` (창).
  - `app/ui_desktop.c`: Help 메뉴, 표에서 온 메뉴 라벨, 그리고 Space와 F 코드가 표로 옮겨 감.
  - `app/undo.c`: 키가 `undo_update`를 떠난다; `request`는 표가 단계를 요청하는 방법으로 남는다.
  - `app/main.c`: W, E, R이 `draw_gizmo`를 떠난다; Ctrl을 누른 스냅은 거기서 키를 읽는다; Home의 시작 뷰는 씬을 만들 때
    씬마다 보관한다.
- **문서.** `layout.md` (그 단축키 표가 여기로 옮겨 옴), `gizmo.md`, `undo.md`, `play.md`, `AGENTS.md`, README.

### 단계

1. **표:** 기존 단축키 (Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y, W, E, R, Space, F)를 옮겨 넣은 `shortcuts.c`, 발동 규칙, 엔진의
   가져오기 훅, 표에서 온 Edit 메뉴 라벨. 확인: 각각이 여전히 동작; 필드에 입력하는 동안 아무것도 발동하지 않음 (그리고
   거기서 Ctrl+Z는 씬이 아니라 필드의 텍스트를 undo함); 포인터가 도크 위에 있어도 W, E, R이 동작; 폰 UI에는 없음.
2. **새 단축키:** Ctrl+S (확인: `defaultPrevented`, 그래서 페이지 저장 대화 상자 없음), Escape, X, Ctrl 누른 스냅,
   Shift+F, Home, Ctrl+B, Ctrl+I, 백쿼트. 확인: debug export로 각각의 효과, 그리고 팝업이 Escape를 지킴.
3. **도움말과 문서:** Help 메뉴와 `?` 창, macOS의 Cmd 라벨 (`navigator.platform`을 덮어써서 확인), 위의 문서들.

모든 단계는 헤드리스 Chromium에서 Release와 Debug로 1280×800, US 키보드로 확인한다.

만든 결과로는, 1, 2단계가 함께 왔고 (표 전체와 가져오기 훅이 한 덩어리 코드다) 3단계의 도움말 창과 Cmd 라벨도
그것들과 함께 왔다; 단계들이 그다음 그것을 확인했다. 메모:

- 도움말 창은 평범한 창이 아니라 모달 팝업이다: 뷰포트 위의 평범한 창 위에서 시작한 입력은 카메라 드래그가 될 것이고,
  뷰포트는 이미 열린 팝업은 내버려 둔다. Escape와 `?`가 닫지만, 그것을 연 프레임에서는 아니다 (여는 누름이 아직 그
  프레임의 누름이다).
- Escape는 브라우저로부터 결코 가져오지 않는다 (전체 화면을 나간다); 다른 모든 바인딩된 조합은 텍스트 필드가 키보드를
  원하지 않는 한 가져온다.
- Ctrl과 Shift+F는 `RouteGlobal`과 함께 ImGui의 `Shortcut()`으로 조합을 쓴다; 기즈모 드래그 중 누른 Ctrl은 조합이 아니라
  누르고 있는 수식키이므로 `draw_gizmo`에서 `io.KeyCtrl`을 읽는다.
- Debug 빌드는 테스트가 읽는 `_app_debug_gizmo`, `_app_debug_state` (선택, 따라가기, 도움말 열림, 보이는 도크),
  `_app_debug_selected_position`을 export한다.

### 범위 밖

- 키 재배치와 사용자 바인딩 저장.
- 폰 UI의 단축키, 그리고 키보드 달린 태블릿의 단축키 (폰 UI를 받는다).
- 아직 없는 액션 (복제, 삭제, 이름 바꾸기); 그 키는 위에서 비워 두었다.

## English

Status: implemented (2026-09-30). Changes to this spec are agreed first.

### Goal

Give the desktop UI (`layout.md`) a full set of keyboard shortcuts, kept in one table that drives
the keys, the menus' shortcut labels and a help window, so a shortcut is defined once and shown
wherever its action appears.

Today a few keys are handled where their action lives, each with its own checks: Ctrl+Z,
Ctrl+Shift+Z and Ctrl+Y in `app/undo.c`, W, E and R in `draw_gizmo` (only while the pointer is over
the viewport), Space and F in `app/ui_desktop.c`. The Edit menu writes "Ctrl+Z" as a literal. And
a key the page does not consume goes on to the browser: Ctrl+S opens the browser's "Save page"
dialog, because the engine consumes a key only while ImGui wants the keyboard (a text field).

### Approaches

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **Our table, fired through Dear ImGui's `Shortcut()`** (recommended) | `app/shortcuts.c` holds one row per shortcut: its key chord, its name, when it applies and the function it calls. Each frame the desktop UI asks ImGui's `igShortcut_Nil(chord, RouteGlobal)` for every row | ImGui 1.92 already routes chords: a text field that is being edited owns the keys it uses (Ctrl+Z undoes the field's text, a typed W stays a W), and exact modifiers are matched (Ctrl+Z is not Ctrl+Shift+Z). The table gives the menus and the help window the same labels. Plain C, fixed arrays | Our own code for the table, the help window and the browser hook (below) |
| Keep checking keys at each action | As today | Nothing to write | Checks drift apart (W/E/R need the pointer in the viewport, Space does not), labels are typed twice, and nothing lists the keys |
| A web hotkey library (hotkeys-js, Mousetrap; JavaScript, MIT) | Binds keys in the page | Handles the browser side | Keys would reach the app outside ImGui's input, so a text field could not keep its keys; a second input path beside `imgui.c`. Not worth a dependency |
| ImGui's keyboard navigation (`NavEnableKeyboard`) | Arrow keys and Enter move through widgets | Built in | Not shortcuts; it also makes ImGui want the keyboard whenever a window is focused, which would take keys from the viewport. Stays off |

Recommendation: our table, fired through `igShortcut_Nil`. No third-party library.

### Decisions

| Topic | Decision |
|---|---|
| Where | Desktop UI only. The phone UI has no shortcuts, as `layout.md` says: Ctrl+Z, Ctrl+Shift+Z and Ctrl+Y move from `undo_update` into the table, so a phone (or a tablet with a keyboard, which gets the phone UI) no longer has them |
| One table | `app/shortcuts.c`: `Shortcut {ShortcutId id; ImGuiKeyChord chord; const char* name; u32 when; void (*run)(App*); b32 repeat;}`. A row may have a second chord (Ctrl+Y beside Ctrl+Shift+Z). Menus call `shortcut_label(id)` for their right-hand text; the help window lists the rows |
| When one fires | Through `igShortcut_Nil(chord, ImGuiInputFlags_RouteGlobal)`, once per frame, after the docks are built. Never while a text field is being edited (`io.WantTextInput`), a popup or modal is open (Escape is theirs), or the gizmo is being dragged. Rows can further require the showcase (`WHEN_SHOWCASE`), Edit mode (`WHEN_EDITING`) or a selection (`WHEN_SELECTION`). Unlike today, W, E and R no longer need the pointer over the viewport: the text-field rule is what keeps typing safe |
| Repeat | Only Undo and Redo repeat while held; everything else fires once per press |
| The browser's own keys | The engine gains a hook, `NvImgui.claims_key(data, key, mods)`, asked on every keydown: when it says yes, `on_key` consumes the event (`preventDefault`) even though ImGui does not want the keyboard. The app answers from the table, so Ctrl+S saves the app and not the web page. Keys a browser never gives to a page are never bound: Ctrl+W, Ctrl+T, Ctrl+N, Ctrl+Tab, Ctrl+Shift+W/T/N, Ctrl+1 to Ctrl+9, Ctrl+Q, F11. Reload (F5, Ctrl+R) and developer tools (F12, Ctrl+Shift+I) are left to the browser on purpose |
| Keys by position | Letters are matched by physical position (`KeyboardEvent.code`), as `imgui.c` already reads them, so W, E and R sit together on any layout. Labels name the US-layout key. `?` is Shift+Slash by position too |
| macOS | Cmd already arrives as Ctrl (`imgui.c`). Labels say "Cmd" instead of "Ctrl" on macOS (from `navigator.platform`), spelled out since the default font has no ⌘ glyph |
| Help | **Help > Keyboard shortcuts** and the `?` key open a window listing every row by group, with its keys, greyed while it does not apply. Escape or the window's close button closes it |
| Tests | Debug builds export `Module._app_debug_gizmo(n)` (0 operation, 1 local, 2 snap), `_app_debug_docks_shown(n)` and `_app_debug_selected()`, beside the existing `_app_debug_playing`, `_app_debug_view` and `_app_debug_undo_done`. Playwright presses the keys and checks those, and checks `defaultPrevented` on a claimed key with a listener of its own |
| Third-party | None |

### The shortcuts

Existing ones are marked; the rest are new.

| Group | Keys | Does | When |
|---|---|---|---|
| File | Ctrl+S | Save now (the File menu's item) | Storage available |
| Edit | Ctrl+Z (existing) | Undo | Showcase, Edit mode; repeats |
| Edit | Ctrl+Shift+Z, Ctrl+Y (existing) | Redo | Showcase, Edit mode; repeats |
| Edit | Escape | Clear the selection | A selection; no popup open |
| Play | Space (existing) | Play or Stop | Showcase |
| Gizmo | W, E, R (existing, now global) | Move, Rotate, Scale | |
| Gizmo | X | Switch Local and World axes (Unity's key) | |
| Gizmo | Ctrl, held during a drag | Snap for this drag (the opposite of the Snap box while held) | Dragging the gizmo |
| View | F (existing) | Move the orbit point to the selection | A selection |
| View | Shift+F | Turn "Camera follows selection" on or off | |
| View | Home | Back to the scene's start view (yaw, pitch, distance, orbit point) | |
| Docks | Ctrl+B | Show or hide the Scene dock (left) | |
| Docks | Ctrl+I | Show or hide the Inspector dock (right) | |
| Docks | \` (backquote) | Show or hide the Console dock; opens it if it is collapsed | |
| Help | ? (Shift+/) | Open the Keyboard shortcuts window | |

Kept free for later work: Delete, Ctrl+D (duplicate) and F2 (rename) for scene editing. Ctrl+D is
the browser's bookmark key: the claim hook will take it when duplicate exists.

Added by `search.md`:

| Group | Keys | Does | When |
|---|---|---|---|
| Find | Ctrl+Shift+P, F1 | Open the command palette (Firefox keeps Ctrl+Shift+P for its private window, so F1 is the way there) | |
| Find | Ctrl+F | Focus the search box of the panel under the pointer, else of the right dock's current tab | |

The rows without keys in the table (Show save, Reset everything..., Show the Showcase / Stress
scene, Open the Textures tab, Clear the console) are palette only: the palette lists every row
that has something to run, and the help window only the rows that have keys.

### Changes

- **Engine (`engine/imgui.h`, `engine/imgui.c`).** `NvImgui.claims_key` and `claims_key_data`; `on_key`
  returns true when the hook claims the key.
- **App.**
  - `app/shortcuts.c` (new): the table, `shortcuts_update` (fires rows, desktop only),
    `shortcut_label`, `shortcuts_claim` (the hook), `shortcuts_help` (the window).
  - `app/ui_desktop.c`: the Help menu, menu labels from the table, and its Space and F code moves
    into the table.
  - `app/undo.c`: the keys leave `undo_update`; `request` stays how the table asks for a step.
  - `app/main.c`: W, E, R leave `draw_gizmo`; Ctrl-held snap reads the key there; Home's start
    view is kept per scene when it is built.
- **Docs.** `layout.md` (its shortcut table moves here), `gizmo.md`, `undo.md`, `play.md`,
  `AGENTS.md` and README.

### Phases

1. **The table:** `shortcuts.c` with the existing shortcuts moved into it (Ctrl+Z, Ctrl+Shift+Z,
   Ctrl+Y, W, E, R, Space, F), the firing rules, the engine's claim hook, and the Edit menu's labels
   from the table. Checked: each one still works; none fires while typing in a field (and Ctrl+Z
   there undoes the field's text, not the scene); W, E and R work with the pointer over a dock; the
   phone UI has none.
2. **New shortcuts:** Ctrl+S (checked: `defaultPrevented`, so no Save page dialog), Escape, X,
   Ctrl-held snap, Shift+F, Home, Ctrl+B, Ctrl+I and backquote. Checked: each one's effect through
   the debug exports, and that popups keep Escape.
3. **Help and docs:** the Help menu and `?` window, Cmd labels on macOS (checked by overriding
   `navigator.platform`), and the documents above.

Every phase is checked in Release and Debug in headless Chromium at 1280×800 with a US keyboard.

As built, phases 1 and 2 arrived together (the whole table and the claim hook are one piece of
code) and phase 3's help window and Cmd labels with them; the phases then checked them. Notes:

- The help window is a modal popup, not a plain window: input that starts over a plain window
  on the viewport would become a camera drag, and the viewport already leaves open popups alone.
  Escape and `?` close it, but not in the frame that opened it (the opening press is still that
  frame's press).
- Escape is never claimed from the browser (it leaves full screen); every other bound chord is, unless
  a text field wants the keyboard.
- Ctrl and Shift+F use the chords through ImGui's `Shortcut()` with `RouteGlobal`; Ctrl held during
  a gizmo drag reads `io.KeyCtrl` in `draw_gizmo`, since it is a held modifier, not a chord.
- Debug builds export `_app_debug_gizmo`, `_app_debug_state` (selection, follow, help open, docks
  shown) and `_app_debug_selected_position`, which the tests read.

### Out of scope

- Remapping keys, and saving custom bindings.
- Shortcuts on the phone UI, and for a tablet with a keyboard (it gets the phone UI).
- Actions that do not exist yet (duplicate, delete, rename); their keys are
  kept free above.

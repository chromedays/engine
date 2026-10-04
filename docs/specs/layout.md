# Desktop and phone layout spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-09-30). 이 스펙의 변경은 먼저 합의한다.

### 목표

데스크톱과 폰에 각자의 입력 장치에 맞춘 별개의 에디터 UI 두 개를 주고, 둘 다 Play / Stop 버튼을 화면 위쪽
가운데에 둔다.

지금은 배치 하나가 둘 다를 맡는다: 캔버스 위쪽 60%에 씬 뷰포트, 아래쪽 40%에 탭이 있는 패널 하나
(`nv_editor_layout`, `app_build_ui`). 세워 든 폰에는 맞다. 넓은 화면에서는 씬이 가는 띠가 되고, Scene 트리와
Inspector 사이에서 탭을 바꿔야 하고, Play, Undo, Redo가 패널 안의 한 줄에 머문다.

### 접근법

| 접근법 | 무엇인가 | 맞음 | 장단점 |
|---|---|---|---|
| **자체 사각형 위의 UI 두 개** (추천) | 앱이 모든 영역의 사각형을 직접 계산하고, 지금 `nv_imgui_begin_panel`이 하듯 영역마다 ImGui 창 하나를 연다. 장치마다 자체 배치 함수와 자체 파일이 있다 | 뷰포트는 이미 모든 곳에서 "사각형"이다 (`NvImgui.view_rect`, 렌더러의 뷰포트, picking, 기즈모), 그래서 뷰포트가 캔버스 가운데 있어도 숫자만 바뀐다. 저장과 맞춰야 할 도킹 상태가 없다 | 분할선과 접기는 우리가 써야 한다 (3단계) |
| Dear ImGui 도킹 | cimgui 빌드에 이미 `ImGuiConfigFlags_DockingEnable`이 있다. 사용자가 창을 어떤 배치로든 끌어 놓는다 | 끌어 놓기 도킹이 공짜 | 창 위치가 우리 저장 형식과 그 버전 관리와 별개인 ImGui 자체 `.ini` 텍스트에 산다; 뷰포트는 매 프레임 사각형을 읽어 와야 하는 도크 노드가 되고, 엉뚱한 곳에 놓으면 뷰포트나 Play 버튼이 숨을 수 있다. 도킹은 마우스 기능이라 이 데스크톱 UI는 쓰고 폰 UI는 쓰면 안 된다 |
| 적응형 배치 하나 | 같은 코드 안에서 창 크기에 따라 배치를 바꾼다 | 코드 경로 하나 | 두 장치는 배치만이 아니라 조작, 크기, 제스처가 다르다; 함수 하나가 `if (phone)`으로 가득해진다. 기각: UI는 완전히 분리한다 |
| 라이브러리 | 맞는 것이 없다: 배치는 앱 자체의 창이다 | | |

추천: 자체 사각형 위의 UI 두 개. 도킹은 나중에 바꿀 수 있게 남는다; 여기의 어떤 것도 그것을 막지 않는다.

### 결정

| 주제 | 결정 |
|---|---|
| UI 고르기 | 시작할 때 한 번, 주 포인터로: 거친 포인터 (터치)는 **폰 UI**, 그 밖은 **데스크톱 UI** (`js_touch_is_primary`, 이미 `NvImgui.ui_scale`을 설정함). 선택은 창 크기가 아니라 입력 장치를 따른다: 좁은 데스크톱 창은 데스크톱 UI를 유지한다. 지금은 전환이 없다 |
| 분리 | `app/ui_desktop.c`와 `app/ui_phone.c`가 각자의 배치, 창, 툴바, 크기, 제스처를 소유한다. `app_build_ui`는 그중 하나의 호출이 된다. 어느 파일도 다른 쪽 장치로 분기하지 않는다. 둘이 나누는 것은 **섹션**뿐이다: 패널 하나의 내용을 현재 창에 그리는 함수들 (`scene_tab`, `inspector_tab`, `view_tab`, `stress_ui`, `console_tab`, `undo_ui`, 그 안의 저장과 그림자 섹션), 그래서 편집 로직은 한 번만 있다. 섹션은 `app->ui_mode`에 크기를 물을 수 있지만, 편집하는 것을 바꾸려고 물으면 안 된다 |
| Play / Stop | 두 UI 모두 화면 위쪽 가운데: 씬 뷰포트 위에 전체 폭으로 걸친 **상단 바** 안에, 중심이 캔버스의 가로 중심인 버튼. 지금처럼 Edit 모드에서는 Play를, 재생 중에는 Stop을 보여 주고, 스트레스 씬이 보이는 동안은 숨는다 (`play.md`) |
| 상단 바, 데스크톱 | 높이 32 px. 왼쪽: 메뉴 바 (File, Edit, View). 가운데: Play / Stop, 그리고 재생 중에는 바로 뒤에 "Playing: edits are lost on Stop" 안내. 오른쪽: 프레임 시간과 보이는 씬의 이름 |
| 상단 바, 폰 | 높이 48 CSS 픽셀 (손가락이 닿는 터치 영역). 왼쪽: Undo, 그다음 Find (명령 팔레트를 연다, `search.md`). 가운데: Play / Stop. 오른쪽: Redo. 다른 것은 없다. Play, Undo, Redo 줄이 패널을 떠나고, 패널은 그 줄을 돌려받는다 |
| 데스크톱 영역 | 상단 바 아래: **왼쪽 도크** (Scene 트리, 폭 260 px), **오른쪽 도크** (탭 Inspector와 View, 폭 340 px), **하단 도크** (탭 Console과, 스트레스 씬이 보이는 동안 Stress; 높이 220 px, 탭 줄까지 접을 수 있음), 그리고 가운데 남은 곳을 채우는 **씬 뷰포트** |
| 폰 영역 | 상단 바 아래, 지금처럼: 나머지의 위쪽 60%에 뷰포트, 아래쪽 40%에 탭이 있는 패널 하나 (Scene, Inspector, View, Console, 보이는 동안 Stress). 노드를 고르면 여전히 Inspector로 넘어간다 |
| 빌드 라벨 | 두 UI 모두 배지와 함께 뷰포트 왼쪽 위 모서리에 남는다; 그것을 탭하거나 클릭하면 Console이 열린다 (데스크톱에서는 하단 도크 탭, 폰에서는 Console 탭). 폰에서는 상단 바 바로 아래에 있어 Play 버튼과 만나지 않는다 |
| Play 색조 | 쇼케이스가 재생되는 동안 패널 (데스크톱의 도크, 폰의 패널)과 상단 바에 색조가 든다, 지금 패널처럼 |
| 단위 | 이 스펙의 크기는 CSS 픽셀 × `ui_scale` (데스크톱 1.0, 폰 1.3)이다; 렌더러, picking, `view_rect`에 넘기는 사각형은 프레임버퍼 픽셀이고, 지금처럼 pixel ratio로 변환한다 |
| 뷰포트 지키기 | 도크는 뷰포트가 캔버스 폭의 40% 또는 상단 바 아래 높이의 40%보다 작아지지 않게 제한된다. 그보다 작으면 (아주 작은 데스크톱 창) 도크는 최솟값 (왼쪽 160, 오른쪽 220, 아래 120)에서 멈추고 뷰포트가 남은 것을 가진다 |
| 분할선 | 도크와 뷰포트 사이의 경계는 끌 수 있고 (마우스만, 8 px 터치 영역, 크기 조절 커서), 하단 도크의 탭 줄을 클릭하면 접히거나 열린다. 크기는 저장된다 |
| 저장 상태 | 에디터 설정에 왼쪽 폭, 오른쪽 폭, 아래 높이, 하단 도크가 열렸는지를 위한 `EDIT` 태그가 생기고 (`save.md`), 불러올 때 제한된다. 이것은 데스크톱 설정이다; 폰 UI에는 저장된 배치가 없다. UI 선택 자체는 저장하지 않는다: 매 시작마다 장치에서 온다 |
| 서드파티 | 없음 |

### 데스크톱 UI

```
+----------------------------------------------------------------------+
| File  Edit  View |        [ Play ]  (note while playing) |  16.7 ms  |   top bar
+----------+--------------------------------------+--------------------+
| Scene    |                                      | Inspector | View   |
| (tree)   |           scene viewport             |                    |
|          |   build label + badge                | selected node's    |
|          |                                      | components         |
+----------+--------------------------------------+--------------------+
| Console | Stress                                                ▾    |   bottom dock
+----------------------------------------------------------------------+
```

- **메뉴 바.**
  - File: Save now, Show save, Reset (확인과 함께), 생기면 Export와 Import.
  - Edit: Undo, Redo, 라벨과 단축키와 함께.
  - View: 왼쪽 도크, 오른쪽 도크, 하단 도크 보이기 또는 숨기기, 그리고 지금 View 탭이 가진 씬 선택기
    (Showcase, Stress).
- **오른쪽 도크 탭.** 지금처럼 노드를 고르면 Inspector가 스스로 선택된다. View는 지금 내용 (카메라, 그림자,
  저장)을 유지한다.
- **하단 도크.** Console, 그리고 스트레스 씬이 보이는 동안 Stress 탭 (지금처럼 스트레스 씬을 열면 스스로
  선택된다). Console 탭의 라벨과 색은 여전히 안 본 경고와 오류를 보여 준다; 데스크톱에서는 라벨에 수를 넣을
  자리가 있다.
- **단축키** (데스크톱만): 표를 담은 `shortcuts.md` 참고 (Space는 재생과 정지, F는 공전 점을 선택으로 옮김, 그
  밖의 것들).

- **마우스 습관.** hover 툴팁이 조작이 무엇을 하는지 말해 준다; 슬라이더에서 Ctrl+클릭하면 숫자를 입력한다. 둘
  다 ImGui 자체 동작이므로 툴팁 몇 개 외에는 코드가 필요 없다.

### 폰 UI

```
+------------------------------+
| Undo Find [ Play ]     Redo  |   top bar, 48 px
+------------------------------+
|  build label + badge         |
|        scene viewport        |   60% of the rest
+------------------------------+
| Scene | Inspector | View | Console
| (the tab's content)          |   40% of the rest
+------------------------------+
```

폰 UI는 지금 하는 것 (탭 패널 하나, 터치 스크롤, 좁은 Console 행, `gizmo.md`와 `console.md`의 크기)을 유지하되
다음이 바뀐다: Play / Undo / Redo 줄이 상단 바로 옮겨 가고, "Playing: edits are lost on Stop." 줄은 패널 맨 위에
(줄바꿈되어) 남고, 빌드 라벨은 상단 바 바로 아래로 옮겨 간다. 메뉴 바, 단축키, 분할선은 없다.

### 변경

- **엔진 (`engine/imgui.h`, `engine/imgui.c`).**
  - `nv_editor_layout`은 두 UI가 부르는 사각형 도우미로 바뀐다: 상단 바 사각형, 그리고 나머지의 분할. 뷰포트는
    더 이상 (0, 0)에서 시작하지 않아도 된다.
  - `in_view`는 이미 `view_rect`를 사각형으로 검사하므로 입력 라우팅은 바뀔 것이 없다. 분할선이나 도크에서
    시작한 누름은 ImGui의 것이다 (도크는 창이므로 `WantCaptureMouse`가 덮는다).
  - 페이지의 캔버스 커서는 ImGui의 마우스 커서를 따른다 (분할선의 크기 조절 커서를 위해).
- **앱.**
  - `app/ui_desktop.c`와 `app/ui_phone.c`; `app/ui.c`는 섹션과 공용 도우미를 유지한다; `App.ui_mode`; `App`
    안의 도크 크기.
  - `app/main.c`는 매 프레임 활성 UI에 뷰포트 사각형을 묻는다. 기즈모, picking, `badge_box`, Play 버튼은 왼쪽 위
    뷰포트를 가정하는 대신 그것을 읽는다.
  - `app/save.c`: `EDIT` 태그 네 개와 스펙 표의 각 행.
- **테스트.** Debug 빌드는 뷰포트와 도크 사각형과 Play 버튼의 사각형을 export하여
  (`Module._app_debug_layout_*`) 테스트가 그것들을 클릭하고 Play 버튼의 중심을 캔버스 중심과 비교할 수 있게
  한다.
- **문서.** `AGENTS.md` (에디터 배치 규칙), README, 그리고 패널, Play 줄, 배치를 언급하는 `viewport.md`,
  `play.md`, `console.md`, `undo.md`, `save.md`.

### 단계

1. **UI 두 개:** UI 선택, 가운데 Play / Stop이 있는 상단 바 (두 UI), 섹션이 든 고정 크기의 데스크톱 도크, Play
   줄을 상단 바에 둔 채 자체 파일로 옮긴 폰 배치, 일반 사각형으로서의 뷰포트. Release와 Debug에서 확인: 데스크톱
   뷰포트의 크기와 위치, 뷰포트 여러 지점 (각 도크 근처 포함)에서의 picking과 기즈모, 여러 창 크기에서 캔버스
   중심과 같은 Play 버튼 중심, 상단 바를 빼고는 바뀌지 않은 폰 UI (터치로 Playwright).
2. **데스크톱 습관:** 메뉴 바, Space와 F, 하단 도크 접기, 툴팁. 필드에 입력하는 동안 단축키가 발동하지 않는지,
   F가 공전 점만 옮기는지 확인.
3. **분할선과 문서:** 제한과 함께 도크 경계 끌기, 크기 조절 커서, 저장된 크기 (새로고침이 유지하고, 태그 없는
   옛 저장은 기본값으로 불러옴), 아주 작은 데스크톱 창, 위의 문서들.

모든 단계는 헤드리스 Chromium에서 Release와 Debug로 확인한다. 데스크톱 UI는 1280×800과 1920×1080에서 마우스로,
폰 UI는 390×664에서 터치로.

만든 결과로는, 메뉴 바, Space와 F, 접히는 하단 도크, 분할선, 저장된 크기가 같은 창 코드의 일부라서 모두 1단계와
함께 왔다; 2단계와 3단계는 그것들을 확인했다. 빌드에 관한 메모:

- File에는 Save now, Show save (View 탭의 저장 뷰어를 연다), Reset...이 있다; Export와 Import는 아직 없다.
- 폰의 Undo와 Redo 버튼은 폭 64 px, Find는 52 px (`search.md`)라서, 셋 다 360 px 화면에서 가운데 Play 버튼 옆에
  들어간다.
- Play 버튼은 데스크톱에서 폭 84 px (폰에서 104)이다; 데스크톱 상단 바는 메뉴 바 자체의 높이라서 32가 아닌
  28 px이다.
- 분할선은 뷰포트 안이 아니라 도크 안쪽 6 px 가장자리 띠 위의 투명한 창이라서, 그 위의 누름은 결코 카메라
  드래그가 되지 않는다. 드래그는 보이는 크기에서 시작하는데, 창이 원하는 크기보다 작게 잘랐을 수 있다.
- 도크 크기는 160..640 (왼쪽), 220..640 (오른쪽), 120..600 (아래)로 제한되고, 그다음 뷰포트가 캔버스 폭과 상단 바
  아래 높이의 40%를 유지하도록 줄어들며 (오른쪽 도크가 먼저 양보한다), 64 px 아래로는 내려가지 않는다. 저장되는
  것은 원하는 크기다.
- 캔버스의 포인터는 ImGui의 커서를 따른다 (`imgui.c`의 `js_set_cursor`): 분할선 위에서는 크기 조절 커서, 필드
  위에서는 텍스트 커서.

### 범위 밖

- 노드 추가, 삭제, 복제, 이름 바꾸기 (트리의 오른쪽 클릭 메뉴에 필요하다): 별도의 씬 편집 스펙.
- 실행 중 UI 전환, 태블릿 전용 배치 (거친 포인터의 태블릿은 지금은 폰 UI를 받는다).
- 패널을 다른 배치로 끌기 (도킹), 여러 뷰포트.

## English

Status: implemented (2026-09-30). Changes to this spec are agreed first.

### Goal

Give the desktop and the phone two separate editor UIs, each built for its input device, and put
the Play / Stop button at the top center of the screen in both.

Today one layout serves both: the scene viewport on the top 60% of the canvas and one tabbed panel
on the bottom 40% (`nv_editor_layout`, `app_build_ui`). That is right for a phone held upright. On
a wide screen it leaves the scene a thin strip, forces tab switches between the Scene tree and the
Inspector, and keeps Play, Undo and Redo in a row inside the panel.

### Approaches

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **Two UIs on our own rectangles** (recommended) | The app computes every region's rectangle itself and opens one ImGui window per region, as `nv_imgui_begin_panel` does today. Each device has its own layout function and its own file | The viewport is already "a rectangle" everywhere (`NvImgui.view_rect`, the renderer's viewport, picking, the gizmo), so the viewport being in the middle of the canvas changes only the numbers. No docking state to keep in step with the save | Splitters and collapsing are ours to write (phase 3) |
| Dear ImGui docking | The cimgui build already has `ImGuiConfigFlags_DockingEnable`. Users drag windows into any arrangement | Free drag-and-dock behavior | Window positions live in ImGui's own `.ini` text, apart from our save format and its versioning; the viewport is then a dock node whose rectangle we read back each frame, and a stray drop can hide the viewport or the Play button. Docking is a mouse feature, which this desktop UI would use and the phone UI must not |
| One adaptive layout | Switch arrangement by window size inside the same code | One code path | The two devices differ in controls, sizes and gestures, not only in arrangement; one function ends up full of `if (phone)`. Rejected: the UIs are to be fully separate |
| A library | None fits: the layout is the app's own windows | | |

Recommendation: two UIs on our own rectangles. Docking stays available to switch to later; nothing
here rules it out.

### Decisions

| Topic | Decision |
|---|---|
| Choosing the UI | Once at start, from the primary pointer: a coarse pointer (touch) gives the **phone UI**, anything else the **desktop UI** (`js_touch_is_primary`, which already sets `NvImgui.ui_scale`). The choice follows the input device, not the window size: a narrow desktop window keeps the desktop UI. There is no switch for now |
| Separation | `app/ui_desktop.c` and `app/ui_phone.c` each own their layout, windows, toolbar, sizes and gestures. `app_build_ui` becomes a call to one of them. Neither file branches on the other's device. They share only the **sections**: functions that draw the content of one panel into the current window (`scene_tab`, `inspector_tab`, `view_tab`, `stress_ui`, `console_tab`, `undo_ui` and the save and shadow sections inside them), so editing logic exists once. A section may ask `app->ui_mode` for a size, never to change what it edits |
| Play / Stop | Top center of the screen in both UIs: a button whose center is the canvas's horizontal center, in a **top bar** across the full width above the scene viewport. It shows Play in Edit mode and Stop while playing, as today, and is hidden while the stress scene is shown (`play.md`) |
| Top bar, desktop | 32 px high. Left: the menu bar (File, Edit, View). Center: Play / Stop, and right after it, while playing, the note "Playing: edits are lost on Stop". Right: frame time and the shown scene's name |
| Top bar, phone | 48 CSS pixels high (hit areas a finger can reach). Left: Undo, then Find (opens the command palette, `search.md`). Center: Play / Stop. Right: Redo. Nothing else. The row of Play, Undo and Redo leaves the panel, which gets that line back |
| Desktop regions | Below the top bar: **left dock** (Scene tree, 260 px wide), **right dock** (tabs Inspector and View, 340 px wide), **bottom dock** (tabs Console and, while the stress scene is shown, Stress; 220 px high, collapsible to its tab strip), and the **scene viewport** filling what is left in the middle |
| Phone regions | Below the top bar, as today: the viewport on the top 60% of the rest and one tabbed panel on the bottom 40% (Scene, Inspector, View, Console, and Stress while shown). Picking a node still jumps to Inspector |
| Build label | Stays in the viewport's top-left corner in both UIs, with its badge; a tap or click on it opens the Console (the bottom dock tab on desktop, the Console tab on phone). On phone it sits just below the top bar, so it never meets the Play button |
| Play tint | The panels (docks on desktop, the panel on phone) and the top bar are tinted while the showcase plays, as the panel is today |
| Units | Sizes in this spec are CSS pixels times `ui_scale` (1.0 on desktop, 1.3 on phone); rectangles handed to the renderer, picking and `view_rect` are in framebuffer pixels, converted by the pixel ratio as now |
| Keeping the viewport | The docks clamp so the viewport is never smaller than 40% of the canvas width or 40% of the height under the top bar. Below that (a very small desktop window) the docks stop at their minimums (left 160, right 220, bottom 120) and the viewport takes what is left |
| Splitters | The borders between the docks and the viewport can be dragged (mouse only, 8 px hit area, resize cursor), and a click on the bottom dock's tab strip collapses or opens it. The sizes are saved |
| Saved state | Editor settings gain `EDIT` tags for the left width, the right width, the bottom height and whether the bottom dock is open (`save.md`), clamped on load. These are desktop settings; the phone UI has no saved layout. The UI choice itself is not saved: it comes from the device every start |
| Third-party | None |

### Desktop UI

```
+----------------------------------------------------------------------+
| File  Edit  View |        [ Play ]  (note while playing) |  16.7 ms  |   top bar
+----------+--------------------------------------+--------------------+
| Scene    |                                      | Inspector | View   |
| (tree)   |           scene viewport             |                    |
|          |   build label + badge                | selected node's    |
|          |                                      | components         |
+----------+--------------------------------------+--------------------+
| Console | Stress                                                ▾    |   bottom dock
+----------------------------------------------------------------------+
```

- **Menu bar.**
  - File: Save now, Show save, Reset (with its confirmation), Export and Import when they exist.
  - Edit: Undo, Redo, with their labels and shortcuts.
  - View: show or hide the left dock, the right dock and the bottom dock, and the scene picker
    (Showcase, Stress) that the View tab holds today.
- **Right dock tabs.** Inspector selects itself when a node is picked, as today. View keeps its
  current contents (camera, shadows, save).
- **Bottom dock.** The Console, and the Stress tab while the stress scene is shown (it selects
  itself when the stress scene is opened, as today). The Console tab's label and color still show
  unseen warnings and errors; on desktop the label has room for the count.
- **Shortcuts** (desktop only): see `shortcuts.md`, which holds the table (Space plays and stops,
  F moves the orbit point to the selection, and the rest).

- **Mouse habits.** Hover tooltips name what a control does; Ctrl+click on a slider types a
  number. Both are ImGui's own behavior, so they need no code beyond a few tooltips.

### Phone UI

```
+------------------------------+
| Undo Find [ Play ]     Redo  |   top bar, 48 px
+------------------------------+
|  build label + badge         |
|        scene viewport        |   60% of the rest
+------------------------------+
| Scene | Inspector | View | Console
| (the tab's content)          |   40% of the rest
+------------------------------+
```

The phone UI keeps what it does today (one panel of tabs, touch scrolling, the compact Console
rows, the sizes in `gizmo.md` and `console.md`), with these changes: the Play / Undo / Redo row
moves into the top bar, the "Playing: edits are lost on Stop." line stays at the top of the panel
(wrapped), and the build label moves just below the top bar. It has no menu bar, no shortcuts,
and no splitters.

### Changes

- **Engine (`engine/imgui.h`, `engine/imgui.c`).**
  - `nv_editor_layout` is replaced by rectangle helpers the two UIs call: a top bar rectangle, and
    a split of the rest. The viewport no longer has to start at (0, 0).
  - `in_view` already tests `view_rect` as a rectangle, so input routing needs no change. A press
    that starts on a splitter or a dock belongs to ImGui (docks are windows, so `WantCaptureMouse`
    covers them).
  - The page's canvas cursor follows ImGui's mouse cursor (for the splitters' resize cursor).
- **App.**
  - `app/ui_desktop.c` and `app/ui_phone.c`; `app/ui.c` keeps the sections and the shared
    helpers; `App.ui_mode`; the docks' sizes in `App`.
  - `app/main.c` asks the active UI for the viewport rectangle each frame. The gizmo, picking,
    `badge_box` and the Play button read it instead of assuming a top-left viewport.
  - `app/save.c`: the four `EDIT` tags and a row each in the spec's table.
- **Tests.** Debug builds export the viewport and dock rectangles and the Play button's rectangle
  (`Module._app_debug_layout_*`) so tests can click them and check the Play button's center against
  the canvas center.
- **Docs.** `AGENTS.md` (the editor-layout rule), README, `viewport.md`, `play.md`, `console.md`,
  `undo.md` and `save.md` where they name the panel, the Play row, or the layout.

### Phases

1. **Two UIs:** the UI choice, the top bar with the centered Play / Stop (both UIs), the desktop
   docks at fixed sizes with the sections in them, the phone layout moved to its own file with
   the Play row in the top bar, and the viewport as a general rectangle. Checked in Release and
   Debug: the desktop viewport's size and position, picking and the gizmo at several points of
   the viewport (including near each dock), the Play button's center equal to the canvas center at
   several window sizes, and the phone UI unchanged apart from the top bar (Playwright with touch).
2. **Desktop habits:** the menu bar, Space and F, the bottom dock's collapse, tooltips. Checked
   that shortcuts do not fire while typing in a field, and that F moves the orbit point only.
3. **Splitters and docs:** dragging the docks' borders with the clamps, the resize cursor, the
   saved sizes (a reload keeps them, an old save without the tags loads with the defaults), a
   very small desktop window, and the documents above.

Every phase is checked in Release and Debug in headless Chromium, the desktop UI with the mouse
at 1280×800 and 1920×1080, the phone UI with touch at 390×664.

As built, the menu bar, Space and F, the collapsing bottom dock, the splitters and the saved sizes
all arrived with phase 1, since they are part of the same window code; phases 2 and 3 then
checked them. Notes on the build:

- File has Save now, Show save (it opens the save viewer in the View tab) and Reset...; Export and
  Import are not there yet.
- The phone's Undo and Redo buttons are 64 px wide, and Find 52 px (`search.md`), so all three fit
  beside the centered Play button on a 360 px screen.
- The Play button is 84 px wide on the desktop (104 on the phone); the desktop top bar is 28 px
  high, not 32, because it is the menu bar's own height.
- A splitter is a transparent window over the 6 px edge strip inside the dock, not inside the
  viewport, so a press on it never becomes a camera drag. A drag starts from the size shown, which
  the window may have cut below the wanted one.
- Dock sizes clamp to 160..640 (left), 220..640 (right) and 120..600 (bottom), then shrink so the
  viewport keeps 40% of the canvas width and of the height under the top bar (the right dock gives
  way first), and never drops below 64 px. The wanted sizes are what is saved.
- The canvas's pointer follows ImGui's cursor (`js_set_cursor` in `imgui.c`): a resize cursor over
  a splitter, a text cursor over a field.

### Out of scope

- Adding, deleting, duplicating and renaming nodes (a right-click menu in the tree needs them): a
  separate scene-editing spec.
- Switching UIs at runtime, and a tablet-specific layout (a tablet with a coarse pointer gets the
  phone UI for now).
- Dragging panels into other arrangements (docking), multiple viewports.

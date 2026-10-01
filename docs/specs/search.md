# Search spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-10-01). 이 스펙의 변경은 먼저 합의한다.

### 목표

Unreal Editor처럼 입력해서 찾는다, 두 가지 방식으로:

1. **패널 검색 상자.** Console을 뺀 모든 에디터 패널 (Scene 트리, Inspector, View, Textures, Stress 탭) 맨 위의 상자가
   입력하는 대로 그 패널을 거른다: Scene 트리는 맞는 노드와 그 부모를 남기고, 다른 패널은 섹션 제목 아래 맞는 행을
   남긴다. World Outliner와 Details 패널의 "Search Details"처럼 동작한다.
2. **명령 팔레트.** 상자 하나 (데스크톱에서 Ctrl+Shift+P)가 모든 것을 한 번에 검색한다: 액션 (단축키 표와 몇 개 더),
   보이는 씬의 노드, 패널의 모든 설정. Enter는 액션을 실행하거나, 노드를 선택하거나, 설정으로 이동한다.

Console은 자체 레벨 필터 (`console.md`)를 유지하고 상자를 받지 않는다.

### 접근법

| 접근법 | 무엇인가 | 맞음 | 장단점 |
|---|---|---|---|
| **자체 매처와 자체 위젯** (추천) | 모든 상자가 쓰는 매칭 함수 하나 (단어, 대소문자 무시 부분 문자열, 아래); 패널 행은 그릴지 정하는 작은 도우미를 거친다; 팔레트는 결과를 나열하는 모달 팝업이다 | 순수 C, 새 `app/search.c`에 몇백 줄. 모든 곳에서 같은 규칙 | 모든 패널 행이 도우미를 거쳐야 한다 (새 UI 코드의 규칙) |
| Dear ImGui의 `ImGuiTextFilter` | ImGui 내장 필터: 텍스트 상자와 `PassFilter(text)` | 이미 빌드에 있다 | 그 문법은 쉼표 구분에 `-` 제외인데, Unreal과 대부분의 검색 상자처럼 공백으로 구분된 단어가 아니다. 패널이 아니라 문자열을 거른다: 미뤄 그리는 제목, 트리의 부모, 팔레트는 어차피 우리 것이다. 쓰지 않는다 |
| 퍼지 매칭: fts_fuzzy_match (C/C++ 단일 헤더, 퍼블릭 도메인 또는 MIT, Forrest Smith) 또는 fzy의 알고리즘 (C, MIT) | 글자를 순서대로 틈을 두고 점수 매김 ("tgsh"가 "Toggle shadows"를 찾음) | 항목이 수백 개인 팔레트에 좋다 | 순위를 예측하기 어렵다; 긴 설정 목록이 너무 많이 맞는다. 우리 규모에서는 필요 없다; 나중에 다른 것을 바꾸지 않고 팔레트의 점수 매기기를 대신할 수 있다 |
| 웹 라이브러리 (Fuse.js, JavaScript, Apache-2.0) | 페이지 안의 퍼지 검색 | | WebAssembly에 사는 문자열에 대해, 키를 누를 때마다 결과가 JavaScript 경계를 넘는다. 안 됨 |

추천: 자체 매처와 위젯. 서드파티 라이브러리 없음.

### 매칭

- 질의는 공백에서 단어로 나뉜다; 후보는 **모든** 단어가 ASCII 대소문자를 무시한 부분 문자열일 때 맞는다 (`shad res`가
  "Shadow map resolution"에 맞음). ASCII 밖의 바이트는 그대로 비교하므로, UTF-8 노드 이름은 자기 철자에 맞는다.
- 후보의 텍스트: 노드의 이름; 패널 행의 라벨 더하기 섹션 제목 더하기 행이 주는 추가 키워드 (그래서 `msaa`가
  Anti-aliasing 콤보를 찾고, `shadows`가 Shadows 섹션 전체를 보여 준다); 액션의 이름 더하기 그룹.
- 순위 (팔레트만; 패널은 순서를 유지): 후보 안 단어의 시작에 맞는 단어가 가운데 맞는 것보다 위; 그다음 짧은 후보가
  먼저; 그다음 나열된 순서.
- 빈 질의는 모든 것에 맞는다.

### 패널 검색 상자

| 주제 | 결정 |
|---|---|
| 패널 | Scene, Inspector, View, Textures, Stress, 두 UI 모두. Console은 아님 |
| 상자 | 패널 폭에 걸친 "Search" 힌트의 입력, 내용 위에, 텍스트가 있는 동안 지우기 버튼 (×)과 함께. 스크롤되어 사라지지 않는다: 아래 내용이 자식 창 안에서 스크롤되며, 터치 드래그 스크롤 (`nv_imgui_touch_scroll`)을 유지한다 |
| 상자 안의 키 | Escape는 지우고 (떠나고); Enter는 떠난다. 상자를 편집하는 동안 모든 텍스트 필드처럼 단축키는 발동하지 않는다 |
| 패널마다 질의 하나 | 각 패널은 자체 텍스트를 가지며, 탭, 씬, 선택을 바꿔도 유지된다 (Unreal의 Details 검색이 다른 액터를 선택해도 남는 것처럼). 저장하지 않고 undo되지 않는다 |
| Scene 트리 | 맞는 노드는 모든 부모와 함께 보인다; 맞지 않는 부모는 회색이다. 필터가 켜진 동안 맞는 것의 부모는 열린다 (필터 없을 때의 열림 상태는 유지된다). 깊이 한도 (24)와 닫힌 큰 그룹은 거르는 동안 적용되지 않는다: 스트레스 체인 깊은 곳의 맞는 것은 부모 아래, 가장 가까운 24개로 잘리고 "..." 행과 함께 보인다. 맞는 것은 매 프레임이 아니라 질의나 씬이 바뀔 때 한 번 찾고 (앱 아레나에 노드당 비트 하나), 트리는 맞는 행을 최대 500개 보여 준 뒤 "and N more" |
| 다른 패널 | 행은 맞을 때 그려진다. 섹션 제목 (`igSeparatorText`, 접는 헤더)은 그 행 중 하나가 그려질 때만 그려지고, 제목 자체가 맞으면 섹션의 모든 행이 보인다. 설정이 아닌 텍스트 (도움말 줄, FPS 줄)는 빈 질의에서만 보인다 |
| 맞는 것 없음 | 패널이 "No match for '<query>'"를 보여 준다 |
| 하이라이트 | 보이는 각 라벨의 맞은 부분에 반투명 강조 상자 (Scene 트리 이름, 행 라벨, 팔레트 행) |
| Ctrl+F (데스크톱) | 마우스 포인터 아래 패널의 검색 상자, 아니면 오른쪽 도크의 현재 탭에 포커스를 준다. 단축키 표의 한 행이고 브라우저 (자체 찾기)로부터 가져온다 |
| 폰 | 같은 상자; 입력은 텍스트 에이전트 (`imgui.c`)를 거치므로, 탭하면 폰 키보드가 열린다 |
| 테스트 | Debug 빌드는 `_app_debug_search_rows(panel)` (지난 프레임에 그린 행)와 테스트가 `stringToUTF8`로 쓰는 고정 버퍼에서 질의를 읽는 `_app_debug_search_set(panel)`을 export한다 |

#### 패널 행을 쓰는 방법

패널은 두 호출로 그리므로, 제목이 첫 행을 기다릴 수 있다:

```c
search_section(&app->search, "Shadows");                      // remembered, not drawn yet
if (search_row(&app->search, "Shadow map resolution", NULL))  // draws the heading if pending
    igCombo_Str_arr("Shadow map resolution", ...);
if (search_row(&app->search, "Anti-aliasing", "msaa samples"))
    igCombo_Str_arr("Anti-aliasing", ...);
```

한 줄의 위젯 묶음 (Move, Rotate, Scale, Local, Snap)은 키워드가 그 모두를 부르는 행 하나다. `search_row`는 팔레트를
위해 행을 기록하기도 한다 (아래). 이것은 `AGENTS.md`의 규칙이 된다: 검색 가능한 패널의 모든 위젯은 `search_row`를
거친다.

### 명령 팔레트

| 주제 | 결정 |
|---|---|
| 열기 | 데스크톱: Ctrl+Shift+P (`shortcuts.md`에서 이것을 위해 비워 두었고, 브라우저로부터 가져옴), F1, **Edit > Command palette**. 폰: 상단 바의 Undo와 Play 사이 **Find** 버튼 (`layout.md`의 폰 상단 바가 그것을 얻는다) |
| 창 | 상단 바 아래 가운데의 모달 팝업: 데스크톱에서 폭 560 px, 폰에서 캔버스 폭에서 여백을 뺀 것; 열 때 포커스되는 상자와 최대 12개의 결과 행, 그 너머는 스크롤. 상단 바를 결코 덮지 않는다 |
| 키 | Up과 Down은 강조된 행을 옮기고 (돌아감), Page Up과 Page Down은 한 페이지씩, Enter는 그것을 실행, Escape는 닫는다. 마우스 클릭이나 탭은 행을 실행한다. 어디서 입력하든 상자로 간다 |
| 행 | 각 행: 왼쪽에 회색으로 종류 (Action, Node, Setting), 맞은 부분이 강조된 이름, 오른쪽에 단축키 (액션), 부모의 이름 (노드) 또는 패널과 섹션 (설정) |
| 액션 | 실행할 것이 있는 단축키 표의 모든 행, 더하기 팔레트만 제공하는 행 (키 없음): Show save, Reset..., Showcase / Stress 씬 보이기, Textures 탭 열기, 콘솔 지우기. 그것들은 표 하나다: `Shortcut`이 `Command`가 되고, 조합 키 없는 명령은 팔레트 전용이다. 지금 적용되지 않는 액션 (`when`)은 회색으로 마지막에 나열되고 실행할 수 없다 |
| 노드 | 보이는 씬의 모든 노드. Enter는 트리에서 클릭하듯 그것을 선택하고 Inspector를 연다; Shift+Enter는 공전 점도 그리로 옮긴다 (F). 스트레스 씬에서는 노드 결과가 최대 200개 나열되고 "and N more" |
| 설정 | 검색 가능한 패널이 가진 모든 행, `search_row`가 행을 기록하고 false를 돌려주어 아무것도 그리지 않는 수집 모드로 그려서 모은다. Inspector의 행은 현재 선택의 것이다. Enter는 행의 도크와 탭을 보여 주고 (숨겨져 있으면 도크를 열고) 행의 라벨을 그 패널의 검색 상자에 넣으므로, 설정이 거기서 첫 번째가 된다 |
| 빈 질의 | 팔레트에서 실행한 최근 액션 8개 (저장하지 않음), 그다음 표 순서의 모든 액션 |
| 재생 중, 팝업, 기즈모 | 팔레트는 단축키와 같은 규칙으로 열린다 (필드를 편집하는 중, 팝업이 열린 중, 기즈모를 끄는 중에는 아님). 스트레스 씬의 실행 중인 벤치마크 위로는 열리지 않는다 |
| 테스트 | `_app_debug_palette(n)` (0 열림, 1 결과 수, 2 강조 인덱스, 3 강조된 행의 종류)과 패널과 같은 질의 버퍼 |

```
+----------------------------- top bar ------------------------------+
|        +--------------------------------------------------+        |
|        | shad                                          [x] |        |
|        | Setting  Shadows                    View > Shadows |        |
|        | Setting  Shadow map resolution      View > Shadows |        |
|        | Action   Show save                                 |        |
|        +--------------------------------------------------+        |
```

### 변경

- **앱.**
  - `app/search.c` (새로 생김): 매처, 패널별 상자와 질의, `search_section`과 `search_row`, 수집 모드, Scene 트리의 맞음
    비트, 팔레트.
  - `app/ui.c`, `app/stress.c`, `app/textures.c`, `app/save.c` (View 탭 안의 그 섹션): `search_row`를 거치는 행, 섹션 맨
    위의 상자; 걸러진 트리.
  - `app/shortcuts.c`: `Shortcut`이 `Command`가 되고, 팔레트 전용 행; Ctrl+F, Ctrl+Shift+P, F1.
  - `app/ui_desktop.c`: Edit > Command palette, 설정 결과로 고르는 오른쪽 도크의 탭. `app/ui_phone.c`: Find 버튼, 설정
    결과로 고르는 패널 탭.
  - `app/app.h`: `App` 안의 `Search` 상태.
- **엔진.** 예상되는 것 없음; 하이라이트는 창 draw list를 쓴다.
- **문서.** `layout.md` (폰 Find 버튼), `shortcuts.md` (Ctrl+F, Ctrl+Shift+P, F1과 팔레트 전용 명령), `textures.md`,
  `stress.md`, `AGENTS.md` (`search_row` 규칙), README.

### 단계

1. **패널 검색:** 매처, 두 UI의 다섯 패널의 상자, 미뤄 그리는 제목, 부모가 있는 Scene 트리 (와 스트레스 씬의 깊은
   체인과 큰 그룹). 확인: 각 패널에서 여러 질의에 대한 맞는 행과 제목, 맞는 것 없음 텍스트, Escape가 지움, 입력하는
   동안 단축키가 발동하지 않음, 폰에서 상자 아래 터치 스크롤, 스트레스 씬의 트리가 느린 프레임 없이 체인 끝의 노드를
   찾음.
2. **명령 팔레트:** 명령 표, 노드, 수집 모드의 설정, 키, Edit 메뉴, 폰의 Find 버튼. 확인: 각 종류의 결과가 실행됨
   (액션, 선택된 노드, 필터가 설정된 채 탭에 보인 설정), 회색 액션은 실행되지 않음, Escape가 닫음, 최근 액션이 먼저
   옴, Ctrl+Shift+P가 브라우저에 닿지 않음.
3. **다듬기와 문서:** 하이라이트, Ctrl+F, F1, 위의 문서들.

모든 단계는 헤드리스 Chromium에서 Release와 Debug로 확인한다. 데스크톱은 1280×800에서 마우스와 키보드로, 폰은
390×664에서 터치로.

만든 결과로는, 세 단계가 함께 왔고 (상자, 팔레트, 다듬기가 한 파일을 나눈다), 단계들이 그다음 그것을 확인했다.
메모:

- 행은 세 호출을 거친다: `search_row` (사각형 오른쪽 끝에 라벨이 있는 위젯 하나: 라벨의 맞음은 위젯을 그린 뒤
  표시된다), `search_group` (여러 위젯이나 버튼: 표시 없음), `search_plain` (설정 아님: 빈 질의에서만). `search_match`는
  그리지 않고 문자열을 검사하며, 목록에 쓴다 (Textures 탭의 항목). 섹션 제목은 모든 행 텍스트의 일부라서, "shadows"는
  섹션 전체를, "autosave"는 그 두 그룹 모두를 보여 준다.
- 설정은 팔레트가 열린 동안 Inspector, View, (보이는 동안) Stress 패널을 화면 밖 창 안에서 수집 모드로 돌려서 모으므로,
  엉뚱한 ImGui 호출이 그릴 수 없다. Textures 탭은 아무것도 더하지 않는다: 그 목록의 행은 설정이 아니라 텍스처다.
- Textures: 상자는 목록 위에 있다; "In use only"는 보이는 채로 남는다. 고른 텍스처만 보여 주는 좁은 패널에는 상자가
  없다. 섹션과 항목은 이름 더하기 그룹으로 맞추고, 질의를 입력하는 동안 그룹이 열린다.
- Stress 탭: 통계도 `search_row`를 거친다; 그 표는 보이는 행을 안 뒤에 시작하므로, 제목이 결코 셀 안에 그려지지 않는다.
- Scene 트리의 표시는 질의, 노드 수, 씬이 바뀔 때, 그리고 30프레임마다 다시 만든다. 트리가 그리는 것 (24)보다 깊은
  경로는 "가장 가까운 24 단계" 대신 "... N more levels"와 그 아래 맞는 것을 평평하게, 최대 50개 보여 준다: 체인이 유일한
  깊은 구조이고, 평평한 목록이 맞는 것을 고르는 사람에게 필요한 것이다. 500행 상한은 부모를 포함해 그린 모든 행을
  세고, "and N more"는 그리지 않은 맞는 것을 센다.
- 팔레트는 세 종류를 (실행 가능, 단어 가운데 맞음, 이름 길이, 종류, 나열 순서)로 함께 순위를 매기므로, 짧은 설정이 같은
  단어의 더 긴 액션보다 먼저 올 수 있다. 비활성 액션은 마지막에 온다. 결과는 열린 동안 매 프레임 정렬된다: 노드는 찾는
  대로 최고 200개만 유지하므로, 16 000개의 그리드 큐브도 한 번의 패스로 든다.
- 팔레트는 Escape와 그 밖의 클릭이나 탭에서 닫힌다. `request` 플래그 (`App.search.palette_request`,
  `App.request_reset`)로 열리므로 단축키, Edit 메뉴 항목, 폰의 Find 버튼이 같은 일을 한다. Reset은 두 UI 모두 상단 바의
  창에서 확인을 연다.
- 빌드 라벨은 팝업 위의 foreground draw list에 그리므로, 이제 어떤 팝업이든 열린 동안은 기다린다.
- 폰의 옆 버튼은 64 px, Find는 52 px (`layout.md`). Reset 확인의 텍스트는 폰 화면보다 넓다; 전에도 그랬다.
- `korean.md` 이후로 행의 매칭 텍스트에 라벨과 섹션의 한국어도 들어가므로, 어느 언어의 질의든 그것을 찾는다
  (`candidate_text`).
- Ctrl+F는 바인딩된 다른 키처럼 브라우저로부터 가져온다; 상자를 편집하는 동안 엔진은 이미 모든 키를 브라우저로부터
  막으므로 (`WantCaptureKeyboard`), 거기서 Ctrl+F는 아무것도 하지 않는다.
- 빠른 입력 (지연 없는 Playwright `keyboard.type`)은 SwiftShader의 적은 초당 프레임에서 ImGui가 입력 큐를 조금씩
  흘려보내므로 여러 프레임에 걸쳐 도착한다: 테스트는 질의가 가라앉기를 기다리거나 지연을 두고 입력한다.
- Debug 빌드는 `_app_debug_search_buffer`, `_app_debug_search_set(panel)` (패널 5는 팔레트), `_app_debug_search_rows(panel)`,
  `_app_debug_search_query(panel, k)`, `_app_debug_palette(n)` (0 열림, 1 결과, 2 강조, 3 강조된 행의 종류, 4 모은 설정,
  5 최근 액션), `_app_debug_palette_result(i, k)`, `_app_debug_palette_query(k)`, `_app_debug_set_chain(links)`를
  export한다.
- Debug (데스크톱 1280×800 키보드와 마우스, 폰 390×664 터치)와 Release (debug export 없음: 입력한 질의와 스크린샷만)에서
  확인: 여러 질의에 대한 각 패널의 걸러진 행, 맞는 것 없음 텍스트, Escape 지우기, 입력하는 동안 기즈모나 단축키가
  발동하지 않음, 1000-링크 체인 안의 노드를 찾고 500행으로 제한되는 스트레스 트리, 팔레트의 세 종류의 결과와 Enter
  동작 (Play가 실행되고 Space가 다시 멈춤, 선택된 노드, Shift+Enter 포커스, 필터와 함께 보인 설정), Enter를 거부하는
  비활성 액션, 먼저 오는 최근 액션, "more" 행과 함께 200개의 노드 결과, 브라우저로부터 막힌 Ctrl+Shift+P, F1, Ctrl+F,
  포인터 아래 패널이나 오른쪽 도크에 닿는 Ctrl+F, 폰의 Find 버튼, 탭한 결과, 팔레트에서 연 Reset 확인, 고정된 상자 아래
  패널의 터치 스크롤.

### 범위 밖

- 퍼지 매칭 (위), 값 안 검색 (노드의 위치, 머티리얼의 색)과 정규 표현식.
- Console 메시지 검색.
- 새로고침을 넘어 질의나 최근 명령 기억하기.
- 아직 없는 명령 (노드 추가, 삭제, 복제, 이름 바꾸기).

## English

Status: implemented (2026-10-01). Changes to this spec are agreed first.

### Goal

Find things by typing, as in Unreal Editor, in two ways:

1. **Panel search boxes.** A box at the top of every editor panel except the Console (the Scene
   tree, Inspector, View, Textures and Stress tabs) filters that panel as you type: the Scene tree
   keeps the matching nodes and their parents, the other panels keep the matching rows under their
   section headings. This works like the World Outliner and the Details panel's "Search Details".
2. **A command palette.** One box (Ctrl+Shift+P on the desktop) searches everything at once:
   actions (the shortcut table and a few more), the nodes of the shown scene, and every setting in
   the panels. Enter runs an action, selects a node, or jumps to a setting.

The Console keeps its own level filters (`console.md`) and gets no box.

### Approaches

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **Our matcher and our widgets** (recommended) | One matching function (words, case-insensitive substrings, below) used by every box; panel rows go through a small helper that decides whether to draw them; the palette is a modal popup listing results | Plain C, a few hundred lines in a new `app/search.c`. Same rules everywhere | Every panel row has to go through the helper (a rule for new UI code) |
| Dear ImGui's `ImGuiTextFilter` | ImGui's built-in filter: a text box and `PassFilter(text)` | Already in the build | Its syntax is comma-separated with `-` exclusions, not words separated by spaces as Unreal and most search boxes behave. It filters strings, not panels: the deferred headings, the tree's parents and the palette are ours anyway. Not used |
| Fuzzy matching: fts_fuzzy_match (C/C++ single header, public domain or MIT, Forrest Smith) or fzy's algorithm (C, MIT) | Scores letters in order with gaps ("tgsh" finds "Toggle shadows") | Good for a palette with hundreds of entries | Rankings are hard to predict; long settings lists match too much. Not needed at our size; it can replace the palette's scoring later without changing anything else |
| A web library (Fuse.js, JavaScript, Apache-2.0) | Fuzzy search in the page | | Results would cross the JavaScript boundary every keystroke, for strings that live in WebAssembly. No |

Recommendation: our matcher and widgets. No third-party library.

### Matching

- The query is split at spaces into words; a candidate matches when **every** word is a substring
  of it, ignoring ASCII case (`shad res` matches "Shadow map resolution"). Bytes outside ASCII
  compare as they are, so a UTF-8 node name matches its own spelling.
- What a candidate's text is: a node's name; a panel row's label plus its section's heading plus
  any extra keywords the row gives (so `msaa` finds the Anti-aliasing combo, and `shadows` shows
  the whole Shadows section); an action's name plus its group.
- Ranking (the palette only; panels keep their order): a word matching the start of a word in the
  candidate ranks above one in its middle; then shorter candidates first; then the order they were
  listed in.
- An empty query matches everything.

### Panel search boxes

| Topic | Decision |
|---|---|
| Panels | Scene, Inspector, View, Textures and Stress, on both UIs. Not the Console |
| The box | An input with the hint "Search" across the panel's width, above its content, with a clear button (×) while it has text. It does not scroll away: the content below scrolls in a child window, which keeps the touch-drag scroll (`nv_imgui_touch_scroll`) |
| Keys in the box | Escape clears it (and leaves it); Enter leaves it. While the box is being edited shortcuts do not fire, as for every text field |
| One query per panel | Each panel keeps its own text, kept when switching tabs, scenes or selections (as Unreal's Details search stays when selecting another actor). Not saved, not undoable |
| Scene tree | Matching nodes are shown with all their parents; parents that do not match are greyed. Parents of matches are opened while the filter is on (their open state without a filter is kept). The depth limit (24) and the closed big groups do not apply while filtering: a match deep in the stress chain is shown under its parents, cut to the 24 nearest with a "..." row. Matches are found once when the query or the scene changes (a bit per node in the app arena), not every frame, and the tree shows at most 500 matching rows, then "and N more" |
| Other panels | A row is drawn when it matches. A section heading (`igSeparatorText`, a collapsing header) is drawn only when one of its rows is, and every row of a section shows when the heading itself matches. Text that is not a setting (help lines, the FPS line) is shown only with an empty query |
| No match | The panel shows "No match for '<query>'" |
| Highlight | The matched part of each shown label gets a translucent accent box (Scene tree names, row labels, palette rows) |
| Ctrl+F (desktop) | Focuses the search box of the panel under the mouse pointer, or else of the right dock's current tab. It is a row in the shortcut table and claimed from the browser (its own Find) |
| Phone | Same boxes; typing goes through the text agent (`imgui.c`), so the phone keyboard opens on tap |
| Tests | Debug builds export `_app_debug_search_rows(panel)` (rows drawn last frame) and `_app_debug_search_set(panel)`, which reads the query from a fixed buffer the test writes with `stringToUTF8` |

#### How a panel row is written

Panels draw through two calls, so headings can wait for their first row:

```c
search_section(&app->search, "Shadows");                      // remembered, not drawn yet
if (search_row(&app->search, "Shadow map resolution", NULL))  // draws the heading if pending
    igCombo_Str_arr("Shadow map resolution", ...);
if (search_row(&app->search, "Anti-aliasing", "msaa samples"))
    igCombo_Str_arr("Anti-aliasing", ...);
```

A group of widgets on one line (Move, Rotate, Scale, Local, Snap) is one row whose keywords name
them all. `search_row` also records the row for the palette (below). This becomes a rule in
`AGENTS.md`: every widget in a searchable panel goes through `search_row`.

### Command palette

| Topic | Decision |
|---|---|
| Opening | Desktop: Ctrl+Shift+P (kept free for it in `shortcuts.md`, and claimed from the browser), F1, and **Edit > Command palette**. Phone: a **Find** button in the top bar between Undo and Play (the phone top bar in `layout.md` gains it) |
| Window | A modal popup centered under the top bar: 560 px wide on the desktop, the canvas's width less the margins on the phone; a box focused on open and up to 12 result rows, scrolling beyond that. It never covers the top bar |
| Keys | Up and Down move the highlighted row (wrapping), Page Up and Page Down by a page, Enter runs it, Escape closes. A mouse click or a tap runs a row. Typing anywhere goes to the box |
| Rows | Each row: its kind (Action, Node, Setting) greyed on the left, its name with the match highlighted, and on the right the shortcut keys (actions), the parent's name (nodes) or the panel and section (settings) |
| Actions | Every row of the shortcut table that has something to run, plus rows that only the palette offers (no keys): Show save, Reset..., Show the Showcase / Stress scene, Open the Textures tab, Clear the console. They are one table: `Shortcut` becomes `Command`, and a command without a chord is palette-only. Actions that do not apply now (`when`) are listed last, greyed, and cannot run |
| Nodes | Every node of the shown scene. Enter selects it and opens the Inspector, as a click in the tree does; Shift+Enter also moves the orbit point to it (F). In the stress scene at most 200 node results are listed, with "and N more" |
| Settings | Every row the searchable panels have, collected by drawing them in a collect mode where `search_row` records the row and returns false, so nothing is drawn. The Inspector's rows are those of the current selection. Enter shows the row's dock and tab (opening the dock if hidden) and puts the row's label into that panel's search box, so the setting is the first thing there |
| Empty query | The last 8 actions run from the palette (not saved), then every action in table order |
| While playing, popups, gizmo | The palette opens under the same rules as a shortcut (not while a field is edited, a popup is open or the gizmo is dragged). It does not open over the stress scene's running benchmark |
| Tests | `_app_debug_palette(n)` (0 open, 1 result count, 2 highlighted index, 3 kind of the highlighted row) and the same query buffer as the panels |

```
+----------------------------- top bar ------------------------------+
|        +--------------------------------------------------+        |
|        | shad                                          [x] |        |
|        | Setting  Shadows                    View > Shadows |        |
|        | Setting  Shadow map resolution      View > Shadows |        |
|        | Action   Show save                                 |        |
|        +--------------------------------------------------+        |
```

### Changes

- **App.**
  - `app/search.c` (new): the matcher, the per-panel boxes and queries, `search_section` and
    `search_row`, the collect mode, the Scene tree's match bits, the palette.
  - `app/ui.c`, `app/stress.c`, `app/textures.c`, `app/save.c` (its section in the View tab): rows
    through `search_row`, boxes at the top of the sections; the tree filtered.
  - `app/shortcuts.c`: `Shortcut` becomes `Command`, with palette-only rows; Ctrl+F, Ctrl+Shift+P
    and F1.
  - `app/ui_desktop.c`: Edit > Command palette, the right dock's tab chosen by a setting result.
    `app/ui_phone.c`: the Find button, the panel tab chosen by a setting result.
  - `app/app.h`: `Search` state in `App`.
- **Engine.** Nothing expected; the highlight uses the window draw list.
- **Docs.** `layout.md` (the phone Find button), `shortcuts.md` (Ctrl+F, Ctrl+Shift+P, F1 and the
  palette-only commands), `textures.md`, `stress.md`, `AGENTS.md` (the `search_row` rule) and
  README.

### Phases

1. **Panel search:** the matcher, the boxes on the five panels in both UIs, the deferred headings,
   the Scene tree with parents (and the stress scene's deep chain and big groups). Checked:
   matching rows and headings for several queries in each panel, no-match text, Escape clears,
   shortcuts do not fire while typing, touch scroll under the box on the phone, the stress scene's
   tree finds a node at the chain's end without a slow frame.
2. **Command palette:** the command table, nodes, settings by collect mode, the keys, Edit menu and
   the phone's Find button. Checked: each kind of result runs (an action, a node selected, a
   setting shown in its tab with the filter set), greyed actions do not run, Escape closes,
   recent actions come first, Ctrl+Shift+P does not reach the browser.
3. **Polish and docs:** the highlight, Ctrl+F, F1, the documents above.

Every phase is checked in Release and Debug in headless Chromium, desktop at 1280×800 with the
mouse and keyboard, phone at 390×664 with touch.

As built, the three phases arrived together (the boxes, the palette and the polish share one file),
and the phases then checked them. Notes:

- Rows go through three calls: `search_row` (one widget with its label at the right end of its
  rectangle: the label's match is marked after the widget is drawn), `search_group` (several widgets
  or a button: no mark) and `search_plain` (no setting: only with an empty query). `search_match`
  tests a string without drawing, for lists (the Textures tab's entries). The section heading is part
  of every row's text, so "shadows" shows the whole section and "autosave" both of its groups.
- The settings are collected while the palette is open, by running the Inspector, View and (while
  shown) Stress panels in collect mode inside an off-screen window, so a stray ImGui call could
  not draw. The Textures tab adds none: its list rows are textures, not settings.
- Textures: the box is above the list; "In use only" stays visible. A narrow panel that shows only
  the picked texture has no box. Sections and entries match by name plus group, and a group
  opens while a query is typed.
- Stress tab: the stats go through `search_row` too; their table starts after the visible rows are
  known, so the heading is never drawn inside a cell.
- The Scene tree's marks are made again when the query, the node count or the scene changes, and
  every 30 frames. A path deeper than the tree draws (24) shows "... N more levels" and then the
  matches under it flat, at most 50, instead of "the 24 nearest levels": the chain is the only deep
  structure, and a flat list is what a person picking a match needs. The 500-row cap counts every
  row drawn, parents included, and "and N more" counts matches not drawn.
- The palette ranks all three kinds together by (can run, words matched mid-word, length of the
  name, kind, listed order), so a short setting can come before a longer action of the same words.
  Disabled actions come last. Results are sorted each frame while it is open: the nodes are kept
  to the best 200 as they are found, so 16 000 grid cubes cost one pass.
- The palette closes on Escape and on a click or tap outside it. It opens from `request` flags
  (`App.search.palette_request`, `App.request_reset`) so the shortcut, the Edit menu item and the
  phone's Find button do the same. Reset opens its confirmation in the top bar's window on both UIs.
- The build label draws on the foreground draw list, above popups, so it now waits while any popup is
  open.
- The phone's side buttons are 64 px and Find 52 px (`layout.md`). The Reset confirmation's text is
  wider than a phone screen; it was before too.
- Since `korean.md` a row's text for matching has the Korean of its label and section too, so a query in
  either language finds it (`candidate_text`).
- Ctrl+F is taken from the browser like any bound key; while a box is being edited the engine
  already keeps every key from the browser (`WantCaptureKeyboard`), so Ctrl+F there does nothing.
- Typing fast (Playwright's `keyboard.type` without a delay) under SwiftShader's few frames per
  second arrives over several frames, since ImGui trickles its input queue: tests wait for the
  query to settle, or type with a delay.
- Debug builds export `_app_debug_search_buffer`, `_app_debug_search_set(panel)` (panel 5 is the
  palette), `_app_debug_search_rows(panel)`, `_app_debug_search_query(panel, k)`,
  `_app_debug_palette(n)` (0 open, 1 results, 2 highlight, 3 kind of the highlighted row, 4 settings
  collected, 5 recent actions), `_app_debug_palette_result(i, k)`, `_app_debug_palette_query(k)` and
  `_app_debug_set_chain(links)`.
- Checked in Debug (desktop at 1280×800 with keyboard and mouse, phone at 390×664 with touch) and
  Release (no debug exports there: typed queries and screenshots only): filtered rows in each panel
  for several queries, no-match text, Escape clearing, no gizmo or shortcut firing while typing,
  the stress tree finding nodes inside the 1000-link chain and capped at 500 rows, the palette's
  three kinds of result and their Enter actions (Play ran and Space stops again, a node selected,
  Shift+Enter focusing, a setting shown with its filter), a disabled action refusing Enter, the
  recent action first, 200 node results with the "more" row, Ctrl+Shift+P, F1 and Ctrl+F kept from
  the browser, Ctrl+F reaching the panel under the pointer or the right dock's, the phone's Find
  button, a tapped result, the Reset confirmation from the palette, and the panel's touch scrolling
  under the pinned box.

### Out of scope

- Fuzzy matching (above), searching inside values (a node's position, a material's color) and
  regular expressions.
- Searching the Console's messages.
- Remembering queries or recent commands across reloads.
- Commands that do not exist yet (add, delete, duplicate, rename nodes).

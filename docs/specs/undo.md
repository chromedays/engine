# Undo and redo spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-09-28). 이 스펙의 변경은 먼저 합의한다.

### 목표

쇼케이스에서의 편집을 되돌리고 다시 한다: 기즈모 드래그, Inspector 필드, 체크박스, 캐릭터에 고른 클립. 사용자 액션 하나는
몇 프레임이 걸렸든 한 단계다.

### 접근법

| 접근법 | 방법 | 장점 | 단점 |
|---|---|---|---|
| **유휴 시 비교로 찾는 범위별 스냅숏** (선택) | 위젯이나 기즈모가 쓰이지 않는 프레임마다 앱이 저장 코드 (`app/save.c`)로 몇 범위의 undo 가능한 상태를 쓰고 마지막으로 커밋한 바이트와 비교한다. 차이는 전후 바이트를 담은 단계가 된다 | 위젯마다의 코드가 없다: 지금과 앞으로의 모든 편집 경로가 잡힌다. 드래그 하나가 한 단계다. 저장 형식의 writer와 reader를 재사용한다 | 앱이 매 프레임 움직이는 값은 스냅숏에서 빼야 한다, 아니면 모든 프레임이 편집처럼 보인다 |
| 편집 종류마다 명령 하나 | 모든 위젯과 기즈모가 바꾸는 것을 기록한다 | 정확하다 | 모든 편집 위치에 코드가 있고, 빠뜨린 위치는 조용한 구멍이다 |
| 상태 전체 스냅숏 | 각 단계 전후의 저장 전체 | 가장 단순 | 편집 하나를 되돌리면 그 뒤로 움직인 모든 것 (걷는 캐릭터, 행성의 회전)도 되돌린다 |
| 라이브러리 | 맞는 것을 찾지 못했다: undo는 앱 자체의 데이터에 관한 것이다 | | |

### undo 가능한 것

쇼케이스만, 그리고 그 내용만. 스트레스 씬은 설정에서 다시 만들어지며 undo되지 않는다 (그것이 보이는 동안 Undo와 Redo는
꺼진다).

| 범위 | 담는 것 (저장의 필드) | 앱이 움직이므로 빼는 것 |
|---|---|---|
| **노드** (선택된 노드) | `NAME`, `POS`, `ROT`, `SCL`, `COLR`, `ATCH`, `CFOV`, `LCOL`, `LINT` | 공전 카메라의 트랜스폼. (Edit와 Play 모드, `play.md` 전까지는 look 타깃의 트랜스폼, 행성과 달의 회전, 걷는 캐릭터의 트랜스폼도; Edit 모드에서는 그것들이 더 이상 스스로 움직이지 않으므로 다른 노드처럼 undo 가능하다) |
| **캐릭터** | `CLIP`, `SPED`, `FADE`, `BLND`, `BLDW`, `RMOT`, `TURN`, `LOOK`, `SWRD` | 클립의 시간, 저장도 더 이상 쓰지 않는다 |
| **씬 설정** | 공전 속도 (`ORBS`, undo만 쓰는 태그: 저장의 `PLNT`는 각도도 담는다), 뼈 표시 | 공전 각도 (재생 중에만 돈다) |
| 재생 중 | 없음: undo는 꺼지고, Stop이 씬을 마지막 커밋으로 되돌린다 | |

undo되지 않는 것: 카메라 뷰 (yaw, pitch, 거리, 팬, 따라가기), 선택, 에디터 설정 (기즈모 모드, 자동 저장), 스트레스 씬의
모든 것. 텍스트 필드는 입력하는 동안 자체 undo를 유지한다 (Dear ImGui의 것); 끝난 편집은 그다음 한 단계다.

### 결정

| 주제 | 결정 |
|---|---|
| 단계를 찍는 때 | 변경 뒤 유휴인 첫 프레임: 활성 ImGui 아이템 없음, 쓰이는 기즈모 없음, 열린 팝업 없음. 범위들의 바이트를 커밋된 것과 비교한다; 다른 각 범위가 단계가 되고 (전은 커밋된 바이트, 후는 현재 바이트), 그다음 커밋된 바이트가 된다 |
| 선택 | 다른 노드를 선택하면 단계 없이 그 바이트를 그대로 커밋한다. 선택된 노드만 편집될 수 있으므로 그것만 비교한다. 그 필드 중 어느 것이 driven인지의 변화 (루트 모션을 켜거나 끔)도 마찬가지라서, 그 변화가 편집으로 오인되지 않는다 |
| Undo / redo | Undo는 단계의 전 바이트를 저장 리더로 적용하며, 있는 필드만 건드린다; redo는 후 바이트를 적용한다. 적용한 바이트가 커밋된 것이 되므로, undo가 새 편집으로 기록되지 않는다. undo 뒤의 새 단계는 다시 할 수 있었던 단계를 버린다 |
| Undo가 바꾼 것을 보여 줌 | 노드 단계는 그 노드를 선택하므로, Inspector (와 따라가는 카메라)가 변화를 보여 준다 |
| 이력 | 메모리에 128 단계; 가득 차면 가장 오래된 것을 버린다. 저장하지 않는다: 새로고침은 이력 없이 시작한다. undo 뒤의 상태는 평소처럼 자동 저장된다 |
| 단계 크기 | 전과 후는 각각 최대 1 KB (노드는 약 120바이트); 들어가지 않는 범위는 기록되지 않고 Debug에서 assert한다 |
| 키 | Ctrl+Z undo; Ctrl+Shift+Z와 Ctrl+Y redo (macOS에서 Cmd는 Ctrl로 센다). 텍스트 필드를 편집하는 동안은 아님. `shortcuts.md` 이후로 데스크톱 UI만: 키는 그 표에 살고 `Undo.request`로 요청한다 |
| UI | Undo와 Redo, 할 것이 없을 때, 재생 중에, 스트레스 씬이 보이는 동안 비활성 (`layout.md`): 데스크톱 UI에서는 Edit 메뉴, 각 항목에 바꿀 것을 라벨로 달고 ("Undo: character Position") 단축키를 보여 줌; 폰 UI에서는 상단 바의 왼쪽과 오른쪽 끝의 버튼 두 개, 라벨 없이. (원래는 패널 탭 위의 줄.) 버튼은 요청만 한다; undo는 프레임 끝에, 진행 중인 편집이 (있다면) 단계가 된 뒤에 일어난다 |
| 테스트 | Debug 빌드는 `Module._app_debug_undo_steps()`와 `Module._app_debug_undo_done()`을 노출한다 |
| 라벨 | 처음으로 다른 필드에서 가져온다: Position, Rotation, Scale, Name, Color, Joint, Field of view, Light color, Intensity, Clip, Speed, Fade, Blend, Root motion, Turn, Look at, Sword, Planet orbit, Show bones |
| 서드파티 | 없음 |

### 앱 변경

- `app/save.c`에 자동 저장과 공유하는 범위별 writer와 reader가 생긴다: 노드의 필드, 캐릭터의 것, 씬 설정의 것, 각각 driven
  값을 빼는 플래그와 함께. 자동 저장은 지금 쓰는 모든 것을 계속 쓴다.
- `app/undo.c`: 단계 링 (영구 아레나의 고정 슬롯), 유휴 비교, undo, redo, 라벨, Undo/Redo 줄.
- `app/ui.c`: 탭 위의 줄과 키.

노드는 저장에서처럼 트리 안의 경로로 식별한다. 에디터는 아직 노드를 추가하거나 제거할 수 없으므로 경로가 유효하게 남는다;
그럴 수 있게 되면 단계에 안정된 노드 id와 노드를 만들고 제거하는 단계가 필요할 것이다.

### 단계

1. **단계:** 범위 writer, 유휴 비교, 링, 키가 있는 undo와 redo. 확인: 기즈모 드래그가 한 단계, undo가 노드를 되돌리고
   redo가 다시 옮김; 슬라이더 드래그가 한 단계; undo 뒤의 새 편집이 redo 단계를 버림.
2. **UI:** 라벨이 있는 Undo/Redo 줄, 바뀐 노드 선택, 스트레스 씬에서 꺼짐, 폰 폭에서의 터치 배치.
3. **경계 사례와 문서:** 걷는 캐릭터 (루트 모션 켜짐)가 스스로 단계를 만들지 않음; 이름 입력은 필드를 떠난 뒤 한 단계;
   진행 중인 점프나 크로스페이드; 130번의 편집이 마지막 128개를 유지; undo 뒤의 새로고침이 되돌린 상태를 유지;
   `AGENTS.md`와 README.

모든 단계는 헤드리스 Chromium에서 Release와 Debug로, 데스크톱 크기의 마우스와 폰 크기의 터치로 확인한다.

## English

Status: implemented (2026-09-28). Changes to this spec are agreed first.

### Goal

Take back an edit in the showcase, and redo it: a gizmo drag, an Inspector field, a checkbox, a
clip picked for the character. One user action is one step, however many frames it lasted.

### Approaches

| Approach | How | Pros | Cons |
|---|---|---|---|
| **Scoped snapshots, found by comparing when idle** (chosen) | Each frame no widget or gizmo is in use, the app writes the undoable state of a few scopes with the save code (`app/save.c`) and compares it to the last committed bytes. A difference becomes a step holding the bytes before and after | No code per widget: every edit path, present and future, is caught. One drag is one step. Reuses the save format's writer and reader | Values the app drives every frame must be left out of the snapshots, or every frame would look like an edit |
| Commands, one per kind of edit | Every widget and the gizmo record what they change | Exact | Code at every edit site, and a missed site is a silent gap |
| Whole-state snapshots | The whole save before and after each step | Simplest | Undoing one edit would also put back everything that moved since (a walking character, the planet's spin) |
| A library | None found that fits: undo is about the app's own data | | |

### What is undoable

Only the showcase, and only its content. The stress scene is rebuilt from its settings and is not
undoable (Undo and Redo are off while it is shown).

| Scope | Holds (the save's fields) | Left out, because the app drives them |
|---|---|---|
| **Node** (the selected node) | `NAME`, `POS`, `ROT`, `SCL`, `COLR`, `ATCH`, `CFOV`, `LCOL`, `LINT` | the orbit camera's transform. (Until Edit and Play modes, `play.md`, also the look target's transform, the planet's and the moon's spin, and a walking character's transform; in Edit mode those no longer move by themselves, so they are undoable like any node) |
| **Character** | `CLIP`, `SPED`, `FADE`, `BLND`, `BLDW`, `RMOT`, `TURN`, `LOOK`, `SWRD` | the clip's time, which the save no longer writes either |
| **Scene settings** | orbit speed (`ORBS`, a tag only undo uses: the save's `PLNT` holds the angle too), show bones | the orbit angle (it only runs while playing) |
| While playing | Nothing: undo is off, and Stop puts the scene back to the last commit | |

Not undoable: the camera view (yaw, pitch, distance, pan, follow), the selection, the editor
settings (gizmo mode, autosave), anything in the stress scene. Text fields keep their own undo
while they are being typed in (Dear ImGui's); the finished edit is then one step.

### Decisions

| Topic | Decision |
|---|---|
| When a step is taken | At the first frame that is idle after a change: no ImGui item active, no gizmo in use, no popup open. The scopes' bytes are compared with the committed ones; each scope that differs becomes a step (committed bytes before, current bytes after), then becomes the committed bytes |
| Selection | Selecting another node commits its bytes as they are, without a step. Only the selected node is compared, since only it can be edited. So does a change in which of its fields are driven (root motion turned on or off), so that change is not mistaken for an edit |
| Undo / redo | Undo applies the step's before-bytes through the save reader, which only touches the fields present; redo applies the after-bytes. The applied bytes become the committed ones, so an undo is not recorded as a new edit. A new step after an undo drops the steps that could have been redone |
| Undo shows what it changed | A node step selects its node, so the Inspector (and a following camera) show the change |
| History | In memory, 128 steps; the oldest is dropped when full. It is not saved: a reload starts with no history. The state after an undo is autosaved as usual |
| Step size | Before and after are at most 1 KB each (a node is about 120 bytes); a scope that does not fit is not recorded and asserts in Debug |
| Keys | Ctrl+Z undo; Ctrl+Shift+Z and Ctrl+Y redo (Cmd counts as Ctrl on macOS). Not while a text field is being edited. Desktop UI only since `shortcuts.md`: the keys live in its table and ask through `Undo.request` |
| UI | Undo and Redo, disabled when there is nothing to do, while playing, or while the stress scene is shown (`layout.md`): on the desktop UI the Edit menu, each item labeled with what it would change ("Undo: character Position") and showing its shortcut; on the phone UI two buttons at the top bar's left and right ends, without the labels. (Originally a row above the panel's tabs.) The buttons only ask; the undo happens at the frame's end, after the edit in progress (if any) has become a step |
| Tests | Debug builds expose `Module._app_debug_undo_steps()` and `Module._app_debug_undo_done()` |
| Labels | Taken from the first field that differs: Position, Rotation, Scale, Name, Color, Joint, Field of view, Light color, Intensity, Clip, Speed, Fade, Blend, Root motion, Turn, Look at, Sword, Planet orbit, Show bones |
| Third-party | None |

### App changes

- `app/save.c` gains scope writers and readers shared with the autosave: a node's fields, the
  character's, the scene settings', each with a flag that leaves out driven values. The autosave
  keeps writing everything it writes today.
- `app/undo.c`: the step ring (fixed slots from the permanent arena), the idle comparison, undo,
  redo, labels, and the Undo/Redo row.
- `app/ui.c`: the row above the tabs, and the keys.

Nodes are identified by their path in the tree, as in the save. The editor cannot add or remove
nodes yet, so paths stay valid; when it can, steps will need stable node ids and steps for
creating and removing nodes.

### Phases

1. **Steps:** the scope writers, the idle comparison, the ring, undo and redo with the keys. Checked:
   a gizmo drag is one step, undo puts the node back and redo moves it again; a slider drag is one
   step; a new edit after an undo drops the redo steps.
2. **UI:** the Undo/Redo row with labels, selecting the changed node, off in the stress scene, the
   touch layout at phone width.
3. **Edge cases and docs:** a walking character (root motion on) makes no steps by itself; typing a
   name is one step after the field is left; a jump or crossfade in progress; 130 edits keep the
   last 128; undo followed by a reload keeps the undone state; `AGENTS.md` and README.

Every phase is checked in Release and Debug in headless Chromium, with the mouse at desktop size
and with touch at phone size.

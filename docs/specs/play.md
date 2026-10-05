# Edit and Play modes spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-09-28). 이 스펙의 변경은 먼저 합의한다.

### 목표

Unity, Unreal, Godot처럼 쇼케이스를 편집하는 것과 실행하는 것을 나눈다.

- **Edit 모드:** 아무것도 씬을 스스로 움직이지 않는다. 화면에 보이는 것이 저장되고 undo되는 것이다.
- **Play 모드:** 씬이 돈다 (행성이 돌고, 캐릭터가 걷고 뛰고, 머리가 타깃을 따라간다). Stop을 누르면 씬이 Play를
  누른 때와 정확히 같게 돌아간다.

지금 쇼케이스는 편집하는 동안 항상 돌므로, undo와 자동 저장이 샌드박스가 매 프레임 바꾸는 값을 빼야 하고, 움직이는 것
(걷는 캐릭터)에 대한 편집은 undo할 수 없다. 모드를 나누면 둘 다 편집 상태만 본다.

### 다른 엔진은 어떻게 하는가

| 엔진 | Play | Stop | 재생 중 편집 |
|---|---|---|---|
| Unity | 열린 씬을 실행 | Play 전의 씬을 복원 (직렬화해 두었음) | 허용, 눈에 띄게 색조가 들고, Stop에서 사라짐 |
| Unreal (Play In Editor) | 월드의 복사본을 실행 | 복사본을 버림 | 복사본에 허용, Stop에서 사라짐 |
| Godot | 별도 프로세스에서 씬을 실행 | 그 프로세스를 끝냄 | 실행 중인 게임에서만 |

우리는 Unity를 따른다: 실행되는 씬은 편집된 씬이고, Stop은 스냅숏에서 그것을 복원한다. 스냅숏은 이미 가진 저장
형식 (`docs/specs/save.md`)이므로, Stop이 복원하는 것은 자동 저장이 유지하는 것과 정확히 같다. 월드 전체를
복사하려면 (Unreal) `NvScene` 밖에 사는 애니메이터, 머티리얼, 샌드박스 상태도 복사해야 한다.

해당하는 서드파티 라이브러리는 없다.

### 각 모드에서 도는 것

| | Edit | Play |
|---|---|---|
| 행성과 달의 회전 | 지정한 회전으로 정지 | 지정한 회전 위에 더해 돈다 (회전이 더 이상 그것을 대신하지 않는다) |
| 캐릭터 애니메이션 | 미리보기로 제자리에서 재생: 포즈는 움직이고 노드는 움직이지 않는다 | 루트 모션이 켜져 있으면 그것과 함께 재생 |
| 루트 모션, 회전 | 꺼짐 (제자리 클립이 재생) | Root motion 상자가 체크되어 있으면 켜짐 |
| look 타깃 | 놓인 곳에 머물고, 다른 노드처럼 옮길 수 있다 | 지금처럼 머리 앞에서 훑는다 |
| 점프 | 꺼짐 | 켜짐 |
| 크로스페이드와 블렌드 | 켜짐 (미리보기의 일부) | 켜짐 |
| 공전 카메라, picking, 기즈모 | 켜짐 | 켜짐 |
| 스트레스 씬 | 영향 없음: 벤치마크는 항상 돌고, 그것이 보이는 동안 Play 버튼은 숨는다 | |

Play 시간은 Play에서 0으로 시작하므로, 실행은 항상 같은 방식으로 시작한다: 클립은 처음부터, 행성은 지정한
회전부터.

### 결정

| 주제 | 결정 |
|---|---|
| 조작 | 화면 위쪽 가운데, 두 UI의 상단 바 안의 **Play** / **Stop** 버튼 (`layout.md`; 원래는 패널 탭 위 줄의 시작). 단축키: 데스크톱 UI에서 어떤 필드도 키보드를 잡고 있지 않은 동안 Space (Ctrl+P는 브라우저에서 인쇄한다) |
| 모드 보이기 | 재생 중에는 패널에 색조가 들고 뷰포트의 빌드 라벨이 "... · Playing"이라고 읽힌다; 안내 ("Playing: edits are lost on Stop.")가 편집이 Stop에서 사라진다고 말한다: 데스크톱 상단 바에서는 Play 버튼 옆에, 폰에서는 패널 맨 위에 (줄바꿈되어), 재생 중에는 탭을 한 줄 아래로 민다 |
| 테스트 | Debug 빌드는 `Module._sandbox_debug_save_crc()`를 노출한다, 상태가 지금 쓸 저장의 CRC-32; Play 전과 Stop 뒤의 값이 같으면 Stop이 씬을 복원한 것이다 |
| Play | 스냅숏을 찍는다: 씬의 저장 바이트 (`save_write`). Play 시간은 0에서 시작한다 |
| Stop | 저장 리더로 스냅숏의 씬 부분 (노드, 캐릭터 설정, 씬 설정)을 복원하므로, 진행 중인 점프, 크로스페이드, 걷기가 씬이 원래대로인 채로 끝난다. 카메라 뷰와 선택은 복원하지 않는다: 씬 내용이 아니다 |
| 재생 중 편집 | 허용 (기즈모와 Inspector가 동작), Unity처럼 Stop에서 사라진다 |
| 재생 중 Undo | 꺼짐: 그 이력은 편집 상태의 것이다. Stop 뒤에는 씬이 다시 마지막 커밋과 같으므로 단계가 생기지 않는다 |
| 재생 중 자동 저장 | 실행 중인 씬이 아니라 스냅숏을 쓴다; 새로고침은 항상 편집 상태의 Edit 모드로 시작한다 |
| 재생 중 쇼케이스 떠나기 | 쇼케이스는 Play 모드로 남되 멈춘다, 지금 스트레스 씬이 보이는 동안처럼 |
| 저장 형식 | 편집 상태에는 실행 시간이 없으므로, 저장은 클립 시간 (`CTIM`)과 행성의 공전 각도를 더 이상 쓰지 않는다 (`PLNT`는 배치를 유지하고 각도를 0으로 쓴다). 옛 저장은 여전히 불러온다; Play가 둘 다 0에서 시작하므로 그 값은 남는 효과가 없다. 이것은 1단계로 옮겨졌다: 미리보기의 클립 시간이 빠져야 Play 전과 Stop 뒤의 저장을 비교할 수 있다 |
| Undo | 공전 카메라만 driven 노드로 남는다 (뷰를 따라간다). 행성, 달, look 타깃, 루트 모션 캐릭터는 다른 노드처럼 undo 가능해진다 |
| 서드파티 | 없음 |

### 변경

- **샌드박스 (`sandbox/main.c`).** `Sandbox.playing`, `Sandbox.play_time`, 스냅숏 버퍼 (`SAVE_MAX_SIZE`). `update_showcase`는 항상 도는
  것 (애니메이션 미리보기, 블렌드)과 재생 중에만 도는 것 (회전, 점프, look 타깃 훑기, 루트 모션과 회전)으로
  나뉜다. `sandbox_play`는 재생 중에만 루트 모션 클립을 고른다. look 타깃은 Edit 모드에서 더 이상 훑지 않으므로 머리
  앞의 시작 위치를 받는다.
- **저장 (`sandbox/save.c`).** `save_load`에 부분 마스크가 생겨서, Stop이 뷰나 에디터 설정 없이 씬을 복원한다. `CTIM`과
  공전 각도는 더 이상 쓰지 않는다. `driven_fields`는 공전 카메라만 남긴다.
- **Undo (`sandbox/undo.c`).** 재생 중 꺼짐; 버튼 비활성.
- **UI (`sandbox/ui.c`).** Play/Stop 버튼, 색조, 라벨, 사라지는 편집에 관한 줄; Edit 모드에서 Jump 비활성.
- **스펙.** 위의 필드에 맞춰 `save.md`와 `undo.md`를 고침.

### 단계

1. **모드:** Play/Stop 버튼, 스냅숏과 복원, 쇼케이스 갱신 나누기, play 시간, 지정한 회전 위의 회전, look 타깃의
   시작 위치, Play에서만 Jump. 확인: 캐릭터가 걷고 뛴 뒤 Play 다음 Stop이 모든 노드를 되돌린다 (Play 전과 Stop
   뒤의 저장 바이트가 같다).
2. **에디터:** 재생 중 undo 꺼짐과 카메라로 줄인 driven 목록; 재생 중 스냅숏을 유지하는 자동 저장; 색조와 라벨;
   저장 형식 변경.
3. **경계 사례와 문서:** 점프 중과 크로스페이드 중 Stop; 재생 중 스트레스 씬으로 갔다 오기; 재생 중 새로고침;
   재생 중 편집 (Stop에서 사라짐); 폰 배치; `AGENTS.md`, README, 다른 스펙들.

모든 단계는 헤드리스 Chromium에서 Release와 Debug로, 데스크톱 크기의 마우스와 폰 크기의 터치로 확인한다.

## English

Status: implemented (2026-09-28). Changes to this spec are agreed first.

### Goal

Separate editing the showcase from running it, as Unity, Unreal and Godot do.

- **Edit mode:** nothing moves the scene by itself. What is on screen is what gets saved and undone.
- **Play mode:** the scene runs (the planet spins, the character walks and jumps, the head
  follows the target). Pressing Stop puts the scene back exactly as it was when Play was pressed.

Today the showcase always runs while it is edited, so undo and the autosave have to leave out the
values the sandbox changes every frame, and an edit to something moving (a walking character) cannot
be undone. With the modes apart, both only ever see the edit state.

### How other engines do it

| Engine | Play | Stop | Edits while playing |
|---|---|---|---|
| Unity | Runs the open scene | Restores the scene from before Play (it was serialized) | Allowed, visibly tinted, and lost on Stop |
| Unreal (Play In Editor) | Runs a copy of the world | Throws the copy away | Allowed on the copy, lost on Stop |
| Godot | Runs the scene in a separate process | Ends that process | In the running game only |

We follow Unity: the running scene is the edited one, and Stop restores it from a snapshot. The
snapshot is the save format we already have (`docs/specs/save.md`), so what Stop restores is
exactly what the autosave keeps. Copying the whole world (Unreal) would also have to copy
animators, materials and sandbox state, which live outside `NvScene`.

No third-party library applies.

### What runs in each mode

| | Edit | Play |
|---|---|---|
| Planet and moon spin | Still, at their authored rotation | Spin on top of their authored rotation (the spin no longer replaces it) |
| Character animation | Plays in place as a preview: the pose moves, the node does not | Plays with root motion when it is on |
| Root motion, turning | Off (the in-place clip plays) | On when the Root motion box is ticked |
| Look target | Stays where it is placed, and can be moved like any node | Sweeps in front of the head, as today |
| Jump | Off | On |
| Crossfades and blends | On (they are part of the preview) | On |
| Orbit camera, picking, gizmo | On | On |
| Stress scene | Not affected: a benchmark always runs, and the Play button is hidden while it is shown | |

Play time starts at 0 on Play, so a run always starts the same way: clips from their start, the
planet from its authored rotation.

### Decisions

| Topic | Decision |
|---|---|
| Controls | A **Play** / **Stop** button at the top center of the screen, in the top bar of both UIs (`layout.md`; originally at the start of the row above the panel's tabs). Shortcut: Space on the desktop UI, while no field holds the keyboard (Ctrl+P prints in browsers) |
| Showing the mode | While playing, the panel is tinted and the build label in the viewport reads "... · Playing"; a note ("Playing: edits are lost on Stop.") says edits are lost on Stop: beside the Play button in the desktop top bar, and at the top of the panel on the phone (wrapped), where it pushes the tabs down one line while playing |
| Tests | Debug builds expose `Module._sandbox_debug_save_crc()`, a CRC-32 of the save the state would write now; equal values before Play and after Stop mean Stop restored the scene |
| Play | Takes a snapshot: the save bytes of the scene (`save_write`). Play time starts at 0 |
| Stop | Restores the scene part of the snapshot (nodes, character settings, scene settings) with the save reader, so a jump, a crossfade or a walk in progress ends with the scene as it was. The camera view and the selection are not restored: they are not scene content |
| Edits while playing | Allowed (the gizmo and the Inspector work), and lost on Stop, as in Unity |
| Undo while playing | Off: its history belongs to the edit state. After Stop the scene equals the last commit again, so no step appears |
| Autosave while playing | Writes the snapshot, never the running scene; a reload always starts in Edit mode with the edit state |
| Leaving the showcase while playing | The showcase stays in Play mode but pauses, as it does today while the stress scene is shown |
| Save format | The edit state has no running time, so the save stops writing the clip time (`CTIM`) and the planet's orbit angle (`PLNT` keeps its layout with the angle written as 0). Older saves still load; the values have no lasting effect, since Play starts both from 0. This moved into phase 1: before-Play and after-Stop saves can only be compared once the preview's clip time is out of them |
| Undo | Only the orbit camera stays a driven node (it follows the view). The planet, the moon, the look target and a root-motion character become undoable like any node |
| Third-party | None |

### Changes

- **Sandbox (`sandbox/main.c`).** `Sandbox.playing`, `Sandbox.play_time`, and the snapshot buffer (`SAVE_MAX_SIZE`).
  `update_showcase` splits into what always runs (the animation preview, blends) and what only
  runs while playing (spin, jump, look target sweep, root motion and turning). `sandbox_play` picks the
  root-motion clip only while playing. The look target gets a start position in front of the
  head, since it no longer sweeps in Edit mode.
- **Save (`sandbox/save.c`).** `save_load` gains a parts mask, so Stop restores the scene without the
  view or the editor settings. `CTIM` and the orbit angle are no longer written. `driven_fields`
  keeps only the orbit camera.
- **Undo (`sandbox/undo.c`).** Off while playing; buttons disabled.
- **UI (`sandbox/ui.c`).** The Play/Stop button, the tint, the label, the line about lost edits; Jump
  disabled in Edit mode.
- **Specs.** `save.md` and `undo.md` updated for the fields above.

### Phases

1. **Modes:** the Play/Stop button, the snapshot and restore, splitting the showcase update, play
   time, spin on top of the authored rotation, the look target's start position, Jump only in Play.
   Checked: Play then Stop after the character has walked and jumped puts every node back (the
   save bytes before Play and after Stop are equal).
2. **Editor:** undo off while playing and the driven list cut to the camera; the autosave keeping
   the snapshot while playing; the tint and the label; the save format changes.
3. **Edge cases and docs:** Stop during a jump and during a crossfade; switching to the stress scene
   while playing and back; reloading while playing; editing while playing (lost on Stop); the phone
   layout; `AGENTS.md`, README and the other specs.

Every phase is checked in Release and Debug in headless Chromium, with the mouse at desktop size
and with touch at phone size.

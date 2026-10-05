# Viewport interaction spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-09-28). 이 스펙의 변경은 먼저 합의한다.

### 목표

씬 뷰포트에서 직접 작업한다: 마우스나 터치로 카메라를 공전, 줌, 팬하고 노드를 고른다. 회전은 인스펙터에서 편집할 수
있게 된다.

### 결정

| 주제 | 결정 |
|---|---|
| 공전 | 왼쪽 드래그 / 한 손가락 드래그 |
| 줌 | 휠 / 두 손가락 핀치 |
| 팬 | 오른쪽 또는 가운데 드래그 / 두 손가락 드래그; 공전 점을 초점 노드에서 옮긴다 |
| 따라가기 | 카메라는 선택을 공전한다; View 탭의 씬별 "Camera follows selection" 체크박스 (기본 꺼짐)가 그것을 켠다; 꺼진 동안 카메라는 제자리에 있고 팬이 공전 점을 옮긴다 |
| 선택 | 클릭 / 탭: 카메라에서 나간 광선이 가장 가까운 메시 노드를 고른다 |
| 선택 해제 | 메시에 맞지 않는 곳을 클릭 / 탭 |
| 캐릭터 | skinned 메시를 고르면 그 부모, 캐릭터 루트가 선택된다 |
| 선택 윤곽 | 고른 노드의 메시 상자, 디버그 라인으로 그림 |
| 회전 | 인스펙터가 오일러 각 (도 단위; yaw, pitch, roll)으로 편집한다 |
| 기즈모 | 나중 단계: `gizmo.md` 참고 |
| 서드파티 | 없음 |

### 엔진 변경

- **입력 라우팅 (`engine/imgui.h`).**
  - `resolution.md` 이후로 씬은 자체 해상도로 렌더링되어 뷰포트 안의 이미지 사각형 (`Sandbox.layout.scene`)에 보일 수 있다:
    picking과 팬은 그 이미지를 쓰고 (`nv_renderer_view_ray`가 `NvSceneOutput`을 받는다), 고정 크기 이미지 주변 띠의 탭은
    아무것도 하지 않는다. 공전, 팬, 줌은 여전히 뷰포트 어디서든 동작한다.
  - `NvImgui.view_rect`는 뷰포트다 (샌드박스가 매 프레임 설정). 열린 ImGui 팝업이 없는 동안 그 안에서 시작하는 마우스와 터치
    입력은 ImGui 제스처 처리를 건너뛰고 `NvImgui.view` (`NvViewInput`)를 채운다:
    - 공전 드래그;
    - 팬 드래그;
    - 줌 배율 (1 = 없음);
    - 탭 위치.
  - 다른 곳에서 시작하는 모든 것은 전처럼 동작한다.
- **메시 경계 (`engine/renderer.h`).**
  - `NvRenderMesh`가 로컬 경계 상자를 유지한다.
  - skinned 메시는 관절마다 그 관절이 가중치를 가진 모든 정점을 감싸는 bind 포즈 상자 하나도 유지한다.
    `nv_renderer_mesh_bounds`가 그 상자들을 현재 스키닝 행렬로 옮기고 합치는데, 이것이 포즈된 메시를 감싼다. skinned
    정점은 관절 이동의 가중 평균이므로 그 합집합 안에 머문다.
  - picking과 선택 윤곽은 포즈된 상자를 쓴다. (처음에는 bind 포즈 상자로 나갔는데, 애니메이션되는 동안 T-포즈 폭을
    유지했다.)
- **Picking (`engine/renderer.h`).**
  - `nv_renderer_view_ray`는 뷰포트 위치를 씬의 활성 카메라를 지나는 월드 광선으로 바꾼다.
  - `nv_renderer_pick`은 각 노드의 자체 공간의 상자를 써서 광선이 맞는 가장 가까운 메시 노드를 돌려준다.
- **수학 (`engine/math.h`).** 방향 변환, 쿼터니언 ↔ 오일러 (YXZ 순서).

### 단계

1. **카메라 조작:** 입력 라우팅, 그다음 샌드박스에서 공전, 줌, 팬.
2. **선택:** 메시 경계, picking, 탭으로 선택 또는 해제, 선택 윤곽.
3. **회전 편집:** 인스펙터의 오일러 각.

모든 단계는 헤드리스 Chromium에서 Release와 Debug로, 데스크톱 크기의 마우스와 폰 크기의 터치로 확인한다. 패널 입력
(스크롤, 탭, 슬라이더)은 계속 동작해야 한다.

## English

Status: implemented (2026-09-28). Changes to this spec are agreed first.

### Goal

Work in the scene viewport directly: orbit, zoom and pan the camera, and pick nodes, by mouse or
touch. Rotation becomes editable in the inspector.

### Decisions

| Topic | Decision |
|---|---|
| Orbit | Left drag / one-finger drag |
| Zoom | Wheel / two-finger pinch |
| Pan | Right or middle drag / two-finger drag; moves the orbit point off the focused node |
| Follow | The camera orbits the selection; a per-scene "Camera follows selection" checkbox in the View tab (off by default) turns it on; while off, the camera stays put and panning moves its orbit point |
| Select | Click / tap: a ray from the camera picks the nearest mesh node. Ctrl / Shift+click, or the phone's Multi toggle, add and remove instead (`selection.md`) |
| Deselect | Click / tap where no mesh is hit |
| Characters | Picking a skinned mesh selects its parent, the character root |
| Selection outline | The picked node's mesh boxes, drawn as debug lines (every selected node's since `selection.md`) |
| Rotation | Inspector edits it as Euler angles in degrees (yaw, pitch, roll) |
| Gizmo | A later step: see `gizmo.md` |
| Third-party | None |

### Engine changes

- **Input routing (`engine/imgui.h`).**
  - Since `resolution.md` the scene may be rendered at a resolution of its own and shown in an
    image rectangle inside the viewport (`Sandbox.layout.scene`): picking and panning use that image
    (`nv_renderer_view_ray` takes an `NvSceneOutput`), and a tap on the bars around a fixed-size
    image does nothing. Orbit, pan and zoom still work anywhere in the viewport.
  - `NvImgui.view_rect` is the viewport (set each frame by the sandbox). Mouse and touch input that
    starts inside it, while no ImGui popup is open, skips the ImGui gesture handling and fills
    `NvImgui.view` (`NvViewInput`):
    - the orbit drag;
    - the pan drag;
    - a zoom factor (1 = none);
    - a tap position.
  - Everything that starts elsewhere works as before.
- **Mesh bounds (`engine/renderer.h`).**
  - `NvRenderMesh` keeps its local bounding box.
  - Skinned meshes also keep one bind-pose box per joint, around every vertex that joint has weight
    on. `nv_renderer_mesh_bounds` moves those boxes by the current skinning matrices and joins
    them, which bounds the posed mesh. A skinned vertex is a weighted average of its joints'
    moves, so it stays inside that union.
  - Picking and the selection outline use the posed box. (First shipped with the bind-pose box,
    which kept a T-pose width while animated.)
- **Picking (`engine/renderer.h`).**
  - `nv_renderer_view_ray` turns a viewport position into a world ray through the scene's active
    camera.
  - `nv_renderer_pick` returns the nearest mesh node the ray hits, using each node's box in its own
    space.
- **Math (`engine/math.h`).** Transforming a direction, and quaternion ↔ Euler (YXZ order).

### Phases

1. **Camera control:** input routing, then orbit, zoom and pan in the sandbox.
2. **Selection:** mesh bounds, picking, tap to select or deselect, the selection outline.
3. **Rotation editing:** Euler angles in the inspector.

Every phase is checked in Release and Debug in headless Chromium, with the mouse at desktop size
and with touch at phone size. Panel input (scrolling, taps, sliders) must keep working.

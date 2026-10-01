# Transform gizmo spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-09-28). 이 스펙의 변경은 먼저 합의한다.

### 목표

인스펙터에 숫자를 입력하는 대신, 씬 뷰포트에서 마우스나 터치로 선택된 노드를 직접 이동, 회전, 크기 조절한다.
이 단계의 후보로 ImGuizmo를 꼽았던 뷰포트 스펙 (`viewport.md`)을 잇는다.

### 서드파티 후보

| 후보 | 무엇인가 | 언어, 라이선스 | 맞음 | 장단점 |
|---|---|---|---|---|
| **cimguizmo**를 통한 **ImGuizmo** (추천) | ImGui draw list로 그리는 이동, 회전, 크기 기즈모; 입력은 ImGui IO에서 | C++와 생성된 C API, MIT | 이미 cimgui를 통해 Dear ImGui를 쓴다. 렌더러 작업이 필요 없고, cimguizmo 덕분에 C++ 파일이 하나 (`anim.cpp`)로 유지된다 | 마우스용으로 만들어져서, 터치에는 우리 입력 라우팅 (아래)과 더 큰 핸들이 필요하다. cimguizmo가 우리 Dear ImGui 1.92.9b에 맞춰 빌드되어야 한다 |
| im3d | 정점을 출력하는 즉시 모드 3D 기즈모와 디버그 그리기 | C++, MIT | 렌더러에 무관 | 그 삼각형을 우리가 렌더링하고 (새 파이프라인), C++ API를 우리가 감싸야 한다 (두 번째 C++ 파일) |
| 직접 작성 | `nv_renderer_view_ray`로 맞추는 화살표, 고리, 상자를 디버그 라인이나 ImGui draw list로 그림 | C | 터치 동작을 완전히 제어하고 의존성이 없다 | ImGuizmo의 품질 (회전 고리, 평면 핸들, 스냅)에 이르려면 대략 800줄 이상; 디버그 라인은 1 px 폭이다 |

추천: cimguizmo를 통한 ImGuizmo. cimguizmo가 우리 Dear ImGui 버전에 맞춰 빌드되지 않으면, 두 번째 C++ 파일을
더하기보다 직접 작성하는 쪽으로 물러선다.

### 결정

| 주제 | 결정 |
|---|---|
| 작업 | 이동, 회전, 크기; 한 번에 하나 |
| 공간 | 이동과 회전은 월드 또는 로컬 (크기는 항상 로컬) |
| 스냅 | 기본은 꺼짐; 켜면: 0.5 m, 15°, 0.1 |
| 조작 | Inspector의 트랜스폼 필드 위에: 작업 라디오 (Move / Rotate / Scale), Local 체크박스, Snap 체크박스. 데스크톱 UI에서는 W / E / R로도 작업을 바꾸고, X는 Local과 World를 뒤집고, 드래그 중 Ctrl을 누르고 있으면 Snap 상자가 뒤집힌다 (`shortcuts.md`; W / E / R은 한때 포인터가 뷰포트 위에 있어야 했지만, 이제는 어떤 필드도 키보드를 잡고 있지 않기만 하면 된다) |
| 어떤 노드 | 선택된 노드, 단 활성 카메라는 제외 (공전 카메라가 그것을 소유한다). 캐릭터도 된다: 루트 모션이 옮긴 위치에 계속 더해진다 |
| 부모와 관절 | 기즈모는 월드 행렬을 편집한다. 결과는 parent world × `attach.joint_model`의 역행렬을 거쳐 `local`로 돌아간다 |
| 되쓰기 | ImGuizmo의 오일러 각 분해가 아닌 자체 `nv_mat4_decompose` (이동, 쿼터니언, 크기). 회전된 부모 아래의 비균일 크기에서 생기는 전단은 버린다 |
| 카메라 따라가기 | 기즈모가 드래그하는 동안 카메라는 선택을 따라가는 중이어도 가만히 있다; 드래그되는 노드를 따라가면 포인터의 광선이 함께 움직여 드래그가 달아난다 |
| 이미지 사각형 | ImGuizmo에는 씬 이미지의 사각형 (`resolution.md`)을 화면 크기로 준다, 그래서 기즈모가 다른 해상도로 보이는 씬을 따라간다; 그것은 ImGui이므로 전체 해상도로 그려지고, 고정 크기 이미지 주변의 띠 위로 넘어갈 수 있다 |
| 뷰포트 UI | 기즈모는 뷰포트에 그린다; 빌드 라벨 다음으로 "에디터 UI는 패널 안에"의 두 번째 예외다 |
| 크기 | 약 64 CSS 픽셀 길이 × `NvImgui.ui_scale` (터치 화면에서 96), 매 프레임 픽셀로 설정. ImGuizmo 자체 크기는 뷰포트 폭의 비율이라 폰에서는 약 20 픽셀 길이였고, 고정 픽셀 거리 (화살표 12, 고리 8) 안에서 핸들을 맞춘다. 선 두께도 `ui_scale`에 따라 커진다 |
| 조작 줄 | 좁은 패널에서는 가장자리를 넘어가지 않고 줄바꿈한다 |
| Undo | 지금은 아님 |

### 엔진 변경

- **입력 라우팅 (`nv/imgui.h`).** 지금은 `view_rect`에서 시작하는 모든 누름이 `NvImgui.view`로 가므로 ImGui는
  그것을 보지 못한다. 새 훅 `NvImgui.view_grab`은 앱이 설정하는 함수다.
  - 뷰포트에서 왼쪽 누름이나 한 손가락 터치는 ImGui의 마우스를 그리로 옮기고 두 프레임 기다린다. 터치에는
    hover가 없으므로, ImGuizmo는 마우스가 거기 있는 채로 한 프레임이 돈 뒤에야 손가락 아래에 무엇이 있는지
    안다. 그동안 뷰는 누름의 입력을 적용하지 않고 모은다.
  - 그다음 훅이 결정한다 (앱이 ImGuizmo의 `IsOver`를 부른다). 예: 누름은 눌린 곳에서부터 ImGui 왼쪽 버튼이
    되고, 뷰가 모은 것은 버린다. 아니요: 뷰가 그것을 적용한다, 기다리는 동안 놓인 탭도 포함해서.
  - 두 번째 손가락은 항상 누름을 뷰에 둔다 (핀치와 팬).
- **렌더러 (`nv/renderer.h`).** `nv_renderer_camera_matrices`는 `nv_renderer_draw`가 쓰는 대로 활성 카메라의
  뷰와 투영 행렬을 준다.
- **수학 (`nv/math.h`).** `nv_mat4_decompose`와 `nv_mat4_inverse`는 이미 있다. `nv_mat4_decompose`는 이제
  쿼터니언을 정규화한다. 그렇지 않으면 드래그의 매 프레임 노드를 나누고 다시 만드는 동안 표류한다: 회전과
  크기가 매 프레임 조금씩 줄어들었다.
- **빌드.** cimguizmo는 cimgui처럼 가져와 `cimgui` 라이브러리 안에 빌드하므로, 그 라이브러리의 ImGui
  컨텍스트를 공유한다.

### 앱 변경

- `update_camera` 뒤에 앱은 카메라의 뷰와 투영 행렬과 뷰포트 사각형 (ImGui 좌표)을 ImGuizmo에 넘기고, 선택된
  노드의 월드 행렬로 `Manipulate`를 부른다. 기즈모가 변경을 알리면 앱이 그것을 `local`에 되쓴다.
- 기즈모가 가져간 누름은 `NvImgui.view`에 닿지 않으므로, 공전도 picking도 하지 않는다.
- `ImGuizmo_BeginFrame`은 패널을 만들기 전에 돌므로, ImGuizmo의 전체 화면 창이 먼저 만들어져 패널 뒤에 남는다.
- 우리 투영은 깊이를 OpenGL의 -1..1이 아닌 0..1 (WebGPU)로 매핑한다. ImGuizmo는 깊이 0과 1에서 unproject하고
  카메라에 더 가까운 끝을 고르므로 이것으로 동작한다.

### 단계

1. **이동:** 의존성, 입력 라우팅, 월드 공간의 이동 기즈모, 부모와 관절을 거친 되쓰기.
2. **회전과 크기:** 두 작업, 로컬과 월드 공간, 스냅, Inspector 조작과 W / E / R.
3. **터치와 문서:** 핸들 크기, 폰 크기 확인, `AGENTS.md`와 `docs/CODING_STANDARD.md`의 의존성 섹션.

모든 단계는 헤드리스 Chromium에서 Release와 Debug로, 데스크톱 크기의 마우스와 폰 크기의 터치로 확인한다.
공전, 팬, 줌, picking, 패널 입력이 계속 동작해야 한다.

## English

Status: implemented (2026-09-28). Changes to this spec are agreed first.

### Goal

Move, rotate and scale the selected node directly in the scene viewport, by mouse or touch,
instead of typing numbers into the inspector. Follows the viewport spec (`viewport.md`), which
named ImGuizmo as the candidate for this step.

### Third-party candidates

| Candidate | What it is | Language, license | Fit | Trade-offs |
|---|---|---|---|---|
| **ImGuizmo** through **cimguizmo** (recommended) | Translate, rotate and scale gizmos drawn with ImGui draw lists; input from ImGui IO | C++ with a generated C API, MIT | We already use Dear ImGui through cimgui. It needs no renderer work, and cimguizmo keeps us at one C++ file (`anim.cpp`) | Built for the mouse, so touch needs our input routing (below) and bigger handles. cimguizmo must build against our Dear ImGui 1.92.9b |
| im3d | Immediate-mode 3D gizmos and debug drawing that outputs vertices | C++, MIT | Renderer agnostic | We would render its triangles (a new pipeline) and wrap its C++ API ourselves (a second C++ file) |
| Write it ourselves | Arrows, rings and boxes hit by `nv_renderer_view_ray`, drawn as debug lines or with the ImGui draw list | C | Full control of touch behaviour and no dependency | Roughly 800+ lines to reach ImGuizmo's quality (rotation rings, plane handles, snapping); debug lines are 1 px wide |

Recommendation: ImGuizmo through cimguizmo. If cimguizmo does not build against our Dear ImGui
version, we fall back to writing it ourselves rather than adding a second C++ file.

### Decisions

| Topic | Decision |
|---|---|
| Operations | Translate, rotate, scale; one at a time |
| Space | World or local, for translate and rotate (scale is always local) |
| Snap | Off by default; when on: 0.5 m, 15°, 0.1 |
| Controls | In the Inspector, above the transform fields: an operation radio (Move / Rotate / Scale), a Local checkbox and a Snap checkbox. On the desktop UI, W / E / R also switch the operation, X flips Local and World, and Ctrl held during a drag turns the Snap box around (`shortcuts.md`; W / E / R once needed the pointer over the viewport, now only that no field holds the keyboard) |
| Which nodes | The selected node, unless it is the active camera (the orbit camera owns it). Characters work: root motion keeps adding to the moved position |
| Parents and joints | The gizmo edits the world matrix. The result goes back to `local` through the inverse of parent world × `attach.joint_model` |
| Write-back | Our own `nv_mat4_decompose` (translation, quaternion, scale), not ImGuizmo's Euler-degree decomposition. Shear from non-uniform scale under a rotated parent is dropped |
| Camera follow | While the gizmo drags, the camera holds still even when it follows the selection; following the dragged node would move the pointer's ray with it and the drag would run away |
| Image rectangle | ImGuizmo is given the scene image's rectangle (`resolution.md`) at its screen size, so the gizmo follows a scene shown at another resolution; it is ImGui, drawn at full resolution, and may reach over the bars around a fixed-size image |
| Viewport UI | The gizmo draws in the viewport; it is the second exception to "editor UI stays in the panel", after the build label |
| Size | About 64 CSS pixels long, times `NvImgui.ui_scale` (96 on touch screens), set in pixels every frame. ImGuizmo's own size is a fraction of the viewport width, which left it about 20 pixels long on a phone, and it hits handles within fixed pixel distances (12 for arrows, 8 for rings). Line widths also scale with `ui_scale` |
| Controls row | Wraps on a narrow panel instead of running off its edge |
| Undo | Not now |

### Engine changes

- **Input routing (`nv/imgui.h`).** Today every press that starts in `view_rect` goes to
  `NvImgui.view`, so ImGui never sees it. A new hook, `NvImgui.view_grab`, is a function that the
  app sets.
  - A left press or a one-finger touch in the viewport moves ImGui's mouse there and waits two
    frames. Touch has no hover, so ImGuizmo only knows what is under the finger after a frame has
    run with the mouse there. Meanwhile the view gathers the press's input without applying it.
  - Then the hook decides (the app calls ImGuizmo's `IsOver`). Yes: the press becomes an ImGui
    left button from where it went down, and what the view gathered is dropped. No: the view
    applies it, including a tap that was released while waiting.
  - A second finger always keeps the press in the view (pinch and pan).
- **Renderer (`nv/renderer.h`).** `nv_renderer_camera_matrices` gives the active camera's view and
  projection matrices, as `nv_renderer_draw` uses them.
- **Math (`nv/math.h`).** `nv_mat4_decompose` and `nv_mat4_inverse` already exist.
  `nv_mat4_decompose` now normalizes its quaternion. Otherwise splitting and rebuilding the node
  every frame of a drag drifts: the rotation and scale shrank a little more each frame.
- **Build.** cimguizmo is fetched like cimgui and built into the `cimgui` library, so it shares
  that library's ImGui context.

### App changes

- After `update_camera`, the app passes the camera's view and projection matrices and the
  viewport rect (in ImGui coordinates) to ImGuizmo, then calls `Manipulate` on the selected node's
  world matrix. When the gizmo reports a change, the app writes it back to `local`.
- A press the gizmo takes never reaches `NvImgui.view`, so it neither orbits nor picks.
- `ImGuizmo_BeginFrame` runs before the panel is built, so ImGuizmo's full-screen window is
  created first and stays behind the panel.
- Our projection maps depth to 0..1 (WebGPU), not OpenGL's -1..1. ImGuizmo unprojects at depth 0
  and 1 and picks the end nearer the camera, so this works.

### Phases

1. **Move:** the dependency, input routing, the translate gizmo in world space, and write-back
   through parents and joints.
2. **Rotate and scale:** both operations, local and world space, snapping, the Inspector controls
   and W / E / R.
3. **Touch and docs:** handle sizes, phone-size checks, `AGENTS.md` and the Dependencies section
   of `docs/CODING_STANDARD.md`.

Every phase is checked in Release and Debug in headless Chromium, with the mouse at desktop size
and with touch at phone size. Orbit, pan, zoom, picking and panel input must keep working.

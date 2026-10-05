# One app, one scene spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-09-27). 이 스펙의 변경은 먼저 합의한다.

### 목표

예제 세 개를 씬 하나를 보여 주는 앱 하나로 바꾼다: scene 예제의 행성과 달, character 예제의 애니메이션되는
캐릭터, 검, 타깃을 나란히 둔다. 에디터 패널은 노드 트리와 선택된 노드의 컴포넌트를 보여 준다. 애니메이션이
씬의 일부가 되므로, 샌드박스는 더 이상 애니메이션을 따라가려고 노드를 손으로 움직이지 않는다.

### 결정

| 주제 | 결정 |
|---|---|
| 예제 | `triangle`은 삭제. `scene`과 `character`는 앱 하나로 합친다 |
| 위치 | `sandbox/main.c` (씬 구성, 프레임)와 `sandbox/ui.c` (에디터 패널). CMake 타깃은 `sandbox`. `examples/`는 제거 |
| 주소 | Pages 루트, `https://chromedays.github.io/engine/`. `web/landing.html`은 제거. 나중에 옮김: Release는 `/engine/release/`, Debug는 `/engine/debug/` |
| 옛 주소 | `/engine/character/`, `/engine/scene/`, `/engine/triangle/`은 리다이렉트 없이 없앤다 |
| 에셋 | `assets/` (`quaternius/` 1.3 MB, `fonts/` 0.4 MB)를 담은 미리 불러오는 `sandbox.data` 하나 |
| 씬 안의 애니메이션 | 새 `attach` 노드 컴포넌트, 애니메이터 `owner`, look-at `target_node`, `nv_anim_update_scene` (아래) |
| glTF 로딩 | 파일에 스킨이 있으면 로더가 스켈레톤과 애니메이터를 만든다 |
| `scene.c` | 여전히 anim이나 렌더러를 전혀 모른다. anim이 쓴 행렬을 곱할 뿐이다 |
| 서드파티 | 필요 없음 |

### 구조 변경

`+`는 추가, `-`는 제거, 표시 없는 줄은 그대로.

```diff
 NvScene  (4096 slots)
 ├─ nodes[1..]    NvNode
 │   ├─ parent / first_child / next_sibling
 │   ├─ position, rotation, scale
 │   ├─ world     ← nv_scene_update
+│   │              (× attach.joint_model if attached)
 │   └─ components (0 = none)
 │       ├─ mesh, material ─────────► NvRenderer
 │       ├─ animator NvAnimatorId ────► nv_anim
+│       ├─ attach   NvJointAttach
+│       │    ├─ animator ───────────► nv_anim
+│       │    ├─ joint
+│       │    └─ joint_model  ◄── written by anim
 │       ├─ camera
 │       └─ light
 └─ active_camera
```

```diff
 nv_anim
 └─ animators[64] NvAnimator
     ├─ skeleton
+    ├─ owner  NvNodeId ──► e.g. the character root
     ├─ layers[4]
     ├─ look_at
+    │   └─ target_node NvNodeId (0 = use target)
     ├─ root_motion
     └─ joint_model[]

 NvGltfModel
 ├─ root, mesh_nodes[]
 ├─ joints, inverse_bind
+├─ skeleton   (created by the loader)
+└─ animator   (created by the loader, owner = root)
```

```diff
 Frame
 1  sandbox: play / blend / demo motion ─► NvAnimator
-2  sandbox: nv_anim_update(animator)
-3  sandbox: apply_root_motion ─► root node
-4  sandbox: update_sword ─► sword node
-5  sandbox: update_look_at ─► look_at.target
+2  nv_anim_update_scene(scene, dt)
+     ├─ look_at.target_node ─► look_at.target
+     ├─ every animator ─► joint_model[]
+     ├─ root_motion ─► owner node
+     └─ every attach ─► attach.joint_model
 6  nv_scene_update ─► world
 7  nv_renderer_draw(scene, skins)
```

```diff
-scene example          character example
-├─ camera              ├─ camera
-├─ sun                 ├─ sun
-├─ ground              ├─ ground
-└─ planet              ├─ character
-   └─ moon             │  ├─ mesh ×3  animator=#1
-                       │  └─ sword    (sandbox moves it)
-                       └─ target
+one scene
+├─ camera
+├─ sun
+├─ ground
+├─ planet
+│  └─ moon
+├─ character   owner of animator #1
+│  ├─ mesh ×3  animator = #1
+│  └─ sword    attach = {#1, hand_r}
+└─ target      look_at.target_node of #1
```

### 엔진 API 변경

- **`engine/scene.h`**
  - `NvJointAttach { NvAnimatorId animator; u32 joint; NvMat4 joint_model; }`와 `NvNode.attach`.
  - 부착된 노드의 부모는 그 애니메이터의 owner여야 한다. `nv_scene_update`는
    `world = parent.world × attach.joint_model × local`을 계산한다. `scene.c`의 다른 것은 바뀌지 않는다.
- **`engine/anim.h`**
  - `NvAnimator.owner` (`NvNodeId`)와 `NvLookAt.target_node` (`NvNodeId`).
  - `nv_anim_create_animator(skeleton, owner)`가 `nv_anim_create_animator(skeleton)`을 대신한다.
  - `nv_anim_update_scene(NvScene* scene, f32 dt)`. 애니메이터마다:
    - `look_at.target_node`의 월드 위치를 owner의 모델 공간으로 바꾼다. 지난 프레임의 월드 행렬을 쓰므로
      한 프레임 늦지만, 눈에 보이지 않는다.
    - `nv_anim_update`를 부른다.
    - owner를 `root_motion`만큼 (owner의 회전으로 돌려서) 움직이고, 그것을 비운다.
  - 그다음 모든 노드의 `attach`에 `joint_model[joint]`를 복사한다.
  - `nv_anim_update`는 씬을 쓰지 않는 호출자를 위해 남는다.
  - `nv_anim_clip_skeleton(NvClipId)`과 `nv_anim_clip_count()`: 샌드박스가 자체 테이블을 두지 않고도 UI가
    스켈레톤의 클립을 나열할 수 있게.
- **`engine/gltf.h`**
  - `NvGltfModel.skeleton`과 `NvGltfModel.animator`. 파일에 스킨이 있으면 로더가 둘 다 만들고, `owner`를
    모델 루트로 정하고, 모든 skinned 메시 노드에 `animator`를 설정한다.

### 샌드박스

- **씬 배치:**
  - 캐릭터는 원점에 서고, 행성과 달은 한쪽으로 약 3 m 떨어져 있다.
  - 카메라는 초점 노드를 공전한다: 선택된 노드, 아무것도 선택되지 않았으면 캐릭터. 캐릭터의 루트 모션을
    계속 따라간다.
- **샌드박스에 남는 동작:**
  - 점프 연결 (Jump_Start → Jump_Loop → Jump_Land).
  - 블렌드 도우미.
  - 루트 모션 클립과 제자리 클립 중 고르기.
  - 회전 속도.
  - look-at 타깃 움직이기.
  - 행성의 공전.
  - 뼈 오버레이.
- **에디터 패널 (`nv_imgui_begin_panel`), 탭:**
  - **Scene:** 노드 트리. 노드를 탭하면 선택된다.
  - **Inspector:** 선택된 노드.
    - 이름과 트랜스폼, 지금처럼.
    - 가진 컴포넌트마다 섹션 하나: Mesh (머티리얼 색), Camera, Light (색, 세기), Attach (관절).
    - Animator: 지금의 캐릭터 조작들, 즉 클립, Jump, 속도, 페이드, 블렌드, 레이어 막대, 루트 모션, 회전,
      look at.
  - **View:** FPS, 카메라 yaw와 거리, 뼈 표시, 행성 공전 속도.

### 빌드와 배포

- `sandbox/CMakeLists.txt`: `add_executable(sandbox main.c ui.c)`와
  `nv_setup_executable(sandbox ASSETS assets/quaternius)`.
- `nv_setup_executable`에 하위 폴더 대신 패키지 루트에 설치하는 옵션이 생긴다.
- CI는 같은 단계를 유지하고 설치 배치만 바뀐다. `cp web/landing.html` 단계는 없앤다.

### 단계

1. **엔진:** `attach`, `owner`, `target_node`, `nv_anim_update_scene`, 클립 질의, 애니메이터를 만드는 로더.
   엔진 변경만 따로 확인하려고 character 예제를 동작 변화 없이 이것들로 옮긴다.
2. **샌드박스:**
   - 합친 씬과 Scene / Inspector / View 패널로 `sandbox/`을 만든다.
   - `examples/`를 삭제한다.
3. **배포와 문서:**
   - 루트에 설치하고 CI를 고친다.
   - `AGENTS.md`, `README.md`, `docs/specs/animation.md`의 구현 메모를 고친다.

모든 단계는 헤드리스 Chromium에서 데스크톱과 폰 크기로, Release와 Debug에서 확인한다:

- 모든 클립, 점프, 블렌드, 검, 루트 모션, look at이 여전히 동작한다;
- 행성이 공전한다;
- 노드 선택과 트랜스폼 편집이 동작한다;
- 패널이 터치로 스크롤된다.

## English

Status: implemented (2026-09-27). Changes to this spec are agreed first.

### Goal

Replace the three examples with one app that shows one scene: the scene example's planet and
moon and the character example's animated character, sword and target, side by side. The editor
panel shows the node tree and the selected node's components. Animation becomes part of the
scene, so the sandbox no longer moves nodes by hand to follow the animation.

### Decisions

| Topic | Decision |
|---|---|
| Examples | `triangle` is deleted. `scene` and `character` merge into one app |
| Location | `sandbox/main.c` (scene setup, frame) and `sandbox/ui.c` (editor panel); the CMake target is `sandbox`; `examples/` is removed |
| Address | Pages root, `https://chromedays.github.io/engine/`; `web/landing.html` is removed. Later moved: Release to `/engine/release/`, Debug to `/engine/debug/` |
| Old addresses | `/engine/character/`, `/engine/scene/` and `/engine/triangle/` are dropped, with no redirects |
| Assets | One preloaded `sandbox.data` with `assets/` (`quaternius/` 1.3 MB, `fonts/` 0.4 MB) |
| Animation in the scene | New `attach` node component, animator `owner`, look-at `target_node`, and `nv_anim_update_scene` (below) |
| glTF loading | The loader creates the skeleton and animator when the file has a skin |
| `scene.c` | Still knows nothing about anim or the renderer; it only multiplies by a matrix anim writes |
| Third-party | None needed |

### Structure changes

`+` is added, `-` removed, unmarked lines stay.

```diff
 NvScene  (4096 slots)
 ├─ nodes[1..]    NvNode
 │   ├─ parent / first_child / next_sibling
 │   ├─ position, rotation, scale
 │   ├─ world     ← nv_scene_update
+│   │              (× attach.joint_model if attached)
 │   └─ components (0 = none)
 │       ├─ mesh, material ─────────► NvRenderer
 │       ├─ animator NvAnimatorId ────► nv_anim
+│       ├─ attach   NvJointAttach
+│       │    ├─ animator ───────────► nv_anim
+│       │    ├─ joint
+│       │    └─ joint_model  ◄── written by anim
 │       ├─ camera
 │       └─ light
 └─ active_camera
```

```diff
 nv_anim
 └─ animators[64] NvAnimator
     ├─ skeleton
+    ├─ owner  NvNodeId ──► e.g. the character root
     ├─ layers[4]
     ├─ look_at
+    │   └─ target_node NvNodeId (0 = use target)
     ├─ root_motion
     └─ joint_model[]

 NvGltfModel
 ├─ root, mesh_nodes[]
 ├─ joints, inverse_bind
+├─ skeleton   (created by the loader)
+└─ animator   (created by the loader, owner = root)
```

```diff
 Frame
 1  sandbox: play / blend / demo motion ─► NvAnimator
-2  sandbox: nv_anim_update(animator)
-3  sandbox: apply_root_motion ─► root node
-4  sandbox: update_sword ─► sword node
-5  sandbox: update_look_at ─► look_at.target
+2  nv_anim_update_scene(scene, dt)
+     ├─ look_at.target_node ─► look_at.target
+     ├─ every animator ─► joint_model[]
+     ├─ root_motion ─► owner node
+     └─ every attach ─► attach.joint_model
 6  nv_scene_update ─► world
 7  nv_renderer_draw(scene, skins)
```

```diff
-scene example          character example
-├─ camera              ├─ camera
-├─ sun                 ├─ sun
-├─ ground              ├─ ground
-└─ planet              ├─ character
-   └─ moon             │  ├─ mesh ×3  animator=#1
-                       │  └─ sword    (sandbox moves it)
-                       └─ target
+one scene
+├─ camera
+├─ sun
+├─ ground
+├─ planet
+│  └─ moon
+├─ character   owner of animator #1
+│  ├─ mesh ×3  animator = #1
+│  └─ sword    attach = {#1, hand_r}
+└─ target      look_at.target_node of #1
```

### Engine API changes

- **`engine/scene.h`**
  - `NvJointAttach { NvAnimatorId animator; u32 joint; NvMat4 joint_model; }` and
    `NvNode.attach`.
  - An attached node's parent should be its animator's owner. `nv_scene_update` computes
    `world = parent.world × attach.joint_model × local`. Nothing else in `scene.c` changes.
- **`engine/anim.h`**
  - `NvAnimator.owner` (`NvNodeId`) and `NvLookAt.target_node` (`NvNodeId`).
  - `nv_anim_create_animator(skeleton, owner)` replaces `nv_anim_create_animator(skeleton)`.
  - `nv_anim_update_scene(NvScene* scene, f32 dt)`. For every animator:
    - It turns `look_at.target_node`'s world position into the owner's model space. It uses
      last frame's world matrices, so it lags one frame, which is not visible.
    - It calls `nv_anim_update`.
    - It moves the owner by `root_motion` (rotated by the owner's rotation), then clears it.
  - After that, it copies `joint_model[joint]` into every node's `attach`.
  - `nv_anim_update` stays for callers that do not use a scene.
  - `nv_anim_clip_skeleton(NvClipId)` and `nv_anim_clip_count()`, so UI can list a skeleton's
    clips without the sandbox keeping its own table.
- **`engine/gltf.h`**
  - `NvGltfModel.skeleton` and `NvGltfModel.animator`. When the file has a skin, the loader
    creates both, with `owner` set to the model root, and sets `animator` on every skinned mesh
    node.

### Sandbox

- **Scene layout:**
  - The character stands at the origin, and the planet and moon sit about 3 m to one side.
  - The camera orbits a focus node: the selected node, or the character when nothing is selected.
    It keeps following the character's root motion.
- **Behavior that stays in the sandbox:**
  - The jump chain (Jump_Start → Jump_Loop → Jump_Land).
  - The blend helper.
  - Choosing root-motion or in-place clips.
  - The turn rate.
  - Moving the look-at target.
  - The planet's orbit.
  - The bone overlay.
- **Editor panel (`nv_imgui_begin_panel`), tabs:**
  - **Scene:** the node tree; tapping a node selects it.
  - **Inspector:** the selected node.
    - Its name and transform, as today.
    - One section per component it has: Mesh (material color), Camera, Light (color,
      intensity), Attach (joint).
    - Animator: today's character controls, which are clips, Jump, speed, fade, blend, layer
      bars, root motion, turn and look at.
  - **View:** FPS, camera yaw and distance, show bones, and planet orbit speed.

### Build and deploy

- `sandbox/CMakeLists.txt`: `add_executable(sandbox main.c ui.c)` and
  `nv_setup_executable(sandbox ASSETS assets/quaternius)`.
- `nv_setup_executable` gains an option to install at the package root instead of a subfolder.
- CI keeps the same steps; only the install layout changes. The `cp web/landing.html` step goes.

### Phases

1. **Engine:** `attach`, `owner`, `target_node`, `nv_anim_update_scene`, clip queries, and the
   loader creating animators. Port the character example to them, with its behavior unchanged,
   to check the engine change on its own.
2. **Sandbox:**
   - Create `sandbox/` with the merged scene and the Scene / Inspector / View panel.
   - Delete `examples/`.
3. **Deploy and docs:**
   - Install at the root and update CI.
   - Update `AGENTS.md`, `README.md` and the implementation notes in `docs/specs/animation.md`.

Every phase is checked in Release and Debug in headless Chromium at desktop and phone size:

- all clips, jump, blend, sword, root motion and look at still work;
- the planet orbits;
- selecting nodes and editing transforms works;
- the panel scrolls by touch.

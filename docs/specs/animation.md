# Animation spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (1–4단계, 2026-09-26). 이 스펙의 변경은 먼저 합의한다.

### 목표

glTF에서 불러온 캐릭터의 스켈레탈 애니메이션: 클립을 재생하고, 클립 사이를 블렌딩하고 크로스페이드하며,
ImGui에서 조작한다. nv의 나머지처럼 모두 브라우저에서 돈다.

### 결정

| 주제 | 결정 |
|---|---|
| glTF 파싱 | [cgltf](https://github.com/jkuhlmann/cgltf) (단일 헤더 C, MIT), C에서 사용 |
| 애니메이션 런타임 | [ozz-animation](https://github.com/guillaumeblanc/ozz-animation) (C++17, MIT)을 우리 C API 뒤에 둔다. 래퍼가 우리의 유일한 C++ 파일이다 (`docs/CODING_STANDARD.md` 참고) |
| 캐릭터 | Quaternius Universal Base Characters (Standard, 무료), CC0: 우선 `Superhero_Male_FullBody`만 |
| 애니메이션 | Quaternius Universal Animation Library (Standard, 무료), CC0: 클립 8개 (아래) |
| 텍스처 | base color만, 1024 px로 축소. 노멀과 러프니스 맵은 PBR 셰이딩까지 기다린다 |
| 바이너리 에셋 | `assets/` 아래, Git LFS (`.gitattributes`) |
| 에셋 다듬기 | `tools/trim_assets.sh`에서 `npx`로 gltf-transform 실행. 도구는 저장소에 추가하지 않는다 |

### 원본 에셋

두 팩 모두 itch.io에서 받는다 ("No thanks, just take me to the downloads"):

- <https://quaternius.itch.io/universal-base-characters> →
  `Universal Base Characters[Standard].zip` (122 MB)
- <https://quaternius.itch.io/universal-animation-library> →
  `Universal Animation Library[Standard].zip` (15 MB)

내용물 (2026-09-26 확인):

- **캐릭터:** `Base Characters/Godot - UE/Superhero_{Male,Female}_FullBody.gltf` (+ `.bin`, PNG
  텍스처). **관절 65개**짜리 스킨 하나; `JOINTS_0` / `WEIGHTS_0`가 있는 메시 3개(몸, 얼굴, 눈); base color,
  노멀, 러프니스 텍스처가 있는 머티리얼 3개 (PNG, 각 1–4 MB).
- **애니메이션:** `Unreal-Godot/UAL1_Standard.glb` (7.6 MB), 같은 리그의 마네킹과 **클립 43개**.
  `UAL1_Standard_RM.glb`는 루트 모션이 구워진 같은 파일이다.
- **관절 65개의 이름이** 캐릭터와 애니메이션 파일 사이에 **정확히 일치**하므로, 클립은 리타게팅 없이
  관절 이름으로 캐릭터에 묶인다.

`UAL1_Standard.glb`의 클립: A_TPose, Crouch_Fwd_Loop, Crouch_Idle_Loop, Dance_Loop, Death01,
Driving_Loop, Fixing_Kneeling, Hit_Chest, Hit_Head, Idle_Loop, Idle_Talking_Loop, Idle_Torch_Loop,
Interact, Jog_Fwd_Loop, Jump_Land, Jump_Loop, Jump_Start, PickUp_Table, Pistol_Aim_Down,
Pistol_Aim_Neutral, Pistol_Aim_Up, Pistol_Idle_Loop, Pistol_Reload, Pistol_Shoot, Punch_Cross,
Punch_Jab, Push_Loop, Roll, Sitting_Enter, Sitting_Exit, Sitting_Idle_Loop, Sitting_Talking_Loop,
Spell_Simple_Enter, Spell_Simple_Exit, Spell_Simple_Idle_Loop, Spell_Simple_Shoot, Sprint_Loop,
Swim_Fwd_Loop, Swim_Idle_Loop, Sword_Attack, Sword_Idle, Walk_Formal_Loop, Walk_Loop.

#### 저장소에 들어가는 것

전체 팩은 웹 다운로드에 너무 크므로 `assets/quaternius/`에는 다듬은 일부만 둔다:

- `character.glb`: 메시 3개, 스킨, 1024 px로 줄인 base color 텍스처가 있는 `Superhero_Male_FullBody`
  (노멀과 러프니스 텍스처는 뺀다).
- `clips.glb`: `UAL1_Standard.glb`의 리그와 다음 클립 8개, 메시 없음:
  - Idle_Loop, Walk_Loop, Jog_Fwd_Loop, Sprint_Loop (이동, 크로스페이드용),
  - Jump_Start, Jump_Loop, Jump_Land (이어지는 동작),
  - Dance_Loop (긴 루프).
- `LICENSE.txt`: CC0 전문과 위의 출처 URL.

여성 캐릭터와 더 많은 클립은 나중에 같은 방식으로 추가할 수 있다. 둘 다 같은 리그를 쓴다.

다듬기에는 [gltf-transform](https://gltf-transform.dev/) (Node CLI, MIT)을 `tools/trim_assets.sh`에서
`npx`로 실행한다. 스크립트는 어떤 파일과 클립을 남겼는지 기록하여 에셋을 다시 만들 수 있게 한다. 도구
자체는 저장소, 빌드, CI에 추가하지 않는다.

### 구조

새 모듈 세 개. cgltf는 C 쪽에, ozz는 C++ 래퍼 안에 머물고, 둘은 단순한 C 구조체로 만난다.

#### `nv/renderer.h` (C)

지금 `examples/scene`에 있는 메시, 머티리얼, 그리기 코드를 엔진으로 옮긴다. glTF 로딩이 두 번째 사용처가
되었기 때문이다.

- 정점 형식 두 가지가 있는 메시 레지스트리: static (position, normal, uv)과 skinned (position, normal,
  uv, 관절 인덱스 4개, 가중치 4개).
- 머티리얼: 우선 base color 계수와 텍스처.
- 파이프라인 두 개 (static, skinned). 깊이 버퍼는 렌더러가 소유한다.
- 객체별 데이터 (모델 행렬, 머티리얼, 관절 행렬의 오프셋)는 스토리지 버퍼 하나에, 모든 캐릭터의 스키닝
  행렬은 다른 버퍼 하나에 이어 붙여 프레임마다 한 번 올린다.

#### `nv/gltf.h` (C, cgltf)

- 메시, 머티리얼, 텍스처를 렌더러로 불러온다.
- 캐릭터마다 루트 씬 노드 하나를 만든다. 관절은 씬 노드가 아니다. ozz 스켈레톤이 소유한다.
- 스킨(관절 목록, inverse bind 행렬)을 읽어 스켈레톤용 `NvJointDesc`를 채운다.
- 클립 파일에서 클립을 읽어 `NvTrackDesc`를 채우고, glTF 채널을 이름으로 스켈레톤 관절에 맞춘다.

#### `nv/anim.h` (C API) + `engine/src/anim.cpp` (ozz 래퍼, C++)

```c
#define NV_MAX_SKELETONS   8
#define NV_MAX_CLIPS       64
#define NV_MAX_ANIMATORS   64
#define NV_MAX_JOINTS      128   // the Quaternius rig has 65
#define NV_MAX_ANIM_LAYERS 4

typedef struct NvSkeletonId { u32 index; } NvSkeletonId; // 0 = none
typedef struct NvClipId     { u32 index; } NvClipId;     // 0 = none
typedef struct NvAnimatorId { u32 index; } NvAnimatorId; // 0 = none

// Inputs, filled on the C side (by the glTF loader).
typedef struct NvJointDesc {
    char name[NV_NODE_NAME_MAX];
    s32 parent;      // -1 for the root
    NvVec3 position; // rest pose
    NvQuat rotation;
    NvVec3 scale;
} NvJointDesc;

typedef struct NvTrackDesc { // keyframes for one joint
    u32 joint;
    u32 translation_count, rotation_count, scale_count;
    const f32 *translation_times, *rotation_times, *scale_times;
    const NvVec3* translations;
    const NvQuat* rotations;
    const NvVec3* scales;
} NvTrackDesc;

// Playback state: plain public structs. Layers blend by weight.
typedef struct NvAnimLayer {
    NvClipId clip;   // 0 = empty layer
    f32 time;
    f32 speed;
    f32 weight;
    b32 loop;
} NvAnimLayer;

typedef struct NvAnimator {
    NvSkeletonId skeleton;
    NvAnimLayer layers[NV_MAX_ANIM_LAYERS];
    f32 fade_duration;
    f32 fade_elapsed;
    NvMat4* joint_model; // [joint count] model-space joint matrices, written by nv_anim_update
} NvAnimator;

void         nv_anim_init(NvArena* permanent); // routes ozz allocations to the arena
NvSkeletonId nv_anim_create_skeleton(const NvJointDesc* joints, u32 joint_count);
NvClipId     nv_anim_create_clip(NvSkeletonId skeleton, const char* name, f32 duration,
                                 const NvTrackDesc* tracks, u32 track_count);
NvAnimatorId nv_anim_create_animator(NvSkeletonId skeleton);
NvAnimator*  nv_anim_get(NvAnimatorId id);
void nv_anim_play(NvAnimator* animator, NvClipId clip, f32 fade_seconds); // crossfades
void nv_anim_update(NvAnimator* animator, f32 dt); // advance, sample, blend, local-to-model
s32  nv_anim_find_joint(NvSkeletonId skeleton, const char* name);        // for attachments
```

- ozz의 `Skeleton` / `Animation` 객체와 샘플링 컨텍스트는 래퍼 안의 고정 테이블에 있다. C 코드는 id만
  가진다.
- ozz 데이터는 실행 중에 C 설명으로부터 만든다 (`RawSkeleton` / `RawAnimation`과 그 빌더). 그래서 웹
  빌드에 네이티브 `gltf2ozz` 단계가 필요 없다. 로딩 시간이 문제가 되면 `.ozz` 파일로의 오프라인 변환으로
  바꾼다 (이미 LFS가 추적함).
- ozz의 할당자는 영구 아레나로 보낸다. 모두 로딩 때 한 번 할당되므로 해제는 무시한다.

#### 씬 통합

- `NvNode`에 `NvAnimatorId animator` 컴포넌트가 생긴다 (0 = 없음).
- 관절별 스키닝 행렬 = 노드 월드 × 관절 모델 행렬 × inverse bind 행렬, 렌더러가 계산한다.
- 프레임 순서: 애니메이터 갱신 → `nv_scene_update` → 렌더 (스키닝 행렬 업로드, 그리기).

#### 웹에서 에셋 전달

에셋은 Emscripten의 `--preload-file`로 패키징하고 평범한 `fopen`으로 읽는다. 패키지는 `main`이 돌기 전에
다운로드된다.

### 단계

1. **렌더러와 정적 glTF.** `nv/renderer.h`를 뽑아내고, `examples/scene`을 그 위로 옮기고, cgltf로 캐릭터를
   불러 bind 포즈로 그린다.
2. **ozz 래퍼와 스키닝.** 스켈레톤과 클립을 만들고, 클립 하나를 재생하고, GPU에서 스키닝한다. 캐릭터가
   걷는다.
3. **블렌딩과 도구.** 크로스페이드와 레이어 가중치; 클립 목록, 속도, 페이드 시간, 가중치를 위한 ImGui 패널;
   뼈 디버그 뷰.
4. **선택.** 관절 부착, 루트 모션 (ozz motion extraction, `_RM` 클립), IK (ozz two-bone과 aim IK).

각 단계는 헤드리스 Chromium에서 확인하고 (포즈 변화를 보려고 시간 간격을 두고 몇 번 캡처), Release와 Debug
빌드 둘 다에서 확인한 뒤 배포한다.

### 구현 메모

실제로 나간 것과 위 계획과 다른 점:

- **에셋:** `clips_rm.glb`는 `clips.glb`의 클립 8개와 함께 Walk_Loop, Jog_Fwd_Loop, Sprint_Loop의 루트 모션
  버전을 담는다. 합계: 캐릭터 685 KB, 클립 439 KB, 루트 모션 클립 166 KB.
- **텍스처**는 C 이미지 라이브러리 대신 브라우저가 디코딩하고 (`createImageBitmap`), 로딩 때 박스 필터로
  밉맵을 만든다.
- **렌더러:** skinned 그리기는 `NvNode.animator`로 인덱싱한 `nv_anim_skins()`에서 행렬을 읽는다.
  애니메이터가 없는 skinned 노드는 bind 포즈로 그려진다.
- **루트 모션**은 ozz의 `MotionExtractor` (`root` 관절의 수평 루트 이동)와 `Float3Track`을 쓴다.
  `nv_anim_update`는 모델 공간 이동을 `NvAnimator.root_motion`에 누적한다. 앱 스펙
  (`docs/specs/app.md`) 이후로는 `nv_anim_update_scene`이 그만큼 애니메이터의 `owner` 노드를 움직이고, 앱은
  회전만 더한다.
- **크로스페이드**는 레이어 0 (페이드 인)과 1 (페이드 아웃)을 쓴다. 앱의 수동 블렌드는 레이어 0과 시간이
  맞춰진 레이어 2를 쓴다.
- **Aim IK**는 ozz의 `IKAimJob`으로 관절 하나 (`Head`)를 고친다. 관절의 앞과 위 축은 하드코딩하지 않고
  rest 포즈에서 찾는다.
- **부착:** `hand_r`에서 검까지의 오프셋은 rest 포즈에서 한 번 계산한다 ("주먹 안에 손잡이, 칼날은 앞").
  검 노드의 `attach` 컴포넌트가 그것을 애니메이션되는 손에 붙여 둔다.
- **메모리:** ozz는 자체 아레나에서 할당한다 (모두 불러온 뒤 약 1 MB).

### 해결된 질문

- 에셋 다듬기: 남겨 둔 스크립트(`tools/trim_assets.sh`)에서 `npx`로 gltf-transform.
- 캐릭터: 우선 남성 캐릭터만.
- 클립: 위에 나열한 8개.
- 텍스처: base color만, 1024 px.

## English

Status: implemented (phases 1–4, 2026-09-26). Changes to this spec are agreed first.

### Goal

Skeletal animation for characters loaded from glTF: play clips, blend and crossfade between them,
and control them from ImGui. Everything runs in the browser like the rest of nv.

### Decisions

| Topic | Decision |
|---|---|
| glTF parsing | [cgltf](https://github.com/jkuhlmann/cgltf) (single-header C, MIT), used from C |
| Animation runtime | [ozz-animation](https://github.com/guillaumeblanc/ozz-animation) (C++17, MIT) behind our own C API; the wrapper is the one C++ file of ours (see `docs/CODING_STANDARD.md`) |
| Character | Quaternius Universal Base Characters (Standard, free), CC0: `Superhero_Male_FullBody` only, to start |
| Animations | Quaternius Universal Animation Library (Standard, free), CC0: 8 clips (below) |
| Textures | Base color only, downscaled to 1024 px; normal and roughness maps wait for PBR shading |
| Binary assets | Git LFS (`.gitattributes`), under `assets/` |
| Asset trimming | gltf-transform through `npx` from `tools/trim_assets.sh`; the tool is not added to the repo |

### Source assets

Both packs come from itch.io ("No thanks, just take me to the downloads"):

- <https://quaternius.itch.io/universal-base-characters> →
  `Universal Base Characters[Standard].zip` (122 MB)
- <https://quaternius.itch.io/universal-animation-library> →
  `Universal Animation Library[Standard].zip` (15 MB)

What they contain (checked 2026-09-26):

- **Characters:** `Base Characters/Godot - UE/Superhero_{Male,Female}_FullBody.gltf` (+ `.bin`,
  PNG textures). One skin with **65 joints**; 3 meshes (body, face, eyes) with `JOINTS_0` /
  `WEIGHTS_0`; 3 materials with base color, normal and roughness textures (PNG, 1–4 MB each).
- **Animations:** `Unreal-Godot/UAL1_Standard.glb` (7.6 MB), a mannequin on the same rig with
  **43 clips**. `UAL1_Standard_RM.glb` is the same with root motion baked in.
- **The 65 joint names match exactly** between the characters and the animation file, so clips
  bind to a character by joint name with no retargeting.

Clips in `UAL1_Standard.glb`: A_TPose, Crouch_Fwd_Loop, Crouch_Idle_Loop, Dance_Loop, Death01,
Driving_Loop, Fixing_Kneeling, Hit_Chest, Hit_Head, Idle_Loop, Idle_Talking_Loop, Idle_Torch_Loop,
Interact, Jog_Fwd_Loop, Jump_Land, Jump_Loop, Jump_Start, PickUp_Table, Pistol_Aim_Down,
Pistol_Aim_Neutral, Pistol_Aim_Up, Pistol_Idle_Loop, Pistol_Reload, Pistol_Shoot, Punch_Cross,
Punch_Jab, Push_Loop, Roll, Sitting_Enter, Sitting_Exit, Sitting_Idle_Loop, Sitting_Talking_Loop,
Spell_Simple_Enter, Spell_Simple_Exit, Spell_Simple_Idle_Loop, Spell_Simple_Shoot, Sprint_Loop,
Swim_Fwd_Loop, Swim_Idle_Loop, Sword_Attack, Sword_Idle, Walk_Formal_Loop, Walk_Loop.

#### What goes into the repository

The full packs are too large for a web download, so `assets/quaternius/` holds a trimmed selection:

- `character.glb`: `Superhero_Male_FullBody` with its 3 meshes, the skin, and base color textures
  downscaled to 1024 px (normal and roughness textures dropped).
- `clips.glb`: the rig from `UAL1_Standard.glb` with these 8 clips and no mesh:
  - Idle_Loop, Walk_Loop, Jog_Fwd_Loop, Sprint_Loop (locomotion, for crossfades),
  - Jump_Start, Jump_Loop, Jump_Land (a chained move),
  - Dance_Loop (a long loop).
- `LICENSE.txt`: the CC0 text and the source URLs above.

The female character and more clips can be added later the same way; both use the same rig.

Trimming uses [gltf-transform](https://gltf-transform.dev/) (Node CLI, MIT), run with `npx` from
`tools/trim_assets.sh`. The script records which files and clips were kept so the assets can be
rebuilt; the tool itself is not added to the repository, the build or CI.

### Architecture

Three new modules. cgltf stays on the C side, ozz stays inside the C++ wrapper, and the two meet
through plain C structs.

#### `nv/renderer.h` (C)

The mesh, material and draw code that lives in `examples/scene` today moves into the engine, now
that glTF loading is a second user of it.

- Mesh registry with two vertex formats: static (position, normal, uv) and skinned (position,
  normal, uv, 4 joint indices, 4 weights).
- Materials: base color factor and texture to start.
- Two pipelines (static, skinned); the renderer owns the depth buffer.
- Per-object data (model matrix, material, offset of its joint matrices) in one storage buffer;
  every character's skinning matrices concatenated in another, uploaded once per frame.

#### `nv/gltf.h` (C, cgltf)

- Loads meshes, materials and textures into the renderer.
- Creates a root scene node per character. Joints are not scene nodes; the ozz skeleton owns them.
- Reads the skin (joint list, inverse bind matrices) and fills `NvJointDesc` for the skeleton.
- Reads clips from a clip file and fills `NvTrackDesc`s, matching glTF channels to skeleton joints
  by name.

#### `nv/anim.h` (C API) + `engine/src/anim.cpp` (ozz wrapper, C++)

```c
#define NV_MAX_SKELETONS   8
#define NV_MAX_CLIPS       64
#define NV_MAX_ANIMATORS   64
#define NV_MAX_JOINTS      128   // the Quaternius rig has 65
#define NV_MAX_ANIM_LAYERS 4

typedef struct NvSkeletonId { u32 index; } NvSkeletonId; // 0 = none
typedef struct NvClipId     { u32 index; } NvClipId;     // 0 = none
typedef struct NvAnimatorId { u32 index; } NvAnimatorId; // 0 = none

// Inputs, filled on the C side (by the glTF loader).
typedef struct NvJointDesc {
    char name[NV_NODE_NAME_MAX];
    s32 parent;      // -1 for the root
    NvVec3 position; // rest pose
    NvQuat rotation;
    NvVec3 scale;
} NvJointDesc;

typedef struct NvTrackDesc { // keyframes for one joint
    u32 joint;
    u32 translation_count, rotation_count, scale_count;
    const f32 *translation_times, *rotation_times, *scale_times;
    const NvVec3* translations;
    const NvQuat* rotations;
    const NvVec3* scales;
} NvTrackDesc;

// Playback state: plain public structs. Layers blend by weight.
typedef struct NvAnimLayer {
    NvClipId clip;   // 0 = empty layer
    f32 time;
    f32 speed;
    f32 weight;
    b32 loop;
} NvAnimLayer;

typedef struct NvAnimator {
    NvSkeletonId skeleton;
    NvAnimLayer layers[NV_MAX_ANIM_LAYERS];
    f32 fade_duration;
    f32 fade_elapsed;
    NvMat4* joint_model; // [joint count] model-space joint matrices, written by nv_anim_update
} NvAnimator;

void         nv_anim_init(NvArena* permanent); // routes ozz allocations to the arena
NvSkeletonId nv_anim_create_skeleton(const NvJointDesc* joints, u32 joint_count);
NvClipId     nv_anim_create_clip(NvSkeletonId skeleton, const char* name, f32 duration,
                                 const NvTrackDesc* tracks, u32 track_count);
NvAnimatorId nv_anim_create_animator(NvSkeletonId skeleton);
NvAnimator*  nv_anim_get(NvAnimatorId id);
void nv_anim_play(NvAnimator* animator, NvClipId clip, f32 fade_seconds); // crossfades
void nv_anim_update(NvAnimator* animator, f32 dt); // advance, sample, blend, local-to-model
s32  nv_anim_find_joint(NvSkeletonId skeleton, const char* name);        // for attachments
```

- ozz `Skeleton` / `Animation` objects and sampling contexts live in fixed tables inside the
  wrapper; C code only holds ids.
- ozz data is built at runtime from the C descriptions (`RawSkeleton` / `RawAnimation` and their
  builders), which avoids a native `gltf2ozz` step in the web build. If load time becomes a
  problem, switch to offline conversion to `.ozz` files (already tracked by LFS).
- ozz's allocator is routed to the permanent arena; frees are ignored because everything is
  allocated once at load.

#### Scene integration

- `NvNode` gets an `NvAnimatorId animator` component (0 = none).
- Skinning matrix per joint = node world × joint model matrix × inverse bind matrix, computed by
  the renderer.
- Frame order: update animators → `nv_scene_update` → render (upload skinning matrices, draw).

#### Asset delivery on the web

Assets are packaged with Emscripten's `--preload-file` and read with plain `fopen`. The package
downloads before `main` runs.

### Phases

1. **Renderer and static glTF.** Extract `nv/renderer.h`, move `examples/scene` onto it, load the
   character with cgltf and draw it in its bind pose.
2. **ozz wrapper and skinning.** Build the skeleton and clips, play one clip, skin on the GPU. The
   character walks.
3. **Blending and tools.** Crossfades and layer weights; ImGui panel for the clip list, speed,
   fade time and weights; a bone debug view.
4. **Optional.** Joint attachments, root motion (ozz motion extraction, the `_RM` clips), IK (ozz
   two-bone and aim IK).

Each phase is checked in headless Chromium (captures a few moments apart to see the pose change)
in both Release and Debug builds, and deployed.

### Implementation notes

What shipped, and where it differs from the plan above:

- **Assets:** `clips_rm.glb` holds the root-motion versions of Walk_Loop, Jog_Fwd_Loop and
  Sprint_Loop alongside the 8 clips in `clips.glb`. Totals: character 685 KB, clips 439 KB,
  root-motion clips 166 KB.
- **Textures** are decoded by the browser (`createImageBitmap`) rather than a C image library, and
  get mipmaps from a box filter at load.
- **Renderer:** skinned draws read their matrices from `nv_anim_skins()`, indexed by
  `NvNode.animator`; a skinned node without an animator draws in its bind pose.
- **Root motion** uses ozz's `MotionExtractor` (horizontal root translation of the `root` joint)
  and a `Float3Track`; `nv_anim_update` accumulates the model-space motion in
  `NvAnimator.root_motion`. Since the app spec (`docs/specs/app.md`), `nv_anim_update_scene`
  moves the animator's `owner` node by it; the app only adds turning.
- **Crossfades** use layers 0 (fading in) and 1 (fading out); the app's manual blend uses
  layer 2, time-synchronized to layer 0.
- **Aim IK** corrects one joint (`Head`) with ozz's `IKAimJob`. The joint's forward and up axes are
  found from the rest pose instead of being hard-coded.
- **Attachment:** the sword's offset from `hand_r` is computed once from the rest pose ("grip in the
  fist, blade forward"). The sword node's `attach` component keeps it on the animated hand.
- **Memory:** ozz allocates from its own arena (about 1 MB after loading everything).

### Resolved questions

- Asset trimming: gltf-transform through `npx`, from a kept script (`tools/trim_assets.sh`).
- Character: the male character only, to start.
- Clips: the 8 listed above.
- Textures: base color only, 1024 px.

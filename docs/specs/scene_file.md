# Scene file spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 제안 (2026-10-11). 구현 전이다. 이 스펙의 변경은 먼저 합의한다.

### 목표

샌드박스의 씬을 텍스트 파일로 기술하고 로더 하나로 만든다. 그리고 두 씬을 한 벌의 코드로 돌린다.

지금은:

- `build_world`, `build_character`(`sandbox/main.c`)와 `stress_build`(`sandbox/stress.c`)가 씬을 C로 만든다. 이름, 위치, 메시, 색이
  모두 코드의 상수다.
- 무엇이 도는지가 씬마다 다르다.
  - 쇼케이스의 동작은 `Sandbox`의 필드(행성, 달, 캐릭터, 클립, 점프, 칼, 시선 타깃)와 `update_showcase`의 분기다.
  - 스트레스 씬의 동작은 `Stress`와 `stress_update`다. 이펙트 워크로드는 `Effects` 안에 있다.
  - `frame`, View 탭, Inspector가 `SceneKind`로 분기한다.

바뀐 뒤에는:

- `sandbox/data/showcase.nvscene`과 `sandbox/data/stress.nvscene`이 각 씬을 기술한다. 노드, 메시, 머티리얼, 모델, 뷰, 그리고 노드
  위에서 도는 **동작**(behavior)이 들어 있다.
- 동작은 C로 쓴 고정된 종류(`spin`, `character`, `grid`, `chain`, `crowd`, `effects`)이고, 파라미터는 파일에서 온다.
  - 업데이트 하나가 보이는 씬의 동작을 모두 돌린다.
  - Inspector는 선택한 노드의 동작마다 섹션 하나를 보인다.
  - 어느 쪽도 지금 어느 씬인지 따지지 않는다.
- 씬마다 다른 것은 코드가 아니라 파일의 머리(`mode`)에 있다.

이 스펙은 씬을 만드는 방법의 리팩터링이다. 사용자가 보고 하는 것, 저장 형식과 그 태그, 디버그 내보내기, undo, Play/Stop은 그대로다.
예외는 "사용자가 보는 변화"에 모았다.

### 이 스펙에 없는 것

- 에디터에서 노드를 추가하고 제거하는 것, 그리고 씬 전체를 담는 저장(`SAVE_VERSION` 2가 필요하다). 파일 형식은 나중 스펙이 이것을
  할 수 있게 짠다("나중" 절).
- 사용자 컴퓨터의 `.nvscene` 파일을 열고 저장하는 것(`local_file.h`).
- 리더를 엔진으로 옮기는 것. 오토배틀러는 씬을 `.abproj`에서 만든다. `shared.md`에 따라 코드는 두 번째 앱이 쓸 때 옮긴다.

### 형식 후보 (AGENTS.md에 따라 서드파티부터)

| 후보 | 라이브러리 (라이선스) | 장점 | 단점 |
|---|---|---|---|
| **`.abproj` 같은 줄 기반 자체 텍스트** (제안) | 없음 (토크나이저를 `autobattler/defs.c`에서 엔진으로 옮긴다) | 이미 아는 프로젝트 파일과 같은 문법. Git에서 diff가 보인다. 오류에 파일과 줄이 붙는다. 토크나이저의 두 번째 사용처가 되어, 엔진으로 옮기는 규칙에 맞는다 | 리더를 직접 쓴다 |
| glTF 2.0 + `extras` | cgltf (MIT; 이미 쓴다. `cgltf_write.h`로 쓰기도 된다) | 표준. Blender가 연다 | 프리미티브 메시는 정점 데이터나 extras가 된다. 모델 참조, 관절 부착, 동작, 뷰가 모두 extras라서, glTF로 감싼 JSON 자체 스키마가 된다 |
| JSON | jsmn (MIT; cgltf 안에 이미 있다) | 익숙하다 | 손으로 고치기 번잡하고 주석이 없다 |
| TOML | tomlc99 (MIT) | 주석, 읽기 쉬움 | 새 의존성. 노드 안의 노드처럼 깊은 트리가 TOML에서 어색하다 |
| 청크 바이너리 (`engine/chunk.h`) | 없음 | 저장과 같은 코드 | 손으로 고칠 수 없다. Git에 바이너리(LFS)가 된다 |

제안: 자체 텍스트. 새 서드파티 코드는 없다.

### 파일

| 파일 | 패키지 안의 경로 | 내용 |
|---|---|---|
| `sandbox/data/showcase.nvscene` | `/data/showcase.nvscene` | 쇼케이스 |
| `sandbox/data/stress.nvscene` | `/data/stress.nvscene` | 스트레스 씬과 벤치마크 단계 |

- 텍스트이므로 일반 Git에 둔다(LFS 패턴을 더하지 않는다).
- `sandbox/CMakeLists.txt`: `nv_setup_executable(sandbox ROOT ASSETS ... PRELOAD "${CMAKE_CURRENT_SOURCE_DIR}/data@/data")`.
- `nv_file_read`로 임시 아레나에 읽고, `SceneDesc`(GPU 없는 평범한 데이터)로 파싱한 뒤 만든다.

### 문법

`.abproj`와 같다(`abproj.md`, "문법"): UTF-8, 한 줄에 문장 하나, `#` 주석, 공백 들여쓰기가 중첩을 정함, 탭은 오류, 모르는 키나 값의
개수가 틀린 줄은 파일과 줄 번호가 붙은 오류. 더하는 것:

- **텍스트:** 큰따옴표로 감싼 31바이트까지의 문자열. 공백이 든 노드 이름에 쓴다(`"look target"`). 이스케이프는 없다.
- **각도**는 파일에서 도(degree)다. 회전은 둘 중 하나다:
  - `rotation x y z w`: 쿼터니언, 정확하다.
  - `euler pitch yaw roll`: 도, `nv_quat_from_euler`로 바꾼다.
- **계산으로 나온 값**(태양의 회전, 칼의 오프셋)은 유효숫자 9자리로 쓴다. 그래서 읽으면 같은 float가 된다.
- **최상위 문장의 순서가 정해져 있다:** `nvscene_version` → `mode` → `view` → `mesh` 0개 이상 → `model` 0개 이상 → `node` 1개 이상
  → `benchmark` (`live` 씬만, 0개 또는 1개). 그래서 노드가 메시와 모델을 가리킬 때 그것들이 이미 읽혀 있다.

### 구조

#### `nvscene_version <정수>`

첫 문장. 지금은 `1`. 규칙은 `abproj_version`과 같다.

#### `mode <단어>`

| | `edit_play` (쇼케이스) | `live` (스트레스) |
|---|---|---|
| 무언가를 움직이는 동작 | 재생 중에만 (`play.md`) | 항상 |
| Play / Stop | 보인다 | 숨긴다 |
| Undo | 켜짐 | 꺼짐 |
| 자동 저장 | 씬을 저장한다 | 저장하지 않는다 |

지금 `SceneKind` 검사로 나뉘는 차이를 그대로 데이터로 옮긴 것이다. `edit_play` 씬은 많아야 하나다(저장이 씬 하나를 담는다).
모든 씬은 처음 보일 때 만들어진다. 쇼케이스는 시작할 때 보인다.

#### `view`

| 키 | 값 | |
|---|---|---|
| `camera <id>` | 카메라가 있는 노드 | 필수 |
| `target x y z` | 궤도 중심, m | 기본 0 0 0 |
| `yaw`, `pitch` | 도 | 필수 |
| `distance` | m | 필수 |
| `focus <id>` | 아무것도 선택되지 않았을 때 도는 중심 노드 | 선택 |
| `select <id>` | 시작할 때 선택된 노드 | 선택 |

`ORBIT_LIMITS`는 코드에 남는다. 그것은 씬의 내용이 아니라 에디터의 한계다.

#### `mesh <id>`

블록 안의 프리미티브 문장 하나 이상을 메시 하나에 이어 붙인다. 키와 값 순서는 `engine/mesh.h`의 함수를 그대로 따른다.

| 키 | 값 |
|---|---|
| `box` | cx cy cz hx hy hz |
| `plane` | hx hz |
| `sphere` | cx cy cz radius segments rings |
| `cylinder` | cx cy cz radius half_height segments |
| `cone` | cx cy cz radius half_height segments |
| `capsule` | cx cy cz radius half_height segments rings |
| `torus` | cx cy cz major minor segments tube_segments |

칼은 `box` 세 줄이다. 지금의 `create_sword_mesh`가 그대로 데이터가 된다.

#### `model <id> <텍스트: 경로>`

| 키 | 값 | |
|---|---|---|
| `clips <텍스트: 경로>` | 제자리 클립 파일 | 선택 |
| `root_motion_clips <텍스트: 경로> <관절 이름>` | 루트 모션 클립 파일과 루트 관절 | 선택 |

- 모델은 경로마다 샌드박스 전체에서 한 번만 읽는다. 그리지 않는 **라이브러리 씬**에 `nv_gltf_load_model`로 읽어 두고, 노드는
  `nv_gltf_instantiate`로 복제한다.
- 그래서 두 씬이 같은 경로를 쓰면 메시, 스켈레톤, 클립을 함께 쓴다. 지금 군중이 쇼케이스 캐릭터를 복제하는 것과 같다.
- 라이브러리 씬의 애니메이터는 업데이트되지 않는다(`nv_anim_update_scene`은 자기 씬만 돈다). 스킨이 있는 모델마다 애니메이터 하나가
  쉬는 대가가 있다.

#### `node <id>`

블록 안의 `node` 블록이 자식이다. 파일의 순서가 트리의 순서다.

| 키 | 값 | |
|---|---|---|
| `name <텍스트>` | 보이는 이름(`NvNode.name`) | 기본: id |
| `position x y z` | m | 기본 0 |
| `rotation x y z w` 또는 `euler p y r` | | 기본 단위 회전 |
| `scale x y z` | | 기본 1 1 1 |
| `mesh <mesh id>` | | |
| `color r g b` | 노드 자신의 머티리얼 (Inspector가 색을 바꾸므로 노드마다 하나) | 기본 머티리얼 |
| `double_sided <0/1>` | | 0 |
| `visible <0/1>` | 메시를 그리는지 | 1 |
| `camera perspective <fov> <near> <far>` | fov는 도 | |
| `light directional` | 방향광 | |
| `light_color r g b`, `light_intensity <x>` | | 1 1 1, 1 |
| `model <model id>` | 노드가 모델의 루트가 된다. 모델의 메시 노드(glTF의 이름)가 첫 자식들로 만들어지고, 그 뒤에 파일의 자식 노드가 온다 | |
| `attach <관절 이름>` | 가장 가까운 조상 모델의 애니메이터에서 그 관절을 따라간다 (칼: `attach hand_r`) | |
| 동작 블록 | 아래 | |

- **id**는 파일 안에서 유일한 이름(`[A-Za-z_][A-Za-z0-9_]*`)이다. 동작, 뷰, 벤치마크, 저장의 매핑(아래)이 노드를 id로 가리킨다.
- 실행 중에는 `SceneDoc`이 파일 노드마다 id와 `NvNodeId`의 짝을 지닌다.
- Inspector가 이름을 바꿀 수 있으므로 id는 이름이 아니다.

#### 동작

노드 아래의 블록이다. 노드 하나에 같은 종류는 하나까지, 씬 하나에 64개까지다. 각 종류는 C의 `switch` 한 갈래다. 코딩 표준에 따라
함수 포인터나 vtable은 쓰지 않는다.

**`spin`**: 저작한 회전 위에 축을 따라 돈다. 지금의 행성 궤도와 달의 회전을 대신한다.

| 키 | 값 | |
|---|---|---|
| `axis x y z` | 읽을 때 정규화 | 필수 |
| `speed <도/초>` | | 필수 |

Play를 누르면 노드의 회전을 저작한 회전으로 기록하고(지금의 `planet_rotation`, `moon_rotation`), 재생 시간에 따라 그 위에 돈다.

**`character`**: 모델 노드의 클립 조작. 지금의 `animator_section`과 `Sandbox`의 필드를 대신한다.

| 키 | 값 | |
|---|---|---|
| `clip <이름>` | 시작 클립 | 첫 `_Loop` 클립 |
| `speed`, `fade` | 배, 초 | 1, 0.3 |
| `blend <이름>`, `blend_weight` | | 첫 클립, 0 |
| `root_motion <0/1>`, `turn <도/초>` | | 0, 34.38 |
| `look_at <관절> <노드 id>` | 시선 IK. 축은 레스트 포즈에서 계산한다(`joint_axis_towards`). 재생 중에는 타깃이 머리 앞을 쓸고, 시선이 꺼지면 타깃을 숨긴다 | 선택 |
| `jump <시작 클립> <공중 클립> <착지 클립> <공중 시간>` | 점프의 세 클립 | 선택; 없으면 Jump 버튼 없음 |

점프 단계, 돌아갈 클립, 재생 시간은 동작의 실행 상태다.

**생성기** (`live` 씬만; `edit_play` 씬에 있으면 오류): 노드 아래에 노드를 만든다. 지금의 스트레스 워크로드다. 만든 노드는 저장하거나
undo하지 않는다. 각 생성기의 큰 배열은 빌드할 때 `max`에 맞춰 영구 아레나에서 받는다.

| 종류 | 키 (지금 값) | 지금 코드 |
|---|---|---|
| `grid` | `on 1`, `count 1000`, `max 16000`, `columns 128`, `spacing 0.9`, `mesh cube`, `scale 0.5`, `height 0.25`, `colors_on 0`, `colors 16`, `palette 200`, `churn_on 0`, `churn 32` | `update_grid`, `churn` |
| `chain` | `on 0`, `count 300`, `max 1000`, `mesh small_cube`, `link 0.12 0.03 0`, `twist 14.32`, `wobble 2.29` | `update_chain` |
| `crowd` | `on 1`, `count 16`, `max 200`, `model character`, `columns 20`, `spacing 1.6`, `offset 0.37` | `update_crowd` |
| `effects` | `on 0`, `particles`, `explosions`, `missiles`, `beams`, `decals`, `area 16 12` | `effects_stress_update` |

- 노드 예산은 지금처럼 정해진 순서로 나눈다: crowd, chain, grid. 각자 남은 노드(`NV_MAX_NODES`에서 생성되지 않은 노드를 뺀 것)와
  남은 애니메이터까지만 받는다. 지금의 `FIXED_NODES`, `NV_MAX_ANIMATORS - 2` 같은 상수는 실제 수를 세는 것으로 바뀐다.
- `effects`는 등록된 이펙트(`effects.c`)를 쏜다. 미사일 비행 상태는 이 동작의 실행 상태로 옮긴다.

#### `benchmark` (`live` 씬만)

```
benchmark
    warmup 1
    measure 3
    step "Cubes 250"
        grid count 250
    step "Particles 1M"
        effects particles 1000000
```

- 단계 안의 줄은 `<노드 id> <키> <값>`이다. 그 노드의 생성기를 켜고 값을 바꾼다. 단계에 이름이 나오지 않는 생성기는 그 단계 동안
  꺼진다.
- 그 밖의 설정(색 수, churn 수)은 실행 전 값을 유지한다. 실행이 끝나면 실행 전의 설정을 되돌린다. 지금과 같다.
- 단계는 16개까지다. 지금의 `steps[]` 표가 그대로 파일로 간다.

### 바뀐 뒤의 코드

```c
typedef enum SceneMode { SCENE_MODE_EDIT_PLAY, SCENE_MODE_LIVE } SceneMode;

typedef struct Behavior {
    BehaviorKind kind;
    NvNodeId node;
    union { Spin spin; Character character; Grid grid; Chain chain; Crowd crowd; EffectsLoad effects; };
} Behavior;

typedef struct SceneDoc {           // 읽어서 만든 씬 하나
    b32 built;
    SceneMode mode;
    NvScene* scene;
    SceneView view;                 // 지금의 Sandbox.views[kind]
    FileNode nodes[SCENE_MAX_FILE_NODES]; // 파일 노드: id와 NvNodeId
    u32 node_count;
    Behavior behaviors[SCENE_MAX_BEHAVIORS];
    u32 behavior_count;
    BenchmarkStep steps[STRESS_MAX_STEPS];
    u32 step_count;
    bool show_bones;
} SceneDoc;
```

| 파일 | 맡는 것 |
|---|---|
| `sandbox/scene_file.c` (새로) | 텍스트 → `SceneDesc`. GPU 없음. Node에서 테스트한다 |
| `sandbox/scenes.c` (새로) | `SceneDesc` → `NvScene` + `SceneDoc`, 모델 라이브러리, 씬 전환. `build_world`, `build_character`, `stress_build`를 대신한다 |
| `sandbox/behaviors.c` (새로) | 동작 업데이트(종류마다 `switch`), Inspector의 동작 섹션. `update_showcase`, `update_jump`, `update_blend`, `apply_turn`, `update_look_target`, `stress_update`, `effects_stress_update`를 대신한다 |
| `sandbox/stress.c` | 통계, 벤치마크 실행기, Stress 탭. 워크로드 줄은 씬의 생성기를 차례로 그린다 |
| `sandbox/effects.c` | 이펙트 정의와 시험 발사만 |
| `sandbox/main.c` | 초기화와 프레임 순서만 |

- `Sandbox`에서 빠지는 것: `planet`, `moon`, `orbit_angle`, `orbit_speed`, `character`, `animator`, `clips`, `root_motion_clips`,
  `fade_seconds`, `blend_clip`, `blend_weight`, `jump*`, `show_sword`, `sword`, `sword_mesh`, `root_motion`, `turn_rate`, `look_at`,
  `target`, `target_mesh`, `planet_rotation`, `moon_rotation`, `show_bones`, `stress`의 워크로드, `effects.stress_*`.
- 프레임은 보이는 씬의 `SceneDoc`에 대해 `behaviors_update(sandbox, doc, dt)` 하나를 부른다.
  - 움직이는 동작은 `live`이거나 재생 중일 때만 돈다.
  - 애니메이션 미리보기(크로스페이드, 블렌드)는 지금처럼 Edit 모드에서도 돈다.
- `SceneKind`는 `scenes[]`의 인덱스로 남는다. 씬 콤보가 쓴다.
- `frame`, UI, undo, 저장의 `shown == SCENE_SHOWCASE` 검사는 `doc->mode` 검사가 된다.

### 저장, undo, Play

- `SAVE_VERSION`과 기존 태그의 뜻은 그대로다. 저장은 `edit_play` 씬을 읽고 쓴다.
- `showcase.nvscene`이 만드는 트리는 지금과 같아야 한다(이름, 순서, 깊이). 그래야 `LAYT`가 같고 기존 저장이 모두 읽힌다.
- 쇼케이스만의 태그는 노드 id로 찾아간다.

| 태그 | 지금 | 바뀐 뒤 |
|---|---|---|
| `PLNT` | `orbit_speed` | id `planet` 노드의 `spin` 속도 (rad/s, 지금처럼). 쓰기는 계속하고, 읽기는 그 노드의 `NODE`에 `SPIN`이 없을 때만 |
| `CHAR` | `Sandbox`의 캐릭터 필드 | id `character` 노드의 `character` 동작 |
| `SWRD` (`CHAR` 안) | `show_sword` | id `sword` 노드의 `visible`. 쓰기는 계속하고, 읽기는 `VISI`가 없을 때만 |
| `LOOK` (`CHAR` 안) | `look_at` | `character` 동작의 시선 켜짐 |
| `BONE` | `show_bones` | 씬의 `show_bones` |
| `LAYT`, `VIEW`, `NODE`, 나머지 | | 그대로 |

- **새 태그** (태그를 더하는 것은 버전을 올리지 않는다, `save.md`):

| 태그 | 위치 | 형 | 내용 |
|---|---|---|---|
| `SPIN` | `NODE` | f32 | `spin` 동작이 있는 노드의 속도, rad/s |
| `VISI` | `NODE` | u32 | 메시가 있는 노드의 `visible` |

- 옛 빌드가 새 저장을 읽을 수 있도록 `PLNT`와 `SWRD`도 계속 쓴다.
- 파일에서 id가 사라지면(`planet`을 지운 파일) 그 태그는 건너뛴다.
- undo: Node 범위는 `SPIN`과 `VISI`를 저절로 담는다(`write_node_fields`). 그래서 회전 속도와 보이기 편집도 undo된다.
  - Character 범위는 id `character`의 동작을 가리킨다.
  - Scene 범위는 `BONE`만 남는다. undo 전용 태그 `ORBS`는 없어진다. 저장에는 없던 태그다.
- `driven_fields`(궤도 카메라)와 Play 스냅샷은 그대로다.

### 사용자가 보는 변화

- View 탭의 "Planet orbit" 슬라이더가 행성의 Inspector **Spin** 섹션(Speed)으로 옮겨 간다. 달의 회전도 같은 방식으로 편집된다.
- "Show bones"가 두 씬 모두 View 탭에 있다. Stress 탭의 "Crowd bones"는 여기로 합친다. 씬의 모든 애니메이터의 뼈를 그린다.
- 메시가 있는 모든 노드의 Inspector에 "Visible"이 생긴다. 지금은 칼만 있다.
- 스트레스 씬의 노드도 동작 섹션을 보인다. 예를 들어 "grid" 그룹을 선택하면 Grid 섹션이 보인다. Stress 탭의 워크로드 줄과 같은
  값이다.

### 한계

| 항목 | 한계 |
|---|---|
| 파일 노드 | 파일당 256 (`SCENE_MAX_FILE_NODES`) |
| 메시 | 파일당 32, 각 4096 정점 이하 |
| 모델 | 샌드박스 전체 8 |
| 동작 | 씬당 64 |
| 벤치마크 단계 | 16 |
| id, 이름 | 31바이트 |

### 오류

- 패키지의 파일이 읽히지 않는 것은 버그다. 오류는 파일과 줄을 붙여 `nv_log`(source `"scene"`)로 남긴다. 16개까지다.
- 쇼케이스 파일이 읽히지 않으면 지금 캐릭터를 읽지 못할 때처럼 `fatal`로 멈춘다.
- 스트레스 파일이 읽히지 않으면 씬 전환이 거절되고, View 탭이 첫 오류를 보인다.
- ctest가 패키지의 두 파일이 읽히는지 확인한다. 형식의 각 규칙은 테스트 자신의 텍스트로 시험한다.

### 엔진 변경

- `engine/text.h`, `engine/text.c`: `autobattler/defs.c`의 줄 기반 텍스트 읽기를 옮긴다. 들여쓰기가 있는 줄, 토큰, 숫자, 이름,
  따옴표 텍스트, 파일과 줄이 붙은 오류 목록이다.
  - `defs.c`가 이것을 쓴다.
  - `.abproj`의 읽기와 오류 문구는 전과 같다. `battle_test`와 기존 테스트는 바뀌지 않고 통과한다.
- `nv_gltf_instantiate`: 메시 노드의 로컬 트랜스폼을 복사하지 않는다. 캐릭터의 메시 노드가 단위 트랜스폼인지 확인한다. 아니면
  복사하도록 고친다.
- 장면, 렌더러, 애니메이션 API의 다른 변경은 없다.

### 단계

1. **텍스트 리더:** `engine/text.*`. `defs.c`가 그것을 쓴다. 확인: ctest, 오토배틀러 테스트가 그대로 통과한다.
2. **씬 파일 리더:** `sandbox/scene_file.c`와 `tests/scene_file_test.c`. 확인: 패키지의 파일이 읽힌다, 각 규칙의 오류가 맞다.
3. **쇼케이스를 파일로:**
   - 할 일: `showcase.nvscene`, 모델 라이브러리, `spin`/`character` 동작, `visible`, Inspector 섹션, `SPIN`/`VISI`.
   - 지우는 것: `build_world`, `build_character`, `update_showcase`, `Sandbox`의 쇼케이스 필드.
   - 확인:
     - `LAYT`가 지금 값과 같다.
     - 지금 빌드가 쓴 저장(변경 전에 만든 고정 파일)이 읽히고 같은 씬이 된다.
     - Play 전과 Stop 뒤의 `_sandbox_debug_save_crc`가 같다.
     - 첫 방문의 노드 값이 지금과 1e-5 안에서 같다. 도 변환이 마지막 비트를 바꿀 수 있어서 CRC 대신 이렇게 본다.
4. **스트레스를 파일로:**
   - 할 일: `stress.nvscene`, 생성기 네 종류, 파일의 벤치마크 단계.
   - 지우는 것: `stress_build`의 노드 코드, `StressWorkloads`, `effects_stress_update`.
   - 확인: 각 워크로드가 최대에서 assert 없이 돈다, 벤치마크가 끝까지 돈다, 표의 단계 이름이 같다.
5. **문서:** `AGENTS.md`, `stress.md`, `save.md`(새 태그와 매핑), `undo.md`, `play.md`, `sandbox.h` 머리 주석.

모든 단계는 헤드리스 Chromium에서 Release와 Debug 빌드로 확인한다.

### 나중

- **씬을 담는 저장** (`SAVE_VERSION` 2):
  - `NODE`가 구조(메시, 모델, 동작)까지 담아, 읽으면 노드가 만들어진다.
  - 노드 추가, 삭제, 복제가 된다.
  - 새 저장에는 `LAYT`와 경로가 필요 없다. v1 리더는 남긴다.
- **`.nvscene` 열기와 저장** (`local_file.h`).
- **엔진으로 옮기기:** 오토배틀러가 스테이지를 씬 파일로 만들 때 리더와 빌더를 엔진으로 옮긴다. 동작은 옮기지 않는다.
- **`edit_play` 씬의 생성기.**

### 열린 질문

1. 형식: 자체 텍스트(제안)인가, glTF + extras인가?
2. `mode`: 스트레스를 `live`로 두는가(제안: 벤치마크가 Play 없이 돈다), 스트레스도 Edit/Play를 따르게 하는가?
3. "Planet orbit"이 View 탭에서 Inspector로 옮겨 가도 되는가?
4. 모델 라이브러리 씬은 스킨 있는 모델마다 쉬는 애니메이터 하나를 쓴다. 괜찮은가? 대안은 지금처럼 처음 쓰는 씬에 읽고 다른 씬이
   그것을 복제하는 것이다.

## English

Status: proposal (2026-10-11). Not implemented. Changes to this spec are agreed first.

### Goal

Describe the sandbox's scenes in text files and build them with one loader. Then run the two scenes with one set of code.

Today:

- `build_world` and `build_character` (`sandbox/main.c`) and `stress_build` (`sandbox/stress.c`) build the scenes in C. Every name,
  position, mesh and color is a constant in code.
- What runs differs per scene.
  - The showcase's behavior is fields of `Sandbox` (planet, moon, character, clips, jump, sword, look target) and branches in
    `update_showcase`.
  - The stress scene's is `Stress` and `stress_update`. The effects workload lives in `Effects`.
  - `frame`, the View tab and the Inspector branch on `SceneKind`.

After:

- `sandbox/data/showcase.nvscene` and `sandbox/data/stress.nvscene` describe each scene: nodes, meshes, materials, models, the view,
  and the **behaviors** that run on nodes.
- Behaviors are a fixed set of kinds written in C (`spin`, `character`, `grid`, `chain`, `crowd`, `effects`), with parameters from
  the file.
  - One update runs every behavior of the shown scene.
  - The Inspector shows a section per behavior of the selected node.
  - Neither asks which scene it is in.
- What differs per scene is in the file's header (`mode`), not in code.

This spec is a refactor of how scenes are made. What the user sees and does, the save format and its tags, the debug exports, undo
and Play/Stop stay as they are. The exceptions are collected under "Changes users see".

### Not in this spec

- Adding and removing nodes in the editor, and a save that holds the whole scene (it needs `SAVE_VERSION` 2). The format is designed
  so a later spec can do it ("Later").
- Opening and saving `.nvscene` files on the user's computer (`local_file.h`).
- Moving the reader into the engine. The auto-battler builds its scene from `.abproj`; per `shared.md`, code moves when a second
  app uses it.

### Format candidates (third-party first, per AGENTS.md)

| Candidate | Library (license) | Pros | Cons |
|---|---|---|---|
| **Our own line-based text, like `.abproj`** (proposed) | None (the tokenizer moves from `autobattler/defs.c` into the engine) | The same syntax as the project file we already know. Diffs show in Git. Errors carry a file and line. The tokenizer gets its second user, which is the rule for moving it into the engine | We write the reader |
| glTF 2.0 + `extras` | cgltf (MIT; already used, and `cgltf_write.h` writes) | Standard. Blender opens it | Primitive meshes become vertex data or extras. Model references, joint attachments, behaviors and the view would all be extras: our own schema in JSON with glTF around it |
| JSON | jsmn (MIT; already inside cgltf) | Familiar | Noisy to edit by hand, no comments |
| TOML | tomlc99 (MIT) | Comments, readable | A new dependency. Deep trees (nodes in nodes) are awkward in TOML |
| Chunk binary (`engine/chunk.h`) | None | The same code as the save | Not editable by hand. A binary in Git (LFS) |

Proposed: our own text. No new third-party code.

### Files

| File | Path in the package | Contents |
|---|---|---|
| `sandbox/data/showcase.nvscene` | `/data/showcase.nvscene` | The showcase |
| `sandbox/data/stress.nvscene` | `/data/stress.nvscene` | The stress scene and its benchmark steps |

- Text, so plain Git (no LFS pattern).
- `sandbox/CMakeLists.txt`: `nv_setup_executable(sandbox ROOT ASSETS ... PRELOAD "${CMAKE_CURRENT_SOURCE_DIR}/data@/data")`.
- Read with `nv_file_read` into scratch, parsed into a `SceneDesc` (plain data, no GPU), then built.

### Syntax

As `.abproj` (`abproj.md`, "Syntax"): UTF-8, one statement per line, `#` comments, indentation with spaces sets nesting, a tab is an
error, an unknown key or a line with the wrong number of values is an error with its file and line. Additions:

- **Text:** a double-quoted string of up to 31 bytes, for node names with spaces (`"look target"`). No escapes.
- **Angles** are degrees in the file. A rotation is one of:
  - `rotation x y z w`: a quaternion, exact.
  - `euler pitch yaw roll`: degrees, converted with `nv_quat_from_euler`.
- **Computed values** (the sun's rotation, the sword's offset) are written with 9 significant digits, so they read back as the same
  float.
- **Top-level statements come in a fixed order:** `nvscene_version` → `mode` → `view` → zero or more `mesh` → zero or more `model` →
  one or more `node` → `benchmark` (`live` scenes only, zero or one). So meshes and models are read by the time nodes refer to them.

### Structure

#### `nvscene_version <integer>`

The first statement. Now `1`. The same rules as `abproj_version`.

#### `mode <word>`

| | `edit_play` (showcase) | `live` (stress) |
|---|---|---|
| Behaviors that move things | Only while playing (`play.md`) | Always |
| Play / Stop | Shown | Hidden |
| Undo | On | Off |
| Autosave | The scene is saved | Not saved |

This is today's difference, which `SceneKind` checks make, moved into data unchanged. There is at most one `edit_play` scene (the
save holds one scene). Every scene is built the first time it is shown; the showcase is shown at start.

#### `view`

| Key | Value | |
|---|---|---|
| `camera <id>` | A node with a camera | Required |
| `target x y z` | The orbit's center, m | Default 0 0 0 |
| `yaw`, `pitch` | Degrees | Required |
| `distance` | m | Required |
| `focus <id>` | The node orbited while nothing is selected | Optional |
| `select <id>` | The node selected at start | Optional |

`ORBIT_LIMITS` stays in code: it is the editor's limit, not scene content.

#### `mesh <id>`

One or more primitive statements in the block, appended into one mesh. Keys and value order follow the functions in
`engine/mesh.h`.

| Key | Values |
|---|---|
| `box` | cx cy cz hx hy hz |
| `plane` | hx hz |
| `sphere` | cx cy cz radius segments rings |
| `cylinder` | cx cy cz radius half_height segments |
| `cone` | cx cy cz radius half_height segments |
| `capsule` | cx cy cz radius half_height segments rings |
| `torus` | cx cy cz major minor segments tube_segments |

The sword is three `box` lines: today's `create_sword_mesh` becomes data as it is.

#### `model <id> <text: path>`

| Key | Value | |
|---|---|---|
| `clips <text: path>` | The in-place clip file | Optional |
| `root_motion_clips <text: path> <joint name>` | The root-motion clip file and its root joint | Optional |

- A model is read once per path for the whole sandbox, with `nv_gltf_load_model`, into a **library scene** that is never drawn.
  Nodes copy it with `nv_gltf_instantiate`.
- So two scenes naming the same path share its meshes, skeleton and clips, as the crowd shares the showcase character today.
- The library scene's animators are never updated (`nv_anim_update_scene` runs only its own scene's). The cost is one idle animator
  per skinned model.

#### `node <id>`

`node` blocks inside the block are its children. The file's order is the tree's order.

| Key | Value | |
|---|---|---|
| `name <text>` | The shown name (`NvNode.name`) | Default: the id |
| `position x y z` | m | Default 0 |
| `rotation x y z w` or `euler p y r` | | Default identity |
| `scale x y z` | | Default 1 1 1 |
| `mesh <mesh id>` | | |
| `color r g b` | The node's own material (one per node, since the Inspector changes colors) | The default material |
| `double_sided <0/1>` | | 0 |
| `visible <0/1>` | Whether its mesh is drawn | 1 |
| `camera perspective <fov> <near> <far>` | fov in degrees | |
| `light directional` | A directional light | |
| `light_color r g b`, `light_intensity <x>` | | 1 1 1, 1 |
| `model <model id>` | The node becomes the model's root. The model's mesh nodes (named as in the glTF) are made as its first children, then the file's child nodes follow | |
| `attach <joint name>` | Follows that joint of the nearest ancestor model's animator (the sword: `attach hand_r`) | |
| Behavior blocks | Below | |

- An **id** is a name unique in its file (`[A-Za-z_][A-Za-z0-9_]*`). Behaviors, the view, the benchmark and the save's mapping
  (below) refer to nodes by id.
- At run time, `SceneDoc` keeps an id and `NvNodeId` pair per file node.
- An id is not the name, since the Inspector can rename nodes.

#### Behaviors

Blocks under a node. At most one of each kind per node, and at most 64 per scene. Each kind is one branch of a C `switch`; per the
coding standard there are no function pointers or vtables.

**`spin`**: turns about an axis on top of the authored rotation. It replaces today's planet orbit and moon spin.

| Key | Value | |
|---|---|---|
| `axis x y z` | Normalized when read | Required |
| `speed <degrees/s>` | | Required |

Play records the node's rotation as the authored one (today's `planet_rotation`, `moon_rotation`), and the spin turns on top of it
with the play time.

**`character`**: the clip controls of a model node. It replaces today's `animator_section` and `Sandbox` fields.

| Key | Value | |
|---|---|---|
| `clip <name>` | The clip at start | The first `_Loop` clip |
| `speed`, `fade` | Times, seconds | 1, 0.3 |
| `blend <name>`, `blend_weight` | | The first clip, 0 |
| `root_motion <0/1>`, `turn <degrees/s>` | | 0, 34.38 |
| `look_at <joint> <node id>` | Aim IK. Its axes come from the rest pose (`joint_axis_towards`). While playing the target sweeps in front of the head; with look-at off the target is hidden | Optional |
| `jump <start clip> <air clip> <land clip> <air time>` | The jump's three clips | Optional; without it, no Jump button |

The jump phase, the clip to return to and the play time are the behavior's run-time state.

**Generators** (`live` scenes only; an error in an `edit_play` scene): they make nodes under their node. They are today's stress
workloads. The nodes they make are never saved or undone. Each generator's large arrays come from the permanent arena at build,
sized by its `max`.

| Kind | Keys (today's values) | Today's code |
|---|---|---|
| `grid` | `on 1`, `count 1000`, `max 16000`, `columns 128`, `spacing 0.9`, `mesh cube`, `scale 0.5`, `height 0.25`, `colors_on 0`, `colors 16`, `palette 200`, `churn_on 0`, `churn 32` | `update_grid`, `churn` |
| `chain` | `on 0`, `count 300`, `max 1000`, `mesh small_cube`, `link 0.12 0.03 0`, `twist 14.32`, `wobble 2.29` | `update_chain` |
| `crowd` | `on 1`, `count 16`, `max 200`, `model character`, `columns 20`, `spacing 1.6`, `offset 0.37` | `update_crowd` |
| `effects` | `on 0`, `particles`, `explosions`, `missiles`, `beams`, `decals`, `area 16 12` | `effects_stress_update` |

- The node budget is shared in a fixed order, as today: crowd, chain, grid. Each gets at most the nodes left (`NV_MAX_NODES` minus
  the nodes not generated) and the animators left. Today's constants such as `FIXED_NODES` and `NV_MAX_ANIMATORS - 2` become counts
  of what is actually there.
- `effects` fires the registered effects (`effects.c`). The missiles' flight state moves into this behavior's run-time state.

#### `benchmark` (`live` scenes only)

```
benchmark
    warmup 1
    measure 3
    step "Cubes 250"
        grid count 250
    step "Particles 1M"
        effects particles 1000000
```

- A line in a step is `<node id> <key> <value>`: it turns that node's generator on and sets the value. A generator a step does not
  name is off during that step.
- Other settings (color count, churn count) keep their values from before the run, and the settings from before the run come back
  when it ends, as today.
- At most 16 steps. Today's `steps[]` table moves into the file as it is.

### The code after the change

```c
typedef enum SceneMode { SCENE_MODE_EDIT_PLAY, SCENE_MODE_LIVE } SceneMode;

typedef struct Behavior {
    BehaviorKind kind;
    NvNodeId node;
    union { Spin spin; Character character; Grid grid; Chain chain; Crowd crowd; EffectsLoad effects; };
} Behavior;

typedef struct SceneDoc {           // one scene, read and built
    b32 built;
    SceneMode mode;
    NvScene* scene;
    SceneView view;                 // today's Sandbox.views[kind]
    FileNode nodes[SCENE_MAX_FILE_NODES]; // the file's nodes: id and NvNodeId
    u32 node_count;
    Behavior behaviors[SCENE_MAX_BEHAVIORS];
    u32 behavior_count;
    BenchmarkStep steps[STRESS_MAX_STEPS];
    u32 step_count;
    bool show_bones;
} SceneDoc;
```

| File | Does |
|---|---|
| `sandbox/scene_file.c` (new) | Text → `SceneDesc`. No GPU. Tested under Node |
| `sandbox/scenes.c` (new) | `SceneDesc` → `NvScene` + `SceneDoc`, the model library, switching scenes. Replaces `build_world`, `build_character` and `stress_build` |
| `sandbox/behaviors.c` (new) | The behavior update (a `switch` on the kind) and the Inspector's behavior sections. Replaces `update_showcase`, `update_jump`, `update_blend`, `apply_turn`, `update_look_target`, `stress_update` and `effects_stress_update` |
| `sandbox/stress.c` | Stats, the benchmark runner, the Stress tab. Its workload rows draw the scene's generators in turn |
| `sandbox/effects.c` | Only the effect definitions and the test fires |
| `sandbox/main.c` | Only startup and the frame's order |

- What leaves `Sandbox`: `planet`, `moon`, `orbit_angle`, `orbit_speed`, `character`, `animator`, `clips`, `root_motion_clips`,
  `fade_seconds`, `blend_clip`, `blend_weight`, `jump*`, `show_sword`, `sword`, `sword_mesh`, `root_motion`, `turn_rate`,
  `look_at`, `target`, `target_mesh`, `planet_rotation`, `moon_rotation`, `show_bones`, the workloads in `stress`, and
  `effects.stress_*`.
- The frame calls `behaviors_update(sandbox, doc, dt)` once, for the shown scene's `SceneDoc`.
  - Behaviors that move things run only in a `live` scene or while playing.
  - The animation preview (crossfades, blends) runs in Edit mode too, as today.
- `SceneKind` stays as the index into `scenes[]`, for the scene combo.
- The `shown == SCENE_SHOWCASE` checks in `frame`, the UI, undo and the save become checks of `doc->mode`.

### Save, undo, Play

- `SAVE_VERSION` and the meaning of every existing tag stay. The save reads and writes the `edit_play` scene.
- The tree `showcase.nvscene` builds must be today's tree (names, order, depth). Then `LAYT` is equal and every existing save loads.
- Tags that are the showcase's own reach their values through node ids.

| Tag | Today | After |
|---|---|---|
| `PLNT` | `orbit_speed` | The `spin` speed of node `planet` (rad/s, as today). Still written; read only when that node's `NODE` has no `SPIN` |
| `CHAR` | The character fields of `Sandbox` | The `character` behavior of node `character` |
| `SWRD` (in `CHAR`) | `show_sword` | `visible` of node `sword`. Still written; read only when there is no `VISI` |
| `LOOK` (in `CHAR`) | `look_at` | The `character` behavior's look-at on |
| `BONE` | `show_bones` | The scene's `show_bones` |
| `LAYT`, `VIEW`, `NODE`, the rest | | Unchanged |

- **New tags** (adding a tag does not raise the version, `save.md`):

| Tag | In | Type | Holds |
|---|---|---|---|
| `SPIN` | `NODE` | f32 | The speed of a node with a `spin` behavior, rad/s |
| `VISI` | `NODE` | u32 | `visible` of a node with a mesh |

- `PLNT` and `SWRD` are still written, so older builds can read new saves.
- If an id is gone from the file (a file without `planet`), its tag is skipped.
- Undo: the Node scope picks up `SPIN` and `VISI` by itself (`write_node_fields`), so spin speeds and visibility are undoable too.
  - The Character scope points at the behavior of id `character`.
  - The Scene scope keeps only `BONE`. The undo-only tag `ORBS` goes; it was never in saves.
- `driven_fields` (the orbit camera) and the Play snapshot are unchanged.

### Changes users see

- The View tab's "Planet orbit" slider moves to the planet's Inspector, in a **Spin** section (Speed). The moon's spin becomes
  editable the same way.
- "Show bones" is in the View tab for both scenes, and the Stress tab's "Crowd bones" merges into it. It draws the bones of every
  animator in the scene.
- Every node with a mesh gets "Visible" in the Inspector. Today only the sword has it.
- The stress scene's nodes show behavior sections too. For example, selecting the "grid" group shows a Grid section with the same
  values as the Stress tab's workload rows.

### Limits

| Item | Limit |
|---|---|
| File nodes | 256 per file (`SCENE_MAX_FILE_NODES`) |
| Meshes | 32 per file, each at most 4096 vertices |
| Models | 8 for the whole sandbox |
| Behaviors | 64 per scene |
| Benchmark steps | 16 |
| Ids, names | 31 bytes |

### Errors

- A packaged file that does not read is a bug. Errors are logged with their file and line through `nv_log` (source `"scene"`), at
  most 16.
- If the showcase file does not read, the sandbox stops with `fatal`, as it does today when the character fails to load.
- If the stress file does not read, switching to it is refused and the View tab shows the first error.
- ctest checks that the two packaged files read. Each rule of the format is tested with the test's own text.

### Engine changes

- `engine/text.h`, `engine/text.c`: the line-based text reading moves from `autobattler/defs.c`. Lines with their indentation,
  tokens, numbers, names, quoted text, and a list of errors with file and line.
  - `defs.c` uses it.
  - `.abproj` reads and reports errors exactly as before. `battle_test` and the existing tests pass unchanged.
- `nv_gltf_instantiate` does not copy mesh nodes' local transforms. Check that the character's mesh nodes have identity transforms;
  if not, make it copy them.
- No other changes to the scene, renderer or animation APIs.

### Phases

1. **Text reader:** `engine/text.*`, used by `defs.c`. Check: ctest and the auto-battler's tests pass unchanged.
2. **Scene file reader:** `sandbox/scene_file.c` and `tests/scene_file_test.c`. Check: the packaged files read, and each rule gives
   its error.
3. **The showcase from its file:**
   - Add: `showcase.nvscene`, the model library, the `spin` and `character` behaviors, `visible`, the Inspector sections, `SPIN` and
     `VISI`.
   - Remove: `build_world`, `build_character`, `update_showcase`, and the showcase fields of `Sandbox`.
   - Check:
     - `LAYT` equals today's value.
     - A save written by today's build (a fixed file made before the change) loads into the same scene.
     - `_sandbox_debug_save_crc` before Play equals the one after Stop.
     - First-visit node values match today's within 1e-5. Degree conversions may change the last bit, so this is checked instead of
       the CRC.
4. **The stress scene from its file:**
   - Add: `stress.nvscene`, the four generator kinds, and the benchmark steps from the file.
   - Remove: `stress_build`'s node code, `StressWorkloads`, and `effects_stress_update`.
   - Check: every workload at its maximum without asserts, a full benchmark run, and the same step names in the table.
5. **Docs:** `AGENTS.md`, `stress.md`, `save.md` (new tags and the mapping), `undo.md`, `play.md`, and the top comment of
   `sandbox.h`.

Every phase is checked in Release and Debug builds in headless Chromium.

### Later

- **A save that holds the scene** (`SAVE_VERSION` 2):
  - `NODE` carries the structure too (mesh, model, behaviors), so reading it makes the node.
  - Adding, removing and duplicating nodes works.
  - New saves need no `LAYT` and no paths. The v1 reader stays.
- **Opening and saving `.nvscene` files** (`local_file.h`).
- **Moving into the engine:** when the auto-battler builds its stage from a scene file, the reader and the builder move into the
  engine. The behaviors do not.
- **Generators in `edit_play` scenes.**

### Open questions

1. Format: our own text (proposed), or glTF + extras?
2. `mode`: does the stress scene stay `live` (proposed: the benchmark runs without Play), or does it follow Edit/Play too?
3. May "Planet orbit" move from the View tab to the Inspector?
4. The model library scene costs one idle animator per skinned model. Is that fine? The alternative is today's way: read the model
   into the first scene that uses it, and have other scenes copy it from there.

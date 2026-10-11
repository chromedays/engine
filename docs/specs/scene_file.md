# Scene file spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 제안 (2026-10-11). 구현 전이다. 이 스펙의 변경은 먼저 합의한다.

### 목표

샌드박스의 씬을 텍스트 파일로 기술하고 로더 하나로 만든다. 모든 씬을 한 벌의 코드와 한 벌의 규칙으로 돌린다.

### 원칙

- **규칙은 모든 씬에 같다.** Edit/Play, undo, 자동 저장, 선택, 저장 형식, 씬 전환, 오류 처리가 어느 씬에서나 같다.
- **씬마다 다른 것은 씬 파일이 정한다.** 어떤 오브젝트가 있는지, 각 오브젝트가 어떤 타입인지(메시, 카메라, 빛, 모델, 동작의 종류),
  어떤 값을 갖는지. 오브젝트가 다르게 움직여야 하면 그것은 타입(동작의 종류)과 그 값으로 적는다.
- **코드는 씬을 알아보지 않는다.** 코드에는 특정 씬, 특정 파일 이름, 특정 노드 id를 보고 분기하는 곳이 없다. 씬 파일의 목록만
  데이터로 있다.

### 지금 코드에서 이 원칙을 어기는 곳 (이 스펙이 고치는 것)

- `build_world`, `build_character`(`sandbox/main.c`)와 `stress_build`(`sandbox/stress.c`)가 씬을 C로 만든다. 이름, 위치, 메시, 색이
  모두 코드의 상수다.
- 오브젝트의 움직임이 씬마다 다른 코드에 있다.
  - 쇼케이스: `Sandbox`의 필드(행성, 달, 캐릭터, 클립, 점프, 칼, 시선 타깃)와 `update_showcase`.
  - 스트레스: `Stress`와 `stress_update`. 이펙트 워크로드는 `Effects` 안에 있다.
- 규칙이 씬마다 다르다: 지금은 쇼케이스만 Edit/Play, undo, 자동 저장이 있고, 스트레스 씬은 항상 돌며 저장되지 않는다. `frame`, UI,
  undo, 저장이 `SceneKind`로 분기한다(`shown == SCENE_SHOWCASE`).

### 바뀐 뒤

- `sandbox/data/showcase.nvscene`과 `sandbox/data/stress.nvscene`이 각 씬을 기술한다. 노드, 메시, 팔레트, 모델, 뷰, 그리고 노드
  위에서 도는 **동작**(behavior)이 들어 있다.
- 동작은 C로 쓴 고정된 타입(`spin`, `character`, `grid`, `chain`, `crowd`, `effects`)이고, 값은 파일에서 온다.
  - 각 타입의 파라미터는 표 하나로 정의된다. 그 표가 파일 읽기, UI, 검색, 저장, undo 이름표를 모두 만든다.
  - 업데이트 하나가 보이는 씬의 동작을 모두 돌린다.
- 모든 씬이 Edit/Play(`play.md`), undo, 자동 저장을 똑같이 따른다.
- `SceneKind`와 `shown == SCENE_SHOWCASE` 같은 검사는 모두 없어진다.

### 정한 것

| 주제 | 결정 |
|---|---|
| 형식 | `.abproj`와 같은 줄 기반 자체 텍스트. 새 서드파티 코드는 없다(아래 "형식 후보") |
| 씬의 규칙 | 모든 씬에 같다. 씬마다 다른 것은 씬 파일(오브젝트, 타입, 값)뿐이다 |
| Planet orbit | View 탭의 슬라이더가 행성 Inspector의 **Spin** 섹션으로 옮겨 간다 |

### 이 스펙에 없는 것

- 에디터에서 노드를 추가하고 제거하는 것, 그리고 노드의 구조까지 담는 저장("나중" 절).
- 사용자 컴퓨터의 `.nvscene` 파일을 열고 저장하는 것(`local_file.h`).
- 리더를 엔진으로 옮기는 것. `shared.md`에 따라 두 번째 앱이 쓸 때 옮긴다.

### 형식 후보 (AGENTS.md에 따라 서드파티부터)

| 후보 | 라이브러리 (라이선스) | 장점 | 단점 |
|---|---|---|---|
| **`.abproj` 같은 줄 기반 자체 텍스트** (정함) | 없음 (토크나이저를 `autobattler/defs.c`에서 엔진으로 옮긴다) | 이미 아는 문법. Git에서 diff가 보인다. 오류에 파일과 줄이 붙는다. 토크나이저의 두 번째 사용처가 된다 | 리더를 직접 쓴다 |
| glTF 2.0 + `extras` | cgltf (MIT; 이미 쓴다) | 표준, Blender가 연다 | 프리미티브, 모델 참조, 관절 부착, 동작, 뷰가 모두 extras가 된다 |
| JSON | jsmn (MIT; cgltf 안에 있다) | 익숙하다 | 손으로 고치기 번잡하고 주석이 없다 |
| TOML | tomlc99 (MIT) | 주석, 읽기 쉬움 | 새 의존성, 깊은 트리가 어색하다 |
| 청크 바이너리 (`engine/chunk.h`) | 없음 | 저장과 같은 코드 | 손으로 고칠 수 없다 |

### 파일

| 파일 | 패키지 안의 경로 | 내용 |
|---|---|---|
| `sandbox/data/showcase.nvscene` | `/data/showcase.nvscene` | 쇼케이스 |
| `sandbox/data/stress.nvscene` | `/data/stress.nvscene` | 스트레스 씬과 벤치마크 단계 |

- **씬 목록**은 코드에 있는 유일한 씬 정보이고, 파일 이름의 배열이다:
  `SCENE_FILES[] = {"showcase.nvscene", "stress.nvscene"}`(`sandbox/scenes.c`).
  - View 탭의 씬 콤보와 팔레트의 "Show the ... scene" 명령은 이 목록에서 나온다. 이름은 파일의 `title`이다.
  - 샌드박스는 목록의 첫 씬에서 시작한다.
  - 씬을 더하는 것은 파일을 더하고 목록에 한 줄을 더하는 것이다.
- 텍스트이므로 일반 Git에 둔다.
- `sandbox/CMakeLists.txt`: `nv_setup_executable(sandbox ROOT ASSETS ... PRELOAD "${CMAKE_CURRENT_SOURCE_DIR}/data@/data")`.
- `nv_file_read`로 임시 아레나에 읽고, `SceneDesc`(GPU 없는 평범한 데이터)로 파싱한 뒤 만든다.

### 문법

`.abproj`와 같다(`abproj.md`, "문법"): UTF-8, 한 줄에 문장 하나, `#` 주석, 공백 들여쓰기가 중첩을 정함, 탭은 오류, 모르는 키나 값의
개수가 틀린 줄은 파일과 줄 번호가 붙은 오류. 더하는 것:

- **텍스트:** 큰따옴표로 감싼 31바이트까지의 문자열(`"look target"`). 이스케이프는 없다.
- **단위:** 파일에서 각도는 도, 각속도는 도/초다. 읽을 때 라디안으로 바꾼다. 실행 중의 값, 저장, UI는 지금처럼 라디안
  기준이다(UI의 "rad/s" 슬라이더는 그대로).
- **회전:** `rotation x y z w`(쿼터니언, 정확) 또는 `euler pitch yaw roll`(도).
- **계산으로 나온 값**(태양의 회전, 칼의 오프셋)은 유효숫자 9자리로 써서 같은 float로 읽힌다.
- **최상위 문장의 순서:** `nvscene_version` → `title` → `view` → `mesh`* → `palette`* → `model`* → `node`+ → `benchmark`?.

### 구조

#### `nvscene_version <정수>`

첫 문장. 지금은 `1`. 규칙은 `abproj_version`과 같다.

#### `title <텍스트>`

씬 콤보와 팔레트에 보이는 이름(`T()`를 거친다; 한국어 행은 `sandbox/strings.c`). 쇼케이스 `"Showcase"`, 스트레스 `"Stress"`.

#### `view`

| 키 | 값 | |
|---|---|---|
| `camera <id>` | 카메라가 있는 노드 | 필수 |
| `target x y z` | 궤도 중심, m | 기본 0 0 0 |
| `yaw`, `pitch` | 도 | 필수 |
| `distance` | m | 필수 |
| `focus <id>` | 아무것도 선택되지 않았을 때 도는 중심 | 선택 |
| `select <id>` | 시작할 때 선택된 노드 | 선택 |

`ORBIT_LIMITS`는 에디터의 한계이므로 코드에 남는다.

| | 쇼케이스 (지금 `build_world`) | 스트레스 (지금 `stress_build`) |
|---|---|---|
| camera | `camera` | `camera` |
| target | 0 0.92 0 (캐릭터의 머리 높이) | 0 0 0 |
| yaw, pitch, distance | 0.35, 0.12 rad, 5 m | 0.45, 0.42 rad, 28 m |
| focus | `character` | `grid` |
| select | `character` | 없음 |

#### `mesh <id>`

블록 안의 프리미티브 문장 하나 이상을 메시 하나에 이어 붙인다. 값 순서는 `engine/mesh.h`의 함수를 따른다.

| 키 | 값 |
|---|---|
| `box` | cx cy cz hx hy hz |
| `plane` | hx hz |
| `sphere` | cx cy cz radius segments rings |
| `cylinder`, `cone` | cx cy cz radius half_height segments |
| `capsule` | cx cy cz radius half_height segments rings |
| `torus` | cx cy cz major minor segments tube_segments |

지금 코드의 메시: 쇼케이스 `cube`(box 반경 0.5), `ground`(plane 30×30), `sword`(box 세 개: 중심 (0,0,0) 반경 (0.018,0.09,0.018),
중심 (0,0.1,0) 반경 (0.09,0.012,0.025), 중심 (0,0.52,0) 반경 (0.025,0.41,0.006)), `target`(box 반경 0.06). 스트레스 `cube`(반경 0.5),
`small_cube`(반경 0.06).

#### `palette <id>`

생성기가 쓰는 머티리얼 묶음. 지금 `stress.c`의 `colors[]`다.

| 키 | 값 | |
|---|---|---|
| `base r g b` | 색 0. 색을 끈 그리드의 색 | 필수 |
| `wheel <count>` | 색 1부터 count-1까지: 색상환 | 필수 |

색상환(`color_wheel`): 색 `i`의 h = i / count × 6, x = 1 − |h mod 2 − 1|, `floor(h) mod 6`에 따라 (r,g,b)가 (1,x,0), (x,1,0), (0,1,x),
(0,x,1), (x,0,1), (1,0,x). 결과는 0.15 + 0.8 × 채널. 스트레스: `base 0.6 0.62 0.66`, `wheel 200`.

#### `model <id> <텍스트: 경로>`

| 키 | 값 | |
|---|---|---|
| `clips <텍스트: 경로>` | 제자리 클립 파일(`SANDBOX_MAX_CLIPS` 16개까지) | 선택 |
| `root_motion_clips <텍스트: 경로> <관절>` | 루트 모션 사본과 그 루트 관절 | 선택 |

지금: `/assets/quaternius/character.glb`, `clips.glb`, `clips_rm.glb`(관절 `root`). 모델을 어디에 읽어 두는지는 열린 질문 1이다.

#### `node <id>`

블록 안의 `node` 블록이 자식이다. 파일의 순서가 트리의 순서다.

| 키 | 값 | |
|---|---|---|
| `name <텍스트>` | 보이는 이름 | 기본: id |
| `position x y z`, `rotation`/`euler`, `scale x y z` | | 0, 단위, 1 |
| `mesh <id>` | | |
| `color r g b` | 노드 자신의 머티리얼 | 기본 머티리얼 |
| `double_sided <0/1>` | | 0 |
| `visible <0/1>` | 메시를 그리는지 | 1 |
| `camera perspective <fov> <near> <far>` | fov는 도 | |
| `light directional`, `light_color r g b`, `light_intensity x` | | |
| `model <id>` | 노드가 모델의 루트가 된다. 모델의 메시 노드(glTF의 이름)가 첫 자식으로 만들어지고 파일의 자식이 뒤따른다 | |
| `attach <관절>` | 가장 가까운 조상 모델의 애니메이터에서 그 관절을 따라간다 | |
| 동작 블록 | 아래 | |

- id는 파일 안에서 유일한 이름이다. 동작, 뷰, 벤치마크, 저장의 매핑이 id로 노드를 가리킨다. 이름과 다르다(Inspector가 이름을
  바꾼다).
- 쇼케이스 트리는 지금과 같다: `camera`, `sun`, `ground`, `planet` > `moon`, `character`(모델 메시 노드들, `sword`), `look target`.
- 스트레스 트리: `camera`, `sun`, `ground`(cube를 (260, 0.1, 260)로 키워 (0, −0.05, −50)에 둔다, 색 0.28 0.3 0.34), `grid`,
  `chain`(14, 0.5, 8), `crowd`(0, 0, 4).

#### `benchmark`

```
benchmark
    warmup 1
    measure 3
    step "Cubes 250"
        grid count 250
    step "Particles 1M"
        effects particles 1000000
```

세부는 "벤치마크" 절.

### 모든 씬의 Edit과 Play

`play.md`의 규칙이 모든 씬에 적용된다. Edit 모드에서는 아무것도 씬을 스스로 움직이지 않는다. 노드를 만들고 지우는 것(생성기의 수)은
구조의 편집이므로 Edit 모드에서도 바로 따른다.

| 동작 | Edit | Play |
|---|---|---|
| `spin` | 멈춤, 저작한 회전 | 저작한 회전 위에서 돈다 |
| `character`: 클립, 크로스페이드, 블렌드 | 제자리 미리보기 | 돈다 |
| `character`: 루트 모션, 회전 | 꺼짐(제자리 클립) | 체크하면 켜짐 |
| `character`: 점프 | 꺼짐 | 켜짐 |
| `character`: 시선 타깃 | 놓인 곳에 머묾 | 머리 앞을 쓴다 |
| `grid`: 수, 색 | 바로 따름 | 바로 따름 |
| `grid`: churn | 꺼짐 | 켜짐 |
| `chain`: 수 | 바로 따름 | 바로 따름 |
| `chain`: 흔들림 | 멈춤 (각 0.25 rad) | 돈다 |
| `crowd`: 수 | 바로 따름 | 바로 따름 |
| `crowd`: 애니메이션 | 제자리 미리보기 | 돈다 |
| `effects` 워크로드 | 꺼짐 | 켜짐 |
| View 탭의 시험 이펙트 버튼 | 지금처럼 동작 | 동작 |

- Play/Stop은 보이는 씬에 대한 것이다. 씬마다 재생 상태와 스냅샷이 따로 있다.
- 재생 중인 씬을 떠나면 그 씬은 재생 상태로 멈춘다. 지금 쇼케이스가 그렇듯이 애니메이터는 보이는 씬만 갱신된다.
- Play: 그 씬의 스냅샷(저장 형식)을 쓰고, 재생 시간을 0으로 하고, 이펙트를 모두 지우고(`effects_clear`), 각 동작의 Play 시작을
  부른다. Stop: 스냅샷으로 그 씬을 되돌리고, 이펙트를 지우고, 각 동작의 Stop을 부른다.

### 동작의 세부

각 종류는 C `switch`의 한 갈래다. 함수 포인터나 vtable은 쓰지 않는다. 동작은 다음 단계를 갖는다:

- **만들기:** 씬을 만들 때 한 번.
- **적용:** 파라미터가 바뀌었을 때. 파일, 저장, undo가 값을 넣은 뒤.
- **프레임:** 매 프레임. Edit/Play 규칙을 따른다.
- **Play 시작, Stop.**
- **UI:** Inspector 섹션. 생성기는 Stress 탭에도 보인다.

**파라미터 표.** 종류마다 `ParamDef` 표 하나가 있다. 열은 키(파일과 저장의 이름), 이름표(UI와 undo), 검색 키워드, 형(bool, int,
float, 각도, 각속도, 클립), 범위, 구조체 안의 위치다. 이 표가 파일 읽기, 위젯, `search_row`, 저장(`BHVR`), undo 이름표를 만든다.
**구조** 키는 파일에만 있고 실행 중에 바뀌지 않는다. **파라미터**는 UI가 바꾸고, 저장되고, undo된다.

#### `spin`

지금: `update_showcase`(`sandbox/main.c:730`)의 행성과 달, `sandbox_start_playing`의 `planet_rotation`/`moon_rotation`.

| 키 | 종류 | 형, 범위 | UI | 쇼케이스 값 |
|---|---|---|---|---|
| `axis x y z` | 구조 | 읽을 때 정규화 | | 행성 (0,1,0), 달 (1,1,0) |
| `speed` | 파라미터 | 각속도, ±3 rad/s | Spin 섹션 "Speed" 슬라이더, "%.2f rad/s" | 행성 0.7 rad/s (40.107 도/초), 달 2 rad/s (114.592 도/초) |

- **Play 시작:** 노드의 지금 회전을 저작한 회전으로 기록하고 각을 0으로 한다.
- **프레임 (Play):** 각 += speed × dt. 회전 = axis_angle(axis, 각) × 저작한 회전. 속도를 재생 중에 바꾸면 그 순간부터 새 속도로
  쌓인다(지금 행성과 같다).
- **Edit:** 아무것도 하지 않는다. 회전은 기즈모와 Inspector로 편집되고 undo된다.
- **Stop:** 스냅샷이 회전을 되돌린다. 각은 0.
- 달은 행성의 자식이라 행성을 따라 돈다.
- 지금의 달은 `play_time × 2`로 각을 구하고 행성은 속도를 쌓는다. 둘 다 쌓는 방식이 되므로 달의 각은 float 반올림만큼 다를 수 있다.
- 달의 속도는 지금 편집할 수 없었다. 이제 행성과 같이 편집된다.

#### `character`

지금: `sandbox_play`, `sandbox_jump`, `update_jump`, `update_blend`, `apply_turn`, `update_look_target`, `restart_clip`,
`sandbox_back_to_center`(`sandbox/main.c`), `animator_section`, `attach_section`(`sandbox/ui.c`), `read_character_fields`(`sandbox/save.c`).

**구조 키**

| 키 | 값 | 쇼케이스 |
|---|---|---|
| `look_target <관절> <노드 id>` | 시선 IK의 관절과 타깃 노드 | `Head look_target` |
| `jump <시작> <공중> <착지> <공중 시간>` | 점프의 세 클립과 공중 시간(초) | `Jump_Start Jump_Loop Jump_Land 0.35` |

**파라미터**

| 키 | 형, 범위 | UI ("Animator" 섹션) | 기본 | 저장 태그 (옛) |
|---|---|---|---|---|
| `clip` | 클립 이름 | 클립 목록(선택 가능 행) | `Idle_Loop` | `CLIP` |
| `speed` | 0–2 | "Speed", "%.2fx" | 1 | `SPED` |
| `fade` | 0–1 s | "Fade", "%.2f s" | 0.3 | `FADE` |
| `blend` | 클립 이름 | "Blend" 콤보 | 첫 클립 | `BLND` |
| `blend_weight` | 0–1 | "Weight" | 0 | `BLDW` |
| `root_motion` | bool | "Root motion" | 0 | `RMOT` |
| `turn` | ±1.5 rad/s | "Turn", "%.2f rad/s" | 0.6 rad/s (34.377 도/초) | `TURN` |
| `look_at` | bool | "Look at target" | 0 | `LOOK` |

**만들기**

- 모델 노드의 애니메이터를 쓴다. 클립은 모델의 `clips`(제자리)와 `root_motion_clips`(사본)다.
- 시선 IK: 관절 `Head`의 레스트 포즈 모델 행렬에서 `joint_axis_towards`로 +Z에 가장 가까운 축을 `forward`로, +Y에 가장 가까운 축을
  `up`으로 정한다. `weight` 1, `target_node`는 타깃 노드다. 축은 행렬의 세 열을 정규화해 방향과의 내적이 가장 큰 ±축을 고른다.
- 그 뒤 `clip`을 재생한다(아래 "클립 재생", 크로스페이드 없음).

**클립 재생** (지금 `sandbox_play`)

1. 루트 모션 사본 고르기: `root_motion`이 켜져 있고, 재생 중이고, 같은 이름의 사본이 있으면 사본을 재생한다. 그래서 Edit 모드는
   언제나 제자리 클립이다.
2. 점프를 끝낸다(단계 = 없음).
3. `nv_anim_play(animator, clip, fade, loop)`. loop는 이름에 `_Loop`가 들어 있는지다.

`nv_anim_play`의 뜻(`engine/anim.cpp`):

- 레이어 0이 이미 그 클립이면 loop만 바꾸고 다시 시작하지 않는다.
- fade > 0이고 레이어 0에 클립이 있으면 레이어 1 = 이전 레이어 0으로 두고, 레이어 0의 무게는 0에서 시작한다.
  `nv_anim_update`가 fade 동안 레이어 0을 t, 레이어 1을 1 − t로 선형으로 바꾸고, 끝나면 레이어 1을 비운다.
- 아니면 바로 바꾼다(레이어 1을 비우고 무게 1).
- 시간은 0부터, 속도는 레이어 0의 속도를 그대로(0이면 1).
- 레이어 0은 현재 클립, 1은 사라지는 클립, 2는 블렌드 클립이다.

**클립 목록:** 이 스켈레톤의 클립 중 루트 모션 사본이 아닌 것을 클립 순서대로 보인다. 지금 재생 중인 것은 레이어 0의 클립(사본이면
같은 이름의 제자리 클립)이다. 고르면 "클립 재생". 그 밑에 레이어마다 "이름 무게" 진행 막대가 있다(검색이 없을 때만, 무게의 합으로
나눈 비율).

**블렌드** (지금 `update_blend`, Edit과 Play 모두, 매 프레임)

- 켜지는 조건: blend_weight > 0, 레이어 0에 클립이 있음, 블렌드 클립의 id가 레이어 0의 id와 다름, 크로스페이드 중이 아님, 점프 중이
  아님. 레이어 0이 사본이면 같은 이름의 제자리 클립과 id가 다르므로 블렌드가 켜진다. 지금 동작 그대로다.
- 꺼짐: 레이어 2를 비우고, 크로스페이드 중이 아니면 레이어 0의 무게를 1로.
- 켜짐: 레이어 2 = {블렌드 클립, 시간 = 레이어 0 시간 / 레이어 0 길이 × 블렌드 길이, 속도 = 레이어 0 속도 × 블렌드 길이 / 레이어 0 길이,
  무게 = blend_weight, loop}. 레이어 0의 무게 = 1 − blend_weight. 매 프레임 시간을 맞추므로 발이 맞는다.

**루트 모션과 회전**

- "Root motion"을 바꾸면 점프 중이 아닐 때 레이어 0의 제자리 클립으로 "클립 재생"을 한다. 그래서 재생 중에는 fade로 사본과 제자리
  클립이 바뀐다.
- 움직임: `nv_anim_update_scene`이 클립의 루트 이동(소유 노드의 회전으로 돌린 것)을 모델의 루트 노드에 더한다. 사본에만 이동이 있다.
- 회전(Play, root_motion이 켜졌을 때): 루트 회전 = axis_angle(Y, turn × dt) × 루트 회전. 서 있는 클립이어도 제자리에서 돈다.
- "Turn" 슬라이더와 "Back to center" 버튼은 root_motion이 꺼져 있으면 비활성이다. "Back to center"는 루트를 (0,0,0)과 단위 회전으로
  둔다. Edit 모드에서도 된다.

**점프** (지금 `sandbox_jump`, `update_jump`)

- "Jump" 버튼은 재생 중에만 활성이다. 이미 점프 중이면 아무것도 안 한다.
- 시작: 돌아갈 클립 = 레이어 0의 클립(사본일 수 있다), 시작 클립을 fade 0.1, loop 없이 재생 → 단계 시작.
- 프레임 (Play): 끝남 = 레이어 0에 클립이 있고 시간 ≥ 길이 − 0.001.
  - 시작 단계에서 끝나면: 공중 클립, fade 0.05, loop → 공중 시간 0 → 공중 단계.
  - 공중 단계: 공중 시간 += dt. 0.35 s 이상이면 착지 클립, fade 0.05, loop 없음 → 착지 단계.
  - 착지 단계에서 끝나면: 돌아갈 클립을 fade 0.2, loop = 이름에 `_Loop` → 단계 없음.
- 점프 클립은 제자리 클립에서 찾고 `nv_anim_play`를 바로 부른다(사본을 고르지 않는다).
- 점프 중에는 블렌드가 꺼진다.

**시선** (지금 `update_look_target`, 매 프레임)

- 애니메이터의 `look_at.enabled` = look_at. 타깃 노드는 look_at이 꺼져 있으면 보이지 않는다(메시를 그리지 않음; `visible`과
  별개로, 저장되지 않는 상태).
- Play이고 look_at이 켜졌을 때: t = 재생 시간, 각 = sin(0.7 t) × 1.3, 로컬 = (sin(각) × 1.2, 1.75 + 0.3 sin(1.3 t), cos(각) × 1.2).
  타깃 위치 = 루트 위치 + 루트 회전으로 돌린 로컬, 타깃 회전 = axis_angle(Y, 2 t).
- Edit: 타깃은 놓인 곳에 머문다(시작 위치 (0, 1.75, 1.2)). 다른 노드처럼 편집되고 undo된다.

**Play 시작과 Stop** (지금 `restart_clip`)

- Play 시작: 재생 시간 0. 클립 = 점프 중이면 돌아갈 클립, 아니면 레이어 0의 클립. 그 제자리 클립을 fade 없이, 레이어 0을 비운 뒤
  재생하고(같은 클립도 처음부터) 시간 0. 재생 중이므로 루트 모션이 켜져 있으면 사본이 골라진다.
- Stop: 점프를 끝내고, 스냅샷이 파라미터를 되돌린 뒤(아래 "적용"), 같은 방법으로 클립을 처음부터 다시 시작한다.

**적용** (파일, 저장, undo가 파라미터를 넣은 뒤; 지금 `read_character_fields`)

- root_motion, turn, look_at, blend_weight(0–1로 자름), blend(이름으로 찾음; 없으면 그대로)를 넣는다.
- clip이 들어왔으면: 이름의 클립, 없으면 `Idle_Loop`. fade를 잠시 0으로 하고 "클립 재생". 같은 클립이면 계속 재생된다.
- fade(0–1로 자름), speed(레이어 0의 속도)를 넣는다.
- 옛 저장의 `CTIM`은 레이어 0의 시간으로 읽는다(쓰지는 않는다).

**저장되는 상태의 뜻:** 점프 중이면 `clip`은 돌아갈 클립이다. 사본이 재생 중이면 같은 이름의 제자리 클립이다. 시간은 저장되지 않는다.

**Inspector:** 선택한 노드(또는 첫 자식)의 애니메이터가 `character` 동작의 애니메이터면 위의 섹션 전체를 보인다. 다른 애니메이터
(군중)는 "Playing: 클립 시간 / 길이"만 보인다. 지금과 같다.

#### `visible` (노드 키, 동작이 아님)

- 지금은 칼만 있다: `show_sword`가 켜졌을 때만 칼 노드의 메시를 그린다(`update_showcase`). "Visible" 체크박스는 칼의 Attach 섹션에
  있다.
- 바뀐 뒤: 메시가 있는 모든 노드의 Mesh 섹션에 "Visible"이 있다. 꺼지면 렌더러에 메시 0을 넘긴다(노드의 메시 id는 그대로).
- 칼: `attach hand_r`. 로컬 트랜스폼은 지금 계산값(레스트 포즈의 `hand_r` 행렬 H, grip = H의 위치 + (−0.08, −0.02, 0), desired =
  trs(grip, axis_angle(X, π/2), 1), 로컬 = H⁻¹ × desired)을 9자리로 파일에 쓴 것이다. 색 (0.75, 0.77, 0.8), 양면.
- 시선 타깃: box 0.06, 색 (1, 0.75, 0.2), 최상위 노드 "look target".

#### `grid`

지금: `add_grid_cube`, `update_grid`, `churn`(`sandbox/stress.c`).

| 키 | 종류 | 형, 범위 | UI (Stress 탭 "Workloads") | 스트레스 값 |
|---|---|---|---|---|
| `max` | 구조 | | | 16000 |
| `columns`, `spacing`, `height`, `scale` | 구조 | | | 128, 0.9, 0.25, 0.5 |
| `mesh`, `palette` | 구조 | | | `cube`, `wheel` |
| `on` | 파라미터 | bool | "Cube grid" | 1 |
| `count` | 파라미터 | 0–max, 로그 슬라이더 | "Cubes" | 1000 |
| `colors_on` | 파라미터 | bool | "Many colors" | 0 |
| `colors` | 파라미터 | 2–팔레트 색 수 − 1 | "Colors" | 16 |
| `churn_on` | 파라미터 | bool | "Churn" | 0 |
| `churn` | 파라미터 | 1–256 | "Cubes / frame" | 32 |

- 큐브 i: 이름 "cube", 그리드 노드의 자식, 위치 ((i mod columns − (columns − 1)/2) × spacing, height, −floor(i / columns) × spacing),
  scale (s, s, s), 메시, 머티리얼 = 색이 켜졌으면 팔레트 색 1 + (i mod colors), 아니면 팔레트 색 0.
- 프레임(Edit과 Play): 목표 = on ? count : 0(예산으로 자름). 칠한 색 수(colors_on ? colors : 0)가 바뀌면 만든 큐브를 모두 다시 칠한다.
  목표보다 많으면 끝에서부터 지우고, 적으면 끝에 더한다.
- churn (Play만, churn_on): n = min(churn, 만든 수). n번: i = 커서 mod 만든 수, 커서 += 1. 큐브 i를 지우고 같은 자리에 새로 만든다.
  새 노드는 새 슬롯과 세대를 받고 그리드의 마지막 자식이 된다.

#### `chain`

지금: `update_chain`.

| 키 | 종류 | 형, 범위 | UI | 스트레스 값 |
|---|---|---|---|---|
| `max` | 구조 | | | 1000 |
| `mesh`, `palette`, `stride` | 구조 | | | `small_cube`, `wheel`, 7 |
| `link x y z` | 구조 | | | 0.12 0.03 0 |
| `twist`, `wobble` | 구조 | 도 | | 14.324 (0.25 rad), 2.292 (0.04 rad) |
| `on` | 파라미터 | bool | "Deep chain" | 0 |
| `count` | 파라미터 | 1–max | "Links" | 300 |

- 고리 i: 이름 "link", 부모는 고리 i − 1(첫 고리는 체인 노드), 위치는 첫 고리 (0,0,0), 그 뒤 `link`. 메시, 머티리얼 = 팔레트 색
  1 + (i × stride) mod (팔레트 색 수 − 1).
- 프레임: 목표 = on ? count : 0(예산으로 자름). 끝에서 지우거나 끝에 더한다(고리를 지우면 그 아래도 지워진다).
- 흔들림: 각 = twist + wobble × sin(체인 시간). 모든 고리의 회전 = axis_angle(Y, 각). 체인 시간은 Play에서만 dt만큼 늘고, Play 시작과
  Stop에서 0이 된다. 그래서 Edit에서는 각 = twist다. 지금은 체인 시간이 항상 는다.

#### `crowd`

지금: `update_crowd`.

| 키 | 종류 | 형, 범위 | UI | 스트레스 값 |
|---|---|---|---|---|
| `max` | 구조 | | | 200 |
| `model`, `columns`, `spacing`, `offset` | 구조 | | | `character`, 20, 1.6, 0.37 s |
| `on` | 파라미터 | bool | "Crowd" | 1 |
| `count` | 파라미터 | 1–max | "Characters" | 16 |

- 캐릭터는 처음 필요할 때 만들고 지우지 않는다. 애니메이터를 없앨 수 없기 때문이다.
- 캐릭터 i 만들기: 모델을 군중 노드 아래에 `nv_gltf_instantiate`. 루트 위치 ((i mod columns − (columns − 1)/2) × spacing, 0,
  floor(i / columns) × spacing), 이름 "character N"(N = i + 1).
- 클립: 모델의 제자리 클립 중 이름에 `_Loop`가 든 것들을 클립 순서대로 모은 목록에서 i mod 개수 번째. fade 0, loop로 재생하고,
  시간 = (i × offset) mod 길이.
- 프레임: 목표 = on ? count : 0(예산으로 자름). 만든 캐릭터 중 목표 안의 것은 애니메이터의 씬을 이 씬으로 하고 메시 노드에 원래
  메시를 준다. 밖의 것은 애니메이터의 씬을 비워(멈춤) 메시를 0으로 한다(숨김).
- 애니메이션: 모두 제자리 클립이므로 Edit에서는 미리보기로, Play에서는 그대로 돈다. 지금과 같다.

#### `effects`

지금: `effects_stress_update`, `field_point`, `new_flight`(`sandbox/effects.c`).

| 키 | 종류 | 형, 범위 | UI | 스트레스 값 |
|---|---|---|---|---|
| `area x z` | 구조 | m | | 16 12 |
| `on` | 파라미터 | bool | "Fire effects" | 0 |
| `particles` | 파라미터 | 0–4,000,000, 로그 | "Live particles" | 0 |
| `explosions` | 파라미터 | 0–200 /s | "Explosions / s" | 0 |
| `missiles` | 파라미터 | 0–2000 | "Missiles in flight" | 0 |
| `beams` | 파라미터 | 0–500 | "Beams" | 0 |
| `decals` | 파라미터 | 0–2000 /s | "Decals / s" | 0 |

Play에서만 돈다. dt는 0.1 s로 자른다. 들판의 점 = 궤도 중심 ± area.x(x), 지정한 높이, 궤도 중심 ± area.z(z), 난수(`Effects.rng`,
시드 0x9E3779B9, 시험 이펙트와 같이 쓴다).

1. **미사일:** 목표 = on ? missiles : 0. 넘치면 뒤를 버린다. 모자라면 새 비행을 더한다: 각 = 난수 × 2π, 속력 6–10, 위치 = 들판의
   점(높이 0.3), 속도 = (cos 각 × 속력, 3–5, sin 각 × 속력), 수명 1.2–2.0 s. 더할 때 수명에 난수를 곱해 서로 어긋나게 한다.
   - 매 프레임 속도.y −= 4 dt, 위치 += 속도 dt, 수명 −= dt.
   - dt > 0이면 이동 구간에 미사일 이펙트를 (길이 × 12 + 1)개 뿌리고 궤적 선분을 더한다.
   - 수명이 다하거나 높이 < 0.05면 새 비행으로 다시 시작한다.
2. **빔:** 목표 = on ? beams : 0. 빔 i: h = i × 2654435761 + 12345에서 뽑은 네 개의 0–1 값으로 시작 (궤도 중심 ± area, 높이 7)과 끝
   (높이 0.05)을 정한다. 레이저 스타일, 수명 dt × 1.5 + 0.02로 매 프레임 다시 건다.
3. on이 꺼져 있으면 여기서 끝.
4. **파티클:** 남은 몫 += (particles / 2.5) × dt / 512. 몫이 1 이상인 동안 1을 빼고 들판의 점(높이 0.05)에 swarm 폭발 하나(512개).
   한 프레임에 4000번까지, 넘는 몫은 버린다.
5. **폭발:** 몫 += explosions × dt. 한 프레임에 300개까지 들판의 점에 폭발. 그 뒤 몫 > 1이면 0으로.
6. **데칼:** 몫 += decals × dt. 한 프레임에 2000개까지 들판의 점(높이 0)에 그을음, 크기 0.6–1.8, 각도 = vfx 난수.

- 비행 목록과 세 개의 몫은 이 동작의 실행 상태다(지금은 `Effects.stress_*`). `effects_clear`(Play, Stop, 벤치마크 단계)가 비운다.
- 시험 이펙트 버튼과 그 미사일(`Effects.flights`)은 동작이 아니라 View 탭의 도구로 `effects.c`에 남는다.

#### 노드 예산과 생성기의 순서

- 지금(`stress_update`): 애니메이터 여유 = `NV_MAX_ANIMATORS` − 2, 군중 = min(원하는 수, max, 애니메이터 여유), 군중 노드 =
  max(군중, 만든 캐릭터) × 4, 예산 = `NV_MAX_NODES` − 1 − 6 − 군중 노드, 체인 = min(원하는 수, 예산), 그리드 = min(원하는 수, max,
  예산 − 체인).
- 바뀐 뒤: 상수 대신 센다. 애니메이터 여유 = `NV_MAX_ANIMATORS` − 쓰는 애니메이터, 노드 여유 = `NV_MAX_NODES` − 1 − 생성되지 않은 산
  노드. 캐릭터 하나의 노드 수 = 1 + 모델의 메시 노드 수. 나누는 순서는 그대로 crowd, chain, grid.
- 한 프레임의 순서도 그대로다: 그리드 줄이기, 체인 줄이기, 군중, 체인 늘리기(흔들림 포함), 그리드 늘리기(색 포함), churn, effects.
  줄이기가 먼저라 노드 수가 한도를 넘지 않는다.
- 생성기가 만든 노드: 저장하지 않고, undo하지 않는다. 선택하고 Inspector로 볼 수 있지만, 편집은 남지 않는다는 줄
  ("Made by grid: edits are not saved")을 보인다. 생성기 노드는 언제나 부모의 파일 자식들 뒤에 오므로 파일 노드의 경로가 바뀌지 않는다.

#### 뼈 표시

- 씬마다 "Show bones"(View 탭, 저장 `BONE`). 지금 쇼케이스의 `show_bones`와 Stress 탭의 "Crowd bones"가 합쳐진다.
- 이 씬에서 갱신되는 모든 애니메이터에 대해 관절마다 부모에서 자식으로 선(1, 0.8, 0.2)을 그린다(소유 노드의 월드 행렬 × 관절 모델 행렬).
- 시선이 켜진 애니메이터: 머리의 forward 축 0.6 m 초록(0.2, 1, 0.3)과 머리에서 타깃까지 빨강(1, 0.2, 0.2). 지금은 쇼케이스
  캐릭터만 그린다.

### 벤치마크

지금: `start_step`, `start_benchmark`, `stop_benchmark`, `stress_after_frame`, `benchmark_section`, `copy_results`(`sandbox/stress.c`).
씬에 `benchmark` 블록이 있으면 Stress 탭에 Benchmark 섹션이 보인다.

**단계:** 파일의 `step` 블록(16개까지). 단계 안의 줄 `<노드 id> <키> <값>`은 그 노드의 생성기를 켜고 값을 바꾼다. 지금의 표:

| 단계 | 줄 |
|---|---|
| Cubes 250, 1000, 4000, 8000, 16000 | `grid count N` |
| Crowd 8, 32, 60, 120, 200 | `crowd count N` |
| Particles 250k, 1M, 2M | `effects particles N` |
| Missiles 500, 2000 | `effects missiles N` |
| Beams 500 | `effects beams N` |

**실행**

1. "Run benchmark": 씬이 Edit 모드면 Play를 시작한다(실행이 시작한 Play). 실행 전 파라미터를 기억한다. 그림자 설정, MSAA 표본 수,
   해상도 글을 기록한다. 결과를 비운다.
2. 단계 시작: 모든 생성기의 파라미터를 **파일의 값**으로 둔 뒤 모두 끄고, 단계의 줄을 적용한다. `effects_clear`. 시작 시각을 기록한다.
   지금은 색 수, 고리 수, churn 수를 실행 전 값에서 가져오지만, 그 생성기들은 단계에서 꺼져 있으므로 결과는 같다. 파일 값에서
   시작하면 저장된 설정과 상관없이 실행이 같은 곳에서 시작한다.
3. 매 프레임 끝(프레임 시간이 정해진 뒤): 경과 < warmup(1 s)이면 넘긴다. 아니면 프레임, CPU anim/scene/draw/ui, GPU scene/shadow/
   upscale/bloom/particles 시간과 살아 있는 파티클 수를 더하고, 최악 프레임과 프레임 수를 센다.
4. 경과 ≥ warmup + measure(4 s)이면 평균을 내고 다음 단계로, 마지막이면 끝낸다.
5. 끝: 실행이 Play를 시작했으면 Stop(스냅샷이 파라미터까지 되돌린다). 아니면 실행 전 파라미터를 되돌리고 재생을 계속한다.

**멈춤**

- "Stop" 버튼: 5와 같이 끝낸다.
- 실행 중 생성기 파라미터를 건드리면: 실행만 멈추고 건드린 값을 지닌다. 재생은 계속된다(Stop하면 Play의 편집처럼 사라진다).
- 실행 중 씬을 떠나면: 5와 같이 끝낸다.

**표와 복사:** 지금과 같다. 단계 줄 "Step i/n: 이름 (warming up | measuring)", 표(Step, Avg ms, Load %, Worst, CPU a/s/d,
GPU sc/sh/up, GPU bl/pt, Alive), 그림자/MSAA/해상도, "Copy results"(커밋, 빌드, user agent, 캔버스, 타임스탬프 여부와 표).

**통계:** Stress 탭의 "Frame (1 s average, ms)" 표는 지금과 같고 보이는 씬을 센다.

### 바뀐 뒤의 코드

```c
typedef struct ParamDef {
    const char* key;      // file key and save name ("count")
    const char* label;    // UI and undo label ("Cubes")
    const char* keywords; // search
    ParamType type;       // bool, int, float, angle, angle rate, clip
    f32 min, max;
    u32 offset;           // into the behavior's parameters
} ParamDef;

typedef struct Behavior {
    BehaviorKind kind;
    NvNodeId node;
    union { Spin spin; Character character; Grid grid; Chain chain; Crowd crowd; EffectsLoad effects; };
} Behavior;

typedef struct SceneDoc {
    b32 built;
    NvScene* scene;
    SceneView view;
    FileNode nodes[SCENE_MAX_FILE_NODES]; // the file's nodes: id and NvNodeId
    u32 node_count;
    Behavior behaviors[SCENE_MAX_BEHAVIORS];
    u32 behavior_count;
    BenchmarkStep steps[STRESS_MAX_STEPS];
    u32 step_count;
    bool show_bones;
    b32 playing;
    f32 play_time;
    u8* play_snapshot; // [SAVE_MAX_SIZE]
    u32 play_snapshot_size;
    u32 layout;        // LAYT, taken after the file's nodes are built
} SceneDoc;
```

- 생성기의 큰 배열(그리드 노드 16000개, 비행 2000개 등)은 빌드할 때 `max`에 맞춰 영구 아레나에서 받는다.

| 파일 | 맡는 것 |
|---|---|
| `sandbox/scene_file.c` (새로) | 텍스트 → `SceneDesc`. GPU 없음. Node에서 테스트 |
| `sandbox/scenes.c` (새로) | `SceneDesc` → `NvScene` + `SceneDoc`, 모델, 씬 전환, Play/Stop |
| `sandbox/behaviors.c` (새로) | 파라미터 표, 동작의 단계(`switch`), Inspector 섹션 |
| `sandbox/stress.c` | 통계, 벤치마크, Stress 탭(생성기 행은 파라미터 표로 그린다) |
| `sandbox/effects.c` | 이펙트 정의와 시험 발사만 |
| `sandbox/main.c` | 초기화와 프레임 순서만 |

- `Sandbox`에서 빠지는 것: `planet`, `moon`, `orbit_*`, `character`, `animator`, `clips`, `root_motion_clips`, `fade_seconds`,
  `blend_*`, `jump*`, `show_sword`, `sword*`, `root_motion`, `turn_rate`, `look_at`, `target*`, `planet_rotation`, `moon_rotation`,
  `show_bones`, `playing`, `play_time`, `play_snapshot*`, `stress`의 워크로드, `effects.stress_*`, `views[]`(→ `SceneDoc.view`).
- 프레임: `behaviors_update(sandbox, doc, dt)`(동작), `effects_update`(vfx와 시험 미사일), `nv_anim_update_scene`, `nv_scene_update`
  순서. 지금의 순서와 같다.
- `SceneKind`와 `shown == SCENE_SHOWCASE` 검사는 모두 없어진다. 보이는 씬은 씬 목록의 인덱스다.
  - Play 버튼, 색조, "Playing" 표시는 모든 씬에 있다.
  - Stress 탭은 보이는 씬에 생성기나 벤치마크가 있을 때 보인다(오브젝트의 타입으로 정해진다).
  - 팔레트 명령 `SC_SCENE_SHOWCASE`, `SC_SCENE_STRESS`는 씬 목록의 씬마다 하나인 "Show the <title> scene"이 된다.
  - 디버그 내보내기(`_sandbox_debug_set_effects`, 그리드와 체인 설정)는 보이는 씬의 그 타입의 첫 동작에 값을 넣는다.

### 저장

`SAVE_VERSION`과 기존 태그의 뜻은 그대로다. 태그를 더하는 것은 버전을 올리지 않는다(`save.md`).

- **씬마다 `SCNE` 하나**(반복): 모든 씬이 같은 방식으로 저장된다. `FILE`(문자열, 씬 목록의 파일 이름) 다음에 지금 `SCNE`의 자식
  (`LAYT`, `VIEW`, `BONE`, `NODE`)과 새 `BHVR`. 씬 목록의 순서로 쓴다.
- **새 `BHVR`**(씬 안, 반복): 동작 하나의 파라미터. `PATH`(노드 경로), `KIND`(문자열), 그리고 파라미터마다 `PARM`
  {`NAME` 문자열, `VALU` f32[] 또는 `TEXT` 문자열}. 값은 실행 중의 단위(라디안)다. 모르는 이름은 건너뛰고, 없는 이름은 지금 값을
  지닌다. 타입이 다르면 건너뛴다. 읽은 뒤 그 동작의 "적용"을 부른다.
- **새 `VISI`**(`NODE` 안, u32): 메시가 있는 노드의 `visible`.
- **옛 저장과의 호환** (모든 씬에 같은 규칙; 특정 씬, 파일, id를 보지 않는다):
  - `FILE`이 없는 `SCNE`(옛 저장은 `SCNE` 하나뿐이다)는 씬 목록의 첫 씬의 것이다.
  - 옛 빌드는 첫 `SCNE`만 읽고 모르는 태그(`FILE`, `BHVR`, `VISI`)는 건너뛴다. 첫 씬이 맨 앞에 쓰이므로 옛 빌드도 새 저장을 읽는다.
  - 옛 태그는 타입으로 찾아간다. 읽기는 그 씬에 새 태그가 없을 때만이고, 쓰기는 그 타입이 씬에 있을 때 계속한다(옛 빌드를 위해).

| 옛 태그 | 새 자리 |
|---|---|
| `PLNT` | 씬의 첫 `spin` 동작(파일 순서)의 `speed` |
| `CHAR`(`CLIP`, `SPED`, `FADE`, `BLND`, `BLDW`, `RMOT`, `TURN`, `LOOK`) | 씬의 첫 `character` 동작의 파라미터 |
| `SWRD`(`CHAR` 안) | 그 `character` 모델 아래의 첫 `attach` 노드의 `visible` |

- **생성된 노드:** `NODE`로 쓰지 않는다. 선택된 생성 노드는 `SELN`/`SELO`에서 빠진다.
- **`LAYT`:** 파일의 노드를 만든 직후, 생성기가 돌기 전의 트리에서 잰다. 쇼케이스 파일이 만드는 트리는 지금과 같으므로 기존 저장의
  `LAYT`가 맞는다.
- **늦게 만들어지는 씬:** 모든 씬은 처음 보일 때 만들어진다. 그 전에 읽은 `SCNE` 바이트는 지녀 두었다가 만들 때 적용한다. 만들기
  전에 저장하면 그 바이트를 그대로 다시 쓴다.
- **재생 중:** 자동 저장은 재생 중인 씬의 스냅샷을 쓴다.
- 샌드박스는 씬 목록의 첫 씬에서 시작한다.
- 저장 뷰어는 `FILE`, `BHVR`, `PARM`을 이름으로 보인다.

### Undo

- 기록은 하나다. 단계마다 씬을 지닌다.
- 범위(보이는 씬에 대해):
  - **Node:** 선택한 파일 노드의 필드(`VISI` 포함). 생성된 노드는 빠진다.
  - **Behaviors:** 씬의 모든 `BHVR`. 지금의 Character 범위를 대신한다.
  - **Scene:** `BONE`. undo 전용 `ORBS`는 없어진다(행성 속도는 `BHVR`에 있다).
- 이름표: "노드 이름 + 파라미터 이름표"(예: "planet Speed", "grid Cubes").
- 보이는 씬이 재생 중이면 Undo와 Redo는 꺼진다. 다른 씬의 단계를 되돌리면 그 씬을 먼저 보인다. 노드 단계가 그 노드를 선택하는 것과
  같은 이유다. 그 씬이 재생 중이면 버튼이 꺼진다.
- 씬을 바꾸면 그 씬의 지금 바이트를 단계 없이 커밋한다.

### 사용자가 보는 변화

- 스트레스 씬에 Play/Stop, undo, 자동 저장이 생긴다.
  - Edit 모드에서는 churn, 체인의 흔들림, 이펙트 워크로드가 돌지 않는다. 그리드, 체인, 군중의 수는 바로 따른다.
  - 벤치마크는 Play에서 돈다.
  - 다시 열면 스트레스 씬의 설정과 편집이 남아 있다.
- "Planet orbit"이 행성의 Inspector **Spin** 섹션으로 옮겨 간다. 달의 회전 속도도 편집된다.
- "Show bones"가 두 씬 모두 View 탭에 있다. Stress 탭의 "Crowd bones"는 여기로 합친다. 시선 선도 그린다.
- 메시가 있는 모든 노드에 "Visible"이 생긴다.
- 스트레스 씬의 그룹 노드는 생성기 섹션을 보인다(Stress 탭의 행과 같은 값).
- undo 이름표가 "노드 + 파라미터"가 된다(예: "Planet orbit" → "planet Speed", "Sword" → "sword Visible").

### 한계

| 항목 | 한계 |
|---|---|
| 파일 노드 | 파일당 256 |
| 메시 | 파일당 32, 각 4096 정점 이하 |
| 팔레트 | 파일당 4, 각 256색 이하 (`NV_MAX_MATERIALS` 256 안에서) |
| 모델 | 샌드박스 전체 8 |
| 동작 | 씬당 64 |
| 벤치마크 단계 | 16 |
| id, 이름 | 31바이트 |

### 오류

- 패키지의 파일이 읽히지 않는 것은 버그다. 오류는 파일과 줄을 붙여 `nv_log`(source `"scene"`)로 남긴다. 16개까지다.
- 읽히지 않는 씬은 보일 수 없다(어느 씬이든 같다). 그 씬으로의 전환은 거절되고 View 탭이 첫 오류를 보인다.
- 시작할 때 첫 씬이 읽히지 않으면 보일 씬이 없으므로 `fatal`로 멈춘다(지금 캐릭터를 읽지 못할 때와 같다).
- ctest가 두 파일이 읽히는지 확인한다. 형식의 각 규칙은 테스트 자신의 텍스트로 시험한다.

### 엔진 변경

- `engine/text.h`, `engine/text.c`: `autobattler/defs.c`의 줄 기반 텍스트 읽기를 옮긴다. `defs.c`가 그것을 쓰고, `.abproj`의 읽기와
  오류 문구는 그대로다.
- `nv_gltf_instantiate`: 메시 노드의 로컬 트랜스폼을 복사하지 않는다. 캐릭터의 메시 노드가 단위 트랜스폼인지 확인하고, 아니면 복사하게
  고친다.

### 단계

1. **텍스트 리더:** `engine/text.*`. 확인: ctest, 오토배틀러 테스트가 그대로 통과한다.
2. **씬 파일 리더:** `sandbox/scene_file.c`, 파라미터 표, `tests/scene_file_test.c`.
3. **쇼케이스를 파일로:** `showcase.nvscene`, `spin`, `character`, `visible`, Inspector 섹션, `BHVR`/`VISI`. `build_world`,
   `build_character`, `update_showcase`와 `Sandbox`의 쇼케이스 필드를 지운다. 확인:
   - `LAYT`가 지금 값과 같다.
   - 지금 빌드가 쓴 저장(변경 전에 만든 고정 파일)이 같은 씬으로 읽힌다.
   - Play 전과 Stop 뒤의 `_sandbox_debug_save_crc`가 같다.
   - 첫 방문의 노드 값이 지금과 1e-5 안에서 같다(도 변환이 마지막 비트를 바꿀 수 있다).
4. **스트레스를 파일로, Edit/Play로:** `stress.nvscene`, 생성기 네 종류, 벤치마크, 씬마다의 `SCNE`, 씬마다의 재생 상태, undo의 씬. `stress_build`의
   노드 코드, `StressWorkloads`, `effects_stress_update`를 지운다. 확인:
   - 각 워크로드가 최대에서 assert 없이 돈다.
   - 벤치마크가 Edit에서 시작해 끝까지 돌고 Stop으로 돌아온다.
   - 스트레스 씬의 편집이 다시 열어도 남는다.
   - 스트레스 씬의 Play 전과 Stop 뒤 CRC가 같다.
   - 워크로드를 쓰는 기존 Playwright 테스트는 움직임을 볼 때 Play를 누른다.
5. **문서:** `AGENTS.md`, `stress.md`, `save.md`, `undo.md`, `play.md`, `sandbox.h` 머리 주석.

모든 단계는 헤드리스 Chromium에서 Release와 Debug 빌드로 확인한다.

### 나중

- **노드 추가와 제거:** 지금 저장은 파일이 만든 노드에 값을 덮어쓴다. 노드를 더하거나 지우려면 저장이 노드의 구조(어떤 메시, 모델,
  동작인지와 부모)까지 담아 읽을 때 노드를 만들어야 한다(`SAVE_VERSION` 2). 그러면 `LAYT`와 경로 맞추기가 필요 없어진다.
- `.nvscene` 열기와 저장(`local_file.h`).
- 오토배틀러가 스테이지를 씬 파일로 만들 때 리더와 빌더를 엔진으로 옮긴다.

### 열린 질문

1. **모델을 어디에 읽어 두는가.** 같은 캐릭터 glb를 두 씬이 쓴다. `nv_gltf_load_model`은 모델을 읽으면서 노드와 애니메이터를 어떤
   씬에 하나 만든다.
   - (가) 그리지 않는 **라이브러리 씬**에 한 번 읽어 두고, 두 씬 모두 거기서 복제한다. 대칭이지만, 라이브러리 씬의 애니메이터 하나가
     쓰이지 않은 채 남는다(256개 중 1개, 메모리 조금).
   - (나) **지금처럼** 처음 쓰는 씬(쇼케이스)에 읽고, 다른 씬(스트레스)은 그것을 복제한다. 낭비는 없지만, 스트레스 씬이 쇼케이스가 먼저
     만들어져 있어야 하는 순서 의존이 남는다.

## English

Status: proposal (2026-10-11). Not implemented. Changes to this spec are agreed first.

### Goal

Describe the sandbox's scenes in text files and build them with one loader. Run every scene with one set of code and one set of
rules.

### Principles

- **The rules are the same in every scene.** Edit/Play, undo, autosave, selection, the save format, switching scenes and error
  handling work the same in any scene.
- **What differs between scenes is set by the scene file:** which objects there are, what type each object is (mesh, camera, light,
  model, behavior type) and what values it has. If an object has to move differently, that is written as its type (a behavior type)
  and its values.
- **The code does not recognize scenes.** No code branches on a particular scene, file name or node id. Only the list of scene files
  is data in code.

### Where today's code breaks these principles (what this spec fixes)

- `build_world` and `build_character` (`sandbox/main.c`) and `stress_build` (`sandbox/stress.c`) build the scenes in C. Every name,
  position, mesh and color is a constant in code.
- How objects move lives in different code per scene.
  - Showcase: fields of `Sandbox` (planet, moon, character, clips, jump, sword, look target) and `update_showcase`.
  - Stress: `Stress` and `stress_update`. The effects workload lives in `Effects`.
- The rules differ per scene: today only the showcase has Edit/Play, undo and autosave; the stress scene always runs and is not saved.
  `frame`, the UI, undo and the save branch on `SceneKind` (`shown == SCENE_SHOWCASE`).

### After

- `sandbox/data/showcase.nvscene` and `sandbox/data/stress.nvscene` describe each scene: nodes, meshes, palettes, models, the view,
  and the **behaviors** that run on nodes.
- Behaviors are a fixed set of types written in C (`spin`, `character`, `grid`, `chain`, `crowd`, `effects`), with values from the
  file.
  - Each type's parameters are defined by one table. That table makes the file reading, the UI, search, the save and the undo labels.
  - One update runs every behavior of the shown scene.
- Every scene follows Edit/Play (`play.md`), undo and autosave the same way.
- `SceneKind` and checks such as `shown == SCENE_SHOWCASE` all go.

### Decided

| Topic | Decision |
|---|---|
| Format | Our own line-based text, like `.abproj`. No new third-party code ("Format candidates" below) |
| Scene rules | The same in every scene. What differs is only the scene file (objects, types, values) |
| Planet orbit | The View tab's slider moves to the planet's Inspector, in a **Spin** section |

### Not in this spec

- Adding and removing nodes in the editor, and a save that holds nodes' structure too ("Later").
- Opening and saving `.nvscene` files on the user's computer (`local_file.h`).
- Moving the reader into the engine. Per `shared.md`, it moves when a second app uses it.

### Format candidates (third-party first, per AGENTS.md)

| Candidate | Library (license) | Pros | Cons |
|---|---|---|---|
| **Our own line-based text, like `.abproj`** (decided) | None (the tokenizer moves from `autobattler/defs.c` into the engine) | A syntax we already know. Diffs show in Git. Errors carry a file and line. The tokenizer gets its second user | We write the reader |
| glTF 2.0 + `extras` | cgltf (MIT; already used) | Standard, Blender opens it | Primitives, model references, joint attachments, behaviors and the view would all be extras |
| JSON | jsmn (MIT; inside cgltf) | Familiar | Noisy to edit by hand, no comments |
| TOML | tomlc99 (MIT) | Comments, readable | A new dependency; deep trees are awkward |
| Chunk binary (`engine/chunk.h`) | None | The same code as the save | Not editable by hand |

### Files

| File | Path in the package | Contents |
|---|---|---|
| `sandbox/data/showcase.nvscene` | `/data/showcase.nvscene` | The showcase |
| `sandbox/data/stress.nvscene` | `/data/stress.nvscene` | The stress scene and its benchmark steps |

- **The scene list** is the only scene information in code: an array of file names,
  `SCENE_FILES[] = {"showcase.nvscene", "stress.nvscene"}` (`sandbox/scenes.c`).
  - The View tab's scene combo and the palette's "Show the ... scene" commands come from this list. The names are the files' `title`s.
  - The sandbox starts on the first scene in the list.
  - Adding a scene is adding a file and one line to the list.
- Text, so plain Git.
- `sandbox/CMakeLists.txt`: `nv_setup_executable(sandbox ROOT ASSETS ... PRELOAD "${CMAKE_CURRENT_SOURCE_DIR}/data@/data")`.
- Read with `nv_file_read` into scratch, parsed into a `SceneDesc` (plain data, no GPU), then built.

### Syntax

As `.abproj` (`abproj.md`, "Syntax"): UTF-8, one statement per line, `#` comments, indentation with spaces sets nesting, a tab is an
error, an unknown key or a line with the wrong number of values is an error with its file and line. Additions:

- **Text:** a double-quoted string of up to 31 bytes (`"look target"`). No escapes.
- **Units:** in the file, angles are degrees and angular speeds degrees per second; they become radians when read. Run-time values,
  the save and the UI stay in radians, as today (the UI's "rad/s" sliders are unchanged).
- **Rotations:** `rotation x y z w` (a quaternion, exact) or `euler pitch yaw roll` (degrees).
- **Computed values** (the sun's rotation, the sword's offset) are written with 9 significant digits, so they read back as the same
  float.
- **Top-level order:** `nvscene_version` → `title` → `view` → `mesh`* → `palette`* → `model`* → `node`+ → `benchmark`?.

### Structure

#### `nvscene_version <integer>`

The first statement. Now `1`. The same rules as `abproj_version`.

#### `title <text>`

The name shown in the scene combo and the palette (through `T()`; Korean rows in `sandbox/strings.c`). Showcase `"Showcase"`, stress `"Stress"`.

#### `view`

| Key | Value | |
|---|---|---|
| `camera <id>` | A node with a camera | Required |
| `target x y z` | The orbit's center, m | Default 0 0 0 |
| `yaw`, `pitch` | Degrees | Required |
| `distance` | m | Required |
| `focus <id>` | Orbited while nothing is selected | Optional |
| `select <id>` | Selected at start | Optional |

`ORBIT_LIMITS` is the editor's limit, so it stays in code.

| | Showcase (today's `build_world`) | Stress (today's `stress_build`) |
|---|---|---|
| camera | `camera` | `camera` |
| target | 0 0.92 0 (the character's head height) | 0 0 0 |
| yaw, pitch, distance | 0.35, 0.12 rad, 5 m | 0.45, 0.42 rad, 28 m |
| focus | `character` | `grid` |
| select | `character` | None |

#### `mesh <id>`

One or more primitive statements in the block, appended into one mesh. Value order follows the functions in `engine/mesh.h`.

| Key | Values |
|---|---|
| `box` | cx cy cz hx hy hz |
| `plane` | hx hz |
| `sphere` | cx cy cz radius segments rings |
| `cylinder`, `cone` | cx cy cz radius half_height segments |
| `capsule` | cx cy cz radius half_height segments rings |
| `torus` | cx cy cz major minor segments tube_segments |

Today's meshes: showcase `cube` (box, half 0.5), `ground` (plane 30×30), `sword` (three boxes: center (0,0,0) half (0.018,0.09,0.018),
center (0,0.1,0) half (0.09,0.012,0.025), center (0,0.52,0) half (0.025,0.41,0.006)), `target` (box, half 0.06). Stress `cube` (half
0.5), `small_cube` (half 0.06).

#### `palette <id>`

A set of materials generators use: today's `colors[]` in `stress.c`.

| Key | Value | |
|---|---|---|
| `base r g b` | Color 0: the grid's color while colors are off | Required |
| `wheel <count>` | Colors 1 to count-1: a color wheel | Required |

The color wheel (`color_wheel`): for color `i`, h = i / count × 6 and x = 1 − |h mod 2 − 1|; by `floor(h) mod 6`, (r,g,b) is
(1,x,0), (x,1,0), (0,1,x), (0,x,1), (x,0,1) or (1,0,x). The result is 0.15 + 0.8 × channel. Stress: `base 0.6 0.62 0.66`, `wheel 200`.

#### `model <id> <text: path>`

| Key | Value | |
|---|---|---|
| `clips <text: path>` | The in-place clip file (up to `SANDBOX_MAX_CLIPS`, 16) | Optional |
| `root_motion_clips <text: path> <joint>` | The root-motion copies and their root joint | Optional |

Today: `/assets/quaternius/character.glb`, `clips.glb`, `clips_rm.glb` (joint `root`). Where models are loaded is open question 1.

#### `node <id>`

`node` blocks inside the block are its children. The file's order is the tree's order.

| Key | Value | |
|---|---|---|
| `name <text>` | The shown name | Default: the id |
| `position x y z`, `rotation`/`euler`, `scale x y z` | | 0, identity, 1 |
| `mesh <id>` | | |
| `color r g b` | The node's own material | The default material |
| `double_sided <0/1>` | | 0 |
| `visible <0/1>` | Whether its mesh is drawn | 1 |
| `camera perspective <fov> <near> <far>` | fov in degrees | |
| `light directional`, `light_color r g b`, `light_intensity x` | | |
| `model <id>` | The node becomes the model's root. The model's mesh nodes (named as in the glTF) are made as its first children, then the file's children follow | |
| `attach <joint>` | Follows that joint of the nearest ancestor model's animator | |
| Behavior blocks | Below | |

- An id is a name unique in its file. Behaviors, the view, the benchmark and the save's mapping refer to nodes by id. It is not the
  name (the Inspector renames nodes).
- The showcase tree is today's: `camera`, `sun`, `ground`, `planet` > `moon`, `character` (the model's mesh nodes, `sword`),
  `look target`.
- The stress tree: `camera`, `sun`, `ground` (the cube scaled to (260, 0.1, 260) at (0, −0.05, −50), color 0.28 0.3 0.34), `grid`,
  `chain` (14, 0.5, 8), `crowd` (0, 0, 4).

#### `benchmark`

```
benchmark
    warmup 1
    measure 3
    step "Cubes 250"
        grid count 250
    step "Particles 1M"
        effects particles 1000000
```

Details are under "Benchmark".

### Edit and Play in every scene

`play.md`'s rules apply to every scene: in Edit mode nothing moves the scene by itself. Making and removing nodes (a generator's
count) is an edit of structure, so it follows at once in Edit mode too.

| Behavior | Edit | Play |
|---|---|---|
| `spin` | Still, at the authored rotation | Turns on top of the authored rotation |
| `character`: clips, crossfades, blend | Previews in place | Runs |
| `character`: root motion, turning | Off (the in-place clip) | On when ticked |
| `character`: jump | Off | On |
| `character`: look target | Stays where it is put | Sweeps in front of the head |
| `grid`: count, colors | Follow at once | Follow at once |
| `grid`: churn | Off | On |
| `chain`: count | Follows at once | Follows at once |
| `chain`: wobble | Still (angle 0.25 rad) | Runs |
| `crowd`: count | Follows at once | Follows at once |
| `crowd`: animation | Previews in place | Runs |
| `effects` workload | Off | On |
| The View tab's test effect buttons | Work as today | Work |

- Play/Stop acts on the shown scene. Each scene has its own playing state and snapshot.
- Leaving a playing scene leaves it paused in Play mode, as the showcase is today (only the shown scene's animators update).
- Play: writes that scene's snapshot (the save format), sets play time to 0, clears every effect (`effects_clear`) and calls each
  behavior's Play start. Stop: restores that scene from the snapshot, clears effects and calls each behavior's Stop.

### Behaviors in detail

Each kind is one branch of a C `switch`; no function pointers or vtables. A behavior has these stages:

- **Build:** once, when the scene is built.
- **Apply:** when parameters changed, after the file, a save or undo put values in.
- **Frame:** every frame, following the Edit/Play rules.
- **Play start, Stop.**
- **UI:** an Inspector section. Generators also show in the Stress tab.

**Parameter tables.** Each kind has one `ParamDef` table: key (the file's and the save's name), label (UI and undo), search keywords,
type (bool, int, float, angle, angular speed, clip), range, and where it lives in the struct. The table makes the file reading, the
widgets, `search_row`, the save (`BHVR`) and the undo labels. **Structure** keys are only in the file and do not change while running.
**Parameters** are changed by the UI, saved and undone.

#### `spin`

Today: the planet and moon in `update_showcase` (`sandbox/main.c:730`), and `planet_rotation`/`moon_rotation` in
`sandbox_start_playing`.

| Key | Kind | Type, range | UI | Showcase values |
|---|---|---|---|---|
| `axis x y z` | Structure | Normalized when read | | Planet (0,1,0), moon (1,1,0) |
| `speed` | Parameter | Angular speed, ±3 rad/s | Spin section, "Speed" slider, "%.2f rad/s" | Planet 0.7 rad/s (40.107 deg/s), moon 2 rad/s (114.592 deg/s) |

- **Play start:** records the node's rotation as the authored one and sets the angle to 0.
- **Frame (Play):** angle += speed × dt; rotation = axis_angle(axis, angle) × authored rotation. A speed changed while playing adds
  up at the new speed from then on (as the planet does today).
- **Edit:** does nothing. The rotation is edited with the gizmo and the Inspector, and undone.
- **Stop:** the snapshot puts the rotation back; the angle is 0.
- The moon is the planet's child, so it goes around with it.
- Today the moon's angle is `play_time × 2` while the planet adds up its speed. Both now add up, so the moon's angle can differ by
  float rounding.
- The moon's speed could not be edited; now it is edited like the planet's.

#### `character`

Today: `sandbox_play`, `sandbox_jump`, `update_jump`, `update_blend`, `apply_turn`, `update_look_target`, `restart_clip`,
`sandbox_back_to_center` (`sandbox/main.c`), `animator_section`, `attach_section` (`sandbox/ui.c`), `read_character_fields`
(`sandbox/save.c`).

**Structure keys**

| Key | Value | Showcase |
|---|---|---|
| `look_target <joint> <node id>` | The aim IK's joint and target node | `Head look_target` |
| `jump <start> <air> <land> <air time>` | The jump's three clips and air time (s) | `Jump_Start Jump_Loop Jump_Land 0.35` |

**Parameters**

| Key | Type, range | UI ("Animator" section) | Default | Save tag (old) |
|---|---|---|---|---|
| `clip` | Clip name | The clip list (selectable rows) | `Idle_Loop` | `CLIP` |
| `speed` | 0–2 | "Speed", "%.2fx" | 1 | `SPED` |
| `fade` | 0–1 s | "Fade", "%.2f s" | 0.3 | `FADE` |
| `blend` | Clip name | "Blend" combo | The first clip | `BLND` |
| `blend_weight` | 0–1 | "Weight" | 0 | `BLDW` |
| `root_motion` | bool | "Root motion" | 0 | `RMOT` |
| `turn` | ±1.5 rad/s | "Turn", "%.2f rad/s" | 0.6 rad/s (34.377 deg/s) | `TURN` |
| `look_at` | bool | "Look at target" | 0 | `LOOK` |

**Build**

- Uses the model node's animator. The clips are the model's `clips` (in place) and `root_motion_clips` (the copies).
- Aim IK: from the `Head` joint's rest-pose model matrix, `joint_axis_towards` picks the axis closest to +Z as `forward` and the one
  closest to +Y as `up`. `weight` is 1 and `target_node` is the target. The axis is the ± of the matrix's three normalized columns with
  the largest dot product with the direction.
- Then plays `clip` ("Playing a clip" below, without a crossfade).

**Playing a clip** (today's `sandbox_play`)

1. Pick the root-motion copy: if `root_motion` is on, the scene is playing and a copy with the same name exists, the copy plays. So
   Edit mode always plays the in-place clip.
2. End the jump (phase = none).
3. `nv_anim_play(animator, clip, fade, loop)`, with loop when the name contains `_Loop`.

What `nv_anim_play` does (`engine/anim.cpp`):

- If layer 0 already plays that clip, only its loop flag changes; it does not restart.
- If fade > 0 and layer 0 has a clip, layer 1 becomes the old layer 0 and layer 0 starts at weight 0. `nv_anim_update` moves layer 0
  to t and layer 1 to 1 − t linearly over the fade, then clears layer 1.
- Otherwise it cuts (layer 1 cleared, weight 1).
- Time starts at 0; speed keeps layer 0's (1 if it was 0).
- Layer 0 is the current clip, 1 the one fading out, 2 the blend clip.

**The clip list** shows this skeleton's clips that are not root-motion copies, in clip order. The current one is layer 0's clip (its
in-place clip of the same name if a copy plays). Picking one is "Playing a clip". Under it, a bar per layer shows "name weight"
(only without a search, as a share of the weights' total).

**Blend** (today's `update_blend`, Edit and Play, every frame)

- On when: blend_weight > 0, layer 0 has a clip, the blend clip's id differs from layer 0's, no crossfade is running, no jump is
  running. A copy on layer 0 has a different id from its in-place clip of the same name, so the blend turns on; that is today's
  behavior, kept.
- Off: clears layer 2, and sets layer 0's weight to 1 unless a crossfade runs.
- On: layer 2 = {the blend clip, time = layer 0 time / layer 0 length × blend length, speed = layer 0 speed × blend length / layer 0
  length, weight = blend_weight, loop}; layer 0's weight = 1 − blend_weight. The time is matched every frame, so feet stay in step.

**Root motion and turning**

- Changing "Root motion" while no jump runs plays layer 0's in-place clip ("Playing a clip"). So while playing, the fade switches
  between the copy and the in-place clip.
- Movement: `nv_anim_update_scene` adds the clip's root movement (turned by the owner's rotation) to the model's root node. Only the
  copies move.
- Turning (Play, root_motion on): root rotation = axis_angle(Y, turn × dt) × root rotation. It turns in place even on a standing clip.
- The "Turn" slider and the "Back to center" button are disabled while root_motion is off. "Back to center" sets the root to (0,0,0)
  and the identity rotation; it works in Edit mode too.

**Jump** (today's `sandbox_jump`, `update_jump`)

- The "Jump" button is enabled only while playing. It does nothing during a jump.
- Start: the clip to return to = layer 0's clip (it may be a copy); play the start clip with fade 0.1 and no loop → phase start.
- Frame (Play): finished = layer 0 has a clip and time ≥ length − 0.001.
  - Start phase, finished: the air clip, fade 0.05, loop; air time 0 → air phase.
  - Air phase: air time += dt. At 0.35 s or more: the land clip, fade 0.05, no loop → land phase.
  - Land phase, finished: the clip to return to, fade 0.2, loop when its name has `_Loop` → no phase.
- Jump clips are found among the in-place clips and played with `nv_anim_play` directly (no copy is picked).
- The blend is off during a jump.

**Look-at** (today's `update_look_target`, every frame)

- The animator's `look_at.enabled` = look_at. While look_at is off the target is not drawn (its mesh is not drawn; separate from
  `visible`, and not saved).
- Play with look_at on: t = play time, angle = sin(0.7 t) × 1.3, local = (sin(angle) × 1.2, 1.75 + 0.3 sin(1.3 t), cos(angle) × 1.2).
  Target position = root position + local turned by the root rotation; target rotation = axis_angle(Y, 2 t).
- Edit: the target stays where it is put (it starts at (0, 1.75, 1.2)). It is edited and undone like any node.

**Play start and Stop** (today's `restart_clip`)

- Play start: play time 0. The clip = the clip to return to during a jump, otherwise layer 0's clip. Its in-place clip plays with no
  fade after layer 0 is cleared (so the same clip restarts too), at time 0. Since the scene is playing, the copy is picked when root
  motion is on.
- Stop: ends the jump; the snapshot puts the parameters back ("Apply" below); the clip restarts the same way.

**Apply** (after the file, a save or undo put parameters in; today's `read_character_fields`)

- Puts in root_motion, turn, look_at, blend_weight (clamped to 0–1) and blend (found by name; unchanged if missing).
- If clip came in: the clip of that name, or `Idle_Loop` if there is none. Fade is set to 0 for a moment and it is played ("Playing a
  clip"); the same clip keeps playing.
- Puts in fade (clamped to 0–1) and speed (layer 0's speed).
- An old save's `CTIM` is read as layer 0's time (never written).

**What the saved state means:** during a jump, `clip` is the clip to return to. While a copy plays, it is the in-place clip of the same
name. No time is saved.

**Inspector:** when the selected node's animator (or its first child's) is a `character` behavior's animator, the whole section above
shows. Other animators (the crowd) show only "Playing: clip time / length". As today.

#### `visible` (a node key, not a behavior)

- Today only the sword has it: the sword node's mesh is drawn only while `show_sword` is on (`update_showcase`). The "Visible"
  checkbox is in the sword's Attach section.
- After: every node with a mesh has "Visible" in its Mesh section. When off, the renderer gets mesh 0 (the node's mesh id is kept).
- The sword: `attach hand_r`. Its local transform is today's computed value (H = `hand_r`'s rest-pose matrix, grip = H's position +
  (−0.08, −0.02, 0), desired = trs(grip, axis_angle(X, π/2), 1), local = H⁻¹ × desired) written to the file with 9 digits. Color (0.75,
  0.77, 0.8), double-sided.
- The look target: box 0.06, color (1, 0.75, 0.2), a top-level node named "look target".

#### `grid`

Today: `add_grid_cube`, `update_grid`, `churn` (`sandbox/stress.c`).

| Key | Kind | Type, range | UI (Stress tab "Workloads") | Stress value |
|---|---|---|---|---|
| `max` | Structure | | | 16000 |
| `columns`, `spacing`, `height`, `scale` | Structure | | | 128, 0.9, 0.25, 0.5 |
| `mesh`, `palette` | Structure | | | `cube`, `wheel` |
| `on` | Parameter | bool | "Cube grid" | 1 |
| `count` | Parameter | 0–max, logarithmic slider | "Cubes" | 1000 |
| `colors_on` | Parameter | bool | "Many colors" | 0 |
| `colors` | Parameter | 2 to the palette's colors − 1 | "Colors" | 16 |
| `churn_on` | Parameter | bool | "Churn" | 0 |
| `churn` | Parameter | 1–256 | "Cubes / frame" | 32 |

- Cube i: named "cube", a child of the grid node, at ((i mod columns − (columns − 1)/2) × spacing, height, −floor(i / columns) ×
  spacing), scale (s, s, s), the mesh, and material = palette color 1 + (i mod colors) while colors are on, otherwise palette color 0.
- Frame (Edit and Play): target = on ? count : 0 (cut by the budget). When the painted count (colors_on ? colors : 0) changes, every
  built cube is repainted. Above the target, cubes are removed from the end; below it, added at the end.
- Churn (Play only, churn_on): n = min(churn, built). n times: i = cursor mod built, cursor += 1; cube i is removed and made again in
  the same place. The new node gets a new slot and generation and becomes the grid's last child.

#### `chain`

Today: `update_chain`.

| Key | Kind | Type, range | UI | Stress value |
|---|---|---|---|---|
| `max` | Structure | | | 1000 |
| `mesh`, `palette`, `stride` | Structure | | | `small_cube`, `wheel`, 7 |
| `link x y z` | Structure | | | 0.12 0.03 0 |
| `twist`, `wobble` | Structure | Degrees | | 14.324 (0.25 rad), 2.292 (0.04 rad) |
| `on` | Parameter | bool | "Deep chain" | 0 |
| `count` | Parameter | 1–max | "Links" | 300 |

- Link i: named "link", its parent is link i − 1 (the first link's is the chain node), at (0,0,0) for the first and `link` after.
  The mesh, and material = palette color 1 + (i × stride) mod (the palette's colors − 1).
- Frame: target = on ? count : 0 (cut by the budget). Links are removed from or added at the end (removing a link removes those below).
- Wobble: angle = twist + wobble × sin(chain time); every link's rotation = axis_angle(Y, angle). Chain time grows by dt only in Play
  and is 0 at Play start and Stop, so in Edit the angle is twist. Today chain time always grows.

#### `crowd`

Today: `update_crowd`.

| Key | Kind | Type, range | UI | Stress value |
|---|---|---|---|---|
| `max` | Structure | | | 200 |
| `model`, `columns`, `spacing`, `offset` | Structure | | | `character`, 20, 1.6, 0.37 s |
| `on` | Parameter | bool | "Crowd" | 1 |
| `count` | Parameter | 1–max | "Characters" | 16 |

- Characters are made when first needed and never removed, since an animator cannot be.
- Making character i: `nv_gltf_instantiate` of the model under the crowd node. Root at ((i mod columns − (columns − 1)/2) × spacing, 0,
  floor(i / columns) × spacing), named "character N" (N = i + 1).
- Clip: from the model's in-place clips whose names contain `_Loop`, in clip order, the (i mod count)-th. It plays with fade 0 and
  loop, at time = (i × offset) mod length.
- Frame: target = on ? count : 0 (cut by the budget). Built characters inside the target get this scene as their animator's scene and
  their mesh nodes' meshes back; the rest get no scene (paused) and mesh 0 (hidden).
- Animation: every clip is in place, so it previews in Edit and runs in Play, as today.

#### `effects`

Today: `effects_stress_update`, `field_point`, `new_flight` (`sandbox/effects.c`).

| Key | Kind | Type, range | UI | Stress value |
|---|---|---|---|---|
| `area x z` | Structure | m | | 16 12 |
| `on` | Parameter | bool | "Fire effects" | 0 |
| `particles` | Parameter | 0–4,000,000, logarithmic | "Live particles" | 0 |
| `explosions` | Parameter | 0–200 /s | "Explosions / s" | 0 |
| `missiles` | Parameter | 0–2000 | "Missiles in flight" | 0 |
| `beams` | Parameter | 0–500 | "Beams" | 0 |
| `decals` | Parameter | 0–2000 /s | "Decals / s" | 0 |

It runs only in Play. dt is cut to 0.1 s. A field point = the orbit center ± area.x (x), a given height, the orbit center ± area.z
(z), at random (`Effects.rng`, seeded 0x9E3779B9, shared with the test effects).

1. **Missiles:** target = on ? missiles : 0. Extra flights are dropped from the end. Missing ones are added: angle = random × 2π,
   speed 6–10, position = a field point (height 0.3), velocity = (cos angle × speed, 3–5, sin angle × speed), life 1.2–2.0 s. A new
   flight's life is multiplied by a random number, so they are out of step.
   - Every frame: velocity.y −= 4 dt, position += velocity dt, life −= dt.
   - With dt > 0, (length × 12 + 1) missile particles are emitted along the step and a trail segment is added.
   - A flight whose life ran out or whose height is below 0.05 starts again as a new flight.
2. **Beams:** target = on ? beams : 0. Beam i: four 0–1 values drawn from h = i × 2654435761 + 12345 set its start (the orbit center ±
   area, height 7) and end (height 0.05). It is held every frame in the laser style with life dt × 1.5 + 0.02.
3. If on is off, stop here.
4. **Particles:** carry += (particles / 2.5) × dt / 512. While the carry is 1 or more, subtract 1 and fire one swarm burst (512) at a
   field point (height 0.05). At most 4000 a frame; the rest of the carry is dropped.
5. **Explosions:** carry += explosions × dt. At most 300 explosions a frame at field points; then a carry above 1 becomes 0.
6. **Decals:** carry += decals × dt. At most 2000 scorch marks a frame at field points (height 0), size 0.6–1.8, angle from the vfx
   random number.

- The flight list and the three carries are this behavior's run-time state (today `Effects.stress_*`). `effects_clear` (Play, Stop,
  a benchmark step) empties them.
- The test effect buttons and their missiles (`Effects.flights`) are a View tab tool, not a behavior, and stay in `effects.c`.

#### The node budget and the generators' order

- Today (`stress_update`): animators left = `NV_MAX_ANIMATORS` − 2, crowd = min(wanted, max, animators left), crowd nodes =
  max(crowd, built characters) × 4, budget = `NV_MAX_NODES` − 1 − 6 − crowd nodes, chain = min(wanted, budget), grid = min(wanted, max,
  budget − chain).
- After: counts instead of constants. Animators left = `NV_MAX_ANIMATORS` − animators in use; nodes left = `NV_MAX_NODES` − 1 − live
  nodes not generated. One character's nodes = 1 + the model's mesh nodes. The order stays crowd, chain, grid.
- The frame's order also stays: shrink the grid, shrink the chain, the crowd, grow the chain (with the wobble), grow the grid (with
  the colors), churn, effects. Shrinking first keeps the node count under the limit.
- Generated nodes are not saved and not undone. They can be selected and inspected, but the Inspector shows a line saying edits do
  not stay ("Made by grid: edits are not saved"). A generator's nodes always come after their parent's file children, so file nodes'
  paths do not change.

#### Bones overlay

- "Show bones" per scene (View tab, saved as `BONE`). Today's showcase `show_bones` and the Stress tab's "Crowd bones" merge.
- For every animator this scene updates, a line from each joint's parent to the joint (1, 0.8, 0.2) (owner's world matrix × joint
  model matrix).
- An animator with look-at on also gets the head's forward axis, 0.6 m in green (0.2, 1, 0.3), and a red line (1, 0.2, 0.2) from the
  head to the target. Today only the showcase character gets these.

### Benchmark

Today: `start_step`, `start_benchmark`, `stop_benchmark`, `stress_after_frame`, `benchmark_section`, `copy_results`
(`sandbox/stress.c`). A scene with a `benchmark` block shows the Benchmark section in the Stress tab.

**Steps:** the file's `step` blocks (at most 16). A line `<node id> <key> <value>` in a step turns that node's generator on and sets
the value. Today's table:

| Steps | Line |
|---|---|
| Cubes 250, 1000, 4000, 8000, 16000 | `grid count N` |
| Crowd 8, 32, 60, 120, 200 | `crowd count N` |
| Particles 250k, 1M, 2M | `effects particles N` |
| Missiles 500, 2000 | `effects missiles N` |
| Beams 500 | `effects beams N` |

**A run**

1. "Run benchmark": if the scene is in Edit mode, Play starts (a Play the run started). The parameters before the run are kept. The
   shadow settings, the MSAA sample count and the resolution text are recorded. Results are cleared.
2. Step start: every generator's parameters are set to **the file's values**, all generators are turned off, and the step's lines are
   applied. `effects_clear`. The start time is recorded. Today the color, link and churn counts come from before the run, but those
   generators are off in every step, so the results are the same. Starting from the file's values makes every run start in the same
   place, whatever the saved settings are.
3. At the end of each frame (once its times are known): while elapsed < warmup (1 s), skip. Otherwise add up the frame, CPU
   anim/scene/draw/ui and GPU scene/shadow/upscale/bloom/particles times and the live particle count, and track the worst frame and
   the frame count.
4. When elapsed ≥ warmup + measure (4 s), take the averages and go to the next step, or end after the last.
5. End: if the run started Play, Stop (the snapshot puts the parameters back too). Otherwise put back the parameters from before the
   run and keep playing.

**Stopping**

- The "Stop" button: ends as in 5.
- Touching a generator parameter during a run: stops only the run and keeps the touched value. Playing goes on (Stop then drops it, as
  any edit made in Play).
- Leaving the scene during a run: ends as in 5.

**Table and copy:** as today. The step line "Step i/n: name (warming up | measuring)", the table (Step, Avg ms, Load %, Worst,
CPU a/s/d, GPU sc/sh/up, GPU bl/pt, Alive), shadows/MSAA/resolution, and "Copy results" (commit, build, user agent, canvas, whether
there are timestamps, and the table).

**Stats:** the Stress tab's "Frame (1 s average, ms)" table is unchanged and counts the shown scene.

### The code after the change

```c
typedef struct ParamDef {
    const char* key;      // file key and save name ("count")
    const char* label;    // UI and undo label ("Cubes")
    const char* keywords; // search
    ParamType type;       // bool, int, float, angle, angle rate, clip
    f32 min, max;
    u32 offset;           // into the behavior's parameters
} ParamDef;

typedef struct Behavior {
    BehaviorKind kind;
    NvNodeId node;
    union { Spin spin; Character character; Grid grid; Chain chain; Crowd crowd; EffectsLoad effects; };
} Behavior;

typedef struct SceneDoc {
    b32 built;
    NvScene* scene;
    SceneView view;
    FileNode nodes[SCENE_MAX_FILE_NODES]; // the file's nodes: id and NvNodeId
    u32 node_count;
    Behavior behaviors[SCENE_MAX_BEHAVIORS];
    u32 behavior_count;
    BenchmarkStep steps[STRESS_MAX_STEPS];
    u32 step_count;
    bool show_bones;
    b32 playing;
    f32 play_time;
    u8* play_snapshot; // [SAVE_MAX_SIZE]
    u32 play_snapshot_size;
    u32 layout;        // LAYT, taken after the file's nodes are built
} SceneDoc;
```

- Generators' large arrays (16000 grid nodes, 2000 flights and so on) come from the permanent arena at build, sized by `max`.

| File | Does |
|---|---|
| `sandbox/scene_file.c` (new) | Text → `SceneDesc`. No GPU. Tested under Node |
| `sandbox/scenes.c` (new) | `SceneDesc` → `NvScene` + `SceneDoc`, models, switching scenes, Play/Stop |
| `sandbox/behaviors.c` (new) | Parameter tables, the behavior stages (a `switch`), Inspector sections |
| `sandbox/stress.c` | Stats, the benchmark, the Stress tab (generator rows drawn from the parameter tables) |
| `sandbox/effects.c` | Only the effect definitions and the test fires |
| `sandbox/main.c` | Only startup and the frame's order |

- What leaves `Sandbox`: `planet`, `moon`, `orbit_*`, `character`, `animator`, `clips`, `root_motion_clips`, `fade_seconds`,
  `blend_*`, `jump*`, `show_sword`, `sword*`, `root_motion`, `turn_rate`, `look_at`, `target*`, `planet_rotation`, `moon_rotation`,
  `show_bones`, `playing`, `play_time`, `play_snapshot*`, the workloads in `stress`, `effects.stress_*`, and `views[]` (→
  `SceneDoc.view`).
- Frame: `behaviors_update(sandbox, doc, dt)` (the behaviors), `effects_update` (vfx and the test missiles), `nv_anim_update_scene`,
  `nv_scene_update`, in that order, as today.
- `SceneKind` and every `shown == SCENE_SHOWCASE` check go. The shown scene is an index into the scene list.
  - The Play button, the tint and the "Playing" label are in every scene.
  - The Stress tab shows while the shown scene has generators or a benchmark (decided by its objects' types).
  - The palette commands `SC_SCENE_SHOWCASE` and `SC_SCENE_STRESS` become one "Show the <title> scene" per scene in the list.
  - Debug exports (`_sandbox_debug_set_effects`, the grid and chain setters) set the shown scene's first behavior of that type.

### Save

`SAVE_VERSION` and the meaning of every existing tag stay. Adding tags does not raise the version (`save.md`).

- **One `SCNE` per scene** (repeated): every scene is saved the same way. `FILE` (a string, the file name in the scene list), then
  today's `SCNE` children (`LAYT`, `VIEW`, `BONE`, `NODE`) and the new `BHVR`. Written in the scene list's order.
- **New `BHVR`** (in a scene, repeated): one behavior's parameters. `PATH` (the node's path), `KIND` (a string), and a `PARM` per
  parameter {`NAME` string, `VALU` f32[] or `TEXT` string}. Values are in run-time units (radians). Unknown names are skipped; a missing
  name keeps its current value; a different type is skipped. After reading, the behavior's "Apply" runs.
- **New `VISI`** (in `NODE`, u32): `visible` of a node with a mesh.
- **Compatibility with old saves** (one rule for every scene; no particular scene, file or id is looked at):
  - An `SCNE` without `FILE` (an old save has only one `SCNE`) belongs to the first scene in the list.
  - Old builds read only the first `SCNE` and skip tags they do not know (`FILE`, `BHVR`, `VISI`). The first scene is written first,
    so old builds read new saves too.
  - Old tags reach their values by type. They are read only when the scene has no new tags, and still written when the scene has that
    type (for old builds).

| Old tag | New home |
|---|---|
| `PLNT` | `speed` of the scene's first `spin` behavior (file order) |
| `CHAR` (`CLIP`, `SPED`, `FADE`, `BLND`, `BLDW`, `RMOT`, `TURN`, `LOOK`) | The parameters of the scene's first `character` behavior |
| `SWRD` (in `CHAR`) | `visible` of the first `attach` node under that `character`'s model |

- **Generated nodes** are not written as `NODE`. A selected generated node is left out of `SELN`/`SELO`.
- **`LAYT`** is taken from the tree right after the file's nodes are built, before any generator runs. The showcase file builds
  today's tree, so existing saves' `LAYT` matches.
- **Scenes built later:** every scene is built the first time it is shown. `SCNE` bytes read before that are kept and applied when it
  is built. A save written before it is built writes those bytes back unchanged.
- **While playing:** the autosave writes the playing scene's snapshot.
- The sandbox starts on the first scene in the list.
- The save viewer shows `FILE`, `BHVR` and `PARM` by name.

### Undo

- There is one history. Each step carries its scene.
- Scopes (of the shown scene):
  - **Node:** the selected file nodes' fields (`VISI` included). Generated nodes are left out.
  - **Behaviors:** every `BHVR` of the scene. It replaces today's Character scope.
  - **Scene:** `BONE`. The undo-only `ORBS` goes (the planet's speed is in `BHVR`).
- Labels: "node name + parameter label" (for example "planet Speed", "grid Cubes").
- Undo and Redo are off while the shown scene plays. Undoing a step of another scene shows that scene first, for the same reason a
  node step selects its node. If that scene is playing, the buttons are off.
- Switching scenes commits that scene's current bytes without a step.

### Changes users see

- The stress scene gets Play/Stop, undo and autosave.
  - In Edit mode, churn, the chain's wobble and the effects workload do not run. Grid, chain and crowd counts follow at once.
  - The benchmark runs in Play.
  - The stress scene's settings and edits are still there after a reload.
- "Planet orbit" moves to the planet's Inspector, in a **Spin** section. The moon's spin speed becomes editable.
- "Show bones" is in the View tab for both scenes; the Stress tab's "Crowd bones" merges into it. It draws the look-at lines too.
- Every node with a mesh gets "Visible".
- The stress scene's group nodes show their generator section (the same values as the Stress tab's rows).
- Undo labels become "node + parameter" (for example "Planet orbit" → "planet Speed", "Sword" → "sword Visible").

### Limits

| Item | Limit |
|---|---|
| File nodes | 256 per file |
| Meshes | 32 per file, each at most 4096 vertices |
| Palettes | 4 per file, each at most 256 colors (within `NV_MAX_MATERIALS`, 256) |
| Models | 8 for the whole sandbox |
| Behaviors | 64 per scene |
| Benchmark steps | 16 |
| Ids, names | 31 bytes |

### Errors

- A packaged file that does not read is a bug. Errors are logged with their file and line through `nv_log` (source `"scene"`), at
  most 16.
- A scene that does not read cannot be shown (the same for every scene). Switching to it is refused and the View tab shows the first
  error.
- If the first scene does not read at start, there is no scene to show, so the sandbox stops with `fatal` (as today when the character
  fails to load).
- ctest checks that the two files read. Each rule of the format is tested with the test's own text.

### Engine changes

- `engine/text.h`, `engine/text.c`: the line-based text reading moves from `autobattler/defs.c`. `defs.c` uses it, and `.abproj`
  reads and reports errors exactly as before.
- `nv_gltf_instantiate` does not copy mesh nodes' local transforms. Check that the character's mesh nodes have identity transforms;
  if not, make it copy them.

### Phases

1. **Text reader:** `engine/text.*`. Check: ctest and the auto-battler's tests pass unchanged.
2. **Scene file reader:** `sandbox/scene_file.c`, the parameter tables, `tests/scene_file_test.c`.
3. **The showcase from its file:** `showcase.nvscene`, `spin`, `character`, `visible`, the Inspector sections, `BHVR`/`VISI`. Remove
   `build_world`, `build_character`, `update_showcase` and the showcase fields of `Sandbox`. Check:
   - `LAYT` equals today's value.
   - A save written by today's build (a fixed file made before the change) loads into the same scene.
   - `_sandbox_debug_save_crc` before Play equals the one after Stop.
   - First-visit node values match today's within 1e-5 (degree conversions may change the last bit).
4. **The stress scene from its file, under Edit/Play:** `stress.nvscene`, the four generator kinds, the benchmark, an `SCNE` per scene, per-scene
   playing state, undo's scene. Remove `stress_build`'s node code, `StressWorkloads` and `effects_stress_update`. Check:
   - Every workload at its maximum without asserts.
   - A benchmark started in Edit mode runs to the end and comes back through Stop.
   - The stress scene's edits survive a reload.
   - The stress scene's CRC before Play equals the one after Stop.
   - Existing Playwright tests that use workloads press Play where they look for motion.
5. **Docs:** `AGENTS.md`, `stress.md`, `save.md`, `undo.md`, `play.md`, and the top comment of `sandbox.h`.

Every phase is checked in Release and Debug builds in headless Chromium.

### Later

- **Adding and removing nodes:** today's save writes values onto the nodes the file made. To add or remove nodes, the save has to
  hold nodes' structure too (which mesh, model and behaviors, and the parent) and make the nodes when read (`SAVE_VERSION` 2). Then
  `LAYT` and matching by path are no longer needed.
- Opening and saving `.nvscene` files (`local_file.h`).
- When the auto-battler builds its stage from a scene file, the reader and builder move into the engine.

### Open question

1. **Where models are loaded.** Both scenes use the same character glb. `nv_gltf_load_model` makes nodes and one animator in some
   scene as it loads.
   - (a) Load it once into a **library scene** that is never drawn, and have both scenes copy it from there. Symmetric, but the
     library scene's animator stays unused (1 of 256, and a little memory).
   - (b) **As today:** load it into the first scene that uses it (the showcase), and have other scenes (the stress scene) copy it from
     there. Nothing is wasted, but the stress scene still depends on the showcase being built first.

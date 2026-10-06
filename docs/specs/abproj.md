# Auto-battler project file spec (`.abproj`)

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-10-05). 이 스펙의 변경은 먼저 합의한다.

### 목표

오토배틀러의 디자인 데이터 전부를 **파일 하나**에 담는다. 전에는 두 파일(`units.txt`, `stage.txt`)에 나뉘어 있었고, 코드에
`#define`으로 있던 규칙 상수(격자 크기, 라운드 시간, 중력 등)는 코드 안에 있었다. 이제 모두 이 파일에 있다. 데이터의 가짓수가
적으므로 파일을 나누지 않는다.

이 파일은 나중에 디자이너가 쓰는 에디터가 열고 저장하는 단위이자, 데이터를 별도 리포로 옮길 때 옮기는 단위다(둘 다 이 스펙
밖, "나중에" 절).

### 파일

| 파일 | 패키지 안 경로 | 내용 |
|---|---|---|
| `autobattler/data/default.abproj` | `/data/default.abproj` | 규칙, 유닛 정의, 스테이지 |

- 확장자는 `.abproj`(auto-battler project). 텍스트이므로 일반 Git에 두고(`.gitattributes`에 LFS 패턴을 더하지 않는다), 코드처럼
  리뷰한다.
- `nv_setup_executable`의 `PRELOAD`는 폴더(`autobattler/data@/data`) 그대로다.
- 형식은 직접 만든 줄 단위 텍스트다(`battle.md`의 "서드파티 후보"에서 결정). 새 서드파티는 없다.

### 문법

- UTF-8 텍스트, 줄 끝은 LF나 CRLF.
- 한 줄에 문장 하나: 키 하나와 그 값들을 스페이스로 나눈다. 빈 줄은 무시한다.
- `#`부터 줄 끝까지는 주석이다.
- **들여쓰기가 중첩을 정한다.** 블록을 여는 문장(`rules`, `unit`, `weapon`, `ability`, `stage`) 다음에 더 깊게 들여 쓴 줄들이 그
  블록에 속한다. 같은 블록의 줄은 들여쓰기가 같아야 한다. 들여쓰기는 스페이스만 쓴다(탭은 오류).
- 값의 종류:
  - **수:** 10진수, 부호와 소수점 가능(`120`, `0.5`, `-3`). 지수 표기는 없다. 정수 자리에 소수가 오면 오류다.
  - **이름:** `[A-Za-z_][A-Za-z0-9_]*`, 31바이트까지(`Crawler`).
  - **낱말:** 키마다 정해진 목록 중 하나(`shield`).
- 각도는 파일에서 도(degree)이고 읽을 때 라디안으로 바꾼다. 거리는 m, 시간은 초(읽을 때 틱으로 바꾼다, "규칙" 절).
- 다음은 오류다: 모르는 키, 블록 밖에 온 블록 안 키, 같은 블록 안에서 두 번 나온 키, 빠진 필수 키, 값 개수가 틀린 줄, 범위를
  벗어난 값, 탭, 같은 이름의 유닛 두 개, 정해진 자리가 아닌 곳의 최상위 문장.
- **최상위 문장의 순서가 정해져 있다.** 최상위 문장은 들여쓰지 않은 문장(`abproj_version`, `rules`, `unit`, `stage`)이고,
  이 순서로 온다: `abproj_version` → `rules` → `unit` 하나 이상 → `stage`. 순서가 정해져 있으면 한 번 훑어 읽을 수 있고(유닛을
  읽을 때 규칙이, 스테이지를 읽을 때 유닛이 이미 있다), 에디터가 쓰는 파일도 모양이 하나로 정해진다. 자리가 아닌 곳의 문장은
  오류 하나로 알리고 그 블록은 건너뛴다(두 블록을 바꿔 쓴 실수가 오류 하나가 되고, 거기에 기대는 것들의 오류가 줄줄이 따라오지
  않는다).

### 구조

| 최상위 문장 | 개수 | 내용 |
|---|---|---|
| `abproj_version <정수>` | 정확히 하나, 첫 문장 | 형식 버전("버전" 절). 지금은 `1` |
| `rules` | 블록, 정확히 하나 | 규칙 상수 |
| `unit <이름>` | 블록, 하나 이상, 최대 16 | 유닛 정의 |
| `stage` | 블록, 정확히 하나 | 공급, 시드, 적 배치 |

#### `rules`

모든 키가 필수다. 기본값을 코드에 두지 않으므로 규칙의 값은 이 파일에만 있다.

| 키 | 값 | 범위 | 값 (전에 코드의 상수였던 것) |
|---|---|---|---|
| `cell_size` | 수, m | 0 초과 10 이하 | 2 (`BATTLE_CELL_SIZE`) |
| `grid` | 정수 둘: 너비(칸), 길이(행) | 너비 1–64, 길이 2–128 | 32 48 (`BATTLE_GRID_WIDTH`, `_LENGTH`) |
| `zone_rows` | 정수, 행 | 1–32, 길이의 절반 이하 | 14 (`BATTLE_ZONE_ROWS`) |
| `round_time` | 수, 초 | 0 초과 600 이하 | 60 (`BATTLE_MAX_TICKS` / 30) |
| `gravity` | 수, m/s² | 0 초과 100 이하 | 9.8 (`BATTLE_GRAVITY`) |
| `retarget_interval` | 수, 초 | 0 초과 10 이하 | 0.25 (`BATTLE_RETARGET_TICKS` 8) |
| `stop_fraction` | 수 | 0 초과 1 이하 | 0.9 (`BATTLE_STOP_FRACTION`) |
| `min_damage_fraction` | 수 | 0–1 | 0.25 (`BATTLE_MIN_DAMAGE_FRACTION`) |

- 플레이어 구역은 행 `0`부터 `zone_rows - 1`, 적 구역은 `길이 - zone_rows`부터 `길이 - 1`이다. 두 구역은 겹치지 않는다.
- 시간은 읽을 때 틱으로 바꾼다. 쿨다운과 같은 규칙이다(초 × 30, 올림, `battle_seconds_to_ticks`). `round_time 60`은 1800틱,
  `retarget_interval 0.25`는 8틱(7.5의 올림)이다. 언제나 1틱 이상이다(0틱이 나오면 1틱으로 올린다).
- 규칙 키는 하나의 의미만 가진다. `stop_fraction`은 "목표가 사거리의 이 비율 안에 들면 멈춘다", `min_damage_fraction`은 "방어력이
  한 발에서 깎는 피해는 이 비율 이상을 남긴다"(`max(피해 - 방어력, 피해 × min_damage_fraction)`)다.

**코드에 남는 것과 그 이유:**
- **틱 속도(30 Hz, `BATTLE_TICK_RATE`):** 모든 시간을 틱으로 바꾸는 기준이고, 뷰의 프레임 루프와 속도 조절이 그 위에 있다.
  바꾸면 같은 파일의 결과가 달라지므로(결정론) 데이터가 아니라 엔진의 결정이다.
- **용량:** 배열 크기를 정하는 값(`BATTLE_MAX_UNIT_DEFS`, `BATTLE_MAX_PROJECTILES`, `BATTLE_MAX_EVENTS`, 이름 길이)은 매크로로
  남는다. 격자 크기가 데이터가 되었으므로 그 상한도 매크로다: `BATTLE_MAX_GRID_WIDTH 64`, `BATTLE_MAX_GRID_LENGTH 128`,
  `BATTLE_MAX_ZONE_ROWS 32`, `BATTLE_MAX_PLACES (BATTLE_MAX_ZONE_ROWS * BATTLE_MAX_GRID_WIDTH)`. 위 표의 범위는 이 상한에서
  나온다.

#### `unit <이름>`

| 키 | 값 | 필수 | 범위, 기본값 |
|---|---|---|---|
| `unit <이름>` | 블록 | 하나 이상, 최대 16 | 이름은 파일 안에서 하나뿐 |
| ├ `cost` | 정수 | 예 | 1–10000 |
| ├ `health` | 수 | 예 | > 0 |
| ├ `armor` | 수 | 아니오 | ≥ 0, 기본 0 |
| ├ `speed` | 수, m/s | 예 | ≥ 0 |
| ├ `radius` | 수, m | 예 | > 0 |
| ├ `height` | 수, m | 예 | > 0 |
| ├ `weapon <이름>` | 블록 | 예, 정확히 하나 | |
| │ ├ `range` | 수, m | 예 | > 0 |
| │ ├ `damage` | 수 | 예 | ≥ 0 |
| │ ├ `cooldown` | 수, 초 | 예 | > 0 |
| │ ├ `launch_angle` | 수, 도 | 예 | 0 초과 90 미만 |
| │ ├ `spread` | 수, m | 아니오 | ≥ 0, 기본 0 |
| │ └ `muzzle` | 수 셋(x y z), m, 유닛 공간 | 아니오 | 기본 0 0 0 |
| └ `ability <종류>` | 블록 | 아니오, 하나까지 | 종류: `shield` |
| &nbsp;&nbsp; ├ `radius` | 수, m | 예 | > 0 |
| &nbsp;&nbsp; ├ `capacity` | 수 | 예 | > 0 |
| &nbsp;&nbsp; ├ `regen` | 수, 초당 | 예 | ≥ 0 |
| &nbsp;&nbsp; └ `regen_delay` | 수, 초 | 예 | ≥ 0 |

#### `stage`

| 키 | 값 | 필수 | 범위 |
|---|---|---|---|
| `supply <정수>` | 양쪽의 공급 | 예, 한 번 | 1–100000 |
| `seed <정수>` | 난수 시드(퍼짐). 같은 배치는 같은 결과 | 아니요, 한 번 | 0–4294967295, 기본 1 |
| `place <유닛 이름> <칸 x> <칸 행>` | 적 유닛 하나 | 하나 이상 | 이름은 위의 `unit` 중 하나. x 0–(너비 − 1), 행은 적 구역. 칸마다 하나. 비용 합 ≤ `supply` |

`place`의 행은 전장 전체의 행 번호다(`grid 32 48`, `zone_rows 14`이면 34–47). 범위는 `rules`에서 계산한다. `rules`에 오류가 있으면
칸을 따질 격자를 모르므로 `place`의 칸 검사는 하지 않는다(틀린 격자 때문에 맞는 줄이 오류가 되지 않게). 이름과 수의 검사는 한다.

### 예

`autobattler/data/default.abproj`. 규칙은 전에 코드에 있던 값이라 읽은 결과가 전과 같다(같은 배치와 시드는 같은 해시).

```
# autobattler/data/default.abproj: the auto-battler's rules, units and stage (docs/specs/abproj.md)
abproj_version 1

rules
    cell_size 2              # m
    grid 32 48               # cells across, rows long
    zone_rows 14             # each side's deployment rows
    round_time 60            # s
    gravity 9.8              # m/s²
    retarget_interval 0.25   # s
    stop_fraction 0.9        # of the weapon's range
    min_damage_fraction 0.25 # armor never takes more than 75% of a hit

unit Crawler
    cost 100
    health 120
    armor 5
    speed 5            # m/s
    radius 0.5
    height 0.8
    weapon Lobber
        range 20
        damage 30
        cooldown 1.5
        launch_angle 45    # degrees
        spread 0.5
        muzzle 0 0.6 0.3
    ability shield
        radius 1.2
        capacity 60
        regen 10           # per second
        regen_delay 3

stage
    supply 1000
    seed 1
    place Crawler 12 36
    place Crawler 14 36
    place Crawler 16 36
    place Crawler 18 36
    place Crawler 13 38
    place Crawler 15 38
    place Crawler 17 38
    place Crawler 14 40
    place Crawler 16 40
    place Crawler 15 42
```

### 버전

- 첫 문장 `abproj_version <n>`이 형식 버전이다. 없거나 첫 문장이 아니면 오류다
  (`default.abproj:1: the file must start with 'abproj_version <version>'`; 비어 있는 파일은 줄 번호 없이). 첫 문장이 다른 것이면 그
  하나만 알리고 버전이 있는 것처럼 계속 읽는다.
- 형식을 바꿀 때마다(키를 더하거나 빼거나, 필수가 되거나, 의미나 단위가 바뀔 때) 코드의 `ABPROJ_VERSION`(`battle.h`)을 올리고 이
  절의 표에 행을 더한다. 새 키는 같은 버전에 더하지 않는다.
- 읽는 쪽은 `1`부터 `ABPROJ_VERSION`까지 읽는다. 옛 버전은 읽을 때 지금 형식으로 바꾼다(표의 "옛 파일 읽기"). 더 높은 버전은
  `default.abproj:1: format 3 is newer than this build reads (2)` 오류 하나이고, 그 뒤는 읽지 않는다(새 형식이 무엇을 뜻하는지
  모르므로).
- `abproj_version`은 1 이상 `ABPROJ_VERSION` 이하의 정수다.

| 버전 | 바뀐 것 | 옛 파일 읽기 |
|---|---|---|
| 1 | 첫 형식 | — |

### 읽기와 오류

- `autobattler/defs.c`가 읽는다. 함수 하나다:

  ```c
  b32 defs_read_project(BattleDefs* defs, const char* file_name, const char* text, umm size);
  ```

  메모리의 텍스트를 받으므로 테스트는 문자열로 시험한다. `main.c`는 `/data/default.abproj`를 `nv_file_read`로 임시 아레나에 읽어
  넘긴다. `defs`는 0으로 비운 채 시작한다. GPU와 ImGui를 쓰지 않는다.
- 결과는 고정 용량의 평평한 배열이다: `UnitDef units[16]`, 적 배치 목록, 그리고 `BattleRules rules`. 시간은 틱으로, 각도는 라디안으로
  바뀌어 있다:

  ```c
  typedef struct BattleRules {
      f32 cell_size;                 // meters
      u32 grid_width, grid_length;   // cells, rows
      u32 zone_rows;                 // each side's; the enemy's start at grid_length - zone_rows
      u32 round_ticks, retarget_ticks;
      f32 gravity;                   // m/s²
      f32 stop_fraction, min_damage_fraction;
  } BattleRules;
  ```

- 규칙을 읽는 곳은 `defs->rules`다: `battle.c`(이동, 조준, 포탄, 라운드 끝, 칸 중심 `battle_cell_center(rules, x, row)`, 적 구역의
  첫 행 `battle_enemy_first_row(rules)`), `battle_view.c`(필드 평면, 구역 격자, 칸 고르기, 카메라 목표의 제한, 패널의 시간)와
  `game.h`의 `field_width`, `field_length`. `Battle.placed`는 `[BATTLE_MAX_ZONE_ROWS][BATTLE_MAX_GRID_WIDTH]`이고 쓰는 범위는 규칙의
  크기다. 카메라의 시작 거리는 전장 길이에 비례한다(96 m 길이에서 104 m), 최대 거리와 far 평면도 그에 따라 커진다.
- 수는 `strtod`로 읽는다. 올바르게 반올림하므로 같은 파일은 언제나 같은 값이 된다(결정론).
- 오류마다 파일 이름과 줄 번호를 붙여 `nv_log(NV_LOG_ERROR, "battle", "default.abproj:12: unknown key 'healt'")`로 알린다. 첫
  오류에서 멈추지 않고 파일 끝까지 읽어 오류를 모두 알린다(20개까지). 로그 글은 영어다(`AGENTS.md`). 오류가 하나라도 있으면 읽기는
  실패한다. Battle 패널은 첫 오류와 "Definitions could not be loaded"를 보이고 Start를 막는다. 데이터 오류는 실행 중 일어나는
  실패이므로 assert하지 않는다(코딩 표준).
- 파일 전체의 모자람(`missing 'rules'`, `no units`, `missing 'stage'`)은 다른 오류가 없을 때만 알린다. 앞에서부터 처음 모자란 것
  하나다. 블록의 모자람은 블록의 줄에 알린다(`block 'rules' is missing 'gravity'`, `unit 'A' is missing 'health'`,
  `'stage' has no 'place' lines`). `zone_rows`가 길이의 절반을 넘으면 `zone_rows` 줄에 `'zone_rows' must be at most 24`.
- 파일을 못 읽었거나 오류가 있으면 `main.c`는 뷰가 빈 전장과 오류를 그릴 수 있게 고정된 `BattleRules`(`fallback_rules`)를 `defs.rules`에
  넣는다. 전투는 이 규칙으로 돌지 않는다(`defs_ok`가 거짓이면 `battle_init`을 부르지 않는다).

### 불러오기와 저장

추가(2026-10-05). 패키지에 든 `default.abproj` 말고, 사용자 컴퓨터의 `.abproj` 파일을 열고 저장한다(`docs/specs/local_files.md`).
다시 빌드하지 않고 바꾼 값을 바로 해 본다.

- **패널:** Battle 패널의 언어 아래 "Project: <파일 이름>"과 버튼 넷: **Open...**(대화 상자로 연다), **Reload**(붙잡아 둔 파일을 다시
  읽는다), **Save**(그 파일에 제자리로 쓴다), **Save as...**(새 파일에 쓴다, 대체 경로는 내려받기). 그 아래 마지막 결과("Opened x",
  "Saved x", 오류는 빨강).
- **불러오기(`game_load_project`):** 읽은 글을 임시 `BattleDefs`로 읽는다. 좋으면 그것이 defs가 되고, 전투는 배치 단계로 돌아가고
  (`battle_init`), 뷰는 전장 평면, 유닛 메시, 카메라를 새로 맞춘다(`view_apply_project`). 메시는 자기 슬롯에서 다시 만든다
  (`nv_renderer_replace_mesh`), 그래서 여러 번 불러도 메시 슬롯이 늘지 않는다. 오류가 있으면 지금 프로젝트를 그대로 두고
  "x has N error(s), so the project in use stays. The first: ..."를 보인다. 좋은 프로젝트가 아직 없을 때(시작 때 실패)만 그 오류가
  패널의 오류 표시가 된다.
- **저장은 읽은 글에 패널의 고침만 더한 것이다.** 프로젝트의 글(`Game.project_text`, 256 KB까지)을 그대로 두었다가 쓰므로 주석과
  순서가 그대로다. 패널의 Rules 절은 이 글에서 바뀐 값만 제자리에서 바꾼다("규칙 고치기").
- **제자리 저장은 이 프로젝트의 파일에만:** 오류 있는 파일을 열면 브라우저가 그 파일을 붙잡으므로, 지금 프로젝트의 글을 거기에 쓰지
  않도록 Save를 끈다(`kept_is_project`). Reload는 된다(그 파일을 고치고 다시 읽는다). 바깥에서 고친 파일에는 쓰지 않는다
  (`local_files.md`의 바깥 수정 보호).
- 디자이너의 작업: 리포를 받아 둔 폴더의 `.abproj`를 Open하고, 텍스트 편집기에서 고치고, Reload해서 해 본다. 다시 빌드할 필요가 없다.
- Debug 빌드는 `Module._battle_debug_load(name, text)`, `_battle_debug_project_action(action)`, `_battle_debug_project_message()`와
  `_battle_debug(14)`(놓인 프로젝트 수), `(15)`(Save가 제자리에 쓸 수 있는지), `(16)`(격자 너비), `(17)`(로컬 파일 상태)를 내보낸다.

### 규칙 고치기

추가(2026-10-05). Battle 패널에서 규칙(`rules` 블록의 여덟 키)을 고친다. 텍스트 편집기 없이 값을 바꿔 바로 해 보고, Save로 파일에 쓴다.

- **패널:** Battle 모드에서 프로젝트 줄 아래 접힌 **Rules** 머리. 펼치면 키마다 위젯 하나: Cell size (m), Grid (across, long)(정수 둘), Zone rows,
  Round time (s), Gravity (m/s²), Retarget interval (s), Stop fraction, Min. damage fraction. 끌거나, 두 번 눌러(또는 Ctrl+클릭)
  값을 친다. 위젯의 범위는 끌기를 돕는 것뿐이고, 판단은 읽기가 한다.
- **글이 원본이다.** 위젯은 프로젝트 글에 적힌 값을 보인다(`defs_value_get`: 시간은 틱이 아니라 파일의 초). 바꾸면 그 키의 줄에서 값만
  새 값으로 바꾼 글을 만들고(`defs_value_set`), 그 글 전체를 `defs_read_project`로 읽는다. 다른 줄과 그 줄의 들여쓰기, 주석은 그대로다.
  값이 길어지거나 짧아지면 주석 앞의 공백이 줄거나 늘어 주석이 제 열에 남는다(적어도 한 칸). 값은 정수거나 소수점 아래 넷째 자리까지
  쓰고 끝의 0은 뺀다("9.8", "60"). 지수 표기는 쓰지 않는다(형식에 없다).
- **좋으면 바로 놓인다(`game_set_value`):** 새 defs와 글이 자리에 들어가고, 플레이어의 배치는 새 규칙에서도 되는 칸에 그대로 남는다
  (구역 밖이나 공급을 넘는 유닛은 빠진다; 줄 순서, 칸 순서로 다시 놓는다). 전장 평면과 유닛 메시는 새로 맞추고, 카메라는 전장 크기가
  바뀔 때만 처음 자리로 간다(`view_apply_edit`). 놓인 프로젝트 수(`project_loads`)는 늘지 않는다.
- **나쁘면 아무것도 바꾸지 않는다:** "Not changed: <첫 오류>"를 빨강으로 보인다. 예: `zone_rows`가 격자 길이의 반을 넘음, 격자를 줄여
  스테이지의 `place`가 적 구역 밖으로 나감. 다음에 받아들여진 고침이 이 알림을 지운다.
- **배치 단계에서만:** 라운드가 시작되면 위젯이 꺼진다("Rules change only in deployment"). 되감기(`battle_seek`)는 라운드를 처음부터 다시
  돌리므로, 규칙이 라운드 중에 바뀌면 그 라운드를 재현할 수 없다. 로컬 파일을 읽거나 쓰는 중에도 꺼진다(저장이 보낸 글과 "저장됨"
  표시가 맞도록).
- **바뀜 표시:** 고친 뒤에는 "Project: x (changed)". Save나 Save as가 끝나면(대체 경로의 내려받기 포함) 지운다. Open과 Reload는 고침을
  버린다(묻지 않는다).
- 실행 취소는 없다(값을 다시 바꾸거나 Reload한다).
- Debug 빌드는 `Module._battle_debug_set_value(block, unit, key, a, b, c)`(아래 "유닛 고치기"), `_battle_debug_project_text()`와
  `_battle_debug(18)`(저장하지 않은 고침이 있는지), `(19)`(라운드 길이, 틱)를 내보낸다.

### 유닛 고치기

추가(2026-10-06). 유닛의 값(유닛, 무기, 실드 능력의 키)을 따로 된 Units 모드에서 고친다. 규칙 고치기와 같은 방식이다: 글이 원본이고, 바뀐
줄의 값만 바꾸고, 글 전체를 읽어 좋으면 바로 놓는다.

- **모드:** 패널 맨 위에 **Battle**과 **Units** 버튼(지금 모드가 밝다, `Game.mode`). Units는 배치 단계에서만 누를 수 있고, Units 모드에서는
  전투를 시작할 수 없다(Start는 Battle 모드에 있다). 언어와 프로젝트 줄(Open..., Reload, Save, Save as..., 알림)은 두 모드에 다 있다.
- **뷰포트(Units 모드):** 전장, 놓인 유닛, 포탄을 감추고 고른 유닛 하나를 원점에 +Z를 보게 세운다(자기 메시, 플레이어 색, 바닥 평면 하나).
  선으로 그리는 것:
  - 바닥에 유닛 반지름(흰색), 무기 사거리(주황), 멈추는 거리(사거리 × 규칙의 `stop_fraction`, 어두운 주황).
  - 총구(`muzzle`) 자리의 작은 십자와, 거기서 사거리 끝의 바닥까지 날아가는 포탄의 호(`launch_angle`과 규칙의 `gravity`; `battle.c`의
    `fire_shell`과 같은 계산).
  - 실드가 있으면 몸 가운데를 지나는 세 원(반지름 `radius`, 하늘색).
  - 카메라는 Battle 모드와 따로다(`Game.unit_orbit`, 모드를 오가도 각자 자리를 지킨다). 유닛을 보도록 시작하고, 1–150 m까지 당기고 민다.
    뷰포트의 탭은 아무것도 하지 않는다.
- **패널(Units 모드):** 유닛 콤보(이름; 고른 유닛은 Battle 모드에서 놓을 유닛이기도 하다), 그리고 절 셋:
  - **Unit:** Cost(정수), Health, Armor, Speed (m/s), Radius (m), Height (m)
  - **Weapon <이름>:** Range (m), Damage, Cooldown (s), Launch angle (°), Spread (m), Muzzle (m)(값 셋)
  - **Ability shield:** Radius (m), Capacity, Regen (per s), Regen delay (s). 실드가 없는 유닛은 "No ability".
- **글 고치기(`defs_value_get`, `defs_value_set`):** 값의 자리는 `DefsKey`다: 블록(`DEFS_RULES`, `DEFS_UNIT`, `DEFS_WEAPON`, `DEFS_ABILITY`),
  유닛 이름(유닛의 블록일 때), 키. 규칙 고치기의 쓰기 규칙(주석 열, 넷째 자리, 지수 없음)이 그대로다. 각도는 파일처럼 도(°)다.
  - **생략된 키:** 파일에 없는 생략 가능한 키(`armor`, `spread`, `muzzle`)를 읽으면 읽기의 기본값(0)이다. 그 키를 고치면 줄을 하나
    끼워 넣는다: 그 블록의 자기 줄(하위 블록의 줄이 아닌) 중 마지막 줄 바로 뒤, 그 블록의 들여쓰기로, 주석 없이. 줄 끝은 그 앞 줄을
    따른다(CRLF면 CRLF). 끼워 넣은 줄은 값을 0으로 되돌려도 남는다(지우지 않는다).
  - 유닛이나 블록이 없으면(이름이 틀림, 실드 없는 유닛의 `DEFS_ABILITY`) 읽기와 쓰기 모두 0이다. 블록을 더하지 않는다("나중에").
- **놓기(`game_set_value`):** 규칙과 같다. 유닛 메시는 고칠 때마다 다시 만든다(`radius`, `height`). 비용이 오르면 배치가 공급을 넘을 수
  있다: 줄 순서, 칸 순서로 다시 놓아 넘는 유닛이 빠진다. 스테이지가 공급을 넘으면(적의 비용 합) 읽기가 거절한다.
- Debug 빌드는 `Module._battle_debug_set_value(block, unit, key, a, b, c)`(블록은 `DefsBlock`의 수, 값은 키의 개수만큼 쓴다),
  `_battle_debug_set_mode(mode)`(0 Battle, 1 Units; 배치 단계가 아니면 Units가 되지 않는다)와 `_battle_debug(20)`(모드)을 내보낸다.

### 테스트

`tests/battle_test.c`:
- **형식 검사는 테스트 안의 텍스트로만** 한다. 저장소 파일의 값을 확인하지 않는다(디자이너가 값을 바꿔도 테스트가 깨지지 않게).
  `full_project`는 `default.abproj`의 한 때의 사본이고, 값 확인과 라운드는 이것으로 한다.
- 저장소의 `default.abproj`는 오류 없이 읽히고, 플레이어 구역을 채운 한 라운드가 `round_ticks` 안에 끝나는지만 본다.
- 유닛 고치기: 유닛, 무기, 실드의 값 읽기(각도는 도), 다른 유닛의 같은 키는 건드리지 않음, 생략된 키 읽기(0)와 끼워 넣기(자리,
  들여쓰기, CRLF, 마지막 줄에 줄바꿈이 없을 때), 끼워 넣은 글을 읽은 값, 실드 없는 유닛과 없는 유닛 이름은 0, 블록에 없는 키는 0.
- 규칙 고치기: 파일의 값 읽기(초 그대로, 규칙이 아닌 키는 0), 모든 규칙을 자기 값으로 다시 쓰면 바이트가 같다, 길고 짧은 값과 주석 열,
  공백 한 칸 남기기, 주석 없는 줄과 CRLF, 정수 반올림, `grid` 두 값, -0, 범위 밖 값도 쓰고 읽기가 거절한다, 규칙이 아닌 키·값 개수·
  자리 모자람은 거절.
- 규칙 키마다 빠짐과 범위(범위 밖, 값 개수, 정수 아님), 구역 겹침, 버전 줄(없음, 첫 문장이 아님, 0, 정수 아님, 더 높음), 최상위 순서(자리가
  아닌 `unit`, `stage`, `rules`, 두 번째 `stage`), 파일 전체의 모자람, 시간의 틱 변환(0.25 → 8, 60 → 1800, 0.01 → 1).
- 규칙을 바꾼 장면: 다른 격자와 칸 크기(칸 중심, 구역 밖, 전장 가장자리), 짧은 `round_time`에서 그 틱에 끝난다, `retarget_interval`,
  `stop_fraction`, `gravity`(포탄이 어느 중력에서도 목표점에 떨어지고 약하면 오래 난다), `min_damage_fraction` 0과 1.
- **황금 해시:** `full_project`로 시드 1–4의 라운드를 돌린 틱 수, 결과, 300틱의 해시, 끝의 해시, 남은 적 수가 규칙을 코드에서 파일로 옮기기
  전의 값과 같다. 규칙의 계산을 바꿔서 라운드가 달라졌다면 이 값이 바뀌는 것이 맞고, 그때 일부러 고친다.

### 단계

1. **끝남:** `BattleRules`와 용량 매크로, 규칙을 읽는 코드로 바꾸기(값 그대로, 해시가 같음).
2. **끝남:** `defs_read_project`와 `default.abproj`, 두 텍스트 파일 지우기, 테스트 정리.
3. **끝남:** `battle.md`의 "정의 파일" 절을 이 스펙으로 옮기고 `AGENTS.md`의 `autobattler/` 설명 갱신.

### 나중에 (이 스펙 밖)

- **에디터:** 유닛의 구조(추가, 복사, 삭제, 이름 바꾸기와 `place` 줄, 실드 넣기와 빼기)와 스테이지(배치 포함)도 고친다. 값처럼 바뀐
  곳만 제자리에서 바꾼다("유닛 고치기"). 새 문장을 더하는 곳은 최상위 순서가 정해져 있어 분명하다. 실행 취소도 그때 정한다.
- **별도 리포:** `default.abproj`를 데이터 리포로 옮기고 엔진 리포가 서브모듈로 고정한다. 버전 줄이 둘의 호환을 확인한다.
- **스테이지 여러 개:** `stage`에 이름을 붙이고(`stage <이름>`) 여럿을 허용한다. 스테이지마다 전장 크기가 달라지면 `grid`를 `stage`로
  옮긴다. 버전을 올린다.
- **B의 키:** 유닛의 `layer`, `size`, `squad`, `footprint`, `altitude`, 여러 `weapon` 블록과 무기의 `projectile`, `targets`,
  `min_range`, `splash`, `speed`, `turn_rate`, `ability jump`, 스테이지의 `prop`이 같은 문법으로 더해진다. 모르는 키는 여전히 오류다.
  더할 때마다 버전을 올린다.
- **이펙트, 유닛 외형:** 지금은 `battle_view.c`의 코드다. 디자이너가 다뤄야 하면 블록으로 더한다.

### 열린 질문

1. ~~**파일 이름**~~: 해결됨(2026-10-05). 확장자는 `.abproj`, 파일은 `default.abproj`. 리포를 나누면 그 리포의 맨 위에 놓인다.
2. ~~**격자 크기의 위치**~~: 해결됨(2026-10-05). `rules`에 둔다. 스테이지마다 전장 크기가 달라지면 그때 `stage`로 옮기고 버전을
   올린다.
3. ~~**최상위 순서**~~: 해결됨(2026-10-05). 고정한다("문법" 절).

## English

Status: built (2026-10-05). Changes to this spec are agreed first.

### Goal

Keep all of the auto-battler's design data in **one file**. It used to be split over two files (`units.txt`, `stage.txt`), and the
rule constants (grid size, round length, gravity and so on) were `#define`s in code. Now all of it is in this file. There is little
data, so it is not split into files.

This file is what a designer's editor will later open and save, and what moves if the data goes to a repository of its own (both
outside this spec; see "Later").

### File

| File | Path in the package | Contents |
|---|---|---|
| `autobattler/data/default.abproj` | `/data/default.abproj` | Rules, unit definitions, the stage |

- The extension is `.abproj` (auto-battler project). It is text, so it stays in plain Git (no LFS pattern in `.gitattributes`) and
  is reviewed like code.
- `nv_setup_executable`'s `PRELOAD` keeps the folder (`autobattler/data@/data`).
- The format is our own line-based text (decided in `battle.md`, "Third-party candidates"). No new third-party code.

### Syntax

- UTF-8 text, LF or CRLF line ends.
- One statement per line: a key and its values, separated by spaces. Blank lines are ignored.
- `#` starts a comment that runs to the end of the line.
- **Indentation sets nesting.** Lines indented deeper after a statement that opens a block (`rules`, `unit`, `weapon`, `ability`,
  `stage`) belong to that block. Lines of one block have the same indentation. Indentation is spaces only (a tab is an error).
- Kinds of values:
  - **Number:** decimal, with an optional sign and point (`120`, `0.5`, `-3`). No exponents. A fraction where an integer goes is
    an error.
  - **Name:** `[A-Za-z_][A-Za-z0-9_]*`, up to 31 bytes (`Crawler`).
  - **Word:** one of a list fixed per key (`shield`).
- Angles are degrees in the file and become radians when read. Distances are meters, times seconds (they become ticks when read; see
  "Rules").
- These are errors: an unknown key, a block's key outside its block, a key twice in one block, a missing required key, a line with
  the wrong number of values, a value out of range, a tab, two units with the same name, a top-level statement out of its place.
- **Top-level statements come in a fixed order.** Top-level statements are the unindented ones (`abproj_version`, `rules`,
  `unit`, `stage`), and they come in this order: `abproj_version` → `rules` → one or more `unit` → `stage`. A fixed order lets one
  pass read the file (the rules are there when units are read, the units when the stage is) and gives the files an editor writes one
  shape. A statement out of its place is one error and its block is skipped (a swap of two blocks is one error, and not the errors of
  everything that depends on them).

### Structure

| Top-level statement | Count | Contents |
|---|---|---|
| `abproj_version <integer>` | Exactly one, the first statement | The format version ("Versions"). Now `1` |
| `rules` | Block, exactly one | Rule constants |
| `unit <name>` | Block, one or more, at most 16 | A unit definition |
| `stage` | Block, exactly one | Supply, seed, the enemy deployment |

#### `rules`

Every key is required. No defaults live in code, so the rules' values are only in this file.

| Key | Value | Range | Value (the constant it was in code) |
|---|---|---|---|
| `cell_size` | Number, m | Above 0, at most 10 | 2 (`BATTLE_CELL_SIZE`) |
| `grid` | Two integers: width (cells), length (rows) | Width 1–64, length 2–128 | 32 48 (`BATTLE_GRID_WIDTH`, `_LENGTH`) |
| `zone_rows` | Integer, rows | 1–32, at most half the length | 14 (`BATTLE_ZONE_ROWS`) |
| `round_time` | Number, s | Above 0, at most 600 | 60 (`BATTLE_MAX_TICKS` / 30) |
| `gravity` | Number, m/s² | Above 0, at most 100 | 9.8 (`BATTLE_GRAVITY`) |
| `retarget_interval` | Number, s | Above 0, at most 10 | 0.25 (`BATTLE_RETARGET_TICKS` 8) |
| `stop_fraction` | Number | Above 0, at most 1 | 0.9 (`BATTLE_STOP_FRACTION`) |
| `min_damage_fraction` | Number | 0–1 | 0.25 (`BATTLE_MIN_DAMAGE_FRACTION`) |

- The player's zone is rows `0` to `zone_rows - 1`, the enemy's `length - zone_rows` to `length - 1`. The two never overlap.
- Times become ticks when read, by the same rule as cooldowns (seconds × 30, rounded up, `battle_seconds_to_ticks`). `round_time 60`
  is 1800 ticks and `retarget_interval 0.25` is 8 ticks (7.5 rounded up). It is always at least 1 tick (0 ticks is raised to 1).
- Each rule key means one thing. `stop_fraction` is "stop once the target is within this share of the range";
  `min_damage_fraction` is "armor always leaves at least this share of a hit" (`max(damage - armor, damage × min_damage_fraction)`).

**What stays in code, and why:**
- **The tick rate (30 Hz, `BATTLE_TICK_RATE`):** every time is turned into ticks by it, and the view's frame loop and speed controls
  sit on it. Changing it changes what the same file plays out to (determinism), so it is the engine's decision, not data.
- **Capacities:** the values that size arrays (`BATTLE_MAX_UNIT_DEFS`, `BATTLE_MAX_PROJECTILES`, `BATTLE_MAX_EVENTS`, the name
  length) stay macros. Since the grid's size is data, its upper limits are macros too: `BATTLE_MAX_GRID_WIDTH 64`,
  `BATTLE_MAX_GRID_LENGTH 128`, `BATTLE_MAX_ZONE_ROWS 32`, `BATTLE_MAX_PLACES (BATTLE_MAX_ZONE_ROWS * BATTLE_MAX_GRID_WIDTH)`. The
  ranges in the table above come from them.

#### `unit <name>`

| Key | Value | Required | Range, default |
|---|---|---|---|
| `unit <name>` | Block | One or more, at most 16 | The name is unique in the file |
| ├ `cost` | Integer | Yes | 1–10000 |
| ├ `health` | Number | Yes | > 0 |
| ├ `armor` | Number | No | ≥ 0, default 0 |
| ├ `speed` | Number, m/s | Yes | ≥ 0 |
| ├ `radius` | Number, m | Yes | > 0 |
| ├ `height` | Number, m | Yes | > 0 |
| ├ `weapon <name>` | Block | Yes, exactly one | |
| │ ├ `range` | Number, m | Yes | > 0 |
| │ ├ `damage` | Number | Yes | ≥ 0 |
| │ ├ `cooldown` | Number, s | Yes | > 0 |
| │ ├ `launch_angle` | Number, degrees | Yes | Above 0 and below 90 |
| │ ├ `spread` | Number, m | No | ≥ 0, default 0 |
| │ └ `muzzle` | Three numbers (x y z), m, unit space | No | Default 0 0 0 |
| └ `ability <kind>` | Block | No, at most one | Kinds: `shield` |
| &nbsp;&nbsp; ├ `radius` | Number, m | Yes | > 0 |
| &nbsp;&nbsp; ├ `capacity` | Number | Yes | > 0 |
| &nbsp;&nbsp; ├ `regen` | Number, per second | Yes | ≥ 0 |
| &nbsp;&nbsp; └ `regen_delay` | Number, s | Yes | ≥ 0 |

#### `stage`

| Key | Value | Required | Range |
|---|---|---|---|
| `supply <integer>` | Each side's supply | Yes, once | 1–100000 |
| `seed <integer>` | The random seed (spread); the same deployment gives the same result | No, once | 0–4294967295, default 1 |
| `place <unit name> <cell x> <cell row>` | One enemy unit | One or more | The name is one of the `unit`s above. x 0 to (width − 1), the row in the enemy zone. One per cell. Total cost ≤ `supply` |

A `place` row is the row on the whole field (34–47 with `grid 32 48` and `zone_rows 14`). Its range is computed from `rules`. When
`rules` has an error, the grid the cell would be judged against is not known, so `place` has no cell checks (so a wrong grid does not
make right lines errors). Its unit name and numbers are still checked.

### Example

`autobattler/data/default.abproj`. The rules are the values that were in code, so what it reads equals what was read before (the
same deployment and seed give the same hash).

```
# autobattler/data/default.abproj: the auto-battler's rules, units and stage (docs/specs/abproj.md)
abproj_version 1

rules
    cell_size 2              # m
    grid 32 48               # cells across, rows long
    zone_rows 14             # each side's deployment rows
    round_time 60            # s
    gravity 9.8              # m/s²
    retarget_interval 0.25   # s
    stop_fraction 0.9        # of the weapon's range
    min_damage_fraction 0.25 # armor never takes more than 75% of a hit

unit Crawler
    cost 100
    health 120
    armor 5
    speed 5            # m/s
    radius 0.5
    height 0.8
    weapon Lobber
        range 20
        damage 30
        cooldown 1.5
        launch_angle 45    # degrees
        spread 0.5
        muzzle 0 0.6 0.3
    ability shield
        radius 1.2
        capacity 60
        regen 10           # per second
        regen_delay 3

stage
    supply 1000
    seed 1
    place Crawler 12 36
    place Crawler 14 36
    place Crawler 16 36
    place Crawler 18 36
    place Crawler 13 38
    place Crawler 15 38
    place Crawler 17 38
    place Crawler 14 40
    place Crawler 16 40
    place Crawler 15 42
```

### Versions

- The first statement, `abproj_version <n>`, is the format version. A file without it, or with it anywhere but first, is an error
  (`default.abproj:1: the file must start with 'abproj_version <version>'`; an empty file, without a line). If the first statement
  is something else, only that is reported and the read goes on as if there were a version.
- Every change to the format (a key added or removed, made required, or given a new meaning or unit) raises `ABPROJ_VERSION`
  (`battle.h`) and adds a row to this section's table. A new key is not added within a version.
- The reader reads versions `1` to `ABPROJ_VERSION`. An older version is turned into today's format as it is read (the table's
  "Reading older files"). A newer version is one error, `default.abproj:1: format 3 is newer than this build reads (2)`, and the rest is
  not read (it is not known what the new format means).
- `abproj_version` is an integer from 1 to `ABPROJ_VERSION`.

| Version | What changed | Reading older files |
|---|---|---|
| 1 | The first format | — |

### Reading and errors

- `autobattler/defs.c` reads it, with one function:

  ```c
  b32 defs_read_project(BattleDefs* defs, const char* file_name, const char* text, umm size);
  ```

  It takes text in memory, so tests try strings. `main.c` reads `/data/default.abproj` with `nv_file_read` into a scratch arena and
  hands it over. `defs` starts zeroed. It uses no GPU or ImGui.
- The result is flat arrays of fixed capacity: `UnitDef units[16]`, the enemy deployment list, and a `BattleRules rules`, with times
  already in ticks and angles in radians:

  ```c
  typedef struct BattleRules {
      f32 cell_size;                 // meters
      u32 grid_width, grid_length;   // cells, rows
      u32 zone_rows;                 // each side's; the enemy's start at grid_length - zone_rows
      u32 round_ticks, retarget_ticks;
      f32 gravity;                   // m/s²
      f32 stop_fraction, min_damage_fraction;
  } BattleRules;
  ```

- What reads the rules reads `defs->rules`: `battle.c` (movement, aiming, shells, the end of the round, a cell's center
  `battle_cell_center(rules, x, row)`, the enemy zone's first row `battle_enemy_first_row(rules)`), `battle_view.c` (the field's
  planes, the zone grids, picking a cell, the camera target's limits, the panel's time) and `field_width`, `field_length` in `game.h`.
  `Battle.placed` is `[BATTLE_MAX_ZONE_ROWS][BATTLE_MAX_GRID_WIDTH]`, used up to the rules' size. The camera's starting distance is in
  proportion to the field's length (104 m for a 96 m length), and its largest distance and far plane grow with it.
- Numbers are read with `strtod`. It rounds correctly, so the same file always gives the same values (determinism).
- Each error is reported with the file name and line number through `nv_log(NV_LOG_ERROR, "battle", "default.abproj:12: unknown key
  'healt'")`. Reading does not stop at the first error: it goes to the end of the file and reports them all (up to 20). Log text is
  English (`AGENTS.md`). With any error, reading fails. The Battle panel shows the first error and "Definitions could not be loaded",
  and Start is disabled. A data error is a failure that happens at run time, so it does not assert (the coding standard).
- What the whole file lacks (`missing 'rules'`, `no units`, `missing 'stage'`) is reported only when there is no other error, and only
  the first missing part, in order. What a block lacks is reported at the block's line (`block 'rules' is missing 'gravity'`,
  `unit 'A' is missing 'health'`, `'stage' has no 'place' lines`). A `zone_rows` over half the length is reported at the `zone_rows`
  line: `'zone_rows' must be at most 24`.
- When the file could not be read or has an error, `main.c` puts fixed rules (`fallback_rules`) into `defs.rules` so that the view can
  draw an empty field and the error. The battle does not run on them (`battle_init` is not called when `defs_ok` is false).

### Loading and saving

Added (2026-10-05). Besides the `default.abproj` in the package, a `.abproj` file on the user's computer can be opened and saved
(`docs/specs/local_files.md`), so a changed value can be tried at once, without a rebuild.

- **The panel:** under the language in the Battle panel, "Project: <file name>" and four buttons: **Open...** (a dialog),
  **Reload** (reads the kept file again), **Save** (writes it in place), **Save as...** (writes a new file; in the fallback, a
  download). Under them, the last result ("Opened x", "Saved x", errors in red).
- **Loading (`game_load_project`):** the text is read into a scratch `BattleDefs`. A good one becomes the defs, the battle goes back
  to deployment (`battle_init`), and the view fits the field's planes, the unit meshes and the camera to it (`view_apply_project`).
  The meshes are remade in their own slots (`nv_renderer_replace_mesh`), so loading again and again uses up no mesh slots. With an
  error, the project in use stays and the panel says "x has N error(s), so the project in use stays. The first: ...". Only while no
  good project is in place yet (a failure at start) do its errors become the panel's error display.
- **Saving writes the text as it was read, with only the panel's edits.** The project's text (`Game.project_text`, up to 256 KB) is
  kept and written, so comments and order stay. The panel's Rules section changes only the edited values in it, in place ("Editing
  the rules").
- **In place only to this project's file:** after a file with errors is opened, the browser keeps that file, so Save is disabled
  rather than writing the project in use into it (`kept_is_project`). Reload works (fix that file and read it again). A file changed
  outside is not written (`local_files.md`, the outside-edit guard).
- A designer's work: Open the `.abproj` in the cloned repository, edit it in a text editor, and Reload to try it. No rebuild.
- Debug builds export `Module._battle_debug_load(name, text)`, `_battle_debug_project_action(action)`, `_battle_debug_project_message()`
  and `_battle_debug(14)` (projects put in place), `(15)` (whether Save can write in place), `(16)` (the grid's width), `(17)` (the
  local file's status).

### Editing the rules

Added (2026-10-05). The rules (the eight keys of the `rules` block) are edited in the Battle panel: a value is changed and tried at
once, without a text editor, and Save writes it to the file.

- **The panel:** in Battle mode, a closed **Rules** header under the project's row. Opened, it has a widget per key: Cell size (m), Grid (across,
  long) (two integers), Zone rows, Round time (s), Gravity (m/s²), Retarget interval (s), Stop fraction, Min. damage fraction. Drag,
  or double-click (or Ctrl+click) and type. The widgets' ranges only guide a drag; the reader judges.
- **The text is the source.** A widget shows the value the project's text gives (`defs_value_get`: times in the file's seconds, not
  ticks). A change makes the text with only the values on that key's line replaced (`defs_value_set`) and reads all of it with
  `defs_read_project`. Every other line, and the line's indentation and comment, stay. When a value gets longer or shorter, the
  spaces before the comment shrink or grow, so the comment keeps its column (one space at the least). A value is written as an
  integer or with at most four decimals, without trailing zeros ("9.8", "60"); never with an exponent, which the format does not have.
- **A good change is put in place at once (`game_set_value`):** the new defs and text take over, and the player's deployment stays on
  the cells the new rules still allow (units outside the zone or over the supply drop out; they are placed again row by row, cell
  by cell). The field's planes and the unit meshes are fitted again, and the camera starts over only when the field's size changed
  (`view_apply_edit`). It does not count as a project put in place (`project_loads`).
- **A bad change changes nothing:** the panel says "Not changed: <the first error>" in red; for example, `zone_rows` over half the
  grid's length, or a smaller grid that leaves a stage `place` outside the enemy zone. The next change made clears that message.
- **Only in deployment:** once a round starts the widgets are disabled ("Rules change only in deployment"). Stepping back
  (`battle_seek`) plays the round again from its start, so a rule changed during a round would make that round impossible to
  replay. They are also disabled while a local file is read or written (so the text a save sends matches what it marks as saved).
- **The changed mark:** after an edit, "Project: x (changed)". A finished Save or Save as (the fallback's download too) clears it.
  Open and Reload drop the edits (without asking).
- There is no undo (change the value back, or Reload).
- Debug builds export `Module._battle_debug_set_value(block, unit, key, a, b, c)` ("Editing units" below),
  `_battle_debug_project_text()` and `_battle_debug(18)` (whether there are edits not yet saved) and `(19)` (the round's length in ticks).

### Editing units

Added (2026-10-06). A unit's values (the keys of the unit, its weapon and its shield ability) are edited in a Units mode of their own,
the same way as the rules: the text is the source, only the changed line's values change, and the whole text is read and, when good,
put in place at once.

- **The mode:** **Battle** and **Units** buttons at the top of the panel (the current mode lit, `Game.mode`). Units can be pressed only
  in deployment, and no round starts in Units mode (Start is in Battle mode). The language and the project row (Open..., Reload, Save,
  Save as..., the message) are in both modes.
- **The viewport (Units mode):** the field, the placed units and the shells are hidden, and the chosen unit stands alone at the origin
  facing +Z (its own mesh, the player's color, one ground plane). Lines show:
  - On the ground, the unit's radius (white), its weapon's range (orange) and where it stops (the range times the rules'
    `stop_fraction`, dark orange).
  - A small cross at the muzzle (`muzzle`), and the arc of a shell from there to the ground at the end of the range (`launch_angle`
    and the rules' `gravity`; the arithmetic of `fire_shell` in `battle.c`).
  - With a shield, three circles through the middle of the body (its `radius`, sky blue).
  - The camera is apart from Battle mode's (`Game.unit_orbit`; each keeps its place across mode switches). It starts on the unit and
    zooms from 1 to 150 m. A tap in the viewport does nothing.
- **The panel (Units mode):** a unit combo (by name; the unit chosen is also the one Battle mode places), then three sections:
  - **Unit:** Cost (integer), Health, Armor, Speed (m/s), Radius (m), Height (m)
  - **Weapon <name>:** Range (m), Damage, Cooldown (s), Launch angle (°), Spread (m), Muzzle (m) (three values)
  - **Ability shield:** Radius (m), Capacity, Regen (per s), Regen delay (s). A unit without a shield shows "No ability".
- **Editing the text (`defs_value_get`, `defs_value_set`):** a value's place is a `DefsKey`: the block (`DEFS_RULES`, `DEFS_UNIT`,
  `DEFS_WEAPON`, `DEFS_ABILITY`), the unit's name (for a unit's blocks) and the key. The rules' writing rules (the comment's column,
  four decimals, no exponent) hold. Angles are in degrees, as in the file.
  - **Omitted keys:** reading an optional key the file does not have (`armor`, `spread`, `muzzle`) gives the reader's default (0).
    Changing one inserts a line: right after the block's last own line (not a line of a block inside it), at the block's indentation,
    without a comment. Its line end follows the line before it (CRLF after CRLF). An inserted line stays when its value goes back to 0
    (nothing is deleted).
  - Without the unit or the block (a wrong name; `DEFS_ABILITY` of a unit without a shield), reading and writing give 0. No block is
    added ("Later").
- **Putting it in place (`game_set_value`):** as for the rules. The unit meshes are remade on every change (`radius`, `height`). A
  higher cost can take the deployment over the supply: the units are placed again row by row, cell by cell, and those over it drop
  out. A stage over its supply (the enemies' cost) is refused by the reader.
- Debug builds export `Module._battle_debug_set_value(block, unit, key, a, b, c)` (the block is a `DefsBlock` number; as many values
  as the key has are used), `_battle_debug_set_mode(mode)` (0 Battle, 1 Units; Units is refused outside deployment) and
  `_battle_debug(20)` (the mode).

### Tests

`tests/battle_test.c`:
- **The format is tested only with text inside the test.** No test checks the repository file's values (so a designer changing a
  value breaks no test). `full_project` is a copy of `default.abproj` from one time, and the value checks and rounds run on it.
- The repository's `default.abproj` is only checked to read without errors and to play a round, with the player's zone filled, that
  ends within `round_ticks`.
- Editing units: reading a unit's, its weapon's and its shield's values (angles in degrees), the same key of another unit left alone,
  an omitted key read (0) and inserted (its place, indentation, CRLF, and a last line without a line end), the values the inserted
  text reads as, 0 for a unit without a shield, a unit name that is not there and a key not in the block.
- Editing the rules: reading the file's values (seconds as they are; 0 for a key that is not a rule), writing every rule's own
  values back gives the same bytes, longer and shorter values and the comment's column, keeping one space, a line without a comment
  and CRLF, integers rounded, `grid`'s two values, -0, a value out of range is written and the reader refuses it, and refusing a key
  that is not a rule, the wrong number of values and too little room.
- Each `rules` key missing and out of range (out of range, value count, not an integer), zones that overlap, the version line (missing,
  not first, 0, not an integer, newer), the top-level order (a `unit`, `stage` or `rules` out of place, a second `stage`), what the
  whole file lacks, and times as ticks (0.25 → 8, 60 → 1800, 0.01 → 1).
- Scenes on changed rules: another grid and cell size (cell centers, outside the zone, the field's edge), a short `round_time` ends at
  that tick, `retarget_interval`, `stop_fraction`, `gravity` (a shell lands on its aim point under any pull, and flies longer under
  less), `min_damage_fraction` 0 and 1.
- **Golden hashes:** rounds of seeds 1 to 4 on `full_project` give the tick count, the outcome, the hash at tick 300, the hash at the
  end and the enemies left that they gave before the rules moved from code to the file. If a change to the rules' arithmetic changes
  a round, these are meant to change, and are changed on purpose.

### Phases

1. **Done:** `BattleRules`, the capacity macros, and code that reads the rules (same values, same hashes).
2. **Done:** `defs_read_project` and `default.abproj`; the two text files deleted; the tests cleaned up.
3. **Done:** `battle.md`'s "Definition files" section moved into this spec, and the `autobattler/` lines in `AGENTS.md` updated.

### Later (outside this spec)

- **Editor:** a unit's structure (adding, copying, deleting, renaming with its `place` lines, adding and removing the shield) and the
  stage (its places too) are edited as well, changing only what changed, in place, as values are ("Editing units"). The fixed
  top-level order makes it clear where new statements go. Undo is decided then too.
- **A repository of its own:** `default.abproj` moves to a data repository, pinned by the engine repository as a submodule. The
  version line checks that the two fit.
- **Several stages:** `stage` gets a name (`stage <name>`) and there can be several. If stages come to have fields of different
  sizes, `grid` moves to `stage`. Raises the version.
- **B's keys:** a unit's `layer`, `size`, `squad`, `footprint` and `altitude`, several `weapon` blocks with a weapon's `projectile`,
  `targets`, `min_range`, `splash`, `speed` and `turn_rate`, `ability jump`, and the stage's `prop` join with the same syntax. An
  unknown key is still an error. Each raises the version.
- **Effects and unit looks:** code in `battle_view.c` for now. They become blocks if designers need to work on them.

### Open questions

1. ~~**File name**~~: settled (2026-10-05). The extension is `.abproj` and the file `default.abproj`. If the data gets its own
   repository, it sits at that repository's top.
2. ~~**Where the grid size lives**~~: settled (2026-10-05). In `rules`. If stages come to have fields of different sizes, it moves to
   `stage` then, with a version raise.
3. ~~**Top-level order**~~: settled (2026-10-05). Fixed ("Syntax").

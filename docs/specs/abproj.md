# Auto-battler project file spec (`.abproj`)

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 초안 (2026-10-05). 구현 전. 이 스펙의 변경은 먼저 합의한다.

### 목표

오토배틀러의 디자인 데이터 전부를 **파일 하나**에 담는다. 지금 두 파일(`units.txt`, `stage.txt`)에 나뉜 내용을 합치고, 코드에
`#define`으로 있는 규칙 상수(격자 크기, 라운드 시간, 중력 등)를 더한다. 데이터의 가짓수가 적으므로 파일을 나누지 않는다.

이 파일은 나중에 디자이너가 쓰는 에디터가 열고 저장하는 단위이자, 데이터를 별도 리포로 옮길 때 옮기는 단위다(둘 다 이 스펙
밖, "나중에" 절).

### 파일

| 파일 | 패키지 안 경로 | 내용 |
|---|---|---|
| `autobattler/data/default.abproj` | `/data/default.abproj` | 규칙, 유닛 정의, 스테이지 |

- 확장자는 `.abproj`(auto-battler project). 텍스트이므로 일반 Git에 두고(`.gitattributes`에 LFS 패턴을 더하지 않는다), 코드처럼
  리뷰한다.
- `autobattler/data/units.txt`와 `stage.txt`는 없어진다. `nv_setup_executable`의 `PRELOAD`는 폴더(`autobattler/data@/data`)
  그대로다.
- 형식은 직접 만든 줄 단위 텍스트를 그대로 쓴다(`battle.md`의 "서드파티 후보"에서 결정). 새 서드파티는 없다.

### 문법

`battle.md`의 "정의 파일 > 문법"과 같다: UTF-8, 한 줄에 문장 하나, `#` 주석, 스페이스 들여쓰기로 중첩, 수·이름·낱말 값, 각도는
도, 거리는 m, 시간은 초. 바뀌는 점:

- 블록을 여는 문장에 `rules`와 `stage`가 더해진다(값 없음).
- **최상위 문장의 순서가 정해져 있다.** 최상위 문장은 들여쓰지 않은 문장(`abproj`, `rules`, `unit`, `stage`)이고, 이 순서로 온다:
  `abproj` → `rules` → `unit` 하나 이상 → `stage`. 순서가 틀리면 오류다
  (`default.abproj:30: 'rules' must come before the first 'unit'`). 순서가 정해져 있으면 한 번 훑어 읽을 수 있고(유닛을 읽을 때
  규칙이, 스테이지를 읽을 때 유닛이 이미 있다), 에디터가 쓰는 파일도 모양이 하나로 정해진다.

### 구조

| 최상위 문장 | 개수 | 내용 |
|---|---|---|
| `abproj <정수>` | 정확히 하나, 첫 문장 | 형식 버전("버전" 절). 지금은 `1` |
| `rules` | 블록, 정확히 하나 | 규칙 상수 |
| `unit <이름>` | 블록, 하나 이상, 최대 16 | 유닛 정의. `battle.md`의 `units.txt` 표와 같다 |
| `stage` | 블록, 정확히 하나 | 공급, 시드, 적 배치 |

#### `rules`

모든 키가 필수다. 기본값을 코드에 두지 않으므로 규칙의 값은 이 파일에만 있다.

| 키 | 값 | 범위 | 지금 값 (코드의 상수) |
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
- 시간은 읽을 때 틱으로 바꾼다. 쿨다운과 같은 규칙이다(초 × 30, 올림). `round_time 60`은 1800틱, `retarget_interval 0.25`는
  8틱(7.5의 올림)이다. 0보다 크면 언제나 1틱 이상이다(0틱이 나오면 1틱으로 올린다).
- 규칙 키는 하나의 의미만 가진다. `stop_fraction`은 "목표가 사거리의 이 비율 안에 들면 멈춘다", `min_damage_fraction`은 "방어력이
  한 발에서 깎는 피해는 이 비율 이상을 남긴다"(`max(damage - armor, damage × min_damage_fraction)`)다.

**코드에 남는 것과 그 이유:**
- **틱 속도(30 Hz):** 모든 시간을 틱으로 바꾸는 기준이고, 뷰의 프레임 루프와 속도 조절이 그 위에 있다. 바꾸면 같은 파일의 결과가
  달라지므로(결정론) 데이터가 아니라 엔진의 결정이다.
- **용량:** 배열 크기를 정하는 값(`BATTLE_MAX_UNIT_DEFS`, `BATTLE_MAX_PROJECTILES`, `BATTLE_MAX_EVENTS`, 이름 길이)은
  매크로로 남는다. 격자 크기가 데이터가 되므로 그 상한을 매크로로 더한다: `BATTLE_MAX_GRID_WIDTH 64`,
  `BATTLE_MAX_ZONE_ROWS 32`, `BATTLE_MAX_PLACES (BATTLE_MAX_ZONE_ROWS * BATTLE_MAX_GRID_WIDTH)`. 위 표의 범위는 이 상한에서
  나온다.

#### `unit <이름>`

`battle.md`의 "`units.txt` (A)" 표와 키, 값, 범위, 기본값이 모두 같다(`cost`, `health`, `armor`, `speed`, `radius`, `height`,
`weapon` 블록, `ability` 블록). 이 스펙은 그 표를 옮겨 오지 않는다. 구현할 때 표를 `battle.md`에서 이 파일로 옮기고, `battle.md`는
이 파일을 가리킨다.

#### `stage`

| 키 | 값 | 필수 | 범위 |
|---|---|---|---|
| `supply <정수>` | 양쪽의 공급 | 예, 한 번 | 1–100000 |
| `seed <정수>` | 난수 시드 | 아니요, 한 번 | 0–4294967295, 기본 1 |
| `place <유닛 이름> <칸 x> <칸 행>` | 적 유닛 하나 | 하나 이상 | 이름은 위의 `unit` 중 하나. x 0–(너비 − 1), 행은 적 구역. 칸마다 하나. 비용 합 ≤ `supply` |

`place`의 행은 지금처럼 전장 전체의 행 번호다(`grid 32 48`, `zone_rows 14`이면 34–47). 범위는 `rules`에서 계산한다.

### 예

지금의 두 파일과 상수를 합친 `default.abproj`. 읽은 결과는 지금과 같아야 한다(같은 배치는 같은 해시).

```
# autobattler/data/default.abproj: the auto-battler's rules, units and stage (docs/specs/abproj.md)
abproj 1

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

- 첫 문장 `abproj <n>`이 형식 버전이다. 없거나 첫 문장이 아니면 오류다(`default.abproj:1: the file must start with 'abproj <version>'`).
- 형식을 바꿀 때마다(키를 더하거나 빼거나, 필수가 되거나, 의미나 단위가 바뀔 때) 코드의 `ABPROJ_VERSION`을 올리고 이 절의
  표에 행을 더한다.
- 읽는 쪽은 `1`부터 `ABPROJ_VERSION`까지 읽는다. 옛 버전은 읽을 때 지금 형식으로 바꾼다(표의 "옛 파일 읽기"). 더 높은 버전은
  `default.abproj:1: format 3 is newer than this build reads (2)` 오류다.

| 버전 | 바뀐 것 | 옛 파일 읽기 |
|---|---|---|
| 1 | 첫 형식 | — |

### 읽기와 오류

- `autobattler/defs.c`가 읽는다. 두 함수 대신 하나다:

  ```c
  b32 defs_read_project(BattleDefs* defs, const char* file_name, const char* text, umm size);
  ```

  `main.c`는 `/data/default.abproj`를 `fopen`으로 임시 아레나에 읽어 넘긴다. 테스트는 문자열로 시험한다.
- `BattleDefs`에 규칙이 더해진다. 시간은 틱으로, 각도는 라디안으로 바꿔 둔다:

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

- `battle.c`, `battle_view.c`, `game.h`는 규칙 매크로 대신 `defs->rules`를 읽는다(`FIELD_WIDTH`, `FIELD_LENGTH`, 구역 격자 그리기,
  카메라 목표의 제한, 패널의 시간 표시). `Battle.placed`는 `[BATTLE_MAX_ZONE_ROWS][BATTLE_MAX_GRID_WIDTH]`이고, 쓰는 범위는
  규칙의 크기다.
- 오류 보고는 지금과 같다: 파일 이름과 줄 번호(`default.abproj:12: unknown key 'healt'`), 파일 끝까지 읽어 모두(20개까지), 하나라도
  있으면 실패, Battle 패널은 첫 오류를 보이고 Start를 막는다. 더해지는 오류: 버전 줄, 최상위 순서, `rules`나 `stage`가 없거나 둘,
  `rules`의 빠진 키와 범위, 구역이 겹치는 `zone_rows`.
- 규칙 키에 오류가 있으면 그 뒤의 `place` 범위 검사는 하지 않는다(틀린 격자로 맞는 줄을 오류로 알리지 않도록).

### 테스트

`tests/battle_test.c`:
- **형식 검사는 테스트 안의 텍스트로만** 한다. 저장소 파일의 값을 확인하지 않는다(디자이너가 값을 바꿔도 테스트가 깨지지 않게).
  지금 `units.txt`의 값을 확인하는 검사는 같은 내용을 담은 테스트 안 텍스트로 옮긴다.
- 저장소의 `default.abproj`는 오류 없이 읽히고, 한 라운드가 `round_ticks` 안에 끝나는지만 본다.
- 더하는 검사: 버전 줄(없음, 첫 문장이 아님, 더 높음), 최상위 순서, `rules`의 키마다 빠짐과 범위, 구역 겹침, 규칙을 바꾼 텍스트로
  돌린 라운드(작은 격자에서 경계 안에 머문다, 짧은 `round_time`에서 그 틱에 끝난다, `min_damage_fraction 1`이면 방어력이 피해를
  줄이지 않는다), 시간의 틱 변환(0.25 → 8, 60 → 1800).
- 바꾸기 전과 후에 같은 배치, 같은 시드의 해시가 같다(규칙이 매크로에서 데이터로 옮겨도 결과가 그대로).

### 단계

1. `BattleRules`와 용량 매크로, 규칙을 읽는 코드로 바꾸기(값은 그대로). 해시가 같은지 확인.
2. `defs_read_project`와 `default.abproj`, 두 텍스트 파일 지우기, 테스트 정리.
3. `battle.md`의 "정의 파일" 절을 이 스펙으로 옮기고 `AGENTS.md`의 `autobattler/` 설명 갱신.

### 나중에 (이 스펙 밖)

- **에디터:** 이 파일을 열고 저장하는 Edit 모드. 저장할 때 주석과 순서를 지키는 방법(바뀐 값만 제자리에서 바꾸기)은 그 스펙에서
  정한다. 최상위 순서가 정해져 있어 새로 쓰는 위치는 분명하다.
- **별도 리포:** `default.abproj`를 데이터 리포로 옮기고 엔진 리포가 서브모듈로 고정한다. 버전 줄이 둘의 호환을 확인한다.
- **스테이지 여러 개:** `stage`에 이름을 붙이고(`stage <이름>`) 여럿을 허용한다. 버전을 올린다.
- **B의 키:** `battle.md`의 "B에서"에 적힌 키가 같은 문법으로 더해진다. 키마다 버전을 올린다.
- **이펙트, 유닛 외형:** 지금은 `battle_view.c`의 코드다. 디자이너가 다뤄야 하면 블록으로 더한다.

### 열린 질문

1. ~~**파일 이름**~~: 해결됨(2026-10-05). 확장자는 `.abproj`, 파일은 `default.abproj`. 리포를 나누면 그 리포의 맨 위에 놓인다.
2. ~~**격자 크기의 위치**~~: 해결됨(2026-10-05). `rules`에 둔다. 스테이지마다 전장 크기가 달라지면 그때 `stage`로 옮기고 버전을
   올린다.
3. ~~**최상위 순서**~~: 해결됨(2026-10-05). 고정한다("문법" 절).

## English

Status: draft (2026-10-05). Not built. Changes to this spec are agreed first.

### Goal

Keep all of the auto-battler's design data in **one file**. It merges what is now split over two files (`units.txt`, `stage.txt`)
and adds the rule constants that are now `#define`s in code (grid size, round length, gravity and so on). There is little data, so
it is not split into files.

This file is what a designer's editor will later open and save, and what moves if the data goes to a repository of its own (both
outside this spec; see "Later").

### File

| File | Path in the package | Contents |
|---|---|---|
| `autobattler/data/default.abproj` | `/data/default.abproj` | Rules, unit definitions, the stage |

- The extension is `.abproj` (auto-battler project). It is text, so it stays in plain Git (no LFS pattern in `.gitattributes`) and
  is reviewed like code.
- `autobattler/data/units.txt` and `stage.txt` go away. `nv_setup_executable`'s `PRELOAD` keeps the folder
  (`autobattler/data@/data`).
- The format stays our own line-based text (decided in `battle.md`, "Third-party candidates"). No new third-party code.

### Syntax

As in `battle.md`, "Definition files > Syntax": UTF-8, one statement per line, `#` comments, nesting by space indentation, number,
name and word values, angles in degrees, distances in meters, times in seconds. What changes:

- `rules` and `stage` join the statements that open a block (with no value).
- **Top-level statements come in a fixed order.** Top-level statements are the unindented ones (`abproj`, `rules`, `unit`,
  `stage`), and they come in this order: `abproj` → `rules` → one or more `unit` → `stage`. Any other order is an error
  (`default.abproj:30: 'rules' must come before the first 'unit'`). A fixed order lets one pass read the file (the rules are there
  when units are read, the units when the stage is) and gives the files an editor writes one shape.

### Structure

| Top-level statement | Count | Contents |
|---|---|---|
| `abproj <integer>` | Exactly one, the first statement | The format version ("Versions"). Now `1` |
| `rules` | Block, exactly one | Rule constants |
| `unit <name>` | Block, one or more, at most 16 | A unit definition, as in `battle.md`'s `units.txt` table |
| `stage` | Block, exactly one | Supply, seed, the enemy deployment |

#### `rules`

Every key is required. No defaults live in code, so the rules' values are only in this file.

| Key | Value | Range | Value now (the constant in code) |
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
- Times become ticks when read, by the same rule as cooldowns (seconds × 30, rounded up). `round_time 60` is 1800 ticks and
  `retarget_interval 0.25` is 8 ticks (7.5 rounded up). Anything above 0 gives at least 1 tick (0 ticks is raised to 1).
- Each rule key means one thing. `stop_fraction` is "stop once the target is within this share of the range";
  `min_damage_fraction` is "armor always leaves at least this share of a hit" (`max(damage - armor, damage × min_damage_fraction)`).

**What stays in code, and why:**
- **The tick rate (30 Hz):** every time is turned into ticks by it, and the view's frame loop and speed controls sit on it.
  Changing it changes what the same file plays out to (determinism), so it is the engine's decision, not data.
- **Capacities:** the values that size arrays (`BATTLE_MAX_UNIT_DEFS`, `BATTLE_MAX_PROJECTILES`, `BATTLE_MAX_EVENTS`, the name
  length) stay macros. Since the grid's size becomes data, its upper limits become macros: `BATTLE_MAX_GRID_WIDTH 64`,
  `BATTLE_MAX_ZONE_ROWS 32`, `BATTLE_MAX_PLACES (BATTLE_MAX_ZONE_ROWS * BATTLE_MAX_GRID_WIDTH)`. The ranges in the table above come
  from them.

#### `unit <name>`

Keys, values, ranges and defaults are all as in `battle.md`'s "`units.txt` (A)" table (`cost`, `health`, `armor`, `speed`,
`radius`, `height`, the `weapon` block, the `ability` block). This spec does not copy that table. When this is built, the table
moves from `battle.md` to this file and `battle.md` points here.

#### `stage`

| Key | Value | Required | Range |
|---|---|---|---|
| `supply <integer>` | Each side's supply | Yes, once | 1–100000 |
| `seed <integer>` | The random seed | No, once | 0–4294967295, default 1 |
| `place <unit name> <cell x> <cell row>` | One enemy unit | One or more | The name is one of the `unit`s above. x 0 to (width − 1), the row in the enemy zone. One per cell. Total cost ≤ `supply` |

A `place` row is still the row on the whole field (34–47 with `grid 32 48` and `zone_rows 14`). Its range is computed from
`rules`.

### Example

`default.abproj` made from today's two files and constants. What it reads must equal today's (the same deployment gives the same
hash).

```
# autobattler/data/default.abproj: the auto-battler's rules, units and stage (docs/specs/abproj.md)
abproj 1

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

- The first statement, `abproj <n>`, is the format version. A file without it, or with it anywhere but first, is an error
  (`default.abproj:1: the file must start with 'abproj <version>'`).
- Every change to the format (a key added or removed, made required, or given a new meaning or unit) raises `ABPROJ_VERSION` in code
  and adds a row to this section's table.
- The reader reads versions `1` to `ABPROJ_VERSION`. An older version is turned into today's format as it is read (the table's
  "Reading older files"). A newer version is an error: `default.abproj:1: format 3 is newer than this build reads (2)`.

| Version | What changed | Reading older files |
|---|---|---|
| 1 | The first format | — |

### Reading and errors

- `autobattler/defs.c` reads it, with one function instead of two:

  ```c
  b32 defs_read_project(BattleDefs* defs, const char* file_name, const char* text, umm size);
  ```

  `main.c` reads `/data/default.abproj` with `fopen` into a scratch arena and hands it over. Tests try strings.
- `BattleDefs` gains the rules, with times already in ticks and angles in radians:

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

- `battle.c`, `battle_view.c` and `game.h` read `defs->rules` instead of the rule macros (`FIELD_WIDTH`, `FIELD_LENGTH`, drawing
  the zone grids, the camera target's limits, the panel's time). `Battle.placed` is
  `[BATTLE_MAX_ZONE_ROWS][BATTLE_MAX_GRID_WIDTH]`, used up to the rules' size.
- Errors are reported as now: the file name and line (`default.abproj:12: unknown key 'healt'`), all of them to the end of the file (up
  to 20), any one fails the read, and the Battle panel shows the first and disables Start. New errors: the version line, the
  top-level order, a missing or second `rules` or `stage`, a missing `rules` key or one out of range, a `zone_rows` that makes the
  zones overlap.
- When a rule key has an error, the `place` range checks after it are skipped (so a wrong grid does not report right lines as
  wrong).

### Tests

`tests/battle_test.c`:
- **The format is tested only with text inside the test.** No test checks the repository file's values (so a designer changing a
  value breaks no test). Today's checks of `units.txt`'s values move to a text in the test with the same contents.
- The repository's `default.abproj` is only checked to read without errors and to play a round that ends within `round_ticks`.
- New checks: the version line (missing, not first, newer), the top-level order, each `rules` key missing and out of range, zones
  that overlap, rounds played on changed rules (a small grid keeps units inside it, a short `round_time` ends at that tick,
  `min_damage_fraction 1` means armor takes nothing off), and times as ticks (0.25 → 8, 60 → 1800).
- The same deployment and seed give the same hash before and after the change (moving the rules from macros to data changes no
  result).

### Phases

1. `BattleRules`, the capacity macros, and code that reads the rules (same values). Check that the hashes match.
2. `defs_read_project` and `default.abproj`; delete the two text files; clean up the tests.
3. Move `battle.md`'s "Definition files" section into this spec, and update the `autobattler/` lines in `AGENTS.md`.

### Later (outside this spec)

- **Editor:** an Edit mode that opens and saves this file. How a save keeps comments and order (changing only the edited values in
  place) is for that spec. The fixed top-level order makes it clear where new statements go.
- **A repository of its own:** `default.abproj` moves to a data repository, pinned by the engine repository as a submodule. The version
  line checks that the two fit.
- **Several stages:** `stage` gets a name (`stage <name>`) and there can be several. Raises the version.
- **B's keys:** the keys listed in `battle.md`'s "In B" join with the same syntax. Each raises the version.
- **Effects and unit looks:** code in `battle_view.c` for now. They become blocks if designers need to work on them.

### Open questions

1. ~~**File name**~~: settled (2026-10-05). The extension is `.abproj` and the file `default.abproj`. If the data gets its own
   repository, it sits at that repository's top.
2. ~~**Where the grid size lives**~~: settled (2026-10-05). In `rules`. If stages come to have fields of different sizes, it moves to
   `stage` then, with a version raise.
3. ~~**Top-level order**~~: settled (2026-10-05). Fixed ("Syntax").

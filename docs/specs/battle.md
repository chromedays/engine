# Battle prototype spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 초안 (2026-10-02). 구현 전에 "열린 질문"을 합의한다.

### 목표

`docs/autobattler.md`의 1단계(전투 프로토타입): 항목 1(시뮬레이션 코어), 2(공간 질의), 3(군집 이동), 4(데이터 기반
정의)의 최소한. 한 라운드짜리 전투를 지금의 큐브, 디버그 라인, 이펙트로 그린다. 게임이 재미있는지 보기 전에 규칙과 게임
오브젝트 구조를 굳히는 것이 목적이다.

### 범위

| 들어감 | 빠짐 (나중) |
|---|---|
| 칸 단위 배치 그리드, 지형지물 | 높이맵 지형 (지면은 평평한 y = 0) |
| 실제 높이: 공중 유닛의 고도, 탄도 포탄, 점프, 3D 범위 피해 | 여러 라운드, 본부 체력 |
| 유닛 5종, 지상과 공중, 크기 등급 3개 | 업그레이드, 테크, 카드, 전문가 |
| 분대 단위 배치 | AI 상대 (적 배치는 스테이지 데이터에 고정) |
| 유닛당 무기 여러 개, 고유 능력 | 시간 조작 (일시정지, 배속) |
| 투사체 실제 시뮬레이션, 방어력, 범위 피해, 아군 피해 | 대량 렌더링, 애니메이션 (메시 노드 하나씩) |
| 전멸 또는 60초 시간 제한으로 끝나는 한 라운드 | 저장, undo |
| 한국어와 영어 UI (문자열 장치를 엔진으로 옮김) | |
| 에디터와 따로인 실행 파일 `autobattler` | 게임 UI (지금은 ImGui 패널 하나) |

### 규칙

#### 전장

| 항목 | 값 |
|---|---|
| 좌표 | 오른손, Y 위 (`engine/math.h`). 전장 중심이 원점, 플레이어 진영은 +Z, 적 진영은 -Z |
| 칸 | 2 m 정사각형. 전장은 32칸(X) × 48행(Z) = 64 m × 96 m. 행 0이 플레이어 쪽 끝 |
| 배치 구역 | 플레이어: 행 0–13, 적: 행 34–47. 가운데 20행은 배치할 수 없다 |
| 지형지물 | 칸에 맞춘 상자 (칸 범위와 높이). 부서지지 않는다. 지상 이동을 막고, 상자를 지나는 투사체를 멈춘다. 그 칸에는 배치할 수 없다 |
| 지형지물 높이 | 낮은 벽 1.5 m (엄폐: 지상 직사를 막지만 점프로 넘는다), 바위 5 m, 탑 8 m. 공중 고도(10 m)보다 낮다 |
| 지면 | y = 0. 지상 유닛은 점프 중이 아니면 y = 0에 있다. 공중 유닛은 정의의 고도에 있다 |

#### 라운드

1. **배치:** 양쪽 모두 공급 1000. 플레이어는 자기 구역에 분대를 놓고 지운다. 적 배치는 스테이지에 고정돼 있다. Start로
   전투를 시작한다.
2. **전투:** 30 Hz 고정 틱, 최대 1800틱(60초). 입력 없음.
3. **결과:** 한쪽 유닛이 모두 죽으면 다른 쪽이 이긴다(동시에 전멸하면 무승부). 시간이 다 되면 남은 가치(유닛마다
   `분대 비용 / 분대 크기 × 체력 / 최대 체력`의 합)가 큰 쪽이 이기고, 같으면 무승부다. Retry는 같은 배치로 배치 단계에
   돌아가고, Reset은 플레이어 배치를 지운다.

#### 유닛

분대는 배치 단위다: 정의의 footprint만큼 칸을 차지하고, 전투가 시작되면 그 칸 안에 분대 크기만큼 유닛을 격자로 놓는다.
그 뒤 유닛은 각자 움직이고 싸운다(대형 유지 없음). 분대는 살아 있는 유닛 수만 센다.

초기 값은 밸런스 출발점일 뿐이다.

| | Crawler | Ranger | Mortar | Wasp | Fortress |
|---|---|---|---|---|---|
| 층 | 지상 | 지상 | 지상 | 공중 (고도 10 m) | 지상 |
| 크기 등급 | 소형 | 중형 | 중형 | 소형 | 대형 |
| 분대 크기 | 16 | 4 | 2 | 6 | 1 |
| footprint (칸) | 2×2 | 2×2 | 2×2 | 2×2 | 3×3 |
| 비용 | 100 | 150 | 200 | 200 | 400 |
| 체력 | 80 | 400 | 500 | 250 | 6000 |
| 방어력 | 0 | 5 | 10 | 0 | 40 |
| 속도 (m/s) | 7 | 4 | 3 | 8 | 2 |
| 반지름 / 높이 (m) | 0.5 / 0.8 | 0.8 / 2 | 1.2 / 2 | 0.8 / 0.6 | 2.5 / 5 |
| 무기 | Claws | Rifle | Mortar | Rockets | Cannon, Flak |
| 능력 | 없음 | Jump | 없음 | 없음 | Shield |

크기 등급은 분리(밀어내기)의 무게를 정한다: 소형 1, 중형 4, 대형 20. 큰 유닛이 작은 유닛을 밀어낸다.

#### 무기

| 무기 | 투사체 | 대상 | 사거리 (m) | 피해 | 범위 (m) | 쿨다운 (s) | 그 밖 |
|---|---|---|---|---|---|---|---|
| Claws | 근접 (투사체 없음) | 지상 | 1.2 | 25 | – | 0.6 | |
| Rifle | 총탄 | 지상, 공중 | 25 | 40 | – | 1.0 | 속도 90 m/s, 퍼짐 0.3 m |
| Mortar | 포탄 | 지상 | 10–45 | 120 | 4 | 3.0 | 발사각 60°, 퍼짐 1.5 m |
| Rockets | 미사일 | 지상 | 12 | 35 | 1.5 | 0.8 | 속도 30 m/s, 선회 3 rad/s |
| Cannon | 포탄 | 지상 | 35 | 400 | 5 | 4.0 | 발사각 15°, 퍼짐 1 m |
| Flak | 미사일 | 공중 | 30 | 60 | 3 | 1.5 | 속도 50 m/s, 선회 5 rad/s |

#### 능력

- **Jump (Ranger):** 이동 방향 4 m 안에 지형지물이 있거나 적 소형 근접 유닛이 3 m 안에 오면, 이동 방향으로 10 m를
  0.8초에 걸쳐 높이 3 m의 포물선으로 뛴다. 착지점이 지형지물 칸이면 뛰지 않는다. 쿨다운 10초. 뛰는 동안 쏘지 않고 다른
  유닛과 밀어내지 않는다.
- **Shield (Fortress):** 반지름 7 m의 구. 바깥에서 경계를 넘어 들어오는 적 투사체를 없애고 그 피해만큼 에너지를 잃는다
  (용량 1500). 4초 동안 맞지 않으면 초당 100씩 찬다. 근접 공격과 아군 투사체는 통과한다. 에너지가 0이면 꺼진다.

#### 전투 규칙

- **타게팅:** 가장 가까운 적을 먼저 친다.
  - 무기마다 대상(지상, 공중)과 사거리 안의 가장 가까운 적을 고른다.
  - 유닛의 이동 대상은 무기 중 하나라도 칠 수 있는 가장 가까운 적이다(거리 제한 없음).
  - 0.25초(8틱)마다 다시 고르고, 유닛마다 틱을 나눠 분산한다. 대상이 죽으면 바로 다시 고른다.
  - 같은 거리면 슬롯 번호가 작은 유닛.
- **이동:**
  - 지상: 이동 대상이 8 m 안이고 그 사이에 지형지물이 없으면 곧장 향하고, 아니면 흐름장을 따른다. 이동 대상이 무기
    사거리의 90% 안이면 멈춘다. 이웃과 분리하고, 지형지물 칸과 전장 밖으로 나가지 않게 밀어낸다. 몸은 이동 방향으로
    돈다(무기는 360° 포탑).
  - 공중: 지형지물을 무시하고 고도를 지키며 이동 대상으로 곧장 날아간다. 공중 유닛끼리만 분리한다.
- **흐름장:** 팀마다 둘(지상 적만, 모든 적; 공중 적은 지면에 투영). 목표 칸은 적이 있는 칸이다. 8방향 다익스트라, 정수
  비용 10/14, 지형지물 칸은 지날 수 없고 대각선으로 모서리를 자르지 않는다. 10틱마다 다시 만들며, 네 장을 틱에 나눠
  만든다. 지상만 치는 유닛(Crawler, Mortar)은 "지상 적만"을 따른다.
- **발사:** 쿨다운이 끝나고 대상이 사거리 안에 있으면 쏜다. 직사(총탄)는 총구에서 대상 중심까지 지형지물이 없을 때만 쏜다.
  포탄과 미사일은 그 검사가 없다. 리드 사격은 없다: 쏠 때 대상의 위치를 겨눈다.
- **투사체:**
  - 총탄은 직선으로 날아가 사거리만큼 가면 사라진다.
  - 포탄은 중력 9.8 m/s²로 날아가고, 발사각과 목표점(대상 위치 + 시드 난수 퍼짐)에서 초기 속도를 구한다.
  - 미사일은 대상을 향해 선회하고, 대상이 죽으면 마지막 위치로 간다.
  - 매 틱 이전 위치에서 지금 위치까지의 선분을 실드, 지형지물, 지면, 유닛 원기둥에 대해 검사하고 가장 먼저 닿은 것에서
    터진다. 쏜 유닛은 맞지 않는다.
- **피해:**
  - 한 번 맞을 때 `max(피해 - 방어력, 피해 × 0.25)`.
  - 범위 피해는 3D 구(공중 유닛은 지면 폭발에 맞지 않는다). 중심 100%에서 가장자리 25%까지 선형으로 줄고, 방어력은
    유닛마다 적용한다.
  - **아군 피해:** 총탄은 길 위의 첫 유닛을 팀과 상관없이 맞히고, 범위 피해는 범위 안의 모든 유닛에 들어간다.
  - 피해는 틱 동안 더하기만 하고, 죽음은 틱 끝에 한꺼번에 정한다. 그래서 같은 틱 안의 피해 순서가 결과를 바꾸지 않는다.

### 게임 오브젝트 구조

결정:

- **시뮬레이션은 씬 그래프 밖에 있다.** 유닛과 투사체는 `NvNode`가 아니다. `Battle` 구조체의 평평한 배열이며, 렌더링,
  에디터, 프레임 시간을 읽지 않는다. 화면은 매 프레임 그것을 읽어 노드와 이펙트로 옮긴다.
- **정의와 인스턴스를 나눈다.** `UnitDef`, `WeaponDef`는 바뀌지 않는 데이터, `Unit`, `Projectile`은 실행 상태다.
- **수명별 풀.** 분대와 유닛은 전투 시작에 만들어 끝까지 그 자리에 있다. 투사체는 짧게 살고 자주 생긴다.
- **구분된 공용체(discriminated union)는 종류가 서로 배타적이고 모양이 다른 곳에만.** 능력 상태(Jump, Shield)와
  미사일 데이터. 능력의 태그는 정의(`UnitDef.ability.kind`)에 있으므로 인스턴스에 다시 쓰지 않는다. 유닛 종류 사이의
  차이는 대부분 데이터라서 유닛 자체는 union이 아니다.
- **id.** 유닛은 전투 중에 생기지 않고 슬롯이 재사용되지 않으므로 `UnitId`는 슬롯 번호다(0 = 없음). 죽은 유닛은
  배열에 남고 `UNIT_DEAD`로 표시된다. 전투 중 생성(소환)을 더할 때 세대를 더한다.
- **투사체**는 아무도 가리키지 않으므로 사라질 때 마지막 것과 바꿔 지운다(swap-remove). 순서가 바뀌지만 결정적이다.
- **이벤트.** 시뮬레이션은 화면이 쓸 이벤트(발사, 명중, 폭발, 실드 피격, 죽음, 점프)를 틱마다 목록에 쓴다. 화면은 그것으로
  `nv_vfx_*`를 부른다. 시뮬레이션은 이벤트를 읽지 않는다. 목록이 가득 차면 이벤트만 버리고 센다.

```c
#define BATTLE_TICK_RATE        30
#define BATTLE_MAX_TICKS        (60 * BATTLE_TICK_RATE)
#define BATTLE_CELL_SIZE        2.0f
#define BATTLE_GRID_WIDTH       32
#define BATTLE_GRID_LENGTH      48
#define BATTLE_MAX_PROPS        256
#define BATTLE_MAX_SQUADS       128
#define BATTLE_MAX_UNITS        2048
#define BATTLE_MAX_PROJECTILES  8192
#define BATTLE_MAX_EVENTS       4096 // per tick
#define UNIT_MAX_WEAPONS        2

typedef enum UnitLayer { UNIT_LAYER_GROUND, UNIT_LAYER_AIR } UnitLayer;
typedef enum UnitSize { UNIT_SIZE_SMALL, UNIT_SIZE_MEDIUM, UNIT_SIZE_LARGE } UnitSize;
enum { TARGET_GROUND = 1 << 0, TARGET_AIR = 1 << 1 };

typedef enum ProjectileKind {
    PROJECTILE_NONE, // melee: the hit lands when the weapon fires
    PROJECTILE_BULLET,
    PROJECTILE_SHELL,
    PROJECTILE_MISSILE,
} ProjectileKind;

typedef struct WeaponDef {
    const char* name;
    ProjectileKind projectile;
    u32 targets; // TARGET_*
    f32 min_range, range;
    f32 damage, splash_radius;
    f32 cooldown;
    f32 speed;        // bullet, missile
    f32 turn_rate;    // missile, radians per second
    f32 launch_angle; // shell, radians
    f32 spread;       // meters around the aim point
    NvVec3 muzzle;    // in the unit's space
} WeaponDef;

typedef enum AbilityKind { ABILITY_NONE, ABILITY_JUMP, ABILITY_SHIELD } AbilityKind;

typedef struct AbilityDef {
    AbilityKind kind;
    union {
        struct { f32 distance, height, duration, cooldown, prop_range, melee_range; } jump;
        struct { f32 radius, capacity, regen, regen_delay; } shield;
    };
} AbilityDef;

typedef struct UnitDef {
    const char* name;
    UnitLayer layer;
    UnitSize size;
    u32 squad_size;
    u32 footprint; // cells per side
    u32 cost;
    f32 health, armor, speed, turn_rate;
    f32 radius, height, altitude;
    u32 weapon_count;
    WeaponDef weapons[UNIT_MAX_WEAPONS];
    AbilityDef ability;
} UnitDef;

typedef struct UnitId { u32 index; } UnitId; // 0 = none

enum { UNIT_DEAD = 1 << 0, UNIT_JUMPING = 1 << 1 };

typedef struct Unit {
    u32 flags;
    u8 team;
    u8 def;
    u16 squad;
    NvVec3 position, previous_position; // previous: for drawing between ticks
    NvVec3 velocity;
    f32 yaw, previous_yaw;
    f32 health;
    f32 damage_taken; // this tick; applied at the tick's end
    UnitId move_target;
    struct { UnitId target; f32 cooldown; } weapons[UNIT_MAX_WEAPONS];
    union { // tagged by UnitDef.ability.kind
        struct { f32 cooldown, time; NvVec3 from, to; } jump;
        struct { f32 energy, since_hit; } shield;
    } ability;
} Unit;

typedef struct Projectile {
    ProjectileKind kind;
    u8 team;
    u8 weapon; // index into the shooter's UnitDef.weapons
    UnitId shooter;
    NvVec3 position, previous_position, velocity;
    f32 distance_left;
    union { // tagged by kind
        struct { UnitId target; NvVec3 last_target_position; } missile;
    };
} Projectile;

typedef struct Squad {
    u8 team, def;
    u8 cell_x, cell_row; // the footprint's corner nearest the origin of the grid
    u32 first_unit, unit_count, alive_count;
} Squad;

typedef struct Prop {
    u8 cell_x, cell_row, width, length; // in cells
    f32 height;
} Prop;

typedef enum BattleEventKind {
    BATTLE_EVENT_FIRE, BATTLE_EVENT_HIT, BATTLE_EVENT_EXPLODE, BATTLE_EVENT_SHIELD_HIT, BATTLE_EVENT_DEATH, BATTLE_EVENT_JUMP,
} BattleEventKind;

typedef struct BattleEvent { // for the view only
    BattleEventKind kind;
    u8 def, weapon;
    NvVec3 position, direction;
    f32 size;
} BattleEvent;

typedef enum BattlePhase { BATTLE_DEPLOY, BATTLE_FIGHT, BATTLE_RESULT } BattlePhase;
typedef enum BattleOutcome { OUTCOME_NONE, OUTCOME_PLAYER, OUTCOME_ENEMY, OUTCOME_DRAW } BattleOutcome;

typedef struct Battle {
    BattlePhase phase;
    BattleOutcome outcome;
    u32 tick;
    u64 rng; // seeded at Start
    Prop props[BATTLE_MAX_PROPS];
    u32 prop_count;
    u8 blocked[BATTLE_GRID_LENGTH][BATTLE_GRID_WIDTH]; // cells with a prop
    Squad squads[BATTLE_MAX_SQUADS];
    u32 squad_count;
    Unit units[BATTLE_MAX_UNITS]; // [0] is unused
    u32 unit_count;
    Projectile projectiles[BATTLE_MAX_PROJECTILES];
    u32 projectile_count;
    NvFlowField fields[2][2]; // [team][0 ground enemies, 1 all enemies]
    NvSpatialGrid grid;       // rebuilt every tick
    BattleEvent events[BATTLE_MAX_EVENTS];
    u32 event_count, events_dropped;
} Battle;
```

### 틱 순서

1. 모든 유닛의 `previous_position`, `previous_yaw`를 기록하고 공간 격자를 다시 만든다(유닛 슬롯 순서로 넣는다).
2. 이번 틱 차례인 흐름장을 다시 만든다.
3. 이번 틱 차례인 유닛의 대상을 다시 고른다.
4. 이동과 능력(점프 시작과 진행, 분리, 밀어내기, 적분).
5. 무기: 쿨다운, 발사(투사체 생성, 근접 피해).
6. 투사체: 적분, 충돌, 피해와 범위 피해, 실드.
7. 죽음: `damage_taken`을 적용하고 체력이 0 이하인 유닛을 `UNIT_DEAD`로 표시한다.
8. 실드 회복, 끝 조건 검사.

### 결정론

고정 30 Hz 틱, 시드 난수(PCG32, 직접 작성), 모든 반복은 슬롯 순서, 시뮬레이션은 프레임 dt를 읽지 않는다. WebAssembly의
float는 같은 빌드 안에서 결정적이고 `sinf` 같은 libm 함수도 모듈 안에 컴파일되므로, 고정소수점은 필요 없다. 끝난
전투의 상태 해시(FNV-1a)로 비교한다.

### 코드 위치

에디터 앱(`app/`)과 따로, 자기 실행 파일을 가진 `autobattler/` 폴더에 둔다. 엔진(`nv`)만 링크하고 `app/`의 코드는 쓰지 않는다.

| 파일 | 내용 |
|---|---|
| `autobattler/CMakeLists.txt` | 실행 파일 `autobattler`. 설치 구성 요소 `autobattler`의 `<prefix>/`에 설치한다("페이지" 참고) |
| `autobattler/main.c` | 창, GPU, 렌더러, ImGui, 이펙트, 카메라, 프레임 루프(고정 틱 누산기) |
| `autobattler/battle.h`, `autobattler/battle.c` | 규칙과 틱. GPU와 ImGui를 포함하지 않으므로 ctest가 빌드할 수 있다 |
| `autobattler/battle_defs.c` | 유닛 정의 표와 스테이지 하나(지형지물, 적 배치) |
| `autobattler/battle_view.c` | 노드와 이펙트로 그리기, 배치 입력, 패널 |
| `autobattler/strings.c` | 게임의 한국어 표("UI 문자열" 참고) |
| 공간 격자, 흐름장 | 열린 질문 2: `engine/`(`engine/spatial.h`, `engine/flow.h`) 또는 `autobattler/` |

### 실행 파일

- **페이지:** 기본 브랜치의 Release는 Pages의 `autobattler/release/`, Debug는 `autobattler/debug/`, 다른 브랜치의 Debug는
  `autobattler/<branch>/`(`/`는 `-`가 된다). 에디터 앱의 폴더(`release/`, `debug/`, `<branch>/`)는 그대로다. 에디터 앱과 따로
  내려받는다.
  - 루트 `CMakeLists.txt`에 `add_subdirectory(autobattler)`.
  - `nv_setup_executable`에 `COMPONENT <name>` 인수를 더한다(기본값 `web`). `autobattler`는
    `nv_setup_executable(autobattler ROOT COMPONENT autobattler)`이므로 앱의 `web` 패키지에 섞이지 않는다.
  - `build.yml`의 Package가 `--component autobattler`를 `dist/engine-web/autobattler/release`와 `.../debug`에 설치하고,
    stage가 기본 브랜치에서는 `autobattler/release`, `autobattler/debug`를, 다른 브랜치에서는 `autobattler/<branch>`를
    바꾼다. `autobattler`라는 이름의 브랜치는 `release`, `debug`처럼 게시하지 않는다(그 폴더를 덮어쓰므로).
- **화면:** 캔버스 전체가 전장이고 ImGui 창 하나(Battle 패널)가 위에 뜬다. 에디터의 도크, 검색, 팔레트, 저장, undo,
  선택, 콘솔 탭은 없다. 씬 해상도는 캔버스와 같게(`NvSceneOutput`의 `pixel_size` 1) 시작한다.
- **카메라:** 플레이어 진영 뒤 위에서 비스듬히 보는 시점. `NvImgui.view`의 드래그로 팬, 휠과 핀치로 줌, 전장 밖으로 나가지
  않는다.
- **그리기:**
  - 유닛은 팀 색 큐브 메시 노드로, 크기는 반지름과 높이. 틱 사이를 보간한다.
  - 지형지물은 회색 상자, 배치 격자와 구역은 디버그 라인, 체력 바는 유닛 위의 디버그 라인이다.
  - 투사체는 `nv_vfx_trail`, 폭발은 `nv_vfx_burst`와 `nv_vfx_decal`, 실드는 디버그 라인 원으로 그린다.
- **배치 입력:** 패널에서 유닛 종류를 고르고 뷰포트의 칸을 탭하면 놓는다. 놓을 수 있는 칸은 초록, 없는 칸은 빨강으로
  미리 보여 준다. 놓인 분대를 탭하면 지운다. 데스크톱과 폰 모두 같다.
- **Battle 패널:** 단계, 남은 공급, 유닛 종류 버튼(이름과 비용), Start, Retry, Reset, 남은 시간, 팀별 살아 있는 유닛, 결과.
- **디버그 내보내기:** `_battle_debug(n)`(단계, 틱, 결과, 유닛 수, 해시), `_battle_debug_deploy(def, x, row)`,
  `_battle_debug_start()`, `_battle_debug_run(ticks)`.

### UI 문자열 (엔진으로 옮김)

결정(2026-10-02): `T()`, `TL()`과 언어 선택을 엔진(`engine/strings.h`)으로 옮기고, 한국어 표는 실행 파일마다 따로 둔다. 자세한
내용은 `docs/specs/shared.md`의 "UI 문자열"이다. 게임은 `autobattler/strings.c`에 자기 표를 두고, 브라우저 언어로 시작하며,
Battle 패널의 Language 콤보로 바꾼다(저장 없음). 패널의 모든 글자는 `T()`/`TL()`을 거친다.

### 서드파티 후보

| 후보 | 무엇 | 언어, 라이선스 | 판단 |
|---|---|---|---|
| **직접 작성** (추천) | 공간 격자 약 150줄, 흐름장 약 200줄, 규칙 | C17 | 고정 용량, 아레나, 결정적 반복 순서를 그대로 지킨다 |
| flecs | ECS | C, MIT | 콜백과 불투명 저장소가 코딩 표준과 맞지 않는다. 오브젝트 종류가 몇 개뿐이라 이득이 없다 |
| Recast/Detour | navmesh와 군중 | C++, zlib | 전장이 평평한 칸 격자라 navmesh가 필요 없다(`autobattler.md`). 두 번째 C++ 파일이 된다 |
| 정의 데이터: C 표 (추천, 이번 단계) | `battle_defs.c`의 표 | C17 | 바꾸면 다시 빌드해야 한다 |
| 정의 데이터: JSON + cJSON | 텍스트 파일, 파서 | C, MIT | 사람이 읽기 쉽다. 할당자를 우리 아레나로 바꿔야 한다 |
| 정의 데이터: JSON + jsmn | 토크나이저만 | C, MIT | 할당이 없다. 값 해석은 우리 몫 |
| 정의 데이터: `engine/chunk.h` | 우리 바이너리 형식 | C17 | 의존성이 없지만 편집기가 필요하다 |

### 테스트

`tests/battle_test.c`(ctest, Node)가 `autobattler/battle.c`, `autobattler/battle_defs.c`와 공간 격자, 흐름장 소스를 빌드한다:

- 같은 배치와 시드로 두 번 돌리면 같은 해시.
- 공간 격자: 질의가 무차별 검사와 같은 유닛을 같은 순서로 돌려준다.
- 흐름장: 벽을 돌아가는 경로, 막힌 칸, 목표 없음.
- 규칙 장면 하나씩: 가장 가까운 적을 고른다; 낮은 벽이 총탄을 막는다; 포탄 범위 피해가 아군도 깎는다; 지면 폭발이 공중
  유닛을 맞히지 않는다; Wasp가 지형지물 위를 난다; Ranger가 낮은 벽을 넘어 뛴다; 실드가 적 투사체를 막고 아군 것은
  통과시킨다; 박격포의 최소 사거리; 60초에 끝나고 남은 가치로 판정한다.

`tests/strings_test.mjs`가 `autobattler/`의 문자열도 검사한다.

Playwright(Release, Debug, 데스크톱과 폰 크기): `autobattler/` 페이지를 열고, 배치를 탭으로 놓고 지우고, Start에서 결과까지 가고,
assert와 WebGPU 오류가 없는지 본다.

### 단계

1. **선행:** `docs/specs/shared.md`(문자열, 메시, 해상도와 탭, 카메라, 글꼴, 버전, 도우미를 엔진으로).
2. **시뮬레이션:** `spatial`, `flow`, `battle.c`, `battle_defs.c`, `battle_test.c`. 화면 없음.
3. **실행 파일:** `autobattler/main.c`, 카메라, 그리기, 이펙트, Battle 패널과 그 문자열 표, 배치 입력.
4. **확인과 문서:** 디버그 내보내기, Playwright 검사, `AGENTS.md`(지금은 "실행 파일은 `app` 하나")와 `autobattler.md` 갱신.

### 열린 질문

1. **정의 데이터 형식:** 이번 단계는 C 표로 하고 파일 형식은 밸런스 작업 전에 따로 정할까, 아니면 지금 정할까?
2. **공간 격자와 흐름장의 위치:** `autobattler.md`대로 엔진(`engine/spatial.h`, `engine/flow.h`)인가, 아니면 `autobattler/`에서 먼저
   쓰고 두 번째 쓰임이 생길 때 옮길까?
3. **시간 제한 판정:** 남은 가치 비교가 맞는가?
4. **분대:** 시작 뒤 유닛이 각자 행동하는 것(제안)이 맞는가, 대형을 유지해야 하는가?
5. **대형 유닛의 통로:** 흐름장은 칸 하나 너비를 기준으로 하므로 Fortress가 1칸 틈에 낄 수 있다. 이번 단계는 스테이지에
   1칸 틈을 두지 않고, 크기별 흐름장은 나중으로 미룬다. 괜찮은가?
6. **수치:** 위 표의 값은 출발점이다. 바꾸고 싶은 것이 있는가?
7. ~~**UI 문자열**~~: 해결됨. 엔진으로 옮긴다("UI 문자열" 절).
8. **에셋:** 프로토타입에 필요한 것은 UI 글꼴(Pretendard 한 파일)뿐이다. `assets/` 전체를 함께 내려받을까, 아니면
   `assets/fonts/`만 넣을까(`nv_setup_executable`의 `ASSETS`는 폴더 하나를 받는다)? 추천: `assets/fonts/`만.

## English

Status: draft (2026-10-02). The "Open questions" are agreed before it is built.

### Goal

Step 1 of `docs/autobattler.md` (the battle prototype): the least of items 1 (simulation core), 2 (spatial queries), 3
(crowd movement) and 4 (data-driven definitions). One round of battle, drawn with today's cubes, debug lines and effects.
The point is to settle the rules and the game object structure before finding out whether the game is fun.

### Scope

| In | Out (later) |
|---|---|
| A cell-based deployment grid, props | Heightmap terrain (the ground is flat at y = 0) |
| Real height: air units' altitude, ballistic shells, jumps, 3D area damage | Multiple rounds, base health |
| Five unit types, ground and air, three size classes | Upgrades, tech, cards, specialists |
| Deployment by squad | An AI opponent (the enemy deployment is fixed in the stage data) |
| Several weapons per unit, special abilities | Time controls (pause, speed-up) |
| Simulated projectiles, armor, area damage, friendly fire | Mass rendering and animation (one mesh node each) |
| One round, ended by a wipe-out or a 60-second time limit | Saving, undo |
| A Korean and English UI (the string mechanism moves to the engine) | |
| An executable of its own, `autobattler`, apart from the editor | A game UI (one ImGui panel for now) |

### Rules

#### Battlefield

| Topic | Value |
|---|---|
| Coordinates | Right-handed, Y up (`engine/math.h`). The field is centered on the origin; the player's side is +Z, the enemy's −Z |
| Cells | 2 m squares. The field is 32 cells (X) × 48 rows (Z) = 64 m × 96 m. Row 0 is the player's edge |
| Deployment zones | Player: rows 0–13; enemy: rows 34–47. The 20 rows between take no deployment |
| Props | Boxes aligned to cells (a cell range and a height). Indestructible. They block ground movement and stop projectiles that pass through the box. Their cells take no deployment |
| Prop heights | Low wall 1.5 m (cover: stops direct ground fire, but can be jumped), rock 5 m, tower 8 m. All below the air altitude (10 m) |
| Ground | y = 0. Ground units stay at y = 0 unless jumping; air units fly at their definition's altitude |

#### Round

1. **Deploy:** each side has 1000 supply. The player places and removes squads in their zone; the enemy deployment is fixed
   in the stage. Start begins the battle.
2. **Battle:** a fixed 30 Hz tick, at most 1800 ticks (60 seconds). No input.
3. **Result:** when all of one side's units are dead, the other side wins (both at once is a draw). When time runs out, the
   side with more remaining value (the sum, over units, of `squad cost / squad size × health / max health`) wins; equal
   value is a draw. Retry goes back to deployment with the same deployment; Reset clears the player's deployment.

#### Units

A squad is the unit of deployment: it takes its definition's footprint in cells, and when the battle starts its units are
placed on a grid inside those cells. From then on each unit moves and fights on its own (no formation). The squad only counts
its living units.

The values are a starting point for balancing.

| | Crawler | Ranger | Mortar | Wasp | Fortress |
|---|---|---|---|---|---|
| Layer | Ground | Ground | Ground | Air (altitude 10 m) | Ground |
| Size class | Small | Medium | Medium | Small | Large |
| Squad size | 16 | 4 | 2 | 6 | 1 |
| Footprint (cells) | 2×2 | 2×2 | 2×2 | 2×2 | 3×3 |
| Cost | 100 | 150 | 200 | 200 | 400 |
| Health | 80 | 400 | 500 | 250 | 6000 |
| Armor | 0 | 5 | 10 | 0 | 40 |
| Speed (m/s) | 7 | 4 | 3 | 8 | 2 |
| Radius / height (m) | 0.5 / 0.8 | 0.8 / 2 | 1.2 / 2 | 0.8 / 0.6 | 2.5 / 5 |
| Weapons | Claws | Rifle | Mortar | Rockets | Cannon, Flak |
| Ability | None | Jump | None | None | Shield |

The size class sets the weight in separation (pushing): small 1, medium 4, large 20. Large units push small ones aside.

#### Weapons

| Weapon | Projectile | Targets | Range (m) | Damage | Splash (m) | Cooldown (s) | Other |
|---|---|---|---|---|---|---|---|
| Claws | Melee (no projectile) | Ground | 1.2 | 25 | – | 0.6 | |
| Rifle | Bullet | Ground, air | 25 | 40 | – | 1.0 | Speed 90 m/s, spread 0.3 m |
| Mortar | Shell | Ground | 10–45 | 120 | 4 | 3.0 | Launch angle 60°, spread 1.5 m |
| Rockets | Missile | Ground | 12 | 35 | 1.5 | 0.8 | Speed 30 m/s, turn 3 rad/s |
| Cannon | Shell | Ground | 35 | 400 | 5 | 4.0 | Launch angle 15°, spread 1 m |
| Flak | Missile | Air | 30 | 60 | 3 | 1.5 | Speed 50 m/s, turn 5 rad/s |

#### Abilities

- **Jump (Ranger):** when a prop is within 4 m along its movement, or a small enemy melee unit comes within 3 m, it jumps 10 m
  along its movement over 0.8 seconds, on a parabola 3 m high. It does not jump if the landing point is a prop cell.
  Cooldown 10 seconds. While jumping it does not fire and is not pushed.
- **Shield (Fortress):** a sphere of radius 7 m. It removes enemy projectiles that cross its boundary from outside and loses
  their damage as energy (capacity 1500). After 4 seconds without hits it refills at 100 per second. Melee and friendly
  projectiles pass. At zero energy it is off.

#### Combat rules

- **Targeting:** the nearest enemy first.
  - Each weapon picks the nearest enemy within its targets (ground, air) and range.
  - A unit's move target is the nearest enemy any of its weapons can hit (no distance limit).
  - Targets are picked again every 0.25 seconds (8 ticks), spread over ticks by unit, and at once when a target dies.
  - Equal distances go to the lower slot index.
- **Movement:**
  - Ground: if the move target is within 8 m with no prop between, steer straight at it; otherwise follow the flow field. Stop
    when the move target is within 90% of the weapon's range. Separate from neighbors, and push out of prop cells and back
    inside the field. The body turns toward its movement (weapons are 360° turrets).
  - Air: ignore props, hold altitude and fly straight at the move target. Separate from other air units only.
- **Flow fields:** two per team (ground enemies only, all enemies; air enemies projected onto the ground). Goal cells are the
  cells enemies stand in. Eight-way Dijkstra with integer costs 10/14; prop cells cannot be crossed, and diagonals do not cut
  corners. Rebuilt every 10 ticks, the four fields spread over ticks. Units that hit only ground (Crawler, Mortar) follow
  "ground enemies only".
- **Firing:** a weapon fires when its cooldown is over and its target is in range. Direct fire (bullets) fires only when no
  prop lies between the muzzle and the target's center; shells and missiles skip that check. No leading: the weapon aims at
  the target's position when it fires.
- **Projectiles:**
  - Bullets fly straight and vanish after their range.
  - Shells fly under gravity (9.8 m/s²); the launch velocity comes from the launch angle and the aim point (the target's
    position plus seeded random spread).
  - Missiles turn toward their target, and toward its last position once it dies.
  - Every tick the segment from the previous position to the current one is tested against shields, props, the ground and
    unit cylinders, and the projectile hits the first thing it touches. A shooter is never hit by its own projectile.
- **Damage:**
  - Each hit deals `max(damage - armor, damage × 0.25)`.
  - Area damage is a 3D sphere (a ground explosion does not reach air units). It falls linearly from 100% at the center to
    25% at the edge, with armor applied per unit.
  - **Friendly fire:** a bullet hits the first unit on its path whatever its team, and area damage hits every unit inside.
  - Damage only adds up during a tick, and deaths are decided together at the tick's end, so the order of damage within a tick
    does not change the result.

### Game object structure

Decisions:

- **The simulation lives outside the scene graph.** Units and projectiles are not `NvNode`s. They are flat arrays in a
  `Battle` struct that reads no rendering, editor state or frame time. The view reads it every frame and copies it into nodes
  and effects.
- **Definitions apart from instances.** `UnitDef` and `WeaponDef` are unchanging data; `Unit` and `Projectile` are runtime
  state.
- **Pools by lifetime.** Squads and units are made when the battle starts and stay in place until it ends. Projectiles are
  short-lived and made often.
- **Discriminated unions only where kinds are mutually exclusive and differently shaped:** ability state (Jump, Shield) and
  missile data. An ability's tag lives in the definition (`UnitDef.ability.kind`), so instances do not repeat it. Unit types
  differ mostly in data, so a unit itself is not a union.
- **Ids.** Units are not created during a battle and slots are not reused, so a `UnitId` is a slot index (0 = none). Dead units
  stay in the array, marked `UNIT_DEAD`. A generation gets added when creation during battle (summons) does.
- **Projectiles** are referred to by nothing, so a finished one is swapped with the last (swap-remove). That changes the
  order, deterministically.
- **Events.** Every tick the simulation writes events for the view (fire, hit, explosion, shield hit, death, jump) into a list,
  and the view turns them into `nv_vfx_*` calls. The simulation never reads events. When the list is full, only events are
  dropped, and counted.

```c
#define BATTLE_TICK_RATE        30
#define BATTLE_MAX_TICKS        (60 * BATTLE_TICK_RATE)
#define BATTLE_CELL_SIZE        2.0f
#define BATTLE_GRID_WIDTH       32
#define BATTLE_GRID_LENGTH      48
#define BATTLE_MAX_PROPS        256
#define BATTLE_MAX_SQUADS       128
#define BATTLE_MAX_UNITS        2048
#define BATTLE_MAX_PROJECTILES  8192
#define BATTLE_MAX_EVENTS       4096 // per tick
#define UNIT_MAX_WEAPONS        2

typedef enum UnitLayer { UNIT_LAYER_GROUND, UNIT_LAYER_AIR } UnitLayer;
typedef enum UnitSize { UNIT_SIZE_SMALL, UNIT_SIZE_MEDIUM, UNIT_SIZE_LARGE } UnitSize;
enum { TARGET_GROUND = 1 << 0, TARGET_AIR = 1 << 1 };

typedef enum ProjectileKind {
    PROJECTILE_NONE, // melee: the hit lands when the weapon fires
    PROJECTILE_BULLET,
    PROJECTILE_SHELL,
    PROJECTILE_MISSILE,
} ProjectileKind;

typedef struct WeaponDef {
    const char* name;
    ProjectileKind projectile;
    u32 targets; // TARGET_*
    f32 min_range, range;
    f32 damage, splash_radius;
    f32 cooldown;
    f32 speed;        // bullet, missile
    f32 turn_rate;    // missile, radians per second
    f32 launch_angle; // shell, radians
    f32 spread;       // meters around the aim point
    NvVec3 muzzle;    // in the unit's space
} WeaponDef;

typedef enum AbilityKind { ABILITY_NONE, ABILITY_JUMP, ABILITY_SHIELD } AbilityKind;

typedef struct AbilityDef {
    AbilityKind kind;
    union {
        struct { f32 distance, height, duration, cooldown, prop_range, melee_range; } jump;
        struct { f32 radius, capacity, regen, regen_delay; } shield;
    };
} AbilityDef;

typedef struct UnitDef {
    const char* name;
    UnitLayer layer;
    UnitSize size;
    u32 squad_size;
    u32 footprint; // cells per side
    u32 cost;
    f32 health, armor, speed, turn_rate;
    f32 radius, height, altitude;
    u32 weapon_count;
    WeaponDef weapons[UNIT_MAX_WEAPONS];
    AbilityDef ability;
} UnitDef;

typedef struct UnitId { u32 index; } UnitId; // 0 = none

enum { UNIT_DEAD = 1 << 0, UNIT_JUMPING = 1 << 1 };

typedef struct Unit {
    u32 flags;
    u8 team;
    u8 def;
    u16 squad;
    NvVec3 position, previous_position; // previous: for drawing between ticks
    NvVec3 velocity;
    f32 yaw, previous_yaw;
    f32 health;
    f32 damage_taken; // this tick; applied at the tick's end
    UnitId move_target;
    struct { UnitId target; f32 cooldown; } weapons[UNIT_MAX_WEAPONS];
    union { // tagged by UnitDef.ability.kind
        struct { f32 cooldown, time; NvVec3 from, to; } jump;
        struct { f32 energy, since_hit; } shield;
    } ability;
} Unit;

typedef struct Projectile {
    ProjectileKind kind;
    u8 team;
    u8 weapon; // index into the shooter's UnitDef.weapons
    UnitId shooter;
    NvVec3 position, previous_position, velocity;
    f32 distance_left;
    union { // tagged by kind
        struct { UnitId target; NvVec3 last_target_position; } missile;
    };
} Projectile;

typedef struct Squad {
    u8 team, def;
    u8 cell_x, cell_row; // the footprint's corner nearest the origin of the grid
    u32 first_unit, unit_count, alive_count;
} Squad;

typedef struct Prop {
    u8 cell_x, cell_row, width, length; // in cells
    f32 height;
} Prop;

typedef enum BattleEventKind {
    BATTLE_EVENT_FIRE, BATTLE_EVENT_HIT, BATTLE_EVENT_EXPLODE, BATTLE_EVENT_SHIELD_HIT, BATTLE_EVENT_DEATH, BATTLE_EVENT_JUMP,
} BattleEventKind;

typedef struct BattleEvent { // for the view only
    BattleEventKind kind;
    u8 def, weapon;
    NvVec3 position, direction;
    f32 size;
} BattleEvent;

typedef enum BattlePhase { BATTLE_DEPLOY, BATTLE_FIGHT, BATTLE_RESULT } BattlePhase;
typedef enum BattleOutcome { OUTCOME_NONE, OUTCOME_PLAYER, OUTCOME_ENEMY, OUTCOME_DRAW } BattleOutcome;

typedef struct Battle {
    BattlePhase phase;
    BattleOutcome outcome;
    u32 tick;
    u64 rng; // seeded at Start
    Prop props[BATTLE_MAX_PROPS];
    u32 prop_count;
    u8 blocked[BATTLE_GRID_LENGTH][BATTLE_GRID_WIDTH]; // cells with a prop
    Squad squads[BATTLE_MAX_SQUADS];
    u32 squad_count;
    Unit units[BATTLE_MAX_UNITS]; // [0] is unused
    u32 unit_count;
    Projectile projectiles[BATTLE_MAX_PROJECTILES];
    u32 projectile_count;
    NvFlowField fields[2][2]; // [team][0 ground enemies, 1 all enemies]
    NvSpatialGrid grid;       // rebuilt every tick
    BattleEvent events[BATTLE_MAX_EVENTS];
    u32 event_count, events_dropped;
} Battle;
```

### Tick order

1. Record every unit's `previous_position` and `previous_yaw`, and rebuild the spatial grid (units go in by slot order).
2. Rebuild the flow field whose turn it is.
3. Pick targets again for the units whose turn it is.
4. Movement and abilities (starting and advancing jumps, separation, pushing out, integration).
5. Weapons: cooldowns, firing (new projectiles, melee damage).
6. Projectiles: integration, collisions, damage and area damage, shields.
7. Deaths: apply `damage_taken`, and mark units at zero health or below `UNIT_DEAD`.
8. Shield refill, end conditions.

### Determinism

A fixed 30 Hz tick, seeded random numbers (PCG32, written here), every loop in slot order, and no frame dt read by the
simulation. WebAssembly floats are deterministic within one build, and libm functions such as `sinf` are compiled into the
module, so no fixed-point math is needed. Finished battles are compared by a hash (FNV-1a) of their state.

### Where the code goes

In a folder of its own, `autobattler/`, with its own executable, apart from the editor app (`app/`). It links only the engine
(`nv`) and uses no code from `app/`.

| File | Contents |
|---|---|
| `autobattler/CMakeLists.txt` | The `autobattler` executable, installed into `<prefix>/` of the `autobattler` install component (see "Page") |
| `autobattler/main.c` | Window, GPU, renderer, ImGui, effects, camera, the frame loop (a fixed-tick accumulator) |
| `autobattler/battle.h`, `autobattler/battle.c` | The rules and the tick. No GPU or ImGui, so ctest can build it |
| `autobattler/battle_defs.c` | The unit definition table and one stage (props, enemy deployment) |
| `autobattler/battle_view.c` | Drawing with nodes and effects, deployment input, the panel |
| `autobattler/strings.c` | The game's Korean table (see "UI strings") |
| Spatial grid, flow field | Open question 2: `engine/` (`engine/spatial.h`, `engine/flow.h`) or `autobattler/` |

### Executable

- **Page:** the default branch's Release at `autobattler/release/` on Pages and its Debug at `autobattler/debug/`; other
  branches' Debug at `autobattler/<branch>/` (`/` becomes `-`). The editor app's folders (`release/`, `debug/`, `<branch>/`)
  stay as they are. Downloaded apart from the editor app.
  - The root `CMakeLists.txt` gets `add_subdirectory(autobattler)`.
  - `nv_setup_executable` gets a `COMPONENT <name>` argument (default `web`). The game is
    `nv_setup_executable(autobattler ROOT COMPONENT autobattler)`, so it stays out of the app's `web` package.
  - In `build.yml`, Package installs `--component autobattler` into `dist/engine-web/autobattler/release` and `.../debug`,
    and stage replaces `autobattler/release` and `autobattler/debug` for the default branch, `autobattler/<branch>` for others.
    A branch named `autobattler` is not published, like `release` and `debug` (it would replace that folder).
- **Screen:** the whole canvas is the battlefield, with one ImGui window (the Battle panel) over it. None of the editor's docks,
  search, palette, saving, undo, selection or Console tab. The scene resolution starts equal to the canvas (`pixel_size` 1 in
  `NvSceneOutput`).
- **Camera:** an angled view from above and behind the player's side. Pan by dragging in `NvImgui.view`, zoom with the wheel
  and pinch, kept over the field.
- **Drawing:**
  - Units are cube mesh nodes in team colors, sized by radius and height, interpolated between ticks.
  - Props are gray boxes; the deployment grid and zones are debug lines; health bars are debug lines above units.
  - Projectiles are `nv_vfx_trail`, explosions `nv_vfx_burst` and `nv_vfx_decal`, shields a debug-line circle.
- **Deployment input:** pick a unit type in the panel, then tap a cell in the viewport to place it. Cells that can take it
  preview green, others red. Tapping a placed squad removes it. The same on desktop and phone.
- **Battle panel:** the phase, remaining supply, a button per unit type (name and cost), Start, Retry, Reset, time left, living
  units per team, the result.
- **Debug exports:** `_battle_debug(n)` (phase, tick, outcome, unit counts, hash), `_battle_debug_deploy(def, x, row)`,
  `_battle_debug_start()`, `_battle_debug_run(ticks)`.

### UI strings (moved to the engine)

Decided (2026-10-02): `T()`, `TL()` and language selection move to the engine (`engine/strings.h`), and each executable keeps its
own Korean table. The details are in "UI strings" of `docs/specs/shared.md`. The game keeps its table in
`autobattler/strings.c`, starts in the browser's language and switches with a Language combo in the Battle panel (not
saved). Every text in the panel goes through `T()`/`TL()`.

### Third-party candidates

| Candidate | What it is | Language, license | Judgment |
|---|---|---|---|
| **Write it ourselves** (recommended) | Spatial grid about 150 lines, flow field about 200, the rules | C17 | Keeps fixed capacities, arenas and a deterministic iteration order as they are |
| flecs | An ECS | C, MIT | Callbacks and opaque storage clash with the coding standard; with a handful of object kinds there is no gain |
| Recast/Detour | Navmesh and crowds | C++, zlib | The field is a flat cell grid, so no navmesh is needed (`autobattler.md`). It would be a second C++ file |
| Definitions: C tables (recommended, this step) | Tables in `battle_defs.c` | C17 | A change needs a rebuild |
| Definitions: JSON + cJSON | Text files, a parser | C, MIT | Easy to read; its allocator must be pointed at our arenas |
| Definitions: JSON + jsmn | A tokenizer only | C, MIT | No allocation; interpreting values is ours |
| Definitions: `engine/chunk.h` | Our binary format | C17 | No dependency, but needs an editor |

### Tests

`tests/battle_test.c` (ctest, Node) builds `autobattler/battle.c`, `autobattler/battle_defs.c` and the spatial grid and flow field sources:

- The same deployment and seed run twice give the same hash.
- Spatial grid: queries return the same units, in the same order, as a brute-force check.
- Flow field: a path around a wall, blocked cells, no goal.
- One scene per rule: the nearest enemy is picked; a low wall stops bullets; shell splash hurts allies too; a ground explosion
  does not hit air units; a Wasp flies over props; a Ranger jumps a low wall; a shield stops enemy projectiles and lets
  friendly ones through; the mortar's minimum range; the battle ends at 60 seconds and is judged by remaining value.

`tests/strings_test.mjs` checks `autobattler/`'s strings too.

Playwright (Release and Debug, desktop and phone sizes): open the `autobattler/` page, place and remove squads by tapping, go
from Start to a result, and check for no asserts and no WebGPU errors.

### Phases

1. **First:** `docs/specs/shared.md` (strings, meshes, resolution and taps, the camera, the font, the version and helpers move
   to the engine).
2. **Simulation:** `spatial`, `flow`, `battle.c`, `battle_defs.c`, `battle_test.c`. No view.
3. **Executable:** `autobattler/main.c`, the camera, drawing, effects, the Battle panel and its string table, deployment input.
4. **Checks and docs:** debug exports, Playwright checks, updates to `AGENTS.md` (which now says "one executable, `app`") and `autobattler.md`.

### Open questions

1. **Definition data format:** C tables for this step and a file format decided separately before balancing, or decide now?
2. **Where the spatial grid and flow field go:** the engine (`engine/spatial.h`, `engine/flow.h`), as `autobattler.md` says, or
   `autobattler/` first, moving them when a second use appears?
3. **Time-limit judgment:** is comparing remaining value right?
4. **Squads:** units act on their own after the start (proposed), or keep a formation?
5. **Passages for large units:** the flow field assumes a one-cell width, so a Fortress can get stuck in a one-cell gap. This
   step keeps one-cell gaps out of the stage and leaves per-size flow fields for later. Is that acceptable?
6. **Numbers:** the table values are a starting point. Anything to change?
7. ~~**UI strings**~~: resolved. They move to the engine ("UI strings" section).
8. **Assets:** the prototype needs only the UI font (one Pretendard file). Ship the whole `assets/` directory, or
   only `assets/fonts/` (`ASSETS` in `nv_setup_executable` takes one directory)? Recommended: only `assets/fonts/`.

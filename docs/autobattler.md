# Single-player auto-battler: what the engine needs

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 레퍼런스 (2026-10-01). 스펙이 아니다. 각 항목은 만들기 전에 `docs/specs/`에 스펙을 따로 쓴다.

### 목표

메카벨리움 스타일의 싱글플레이 게임을 만들기 위해 이 엔진과 앱이 갖춰야 할 기능 목록. 온라인 대전은
당분간 범위 밖이다.

### 레퍼런스 게임

메카벨리움(Mechabellum; Game River 개발, Paradox Arc 퍼블리싱, 2024년 정식 출시)은 턴제 오토배틀러다.
아래 요약은 기억에 의존한 것이며 게임으로 확인하지 않았다.

- **배치 단계**: 공급(돈)으로 유닛을 사서 그리드의 내 진영에 놓고, 유닛 업그레이드, 테크, 전문가,
  증원 카드를 고른다.
- **전투 단계**: 작은 크롤러 떼부터 포트리스, 오버로드 같은 거대 메카까지 수백 기가 입력 없이 싸운다.
  살아남은 유닛만큼 진 쪽 본부의 체력이 깎인다.
- 온라인 1v1, 2v2, 리플레이, 관전.

### 싱글플레이라서 바뀌는 점

| 주제 | 영향 |
|---|---|
| 네트워크, 매치메이킹, 관전 | 빠진다 |
| 결정론 | 대전 검증에는 필요 없지만 리플레이, 재시도, 테스트에는 여전히 유용하다. WebAssembly의 float 연산은 같은 빌드 안에서 결정적이므로 고정 틱, 시드 난수, 순서 고정 반복이면 충분하다. 고정소수점은 필요 없다 |
| AI 상대, 캠페인, 난이도, 진행 | 핵심이 된다. 사람 상대가 없으니 재미가 여기서 나온다 |
| 시간 조작 | 자유롭게 넣을 수 있다: 일시정지, 배속, 슬로 모션 |

### 엔진의 현재 상태

WebGPU 렌더러, 씬 그래프, glTF 로딩, ozz-animation 스켈레탈 애니메이션, 그림자 맵 하나, MSAA(multisample
anti-aliasing, 다중 샘플 안티앨리어싱), 씬 고유 해상도, ImGui 에디터, IndexedDB 저장, undo, 스트레스 씬과
벤치마크. 상한: `NV_MAX_NODES` 16384, `NV_MAX_ANIMATORS` 256. 메시 노드마다 draw call이 하나이고 컬링이
없으며, 오디오, 파티클 시스템, 게임 UI가 없다.

### 필요한 기능

우선순위 순. "위치"는 `AGENTS.md`의 엔진/앱 기준(다른 앱도 쓸까?)을 따른다.

| # | 기능 | 내용 | 위치 |
|---|---|---|---|
| 1 | 시뮬레이션 코어 | 고정 틱; 유닛 스탯, 타게팅, 이동, 사거리, 투사체, 데미지; 라운드 상태 기계(배치, 전투, 정산) | app |
| 2 | 공간 질의 | 사거리 내 적과 범위 피해를 위한 균일 그리드나 공간 해시 (picking과 별개) | engine |
| 3 | 군집 이동 | flow field(흐름장)와 분리, 회피. 전장이 평평하므로 navmesh는 필요 없다 | engine |
| 4 | 데이터 기반 정의 | 유닛, 테크, 카드, 스테이지를 데이터 파일로 두어 밸런스 조정에 재빌드가 필요 없게 | app |
| 5 | AI 상대 | 배치, 구매, 업그레이드 휴리스틱; 난이도별 파라미터; 플레이어 배치에 대한 카운터 | app |
| 6 | 대량 렌더링 | 메시별 인스턴싱 배치, frustum culling, LOD(level of detail, 거리별 세부 단계) | engine |
| 7 | 대량 애니메이션 | 메카용 리지드(관절별 강체) 애니메이션이나 VAT(vertex animation texture, 정점 애니메이션 텍스처). 애니메이터 256개 상한을 넘기 위해 | engine |
| 8 | VFX(visual effects, 시각 효과) | GPU 파티클(compute shader), 미사일 궤적, 빔, 가산 블렌딩, bloom, 데칼. 스펙: `docs/specs/vfx.md` | engine |
| 9 | 시간 조작 | 일시정지, 2배속, 4배속, 슬로 모션: 프레임당 틱 수만 바뀐다 (`autobattler/`에 구현됨) | app |
| 10 | 배치 UI와 게임 UI | 그리드 스냅 배치, 드래그와 회전, 상점, 카드, 체력바, 라운드 결과. ImGui 에디터와 분리된 게임 UI | engine (UI 기반), app (화면) |
| 11 | 오디오 | Web Audio API 래퍼, 위치 사운드, 동시 재생 수 제한, 음악 | engine |
| 12 | 캠페인과 진행 | 스테이지 목록, 승리 조건이나 별점, 유닛과 테크 해금, 난이도. 진행은 `app/save.c`에 새 태그로 저장 | app |
| 13 | 리플레이와 재시도 | 초기 상태와 라운드별 배치만 저장; 진 라운드를 배치만 바꿔 다시 하기 | app |
| 14 | 튜토리얼 | 단계별 안내, 하이라이트, 입력 제한 | app |
| 15 | 카메라와 전장 | 줌, 팬, 경계가 있는 RTS(real-time strategy, 실시간 전략) 카메라; 지형; 본부 오브젝트; 전투 중 유닛 추적 | engine 일부, app |

### 추천 순서

1. **전투 프로토타입** (1, 2, 3, 4): 엔진의 기본 도형(`docs/specs/mesh.md`)과 디버그 라인으로 그린다. ctest 테스트로 같은 배치가 같은
   결과 해시를 내는지 확인한다. A 단계(유닛 한 종류의 한 라운드)는 `autobattler/`에 구현되었다(`docs/specs/battle.md`); B 단계가 남아 있다.
2. **라운드 루프와 AI** (5, 9, 10의 최소한): 게임이 재미있는지 여기서 판가름 난다.
3. **대량 렌더링과 애니메이션** (6, 7): 유닛 약 1,000기와 투사체 5,000개의 스트레스 씬 워크로드로 측정한다.
4. **연출** (8, 11, 15).
5. **콘텐츠** (12, 13, 14).

1, 2단계는 엔진에 새로 필요한 것이 없다. 각 단계의 스펙은 `AGENTS.md`의 요구대로 서드파티 라이브러리
후보(공간 해시, flow field, 데이터 파일 형식, 오디오)를 먼저 비교한다.

## English

Status: reference (2026-10-01). Not a spec: each item gets its own spec in `docs/specs/` before it
is built.

### Goal

A single-player game in the style of Mechabellum, and the list of what this engine and app must
gain to make it. Online play is out of scope for now.

### The reference game

Mechabellum (Game River, published by Paradox Arc, released 2024) is a turn-based auto-battler.
This summary is from memory, not checked against the game.

- **Deployment phase**: the player spends supply on units and places them on their half of a grid,
  and picks unit upgrades, tech, specialists and reinforcement cards.
- **Battle phase**: hundreds of units, from swarms of small Crawlers to giant mechs such as the
  Fortress and the Overlord, fight with no player input. Surviving units take HP off the loser's
  base.
- Online 1v1 and 2v2, replays and spectating.

### What single-player changes

| Topic | Effect |
|---|---|
| Networking, matchmaking, spectating | Dropped |
| Determinism | Not needed to check matches, still useful for replays, retries and tests. WebAssembly float math is deterministic within one build, so a fixed tick, seeded random numbers and a stable iteration order are enough; no fixed-point math |
| AI opponent, campaign, difficulty, progression | Become the core: with no human opponent, the fun comes from here |
| Time controls | Free to add: pause, speed-up, slow motion |

### What the engine has today

WebGPU renderer, scene graph, glTF loading, ozz-animation skeletal animation, one shadow map,
MSAA (multisample anti-aliasing), a scene resolution of its own, the ImGui editor, IndexedDB saves,
undo, and the stress scene with its benchmark. Limits: `NV_MAX_NODES` 16384, `NV_MAX_ANIMATORS` 256.
Every mesh node is one draw call, nothing is culled, and there is no audio, particle system or
game UI.

### Features needed

In priority order. "Where" follows the engine/app criterion in `AGENTS.md`: would another app use
it?

| # | Feature | Contents | Where |
|---|---|---|---|
| 1 | Simulation core | Fixed tick; unit stats, targeting, movement, range, projectiles, damage; round state machine (deploy, battle, resolve) | app |
| 2 | Spatial queries | Uniform grid or spatial hash for enemies in range and area damage (separate from picking) | engine |
| 3 | Crowd movement | Flow field with separation and avoidance; the battlefield is flat, so no navmesh | engine |
| 4 | Data-driven definitions | Units, tech, cards and stages in data files, so balancing needs no rebuild | app |
| 5 | AI opponent | Heuristics for deploying, buying and upgrading; parameters per difficulty; counters to the player's deployment | app |
| 6 | Mass rendering | Instanced batches per mesh, frustum culling, LOD (level of detail) | engine |
| 7 | Mass animation | Rigid per-joint animation or VAT (vertex animation textures) for mechs, past the 256-animator limit | engine |
| 8 | VFX (visual effects) | GPU particles (compute shaders), missile trails, beams, additive blending, bloom, decals. Spec: `docs/specs/vfx.md` | engine |
| 9 | Time controls | Pause, 2x and 4x speed, slow motion: the number of ticks per frame changes (built in `autobattler/`) | app |
| 10 | Deployment and game UI | Grid-snapped placement, drag and rotate, shop, cards, health bars, round results; a game UI apart from the ImGui editor | engine (UI base), app (screens) |
| 11 | Audio | Web Audio API wrapper, positional sound, a cap on simultaneous sounds, music | engine |
| 12 | Campaign and progression | Stage list, win conditions or stars, unit and tech unlocks, difficulty; progress saved with new tags in `app/save.c` | app |
| 13 | Replays and retries | Store the initial state and each round's deployment; replay a lost round with a changed deployment | app |
| 14 | Tutorial | Step-by-step guidance, highlights, limited input | app |
| 15 | Camera and battlefield | RTS (real-time strategy) camera with zoom, pan and bounds; terrain; base objects; following units in battle | engine in part, app |

### Suggested order

1. **Battle prototype** (1, 2, 3, 4): drawn with the engine's primitive shapes (`docs/specs/mesh.md`) and debug lines. A ctest test checks
   that the same deployment gives the same result hash. Its stage A (one round with one unit type) is built in `autobattler/`
   (`docs/specs/battle.md`); stage B is still to do.
2. **Round loop and AI** (5, 9, the least of 10): this is where the game proves fun or not.
3. **Mass rendering and animation** (6, 7): measured with a stress scene workload of about 1,000
   units and 5,000 projectiles.
4. **Presentation** (8, 11, 15).
5. **Content** (12, 13, 14).

Steps 1 and 2 need nothing new from the engine. Each step's spec compares third-party library
candidates first (spatial hash, flow field, data file format, audio), as `AGENTS.md` requires.

# Visual effects spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 초안 (2026-10-01). 아래 "확인할 결정"이 합의되기 전에는 구현하지 않는다.

### 목표

`docs/autobattler.md`의 8번 항목: 오토배틀러의 전투에 필요한 시각 효과를 엔진에 더한다.

- **파티클**: 폭발, 불꽃, 연기, 파편, 총구 섬광.
- **궤적과 빔**: 미사일과 예광탄의 궤적, 레이저 빔.
- **데칼**: 지면의 그을음과 탄흔.
- **HDR, 톤 매핑, bloom**: 밝은 효과가 빛나 보이게.

규모의 목표는 같은 화면에서 유닛 약 1,000기와 투사체 5,000개 (`autobattler.md`의 3단계). 폰에서도 감당할 수 있어야 한다.

효과는 겉모습일 뿐이다: 게임 시뮬레이션 (`autobattler.md`의 1번)은 효과를 읽지 않고, 효과는 결정론적일 필요가 없다.

### 지금

- 씬 패스는 불투명 메시와 알파 블렌딩된 디버그 라인만 그린다. 투명한 것을 위한 다른 파이프라인은 없다.
- 씬 색 타깃은 캔버스 형식 (`BGRA8Unorm`, sRGB 뷰)이다. 1을 넘는 밝기는 잘리므로 bloom이 쓸 HDR 값이 없다.
- 업스케일 패스는 씬 색을 최근접 필터로 캔버스에 복사한다 (`resolution.md`). 후처리 패스는 없다.
- compute 패스는 하나도 없다.

### 서드파티 후보

| 후보 | 무엇인가 | 언어, 라이선스 | 맞음 | 장단점 |
|---|---|---|---|---|
| **직접 작성** (추천) | 아래 설계: 상태 없는 GPU 파티클, 인스턴스 선분, 지면 데칼, bloom 체인 | C17과 WGSL | 엔진의 렌더러, 아레나, 고정 용량에 그대로 들어간다. 약 1,500줄로 추정 | 효과 편집기가 없다: 효과는 코드 안의 데이터 표로 정의한다 |
| Effekseer (EffekseerForWeb) | 편집기가 딸린 완전한 파티클 효과 런타임. WebAssembly 빌드가 있고, WebGPU 백엔드는 Dawn 기반의 실험 단계다 | C++, MIT | 편집기에서 만든 효과를 그대로 재생한다 | 자체 그래픽 추상화 (LLGI)와 자체 GPU 리소스를 가진 큰 C++ 런타임이라, 우리의 패스, MSAA, 해상도, 깊이 규칙 (reverse Z)과 맞추는 래퍼가 크다. 두 번째 C++ 파일, 실험 단계 백엔드 |
| PopcornFX | 상용 파티클 미들웨어 | C++, 상용 | | 오픈 소스가 아니다 |
| Khronos PBR Neutral 톤 매퍼 | 공개된 톤 매핑 공식 (약 15줄) | 참조 코드 Apache-2.0 | WGSL로 직접 옮겨 쓴다. 0.76 아래의 색은 거의 그대로 둔다 | 라이브러리가 아니라 공식이다: 출처를 주석에 남긴다 |
| Jimenez 2014 bloom (Call of Duty: Advanced Warfare, SIGGRAPH 발표) | 다운샘플/업샘플 밉 체인 bloom 기법 | 발표 자료 (코드 아님) | 표준적인 방법이고 렌더 패스만으로 된다 | 우리가 쓴다 |

추천: 직접 작성한다. 톤 매핑과 bloom은 공개 기법을 따르고 출처를 주석에 남긴다. 새 라이브러리는 없다.

### 접근법: 파티클 시뮬레이션

| 접근법 | 무엇인가 | 맞음 | 장단점 |
|---|---|---|---|
| **상태 없는 GPU 파티클, 버스트 단위 기록** (추천) | 효과를 터뜨릴 때 CPU는 이미터마다 버스트 기록 하나 (이미터, 위치, 방향, 배율, 생성 시각, 시드, 첫 슬롯, 수; 64바이트)만 올리고, 그 버스트가 받은 파티클 슬롯마다 버스트 번호 (4바이트)를 쓴다. 정점 셰이더는 슬롯에서 버스트를 찾고, 버스트 안의 순번과 시드로 해시 난수를 만들어 파티클의 초기값 (속도, 수명, 크기)을 정한 뒤, 현재 시각으로 위치를 닫힌 식으로 계산한다: 중력, 공기 저항 (지수 감쇠), 지면 높이에서 멈춤, 크기와 색의 시간 곡선 | compute 패스가 없다. 파티클 하나의 GPU 메모리와 업로드가 4바이트라서 100만 개에 4 MB다. 시간을 멈추거나 느리게 하면 그대로 따라간다. 폭발, 불꽃, 연기, 파편에는 충분하다 | 상태에 따른 힘 (끌어당김, 충돌 후 튕김, 다른 것과의 상호작용)은 표현할 수 없다 |
| 상태 없는 GPU 파티클, 파티클마다 생성 기록 (이전 초안) | CPU가 파티클마다 48바이트 기록 (위치, 속도, 수명, ...)을 만들어 올린다 | 셰이더가 조금 단순하다 | 100만 개에 48 MB, 그리고 초당 수십만 개의 기록을 CPU가 난수로 만든다. 백만 단위에서는 버스트 단위가 낫다 |
| compute 셰이더 시뮬레이션 | 파티클 상태 버퍼를 compute 패스가 매 프레임 적분한다. 생성과 소멸은 atomic 카운터와 빈 목록으로 관리한다 | 어떤 힘이든 가능하다 | compute 패스, 간접 그리기, atomic, 빈 목록 관리. 엔진의 첫 compute 코드다. 오토배틀러의 효과에는 필요한 기능을 넘는다 |
| CPU 시뮬레이션 | wasm에서 적분하고 매 프레임 전체를 올린다 | 가장 단순 | 100만 개면 프레임마다 약 32 MB 업로드와 큰 CPU 시간. 백만 단위에서는 불가능하다 |

추천: 버스트 단위 기록의 상태 없는 GPU 파티클. compute 시뮬레이션은 상태에 따른 효과가 필요해질 때 이 설계 옆에 더한다 (버스트 기록과 효과 표는 그대로 쓸 수 있다). `autobattler.md`의 "GPU 파티클 (compute shader)"은 이 결정으로 바뀐다.

### 결정

#### 공통

| 주제 | 결정 |
|---|---|
| 모듈 | `nv/vfx.h`, `engine/src/vfx.c`: 효과 표, 파티클, 선분 (궤적과 빔), 데칼. 렌더러는 `NvRenderer.vfx`가 있으면 씬 패스 안에서 그것을 그린다. HDR, 톤 매핑, bloom은 렌더러에 들어간다 (`renderer.c`) |
| 시각 | `nv_vfx_update(vfx, dt)`가 효과의 시각을 진행한다. 앱은 게임 시각의 dt를 넘기므로, 일시정지는 효과를 멈추고 슬로 모션은 효과를 느리게 한다 (`autobattler.md`의 9번) |
| 효과와 이미터 | 효과는 이미터 최대 8개의 묶음이다 (폭발 = 섬광 + 불꽃 + 연기 + 파편). 이미터는 파티클 흐름 하나로, `NvVfxEmitterDesc`의 행이다: 한 번에 나오는 수, 수명 범위, 속도 범위와 원뿔, 중력, 공기 저항, 크기 시작과 끝, 색 세 점 (시작, 중간, 끝; HDR 값 허용), 모양, 블렌드 모드, 속도 방향 늘이기, 지면 높이, 효과 안에서의 시작 지연. 효과 최대 1,024개 (`NV_VFX_MAX_EFFECTS`), 이미터 최대 4,096개 (`NV_VFX_MAX_EMITTERS`). 이미터 표는 GPU의 스토리지 버퍼 하나 (이미터당 약 128바이트, 최대 512 KB)라서 수를 늘리는 비용이 작다. 앱이 시작할 때 등록한다. 데이터 파일은 `autobattler.md`의 4번 (데이터 기반 정의)에서 다룬다 |
| 호출 | `nv_vfx_burst(vfx, effect, position, direction, scale)`: 한 번에 터뜨리기. `nv_vfx_emit(vfx, effect, from, to, count)`: 이번 틱에 움직인 구간을 따라 고르게 내보내기 (미사일 연기 궤적). 핸들도 해제도 없다: 모든 것은 수명이 끝나면 사라진다 |
| 그리기 순서 | 씬 패스 안에서: 불투명 메시 → 데칼 → 디버그 라인 → 선분 → 알파 파티클 → 가산 파티클. 모두 깊이 테스트를 하고 깊이를 쓰지 않는다 |
| 정렬 | 하지 않는다. 가산 블렌딩은 순서와 무관하다. 알파 블렌딩 (연기)은 낮은 불투명도로 써서 순서 오류가 눈에 띄지 않게 한다. 순서와 무관한 투명도 (OIT)는 범위 밖 |
| 조명과 그림자 | 효과는 빛을 받지 않고 (unlit) 그림자를 드리우지도 받지도 않는다. picking에 걸리지 않는다 |
| MSAA와 해상도 | 효과는 씬 패스 안에 그리므로 씬의 해상도와 샘플 수를 따른다 (`resolution.md`, `msaa.md`) |
| 서드파티 | 없음 |

#### 파티클

| 주제 | 결정 |
|---|---|
| 용량 | 동시에 살아 있는 파티클 수, 모든 효과의 합계. 고정 상수가 아니라 앱이 `nv_vfx_init`에 넘긴다: 기본값은 데스크톱 2,097,152 (2M), 폰 262,144. 상한은 기기의 `maxStorageBufferBindingSize` (WebGPU 기본 128 MiB)인데, 파티클 하나가 4바이트라 3,300만 개까지 들어간다. 그래서 실제 한계는 메모리가 아니라 그리기 비용이다 (아래) |
| 할당 | 블렌드 모드별 링 두 개, 비율은 앱이 정한다 (기본 가산 3/4, 알파 1/4). 효과를 터뜨리면 이미터마다 링에서 연속된 슬롯 구간을 받는다; 가득 차면 가장 오래된 파티클을 덮어쓴다. atomic도 GPU 읽어 오기도 없다. 버스트 기록도 자체 링 (최대 65,536개, 4 MB)에 둔다 |
| 업로드 | 프레임마다 새 버스트 기록과 새 슬롯의 버스트 번호만 `wgpuQueueWriteBuffer`로 올린다 (링이 돌면 두 번). 100만 개가 2초마다 모두 바뀌어도 프레임당 약 35 KB |
| 살아 있는 구간 | CPU는 버스트마다 생성 시각과 가장 긴 수명을 보관한다. 아직 살아 있는 가장 오래된 버스트의 첫 슬롯부터 링의 앞까지를 그린다. 그 안에서 수명이 끝난 파티클은 정점 셰이더가 크기 0으로 버린다. 수명이 긴 연기가 그 뒤의 짧은 불꽃까지 구간에 붙잡아 두면 죽은 파티클의 정점 비용이 는다: 측정해서 필요하면 수명 등급별로 링을 나눈다 |
| 그리기 방식 | 인스턴싱 대신 vertex pulling: 구간의 파티클 N개를 `Draw(6N)` 한 번으로 그리고, 정점 셰이더가 `vertex_index / 6`으로 슬롯을 찾는다. 정점 4–6개짜리 작은 인스턴스는 GPU의 정점 웨이브를 덜 채울 수 있어서, 파티클에서 흔히 쓰는 방식이다 |
| 그리기 비용 | 정점 셰이더가 구간의 모든 파티클을 매 프레임 계산하고 (100만 개 = 정점 600만 개), 겹쳐 그린 픽셀이 fill rate를 쓴다. 보통 큰 가산 파티클의 겹침이 먼저 한계가 된다. 데스크톱 GPU에서는 수백만 개를 예상하고, 폰에서는 수십만 개 언저리를 예상한다. 둘 다 스트레스 씬으로 잰다 (아래) |
| 모양 | 첫 단계는 텍스처 없이 셰이더가 그린다: 부드러운 원, 고리 (충격파), 속도로 늘인 줄 (불꽃), 노이즈 덩어리 (연기). 스프라이트 아틀라스는 나중에 같은 모양 번호 자리에 더한다 |
| 카메라 정렬 | 사각형은 카메라를 향한다. 늘이기가 켜진 효과는 화면 공간의 속도 방향으로 늘인다 (속도는 닫힌 식의 미분으로 구한다) |
| 지면 | 효과마다 지면 높이를 둘 수 있다: 그보다 아래로는 내려가지 않고 그 자리에 머문다 (파편이 땅에 떨어진다). 튕김은 범위 밖 |

#### 궤적과 빔

| 주제 | 결정 |
|---|---|
| 형태 | 둘 다 카메라를 향한 선분 인스턴스다: 끝점 둘, 폭, 색, 생성 시각, 수명. 용량은 앱이 정한다 (기본 262,144; 선분 하나 48바이트, 12 MB), 같은 링 방식 |
| 궤적 | `nv_vfx_trail(vfx, style, from, to)`: 앱이 미사일의 이번 틱 이동 구간마다 부른다. 구간마다 선분 하나가 생기고 수명 동안 가늘어지며 흐려진다. 연기 궤적은 같은 구간에 `nv_vfx_emit`을 함께 쓴다 |
| 빔 | `nv_vfx_beam(vfx, style, from, to, seconds)`: 레이저처럼 두 점 사이에 일정 시간 유지되는 선분. 셰이더가 결을 흐르게 하고 깜박이게 한다. 움직이는 유닛을 따라가야 하면 앱이 매 틱 짧은 수명으로 다시 부른다 |

#### 데칼

| 주제 | 결정 |
|---|---|
| 형태 | 지면 평면 위의 사각형 인스턴스: 위치, 회전, 크기, 모양, 색, 생성 시각, 수명 (끝에서 흐려짐). 용량은 앱이 정한다 (기본 16,384), 같은 링 방식 |
| 범위 | 평평한 지면 위에만 놓는다 (`autobattler.md`: 전장이 평평하다). 메시나 울퉁불퉁한 지형에 투영하는 데칼은 범위 밖: 깊이 버퍼를 읽어야 하는데, 다중 샘플 깊이는 같은 패스에서 읽을 수 없다 |
| 깊이 | 지면과 겹치므로 깊이 바이어스로 지면 위에 그린다. reverse Z이므로 바이어스 부호가 표준과 반대다 |

#### HDR, 톤 매핑, bloom

| 주제 | 결정 |
|---|---|
| 씬 색 | 씬 색 타깃과 4-샘플 색 타깃을 `RGBA16Float`로 바꾼다. WebGPU core에서 렌더링, 블렌딩, 다중 샘플, resolve가 모두 된다. 값이 선형으로 저장되므로 `msaa.md`의 "캔버스처럼 만들기" (sRGB 뷰)는 이 타깃에 더 이상 필요 없다 |
| 메모리 | 색 타깃이 픽셀당 4바이트에서 8바이트가 된다. 3× 폰의 1/2 (585 × 500)에서 4-샘플 색 4.5 MB → 9 MB, 씬 색 1.1 MB → 2.3 MB |
| 톤 매핑 | 업스케일 패스에서 한다: 노출을 곱하고, 톤 매핑하고, 캔버스의 sRGB 뷰에 쓴다. 선택지: **Clamp** (지금의 모습: 1에서 자름), **PBR Neutral** (Khronos; 0.76 아래는 거의 그대로이고 밝은 쪽만 부드럽게 누름), **ACES** (Narkowicz의 근사식; 대비가 강한 영화풍). 기본값은 확인할 결정 3 |
| bloom | Jimenez 2014 방식: 씬 색에서 시작해 반씩 줄이는 다운샘플 6단계 (13탭 필터, 첫 단계는 반짝이는 점을 막는 Karis 평균), 그다음 텐트 필터로 올라오며 더한다. 업스케일 패스가 결과를 세기 (기본 0.04)만큼 섞는다. 임계값은 두지 않는다: HDR 값이 큰 곳만 눈에 띄게 번진다. 각 단계는 작은 렌더 패스다 (compute 없음) |
| bloom 타깃 | `RGBA16Float` 밉 체인, 씬 해상도의 1/2부터. 씬 타깃처럼 64로 올림하고 충분히 크면 유지한다 |
| 비용 표시 | bloom 패스들에 타임스탬프 쌍 하나를 더한다 (`NV_TIMESTAMP_COUNT` 6 → 8). Stress 탭과 벤치마크가 그것을 보여 준다 |
| Textures 탭 | 렌더 타깃 그룹에 bloom 체인의 단계들이 나온다. `RGBA16Float`는 필터링 가능하므로 기존 미리보기 경로로 보인다 (값은 잘려서 보인다) |

#### 앱

| 주제 | 결정 |
|---|---|
| View 탭 | **Post-processing** 섹션: Tone mapping (Clamp, PBR Neutral, ACES), Exposure (0.25–4), Bloom (켜기/끄기), Bloom intensity (0–0.2). **Effects** 섹션: 등록된 효과마다 시험 버튼 (Explosion, Sparks, Smoke, Missile, Laser, Scorch)이 공전 점에서 효과를 낸다. 살아 있는 파티클, 선분, 데칼 수. 모든 행은 `search_row`를 거치고, 문자열은 `T`/`TL`과 한국어 행을 가진다 |
| 저장 | 새 `EDIT` 태그: `TONE` (u32: 0 Clamp, 1 PBR Neutral, 2 ACES), `EXPO` (f32), `BLOM` (u32), `BLMI` (f32). 살아 있는 효과는 저장하지 않는다 |
| Edit와 Play | 시험 버튼은 Edit 모드에서도 동작한다: 애니메이션 미리보기처럼 사용자가 누른 일회성 미리보기다. 계속 나오는 효과 (쇼케이스의 장식 효과가 생긴다면)는 `app->playing` 가지에만 둔다. Play와 Stop은 살아 있는 효과를 모두 지운다 |
| Undo | 효과는 undo 대상이 아니다. Post-processing 설정은 다른 에디터 설정처럼 undo되지 않는다 |
| 스트레스 씬 | **Effects** 워크로드: 살아 있는 파티클 목표 (0–4M, 그 수를 유지하도록 폭발을 터뜨림), 초당 폭발 수 (0–200), 날아다니는 미사일 수 (궤적과 연기, 0–2,000), 빔 수 (0–500), 데칼 수. 통계: 살아 있는 파티클, 선분, 데칼, bloom GPU 시간. 벤치마크에 효과 단계를 더한다 |
| Debug export | `_app_debug_vfx(n)` (0 파티클, 1 선분, 2 데칼, 3 효과 수), `_app_debug_vfx_fire(effect)`, `_app_debug_set_post(tone, exposure, bloom, intensity)` |

### 엔진 API (초안)

```c
#define NV_VFX_MAX_EFFECTS            1024
#define NV_VFX_MAX_EMITTERS           4096
#define NV_VFX_MAX_EMITTERS_PER_EFFECT 8

typedef enum NvVfxBlend { NV_VFX_BLEND_ADD, NV_VFX_BLEND_ALPHA } NvVfxBlend;
typedef enum NvVfxShape { NV_VFX_SHAPE_DISC, NV_VFX_SHAPE_RING, NV_VFX_SHAPE_STREAK, NV_VFX_SHAPE_PUFF } NvVfxShape;

typedef struct NvVfxEmitterDesc { // one stream of particles
    u32 count;                  // particles per burst
    f32 delay;                  // seconds after the effect fires
    f32 life_min, life_max;     // seconds
    f32 speed_min, speed_max;   // m/s
    f32 cone;                   // radians around the direction; pi = every direction
    f32 gravity;                // m/s^2, down
    f32 drag;                   // 1/s, velocity decays by exp(-drag t)
    f32 size_start, size_end;   // meters
    f32 colors[3][4];           // start, middle, end; linear, may exceed 1 (bloom)
    f32 ground;                 // particles stop at this height; -inf = none
    f32 stretch;                // 0 = square, else length per m/s of screen velocity
    NvVfxShape shape;
    NvVfxBlend blend;
} NvVfxEmitterDesc;

typedef struct NvVfxEffectDesc {
    char name[32];
    u32 emitter_count;
    NvVfxEmitterDesc emitters[NV_VFX_MAX_EMITTERS_PER_EFFECT];
} NvVfxEffectDesc;

typedef struct NvVfxCapacity { // chosen by the app per device; zero fields take the defaults
    u32 particles;             // alive at once, all effects combined
    f32 additive_share;        // of `particles`, for the additive ring
    u32 bursts, segments, decals;
} NvVfxCapacity;

typedef struct NvVfxEffectId { u32 index; } NvVfxEffectId; // 0 = none

void          nv_vfx_init(NvVfx* vfx, NvGpu* gpu, NvVfxCapacity capacity, NvArena* arena);
NvVfxEffectId nv_vfx_add_effect(NvVfx* vfx, const NvVfxEffectDesc* desc);
void nv_vfx_update(NvVfx* vfx, f32 dt);  // game time: pauses and slow motion follow it
void nv_vfx_burst(NvVfx* vfx, NvVfxEffectId effect, NvVec3 position, NvVec3 direction, f32 scale);
void nv_vfx_emit(NvVfx* vfx, NvVfxEffectId effect, NvVec3 from, NvVec3 to, u32 count);
void nv_vfx_trail(NvVfx* vfx, const NvVfxLineStyle* style, NvVec3 from, NvVec3 to);
void nv_vfx_beam(NvVfx* vfx, const NvVfxLineStyle* style, NvVec3 from, NvVec3 to, f32 seconds);
void nv_vfx_decal(NvVfx* vfx, const NvVfxDecalStyle* style, NvVec3 position, f32 angle, f32 size);
void nv_vfx_clear(NvVfx* vfx);            // Play and Stop
```

`NvRenderer`에 `vfx` (포인터, NULL = 없음)와 `post` (`NvPostSettings`: 톤 매핑, 노출, bloom, bloom 세기)가 생기고, 앱이
`shadows`처럼 설정한다.

### 확인할 결정

1. **파티클 시뮬레이션:** 상태 없는 GPU 파티클 (추천) 또는 compute 시뮬레이션.
2. **스프라이트:** 첫 단계는 셰이더가 그리는 모양 (추천, 에셋 없음) 또는 Kenney Particle Pack (CC0) 같은 텍스처 아틀라스.
3. **톤 매핑 기본값:** PBR Neutral (추천: 지금 쇼케이스의 모습을 거의 유지하면서 밝은 효과를 부드럽게 누른다) 또는 Clamp (지금과 같은 모습) 또는 ACES.
4. **범위:** 다섯 부분 (HDR과 톤 매핑, bloom, 파티클, 궤적과 빔, 데칼)을 이 스펙 하나로 (추천) 또는 따로.
5. **기본 용량:** 동시에 살아 있는 파티클 데스크톱 2M, 폰 256K (추천; 스트레스 씬의 측정으로 다시 정한다).

### 변경

- **엔진.** `nv/vfx.h`, `engine/src/vfx.c` (효과와 이미터 표, 링, 버스트 기록, 세 파이프라인 묶음, 그리기), `nv/renderer.h`와
  `renderer.c` (`RGBA16Float` 씬 타깃, `NvPostSettings`, 업스케일 패스의 노출과 톤 매핑과 bloom 합성, bloom 체인과 그 패스,
  타임스탬프 8개), `engine/src/imgui.c` (float 텍스처 미리보기 확인).
- **앱.** View 탭의 Post-processing과 Effects 섹션 (`app/ui.c`), 쇼케이스의 시험 효과 정의 (`app/main.c`), 저장 태그
  (`app/save.c`, `save.md`), 스트레스 워크로드와 통계와 벤치마크 (`app/stress.c`), 한국어 문자열 (`app/strings.c`),
  Debug export.
- **테스트.** `tests/vfx_test.c` (Node): 링 할당, 덮어쓰기, 살아 있는 구간의 이진 탐색, 구간 경계.
- **문서.** `msaa.md`와 `resolution.md` (씬 타깃 형식), `textures.md`, `stress.md`, `save.md`, `autobattler.md` (8번의 compute),
  `AGENTS.md`, README.

### 단계

1. **HDR과 톤 매핑:** `RGBA16Float` 씬 타깃, 업스케일 패스의 노출과 톤 매핑, View 탭의 행과 저장 태그. 확인: Clamp와 노출 1에서
   정지 프레임이 바꾸기 전과 같다 (픽셀 비교, MSAA 켜고 끄고); PBR Neutral에서 0.76 아래의 픽셀이 거의 같다; Textures 탭이 float
   타깃을 보여 준다; 크기 조절과 해상도 모드 전환에서 WebGPU 오류 없음.
2. **bloom:** 체인, 패스, 합성, 설정, 타임스탬프. 확인: 밝은 (HDR) 시험 물체 둘레에 번짐이 생기고 끄면 사라진다; 1/1부터 1/4까지와
   Fixed 모드에서 동작; 체인이 매 프레임 다시 만들어지지 않는다 (로그).
3. **파티클:** 효과와 이미터 표, 버스트 기록, 링, 상태 없는 셰이더, 두 블렌드 모드, 셰이더 모양, 늘이기, 지면, 시험 버튼, 일시정지 (dt 0)에서 멈춤.
   확인: 각 시험 효과의 스크린샷; 용량을 넘는 생성이 가장 오래된 것을 덮어쓴다 (Debug export의 수); 프레임마다 올리는 바이트가
   새 버스트와 새 파티클 수에 비례한다; 스트레스 씬에서 100만 개와 그 이상을 데스크톱과 폰 크기에서 띄우고 프레임 시간을 기록한다.
4. **궤적, 빔, 데칼:** 선분과 데칼 링, 시험 버튼 (Missile, Laser, Scorch). 확인: 스크린샷, 수명 끝의 흐려짐, 데칼이 지면에서
   깜박이지 않음 (z-fighting).
5. **비용과 문서:** 스트레스 워크로드, 통계, 벤치마크, 폰 크기 확인, 위의 문서들.

모든 단계는 헤드리스 Chromium에서 Release와 Debug로, 데스크톱과 폰 크기에서 확인한다. SwiftShader의 시간은 의미가 없다; 실제
비용은 장치에서 나온다.

### 범위 밖

- compute 시뮬레이션, 상태에 따른 힘, 지면 외의 충돌과 튕김.
- 정렬과 순서와 무관한 투명도 (OIT), 소프트 파티클 (깊이 읽기가 필요).
- 빛을 받는 파티클, 그림자를 드리우는 파티클, 열기 왜곡 (굴절).
- 메시와 지형에 투영하는 데칼.
- 효과 편집기와 효과 데이터 파일 (`autobattler.md`의 4번에서).
- 이동 흐림, 피사계 심도, 색 보정 (LUT), FSR.

## English

Status: draft (2026-10-01). Nothing is built until the "Decisions to confirm" below are agreed.

### Goal

Item 8 of `docs/autobattler.md`: add the visual effects an auto-battler's battles need to the engine.

- **Particles**: explosions, sparks, smoke, debris, muzzle flashes.
- **Trails and beams**: missile and tracer trails, laser beams.
- **Decals**: scorch marks and impact marks on the ground.
- **HDR, tone mapping and bloom**: so bright effects glow.

The target scale is about 1,000 units and 5,000 projectiles on screen at once (step 3 of `autobattler.md`), affordable on a
phone.

Effects are appearance only: the game simulation (item 1 of `autobattler.md`) never reads them, and they need not be
deterministic.

### Today

- The scene pass draws opaque meshes and alpha-blended debug lines only; there is no other pipeline for transparent things.
- The scene color target is the canvas's format (`BGRA8Unorm`, an sRGB view). Brightness above 1 is cut, so there are no HDR
  values for bloom to use.
- The upscale pass copies the scene color into the canvas with the nearest filter (`resolution.md`). There is no
  post-processing pass.
- There is no compute pass at all.

### Third-party candidates

| Candidate | What it is | Language, license | Fit | Trade-offs |
|---|---|---|---|---|
| **Write it ourselves** (recommended) | The design below: stateless GPU particles, instanced segments, ground decals, a bloom chain | C17 and WGSL | Fits the engine's renderer, arenas and fixed capacities as they are. Estimated at about 1,500 lines | No effect editor: effects are defined as a data table in code |
| Effekseer (EffekseerForWeb) | A complete particle effect runtime with an editor. It has a WebAssembly build; its WebGPU backend is Dawn-based and experimental | C++, MIT | Plays effects made in its editor as they are | A large C++ runtime with its own graphics abstraction (LLGI) and its own GPU resources, so the wrapper that fits it to our passes, MSAA, resolution and depth rules (reverse Z) is large. A second C++ file, an experimental backend |
| PopcornFX | Commercial particle middleware | C++, commercial | | Not open source |
| Khronos PBR Neutral tone mapper | A published tone mapping formula (about 15 lines) | Reference code Apache-2.0 | Ported to WGSL by hand. Leaves colors below 0.76 nearly as they are | A formula, not a library: its source is cited in a comment |
| Jimenez 2014 bloom (Call of Duty: Advanced Warfare, a SIGGRAPH talk) | A downsample/upsample mip chain bloom technique | A talk (no code) | The standard method, done with render passes only | Ours to write |

Recommendation: write it ourselves. Tone mapping and bloom follow published techniques, cited in comments. No new library.

### Approaches: particle simulation

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **Stateless GPU particles, one record per burst** (recommended) | When an effect fires, the CPU uploads one burst record per emitter (emitter, position, direction, scale, birth time, seed, first slot, count; 64 bytes) and writes the burst's number (4 bytes) into each particle slot it got. The vertex shader finds the burst from the slot, makes hash random numbers from the particle's index in the burst and the seed for its starting values (velocity, lifetime, size), then computes its position in closed form from the current time: gravity, drag (exponential decay), stopping at a ground height, size and color curves over time | No compute pass. A particle costs 4 bytes of GPU memory and upload, so a million cost 4 MB. Pausing or slowing time follows directly. Enough for explosions, sparks, smoke and debris | Forces that depend on state (attraction, bouncing after a collision, interacting with other things) cannot be expressed |
| Stateless GPU particles, a spawn record per particle (the earlier draft) | The CPU makes and uploads a 48-byte record (position, velocity, lifetime, ...) per particle | A slightly simpler shader | 48 MB for a million, and the CPU makes hundreds of thousands of records a second with random numbers. At millions, one record per burst is better |
| Compute shader simulation | A compute pass integrates a particle state buffer every frame; spawning and dying use an atomic counter and a free list | Any force is possible | A compute pass, indirect draws, atomics and free-list management: the engine's first compute code, beyond what an auto-battler's effects need |
| CPU simulation | Integrate in wasm and upload everything every frame | Simplest | At a million particles, about 32 MB uploaded per frame plus a large CPU time. Not possible at millions |

Recommendation: stateless GPU particles with one record per burst. A compute simulation is added beside this design when a
state-dependent effect is needed (the burst records and the effect table carry over). This decision changes "GPU particles (compute shaders)" in
`autobattler.md`.

### Decisions

#### Common

| Topic | Decision |
|---|---|
| Module | `nv/vfx.h`, `engine/src/vfx.c`: the effect table, particles, segments (trails and beams), decals. The renderer draws it inside the scene pass when `NvRenderer.vfx` is set. HDR, tone mapping and bloom go in the renderer (`renderer.c`) |
| Time | `nv_vfx_update(vfx, dt)` advances the effects' clock. The app passes the game clock's dt, so a pause freezes effects and slow motion slows them (item 9 of `autobattler.md`) |
| Effects and emitters | An effect is a group of at most 8 emitters (an explosion = flash + sparks + smoke + debris). An emitter is one stream of particles, a row of `NvVfxEmitterDesc`: the count per burst, lifetime range, speed range and cone, gravity, drag, start and end size, three colors (start, middle, end; HDR values allowed), shape, blend mode, stretching along velocity, ground height, a start delay within the effect. At most 1,024 effects (`NV_VFX_MAX_EFFECTS`) and 4,096 emitters (`NV_VFX_MAX_EMITTERS`). The emitter table is one GPU storage buffer (about 128 bytes per emitter, 512 KB at most), so raising the counts costs little. The app registers them at start. Data files are item 4 of `autobattler.md` (data-driven definitions) |
| Calls | `nv_vfx_burst(vfx, effect, position, direction, scale)`: one burst. `nv_vfx_emit(vfx, effect, from, to, count)`: spread evenly along the stretch moved this tick (a missile's smoke trail). No handles and nothing to free: everything disappears when its lifetime ends |
| Draw order | Inside the scene pass: opaque meshes → decals → debug lines → segments → alpha particles → additive particles. All test depth and none write it |
| Sorting | None. Additive blending does not depend on order. Alpha blending (smoke) is used at low opacity so order errors do not show. Order-independent transparency (OIT) is out of scope |
| Lighting and shadows | Effects are unlit, cast and receive no shadows, and are not pickable |
| MSAA and resolution | Effects are drawn in the scene pass, so they follow the scene's resolution and sample count (`resolution.md`, `msaa.md`) |
| Third-party | None |

#### Particles

| Topic | Decision |
|---|---|
| Capacity | The number of particles alive at once, all effects combined. Not a constant: the app passes it to `nv_vfx_init`, by default 2,097,152 (2M) on the desktop and 262,144 on a phone. The upper bound is the device's `maxStorageBufferBindingSize` (128 MiB by WebGPU's default); a particle is 4 bytes, so about 33 million fit. The real limit is the drawing cost (below), not memory |
| Allocation | Two rings, one per blend mode, split as the app chooses (by default 3/4 additive, 1/4 alpha). When an effect fires, each emitter gets a contiguous range of slots from its ring; when full, the oldest particles are overwritten. No atomics and no GPU readback. Burst records sit in their own ring (at most 65,536, 4 MB) |
| Upload | Only the new burst records and the new slots' burst numbers are uploaded each frame with `wgpuQueueWriteBuffer` (twice when a ring wraps). Even with a million particles all replaced every 2 seconds, that is about 35 KB per frame |
| Live range | The CPU keeps each burst's birth time and longest lifetime. The range drawn runs from the first slot of the oldest burst still alive to the ring's head. A particle inside it whose lifetime has ended is dropped by the vertex shader (size 0). Long-lived smoke holding later short sparks inside the range adds the vertex cost of dead particles: if measured to matter, the rings are split by lifetime class |
| Drawing | Vertex pulling instead of instancing: the range's N particles are one `Draw(6N)`, and the vertex shader finds the slot as `vertex_index / 6`. Small instances of 4 to 6 vertices can fill the GPU's vertex waves poorly, which is why particles are commonly drawn this way |
| Drawing cost | The vertex shader computes every particle in the range every frame (a million = 6 million vertices), and overlapping pixels spend fill rate; the overlap of large additive particles is usually the first limit. Millions are expected on a desktop GPU and a few hundred thousand on a phone; both are measured with the stress scene (below) |
| Shapes | The first step draws them in the shader with no texture: a soft disc, a ring (shockwave), a streak stretched by velocity (sparks), a noisy puff (smoke). A sprite atlas comes later in the same shape-number slots |
| Facing | Quads face the camera. Effects with stretching are lengthened along their screen-space velocity (the velocity is the closed form's derivative) |
| Ground | An effect may have a ground height: particles go no lower and stay there (debris falls to the ground). Bouncing is out of scope |

#### Trails and beams

| Topic | Decision |
|---|---|
| Form | Both are camera-facing segment instances: two end points, width, color, birth time, lifetime. Capacity set by the app (262,144 by default; 48 bytes a segment, 12 MB), the same ring scheme |
| Trails | `nv_vfx_trail(vfx, style, from, to)`: the app calls it for each stretch a missile moved this tick. Each stretch adds a segment that thins and fades over its lifetime. A smoke trail adds `nv_vfx_emit` on the same stretch |
| Beams | `nv_vfx_beam(vfx, style, from, to, seconds)`: a segment held between two points for a time, like a laser; the shader scrolls and flickers it. When it must follow moving units, the app calls it again each tick with a short lifetime |

#### Decals

| Topic | Decision |
|---|---|
| Form | Quad instances on the ground plane: position, rotation, size, shape, color, birth time, lifetime (fading at the end). Capacity set by the app (16,384 by default), the same ring scheme |
| Scope | Flat ground only (`autobattler.md`: the battlefield is flat). Decals projected onto meshes or uneven terrain are out of scope: they need to read the depth buffer, and a multisampled depth cannot be read in the same pass |
| Depth | They overlap the ground, so a depth bias draws them on top. With reverse Z the bias's sign is the opposite of the standard one |

#### HDR, tone mapping and bloom

| Topic | Decision |
|---|---|
| Scene color | The scene color target and the 4-sample color target become `RGBA16Float`. WebGPU core can render, blend, multisample and resolve it. Values are stored linear, so `msaa.md`'s "made like the canvas" (the sRGB view) is no longer needed for this target |
| Memory | Color targets go from 4 to 8 bytes per pixel. At 1/2 on a 3× phone (585 × 500), the 4-sample color goes from 4.5 MB to 9 MB, the scene color from 1.1 MB to 2.3 MB |
| Tone mapping | Done in the upscale pass: multiply by the exposure, tone map, write to the canvas's sRGB view. Choices: **Clamp** (today's look: cut at 1), **PBR Neutral** (Khronos; nearly unchanged below 0.76, only the bright end is compressed smoothly), **ACES** (Narkowicz's fit; a contrasty film look). The default is decision 3 below |
| Bloom | Jimenez 2014: six downsample steps halving from the scene color (a 13-tap filter, with a Karis average on the first step against fireflies), then summed back up with a tent filter. The upscale pass mixes the result in by an intensity (0.04 by default). No threshold: only places with large HDR values spread visibly. Each step is a small render pass (no compute) |
| Bloom targets | An `RGBA16Float` mip chain starting at 1/2 of the scene's resolution, rounded up to 64 and kept while large enough, like the scene targets |
| Cost shown | One more timestamp pair around the bloom passes (`NV_TIMESTAMP_COUNT` 6 → 8). The Stress tab and the benchmark show it |
| Textures tab | The bloom chain's steps are listed under Render targets. `RGBA16Float` is filterable, so the existing preview path shows them (values shown clipped) |

#### App

| Topic | Decision |
|---|---|
| View tab | A **Post-processing** section: Tone mapping (Clamp, PBR Neutral, ACES), Exposure (0.25 to 4), Bloom (on/off), Bloom intensity (0 to 0.2). An **Effects** section: a test button per registered effect (Explosion, Sparks, Smoke, Missile, Laser, Scorch) fires it at the orbit point; the counts of live particles, segments and decals. Every row goes through `search_row`, and strings go through `T`/`TL` with Korean rows |
| Saved | New `EDIT` tags: `TONE` (u32: 0 Clamp, 1 PBR Neutral, 2 ACES), `EXPO` (f32), `BLOM` (u32), `BLMI` (f32). Live effects are not saved |
| Edit and Play | Test buttons work in Edit mode too: a one-off preview the user presses, like the animation preview. Continuous effects (if the showcase gets decorative ones) go in the `app->playing` branch only. Play and Stop clear all live effects |
| Undo | Effects are not undoable; the Post-processing settings are not undoable, like other editor settings |
| Stress scene | An **Effects** workload: a target of live particles (0 to 4M, kept by firing explosions), explosions per second (0 to 200), flying missiles (trails and smoke, 0 to 2,000), beams (0 to 500), decals. Stats: live particles, segments, decals, bloom GPU time. The benchmark gains effect steps |
| Debug exports | `_app_debug_vfx(n)` (0 particles, 1 segments, 2 decals, 3 effect count), `_app_debug_vfx_fire(effect)`, `_app_debug_set_post(tone, exposure, bloom, intensity)` |

### Engine API (draft)

```c
#define NV_VFX_MAX_EFFECTS            1024
#define NV_VFX_MAX_EMITTERS           4096
#define NV_VFX_MAX_EMITTERS_PER_EFFECT 8

typedef enum NvVfxBlend { NV_VFX_BLEND_ADD, NV_VFX_BLEND_ALPHA } NvVfxBlend;
typedef enum NvVfxShape { NV_VFX_SHAPE_DISC, NV_VFX_SHAPE_RING, NV_VFX_SHAPE_STREAK, NV_VFX_SHAPE_PUFF } NvVfxShape;

typedef struct NvVfxEmitterDesc { // one stream of particles
    u32 count;                  // particles per burst
    f32 delay;                  // seconds after the effect fires
    f32 life_min, life_max;     // seconds
    f32 speed_min, speed_max;   // m/s
    f32 cone;                   // radians around the direction; pi = every direction
    f32 gravity;                // m/s^2, down
    f32 drag;                   // 1/s, velocity decays by exp(-drag t)
    f32 size_start, size_end;   // meters
    f32 colors[3][4];           // start, middle, end; linear, may exceed 1 (bloom)
    f32 ground;                 // particles stop at this height; -inf = none
    f32 stretch;                // 0 = square, else length per m/s of screen velocity
    NvVfxShape shape;
    NvVfxBlend blend;
} NvVfxEmitterDesc;

typedef struct NvVfxEffectDesc {
    char name[32];
    u32 emitter_count;
    NvVfxEmitterDesc emitters[NV_VFX_MAX_EMITTERS_PER_EFFECT];
} NvVfxEffectDesc;

typedef struct NvVfxCapacity { // chosen by the app per device; zero fields take the defaults
    u32 particles;             // alive at once, all effects combined
    f32 additive_share;        // of `particles`, for the additive ring
    u32 bursts, segments, decals;
} NvVfxCapacity;

typedef struct NvVfxEffectId { u32 index; } NvVfxEffectId; // 0 = none

void          nv_vfx_init(NvVfx* vfx, NvGpu* gpu, NvVfxCapacity capacity, NvArena* arena);
NvVfxEffectId nv_vfx_add_effect(NvVfx* vfx, const NvVfxEffectDesc* desc);
void nv_vfx_update(NvVfx* vfx, f32 dt);  // game time: pauses and slow motion follow it
void nv_vfx_burst(NvVfx* vfx, NvVfxEffectId effect, NvVec3 position, NvVec3 direction, f32 scale);
void nv_vfx_emit(NvVfx* vfx, NvVfxEffectId effect, NvVec3 from, NvVec3 to, u32 count);
void nv_vfx_trail(NvVfx* vfx, const NvVfxLineStyle* style, NvVec3 from, NvVec3 to);
void nv_vfx_beam(NvVfx* vfx, const NvVfxLineStyle* style, NvVec3 from, NvVec3 to, f32 seconds);
void nv_vfx_decal(NvVfx* vfx, const NvVfxDecalStyle* style, NvVec3 position, f32 angle, f32 size);
void nv_vfx_clear(NvVfx* vfx);            // Play and Stop
```

`NvRenderer` gains `vfx` (a pointer, NULL = none) and `post` (`NvPostSettings`: tone, exposure, bloom, bloom_intensity), set
by the app like `shadows`.

### Decisions to confirm

1. **Particle simulation:** stateless GPU particles (recommended) or a compute simulation.
2. **Sprites:** shapes drawn by the shader in the first step (recommended, no assets) or a texture atlas such as Kenney's
   Particle Pack (CC0).
3. **Tone mapping default:** PBR Neutral (recommended: keeps today's showcase nearly as it looks while compressing bright
   effects smoothly), Clamp (exactly today's look) or ACES.
4. **Scope:** the five parts (HDR and tone mapping, bloom, particles, trails and beams, decals) in this one spec
   (recommended) or separately.
5. **Default capacity:** 2M particles alive at once on the desktop, 256K on a phone (recommended; revisited with the
   stress scene's measurements).

### Changes

- **Engine.** `nv/vfx.h`, `engine/src/vfx.c` (the effect and emitter tables, rings, burst records, three sets of pipelines, drawing);
  `nv/renderer.h` and `renderer.c` (`RGBA16Float` scene targets, `NvPostSettings`, exposure, tone mapping and the bloom
  composite in the upscale pass, the bloom chain and its passes, 8 timestamps); `engine/src/imgui.c` (float texture previews
  checked).
- **App.** The View tab's Post-processing and Effects sections (`app/ui.c`), the showcase's test effect definitions
  (`app/main.c`), the save tags (`app/save.c`, `save.md`), the stress workload, stats and benchmark (`app/stress.c`), Korean
  strings (`app/strings.c`), Debug exports.
- **Tests.** `tests/vfx_test.c` (Node): ring allocation, overwriting, the live range's binary search, range edges.
- **Docs.** `msaa.md` and `resolution.md` (the scene target format), `textures.md`, `stress.md`, `save.md`, `autobattler.md`
  (item 8's compute), `AGENTS.md`, README.

### Phases

1. **HDR and tone mapping:** the `RGBA16Float` scene targets, exposure and tone mapping in the upscale pass, the View tab
   rows and save tags. Checked: with Clamp and exposure 1 a still frame equals the one before the change (pixel comparison,
   MSAA on and off); with PBR Neutral pixels below 0.76 are nearly equal; the Textures tab shows the float targets; resizing
   and switching resolution modes raise no WebGPU errors.
2. **Bloom:** the chain, its passes, the composite, the settings, the timestamps. Checked: a glow appears around a bright
   (HDR) test object and goes away when off; works from 1/1 to 1/4 and in Fixed mode; the chain is not remade every frame
   (the log).
3. **Particles:** the effect and emitter tables, burst records, rings, the stateless shader, both blend modes, shader shapes, stretching, ground, the
   test buttons, freezing when paused (dt 0). Checked: screenshots of each test effect; spawning past capacity overwrites
   the oldest (the Debug export's counts); the bytes uploaded per frame follow the new bursts and particles; the stress scene
   holds a million particles and more at desktop and phone size, and the frame times are recorded.
4. **Trails, beams and decals:** the segment and decal rings, test buttons (Missile, Laser, Scorch). Checked: screenshots,
   fading at the end of a lifetime, no flicker of decals on the ground (z-fighting).
5. **Cost and docs:** the stress workload, stats, benchmark, a phone-size check, the documents above.

Every phase is checked in Release and Debug in headless Chromium, at desktop and phone size. SwiftShader's timings mean
nothing; real costs come from devices.

### Out of scope

- A compute simulation, state-dependent forces, collisions and bouncing other than the ground.
- Sorting and order-independent transparency (OIT), soft particles (they need to read depth).
- Lit particles, particles casting shadows, heat distortion (refraction).
- Decals projected onto meshes and terrain.
- An effect editor and effect data files (item 4 of `autobattler.md`).
- Motion blur, depth of field, color grading (LUTs), FSR.

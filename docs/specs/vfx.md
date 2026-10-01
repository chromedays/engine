# Visual effects spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 초안 (2026-10-01; 파티클은 compute 시뮬레이션으로 결정). 아래 "확인할 결정"이 합의되기 전에는 구현하지 않는다.

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
- compute 패스는 하나도 없다. 이 스펙이 첫 compute 패스를 더한다.

### 서드파티 후보

| 후보 | 무엇인가 | 언어, 라이선스 | 맞음 | 장단점 |
|---|---|---|---|---|
| **직접 작성** (추천) | 아래 설계: compute로 시뮬레이션하는 GPU 파티클, 인스턴스 선분, 지면 데칼, bloom 체인 | C17과 WGSL | 엔진의 렌더러, 아레나, 고정 용량에 그대로 들어간다. 약 2,000줄로 추정 | 효과 편집기가 없다: 효과는 코드 안의 데이터 표로 정의한다 |
| Effekseer (EffekseerForWeb) | 편집기가 딸린 완전한 파티클 효과 런타임. WebAssembly 빌드가 있고, WebGPU 백엔드는 Dawn 기반의 실험 단계다 | C++, MIT | 편집기에서 만든 효과를 그대로 재생한다 | 자체 그래픽 추상화 (LLGI)와 자체 GPU 리소스를 가진 큰 C++ 런타임이라, 우리의 패스, MSAA, 해상도, 깊이 규칙 (reverse Z)과 맞추는 래퍼가 크다. 두 번째 C++ 파일, 실험 단계 백엔드 |
| PopcornFX | 상용 파티클 미들웨어 | C++, 상용 | | 오픈 소스가 아니다 |
| Khronos PBR Neutral 톤 매퍼 | 공개된 톤 매핑 공식 (약 15줄) | 참조 코드 Apache-2.0 | WGSL로 직접 옮겨 쓴다. 0.76 아래의 색은 거의 그대로 둔다 | 라이브러리가 아니라 공식이다: 출처를 주석에 남긴다 |
| Jimenez 2014 bloom (Call of Duty: Advanced Warfare, SIGGRAPH 발표) | 다운샘플/업샘플 밉 체인 bloom 기법 | 발표 자료 (코드 아님) | 표준적인 방법이고 렌더 패스만으로 된다 | 우리가 쓴다 |

추천: 직접 작성한다. 톤 매핑과 bloom은 공개 기법을 따르고 출처를 주석에 남긴다. 새 라이브러리는 없다.

### 접근법: 파티클 시뮬레이션

| 접근법 | 무엇인가 | 맞음 | 장단점 |
|---|---|---|---|
| **compute 셰이더 시뮬레이션** (결정) | 파티클 상태 버퍼를 compute 패스가 매 프레임 적분한다. 생성은 빈 목록에서 번호를 꺼내고, 소멸은 번호를 빈 목록에 돌려놓는다. 살아 있는 파티클은 매 프레임 새 목록에 모이고, 그 수가 간접 그리기의 인자가 된다 | 어떤 힘이든 가능하다: 중력, 공기 저항, 바람, 난류 (curl noise), 지면 충돌과 튕김. 죽은 파티클은 그리지 않는다. 나중의 GPU 정렬도 같은 틀에 들어간다 | 엔진의 첫 compute 코드 (compute 파이프라인, 간접 dispatch와 draw, atomic). 살아 있는 파티클마다 매 프레임 상태를 읽고 쓰는 메모리 대역폭 (아래 "비용") |
| 상태 없는 GPU 파티클, 버스트 단위 기록 (이전 초안) | CPU가 버스트 기록만 올리고, 정점 셰이더가 시각으로 위치를 닫힌 식으로 계산한다 | compute가 없고, 파티클당 4바이트 | 상태에 따른 힘 (충돌 후 튕김, 난류의 누적)을 표현할 수 없고, 수명이 끝난 파티클도 그리기 구간에 남아 정점 비용을 낸다 |
| CPU 시뮬레이션 | wasm에서 적분하고 매 프레임 전체를 올린다 | 가장 단순 | 100만 개면 프레임마다 약 32 MB 업로드와 큰 CPU 시간. 백만 단위에서는 불가능하다 |

결정 (2026-10-01): compute 셰이더 시뮬레이션. 구조는 널리 쓰이는 GPU 파티클 방식을 따른다: 빈 목록, 번갈아 쓰는 살아 있는
목록 두 개, GPU가 채우는 간접 인자 (예: Gareth Thomas, "Compute-based GPU Particle Systems", AMD, GDC 2014). `autobattler.md`의
"GPU 파티클 (compute shader)"과 같은 방향이다.

### 결정

#### 공통

| 주제 | 결정 |
|---|---|
| 모듈 | `nv/vfx.h`, `engine/src/vfx.c`: 효과 표, 파티클, 선분 (궤적과 빔), 데칼. 렌더러는 `NvRenderer.vfx`가 있으면 씬 패스 안에서 그것을 그린다. HDR, 톤 매핑, bloom은 렌더러에 들어간다 (`renderer.c`) |
| 시각 | `nv_vfx_update(vfx, dt)`가 효과의 시각을 진행한다. 앱은 게임 시각의 dt를 넘기므로, 일시정지는 효과를 멈추고 슬로 모션은 효과를 느리게 한다 (`autobattler.md`의 9번) |
| 효과와 이미터 | 효과는 이미터 최대 8개의 묶음이다 (폭발 = 섬광 + 불꽃 + 연기 + 파편). 이미터는 파티클 흐름 하나로, `NvVfxEmitterDesc`의 행이다: 한 번에 나오는 수, 수명 범위, 속도 범위와 원뿔, 중력, 공기 저항, 난류, 지면 충돌 (높이, 반발, 마찰), 크기 시작과 끝, 색 세 점 (시작, 중간, 끝; HDR 값 허용), 모양, 블렌드 모드, 속도 방향 늘이기, 효과 안에서의 시작 지연. 효과 최대 1,024개 (`NV_VFX_MAX_EFFECTS`), 이미터 최대 4,096개 (`NV_VFX_MAX_EMITTERS`). 이미터 표는 GPU의 스토리지 버퍼 하나 (이미터당 약 128바이트, 최대 512 KB)라서 수를 늘리는 비용이 작다. 앱이 시작할 때 등록한다. 데이터 파일은 `autobattler.md`의 4번 (데이터 기반 정의)에서 다룬다 |
| 호출 | `nv_vfx_burst(vfx, effect, position, direction, scale)`: 한 번에 터뜨리기. `nv_vfx_emit(vfx, effect, from, to, count)`: 이번 틱에 움직인 구간을 따라 고르게 내보내기 (미사일 연기 궤적). 핸들도 해제도 없다: 모든 것은 수명이 끝나면 사라진다 |
| 그리기 순서 | 씬 패스 안에서: 불투명 메시 → 데칼 → 디버그 라인 → 선분 → 알파 파티클 → 가산 파티클. 모두 깊이 테스트를 하고 깊이를 쓰지 않는다 |
| 정렬 | 하지 않는다. 가산 블렌딩은 순서와 무관하다. 알파 블렌딩 (연기)은 낮은 불투명도로 써서 순서 오류가 눈에 띄지 않게 한다 (파티클의 "정렬" 참고). 순서와 무관한 투명도 (OIT)는 범위 밖 |
| 조명과 그림자 | 효과는 빛을 받지 않고 (unlit) 그림자를 드리우지도 받지도 않는다. picking에 걸리지 않는다 |
| MSAA와 해상도 | 효과는 씬 패스 안에 그리므로 씬의 해상도와 샘플 수를 따른다 (`resolution.md`, `msaa.md`) |
| 서드파티 | 없음 |

#### 파티클

| 주제 | 결정 |
|---|---|
| 용량 | 동시에 살아 있는 파티클 수, 모든 효과의 합계. 고정 상수가 아니라 앱이 `nv_vfx_init`에 넘긴다: 기본값은 데스크톱 2,097,152 (2M), 폰 262,144 |
| 파티클 상태 | 32바이트: 위치 (f32 셋), 나이 (f32), 속도와 수명 (f16 넷), 이미터 번호와 시드 (u32), 크기와 회전 (f16 둘). f16은 WGSL core의 `pack2x16float`와 `unpack2x16float`로 다루므로 `shader-f16` 기능이 필요 없다 |
| 버퍼 | 상태; 빈 목록 (번호 스택); 살아 있는 목록 A와 B (프레임마다 번갈아); 보이는 목록 (이번 프레임에 그릴 것; 블렌드 모드별 구간 둘); 카운터와 간접 인자 (빈 번호 수, 살아 있는 수, 이번 프레임에 버려진 생성 수, `dispatchWorkgroupsIndirect`와 `drawIndirect`의 인자). 모두 GPU에만 있다. 시작할 때 빈 목록을 0..N−1로 채운다 |
| 메모리 | 파티클 하나에 48바이트 (상태 32, 빈 목록 4, 살아 있는 목록 둘 8, 보이는 목록 4): 2M에 96 MB, 256K에 12 MB. 상태 버퍼는 바인딩 하나에 들어가야 하므로, WebGPU 기본 `maxStorageBufferBindingSize` (128 MiB)에서는 약 400만 개가 상한이다. 앱이 그보다 크게 요청하면 `gpu.c`가 어댑터가 허락하는 한도까지 올려 장치를 만든다 |
| 생성 (emit 패스) | 효과를 터뜨리면 CPU는 이미터마다 버스트 기록 하나 (이미터, 위치, 방향, 배율, 시드, 수; 48바이트)를 큐에 넣는다. 지연이 있는 이미터는 그 시각이 될 때까지 CPU 큐에 머문다. 프레임마다 CPU가 버스트들과 작업 목록 (작업 그룹마다 버스트 번호와 그 안의 시작 순번)을 올리고, 생성 수를 알므로 직접 dispatch한다. 스레드마다 빈 목록에서 번호를 꺼내 (atomic), 이미터 설정과 해시 난수로 초기 상태를 쓰고, 살아 있는 목록에 넣는다. 빈 번호가 없으면 그 생성은 버려지고 수를 센다: 가장 오래된 것을 덮어쓰지 않는다 |
| 시뮬레이션 (simulate 패스) | 지난 프레임의 살아 있는 목록 위의 간접 dispatch. 스레드마다 나이를 dt만큼 늘리고, 수명이 끝났으면 번호를 빈 목록에 돌려놓는다; 아니면 힘을 적분하고 (반암시적 오일러) 다음 살아 있는 목록에 넣는다. 그다음 frustum culling: 파티클의 경계 구 (위치와 크기)가 씬 카메라의 frustum 여섯 평면 안에 있으면 블렌드 모드에 맞는 보이는 목록 구간에도 넣는다. 새로 생긴 파티클은 같은 프레임의 목록에 들어가므로 생긴 프레임부터 보인다 |
| 힘 | 이미터별: 중력, 공기 저항, 난류 (curl noise; 세기와 크기), 지면 충돌 (높이, 반발 계수, 마찰; 거의 멈추면 그 자리에 머문다). 전역: 바람 (`NvVfx.wind`). 끌어당기는 점과 파티클끼리의 상호작용은 범위 밖 |
| frustum culling | Wicked Engine처럼 simulate 안에서 한다 (위). 카메라 밖의 파티클은 시뮬레이션은 계속되지만 정점도 픽셀도 쓰지 않는다. RTS 카메라는 전장의 일부만 보이는 때가 많아서 효과가 크다. frustum은 씬 패스와 같은 카메라와 씬 해상도의 종횡비로 렌더러가 만든다. 그림자 패스는 파티클을 그리지 않으므로 빛의 frustum은 필요 없다 |
| atomic 줄이기 | 모든 스레드가 전역 카운터 하나에 atomic을 하면 경합이 생긴다. 작업 그룹 (64 스레드) 안에서 workgroup 메모리로 먼저 세고, 작업 그룹마다 전역 atomic 한 번으로 구간을 받는다. 64는 wave32와 wave64 모두의 배수라서 어느 쪽이든 웨이브를 채운다 |
| 순서 | 한 command encoder 안에서: emit → simulate → prepare (간접 인자를 채우는 1스레드 패스) → 그림자 패스 → 씬 패스. WebGPU가 패스 사이의 버퍼 사용을 맞춰 주므로 배리어를 따로 두지 않는다. 렌더러가 `nv_renderer_draw` 안에서 `NvRenderer.vfx`의 compute 패스를 기록한다 |
| 시각 | dt가 0 (일시정지)이면 simulate를 건너뛰고 목록을 바꾸지 않는다. 슬로 모션은 작은 dt다. 큰 dt (탭이 숨었다 돌아옴)는 0.1초로 자른다 |
| 그리기 | vertex pulling: 블렌드 모드마다 `drawIndirect` 한 번 (정점 수 = 6 × 보이는 수). 정점 셰이더가 `vertex_index / 6`으로 보이는 목록에서 번호를 찾는다. 정점 4–6개짜리 작은 인스턴스는 GPU의 정점 웨이브를 덜 채울 수 있어서 인스턴싱 대신 이 방식을 쓴다 |
| 통계 | 살아 있는 수, 보이는 수, 버려진 생성 수는 GPU에만 있으므로, 타임스탬프처럼 작은 버퍼로 복사해 비동기로 읽는다 (한두 프레임 늦음). Effects 섹션과 Stress 탭이 보여 준다 |
| 비용 | compute는 살아 있는 파티클마다 매 프레임 상태 32바이트를 읽고 쓰고, 그리기가 다시 읽는다: 2M이면 프레임당 약 190 MB, 60 fps에서 초당 약 11 GB. 데스크톱 GPU에는 감당할 만하지만 폰에는 크다 (폰의 기본값이 256K인 이유). 그다음 한계는 겹친 큰 가산 파티클의 fill rate다. compute 패스에 타임스탬프 쌍을 하나 더하고, 둘 다 스트레스 씬으로 잰다 |
| 모양 | 첫 단계는 텍스처 없이 셰이더가 그린다: 부드러운 원, 고리 (충격파), 속도로 늘인 줄 (불꽃), 노이즈 덩어리 (연기). 스프라이트 아틀라스는 나중에 같은 모양 번호 자리에 더한다 |
| 카메라 정렬 | 사각형은 카메라를 향한다. 늘이기가 켜진 이미터는 상태의 속도를 화면 공간으로 옮긴 방향으로 늘인다 |
| 정렬 | 알파 파티클의 목록 순서는 atomic 때문에 프레임마다 바뀌므로, 정렬 없이 겹친 연기는 깜박일 수 있다. 낮은 불투명도로 줄이고, 눈에 띄면 GPU 정렬 (bitonic 또는 radix, compute)을 다음 단계로 더한다 |

#### 궤적과 빔

| 주제 | 결정 |
|---|---|
| 형태 | 둘 다 카메라를 향한 선분 인스턴스다: 끝점 둘, 폭, 색, 생성 시각, 수명. 용량은 앱이 정한다 (기본 262,144; 선분 하나 48바이트, 12 MB). 링 버퍼: CPU가 새 선분만 올리고, 가득 차면 가장 오래된 것을 덮어쓴다. 시뮬레이션이 없으므로 compute를 쓰지 않는다 |
| 궤적 | `nv_vfx_trail(vfx, style, from, to)`: 앱이 미사일의 이번 틱 이동 구간마다 부른다. 구간마다 선분 하나가 생기고 수명 동안 가늘어지며 흐려진다. 연기 궤적은 같은 구간에 `nv_vfx_emit`을 함께 쓴다 |
| 빔 | `nv_vfx_beam(vfx, style, from, to, seconds)`: 레이저처럼 두 점 사이에 일정 시간 유지되는 선분. 셰이더가 결을 흐르게 하고 깜박이게 한다. 움직이는 유닛을 따라가야 하면 앱이 매 틱 짧은 수명으로 다시 부른다 |

#### 데칼

| 주제 | 결정 |
|---|---|
| 형태 | 지면 평면 위의 사각형 인스턴스: 위치, 회전, 크기, 모양, 색, 생성 시각, 수명 (끝에서 흐려짐). 용량은 앱이 정한다 (기본 16,384). 선분과 같은 링 버퍼 |
| 범위 | 평평한 지면 위에만 놓는다 (`autobattler.md`: 전장이 평평하다). 메시나 울퉁불퉁한 지형에 투영하는 데칼은 범위 밖: 깊이 버퍼를 읽어야 하는데, 다중 샘플 깊이는 같은 패스에서 읽을 수 없다 |
| 깊이 | 지면과 겹치므로 깊이 바이어스로 지면 위에 그린다. reverse Z이므로 바이어스 부호가 표준과 반대다 |

#### HDR, 톤 매핑, bloom

| 주제 | 결정 |
|---|---|
| 씬 색 | 씬 색 타깃과 4-샘플 색 타깃을 `RGBA16Float`로 바꾼다. WebGPU core에서 렌더링, 블렌딩, 다중 샘플, resolve가 모두 된다. 값이 선형으로 저장되므로 `msaa.md`의 "캔버스처럼 만들기" (sRGB 뷰)는 이 타깃에 더 이상 필요 없다 |
| 메모리 | 색 타깃이 픽셀당 4바이트에서 8바이트가 된다. 3× 폰의 1/2 (585 × 500)에서 4-샘플 색 4.5 MB → 9 MB, 씬 색 1.1 MB → 2.3 MB |
| 톤 매핑 | 업스케일 패스에서 한다: 노출을 곱하고, 톤 매핑하고, 캔버스의 sRGB 뷰에 쓴다. 선택지: **Clamp** (지금의 모습: 1에서 자름), **PBR Neutral** (Khronos; 0.76 아래는 거의 그대로이고 밝은 쪽만 부드럽게 누름), **ACES** (Narkowicz의 근사식; 대비가 강한 영화풍). 기본값은 확인할 결정 2 |
| bloom | Jimenez 2014 방식: 씬 색에서 시작해 반씩 줄이는 다운샘플 6단계 (13탭 필터, 첫 단계는 반짝이는 점을 막는 Karis 평균), 그다음 텐트 필터로 올라오며 더한다. 업스케일 패스가 결과를 세기 (기본 0.04)만큼 섞는다. 임계값은 두지 않는다: HDR 값이 큰 곳만 눈에 띄게 번진다. 각 단계는 작은 렌더 패스다 (compute 없음) |
| bloom 타깃 | `RGBA16Float` 밉 체인, 씬 해상도의 1/2부터. 씬 타깃처럼 64로 올림하고 충분히 크면 유지한다 |
| 비용 표시 | bloom 패스들에 타임스탬프 쌍 하나를 더한다 (파티클 compute 패스의 쌍과 함께 `NV_TIMESTAMP_COUNT` 6 → 10). Stress 탭과 벤치마크가 그것을 보여 준다 |
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
    f32 turbulence, turbulence_scale; // curl noise: m/s^2, feature size in meters
    f32 size_start, size_end;   // meters
    f32 colors[3][4];           // start, middle, end; linear, may exceed 1 (bloom)
    f32 ground;                 // collision height; -inf = none
    f32 restitution, friction;  // at the ground: share of normal speed kept, of tangential speed lost
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
    u32 bursts;                // queued per frame
    u32 segments, decals;
} NvVfxCapacity;

typedef struct NvVfxEffectId { u32 index; } NvVfxEffectId; // 0 = none

void          nv_vfx_init(NvVfx* vfx, NvGpu* gpu, NvVfxCapacity capacity, NvArena* arena);
NvVfxEffectId nv_vfx_add_effect(NvVfx* vfx, const NvVfxEffectDesc* desc);
void nv_vfx_update(NvVfx* vfx, f32 dt);  // game time: pauses and slow motion follow it
// The renderer records the emit, simulate and prepare passes in nv_renderer_draw.
NvVfxStats nv_vfx_stats(const NvVfx* vfx); // living, dropped: read back, a frame or two late
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

1. **스프라이트:** 첫 단계는 셰이더가 그리는 모양 (추천, 에셋 없음) 또는 Kenney Particle Pack (CC0) 같은 텍스처 아틀라스.
2. **톤 매핑 기본값:** PBR Neutral (추천: 지금 쇼케이스의 모습을 거의 유지하면서 밝은 효과를 부드럽게 누른다) 또는 Clamp (지금과 같은 모습) 또는 ACES.
3. **범위:** 다섯 부분 (HDR과 톤 매핑, bloom, 파티클, 궤적과 빔, 데칼)을 이 스펙 하나로 (추천) 또는 따로.
4. **기본 용량:** 동시에 살아 있는 파티클 데스크톱 2M, 폰 256K (추천; 스트레스 씬의 측정으로 다시 정한다).

파티클 시뮬레이션은 compute로 정해졌다 (2026-10-01).

### 변경

- **엔진.** `nv/vfx.h`, `engine/src/vfx.c` (효과와 이미터 표, 버스트 큐, 파티클 버퍼, emit, simulate, prepare compute 패스, 선분과 데칼 링, 그리기), `nv/renderer.h`와
  `renderer.c` (`RGBA16Float` 씬 타깃, `NvPostSettings`, 업스케일 패스의 노출과 톤 매핑과 bloom 합성, bloom 체인과 그 패스,
  타임스탬프 10개, 첫 compute 파이프라인), `engine/src/gpu.c` (큰 파티클 용량을 위한 장치 한도 요청), `engine/src/imgui.c` (float 텍스처 미리보기 확인).
- **앱.** View 탭의 Post-processing과 Effects 섹션 (`app/ui.c`), 쇼케이스의 시험 효과 정의 (`app/main.c`), 저장 태그
  (`app/save.c`, `save.md`), 스트레스 워크로드와 통계와 벤치마크 (`app/stress.c`), 한국어 문자열 (`app/strings.c`),
  Debug export.
- **테스트.** `tests/vfx_test.c` (Node): 버스트를 작업 그룹 작업으로 나누기 (지연, 경계), 선분과 데칼 링의 할당과 덮어쓰기. GPU 쪽 (emit, simulate, 목록)은 브라우저 테스트로 확인한다.
- **문서.** `msaa.md`와 `resolution.md` (씬 타깃 형식), `textures.md`, `stress.md`, `save.md`, `AGENTS.md`, README.

### 단계

1. **HDR과 톤 매핑:** `RGBA16Float` 씬 타깃, 업스케일 패스의 노출과 톤 매핑, View 탭의 행과 저장 태그. 확인: Clamp와 노출 1에서
   정지 프레임이 바꾸기 전과 같다 (픽셀 비교, MSAA 켜고 끄고); PBR Neutral에서 0.76 아래의 픽셀이 거의 같다; Textures 탭이 float
   타깃을 보여 준다; 크기 조절과 해상도 모드 전환에서 WebGPU 오류 없음.
2. **bloom:** 체인, 패스, 합성, 설정, 타임스탬프. 확인: 밝은 (HDR) 시험 물체 둘레에 번짐이 생기고 끄면 사라진다; 1/1부터 1/4까지와
   Fixed 모드에서 동작; 체인이 매 프레임 다시 만들어지지 않는다 (로그).
3. **파티클:** compute 파이프라인, 파티클 버퍼와 빈 목록, emit, simulate, prepare, 간접 그리기, 효과와 이미터 표, 두 블렌드 모드, 힘 (중력, 공기 저항,
   난류, 바람, 지면 충돌), 셰이더 모양, 늘이기, 비동기 통계, 시험 버튼, 일시정지 (dt 0)에서 멈춤. 확인: 각 시험 효과의 스크린샷;
   터뜨린 수만큼 살아 있는 수가 늘고 수명이 끝나면 0으로 돌아간다 (빈 목록이 새지 않는다); 용량을 넘는 생성은 버려지고 그 수가 보인다;
   일시정지에서 화면이 그대로다; 파편이 지면에서 튕기고 멈춘다; 스트레스 씬에서 100만 개와 그 이상을 데스크톱과 폰 크기에서 띄우고 프레임 시간을 기록한다.
4. **궤적, 빔, 데칼:** 선분과 데칼 링, 시험 버튼 (Missile, Laser, Scorch). 확인: 스크린샷, 수명 끝의 흐려짐, 데칼이 지면에서
   깜박이지 않음 (z-fighting).
5. **비용과 문서:** 스트레스 워크로드, 통계, 벤치마크, 폰 크기 확인, 위의 문서들.

모든 단계는 헤드리스 Chromium에서 Release와 Debug로, 데스크톱과 폰 크기에서 확인한다. SwiftShader의 시간은 의미가 없다; 실제
비용은 장치에서 나온다.

### 범위 밖

- 끌어당기는 점, 파티클끼리의 상호작용, 지면 외의 충돌 (유닛, 지형).
- GPU 정렬 (다음 단계), 순서와 무관한 투명도 (OIT), 소프트 파티클 (깊이 읽기가 필요).
- 빛을 받는 파티클, 그림자를 드리우는 파티클, 열기 왜곡 (굴절).
- 메시와 지형에 투영하는 데칼.
- 효과 편집기와 효과 데이터 파일 (`autobattler.md`의 4번에서).
- 이동 흐림, 피사계 심도, 색 보정 (LUT), FSR.

### 참고 자료

- **설계의 기반:** Gareth Thomas (AMD), "Compute-Based GPU Particle Systems", GDC 2014
  ([GDC Vault](https://gdcvault.com/play/1020002/Advanced-Visual-Effects-with-DirectX)). 파티클 풀, 빈 목록, 살아 있는 목록,
  emit와 simulate 커널, 깊이 버퍼 충돌, bitonic 정렬, 타일 렌더링.
- **구현의 참고:** Wicked Engine 소스 ([GitHub](https://github.com/turanszkij/WickedEngine), MIT): `wiEmittedParticle.cpp`,
  `shaders/emittedparticle_emitCS.hlsl`, `emittedparticle_kickoffUpdateCS.hlsl`, `emittedparticle_simulateCS.hlsl`,
  `emittedparticle_finishUpdateCS.hlsl`. 같은 구조에 번갈아 쓰는 살아 있는 목록, 1스레드 간접 인자 패스, simulate 안의
  frustum culling, GPU radix 정렬 (`wiGPUSortLib`)이 더해져 있다. 코드를 가져오지 않고 구조만 참고한다. 다른 점: 이미터마다
  버퍼와 dispatch가 따로이고 (우리는 전역 풀 하나), 스레드마다 전역 atomic을 한다 (우리는 작업 그룹 단위).
- **나중 단계:** AMD FidelityFX Parallel Sort ([GPUOpen](https://gpuopen.com/fidelityfx-parallel-sort/), MIT; GPU 정렬),
  Bill Rockenbeck, "Blowing from the West: Simulating Wind in Ghost of Tsushima", GDC 2021
  ([GDC Vault](https://gdcvault.com/play/1027124/Blowing-from-the-West-Simulating); 바람장), Brandon Whitley, "The Destiny
  Particle Architecture", SIGGRAPH 2017 ([요약](https://80.lv/articles/siggraph-the-destiny-particle-architecture); 데이터 기반
  저작).

## English

Status: draft (2026-10-01; particles decided as a compute simulation). Nothing is built until the "Decisions to confirm"
below are agreed.

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
- There is no compute pass at all. This spec adds the first.

### Third-party candidates

| Candidate | What it is | Language, license | Fit | Trade-offs |
|---|---|---|---|---|
| **Write it ourselves** (recommended) | The design below: GPU particles simulated in compute, instanced segments, ground decals, a bloom chain | C17 and WGSL | Fits the engine's renderer, arenas and fixed capacities as they are. Estimated at about 2,000 lines | No effect editor: effects are defined as a data table in code |
| Effekseer (EffekseerForWeb) | A complete particle effect runtime with an editor. It has a WebAssembly build; its WebGPU backend is Dawn-based and experimental | C++, MIT | Plays effects made in its editor as they are | A large C++ runtime with its own graphics abstraction (LLGI) and its own GPU resources, so the wrapper that fits it to our passes, MSAA, resolution and depth rules (reverse Z) is large. A second C++ file, an experimental backend |
| PopcornFX | Commercial particle middleware | C++, commercial | | Not open source |
| Khronos PBR Neutral tone mapper | A published tone mapping formula (about 15 lines) | Reference code Apache-2.0 | Ported to WGSL by hand. Leaves colors below 0.76 nearly as they are | A formula, not a library: its source is cited in a comment |
| Jimenez 2014 bloom (Call of Duty: Advanced Warfare, a SIGGRAPH talk) | A downsample/upsample mip chain bloom technique | A talk (no code) | The standard method, done with render passes only | Ours to write |

Recommendation: write it ourselves. Tone mapping and bloom follow published techniques, cited in comments. No new library.

### Approaches: particle simulation

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **Compute shader simulation** (decided) | A compute pass integrates a particle state buffer every frame. Spawning takes an index from a free list and dying puts it back. Living particles gather into a new list every frame, and its count becomes the indirect draw's argument | Any force is possible: gravity, drag, wind, turbulence (curl noise), ground collision and bouncing. Dead particles are not drawn. GPU sorting later fits the same frame | The engine's first compute code (compute pipelines, indirect dispatch and draw, atomics). The memory bandwidth of reading and writing every living particle's state every frame ("Cost" below) |
| Stateless GPU particles, one record per burst (the earlier draft) | The CPU uploads burst records only, and the vertex shader computes positions in closed form from the time | No compute, 4 bytes per particle | Cannot express state-dependent forces (bouncing after a collision, accumulated turbulence), and particles past their lifetime stay in the drawn range and cost vertices |
| CPU simulation | Integrate in wasm and upload everything every frame | Simplest | At a million particles, about 32 MB uploaded per frame plus a large CPU time. Not possible at millions |

Decided (2026-10-01): a compute shader simulation. Its structure follows the common GPU particle design: a free list, two
living lists used in turn, and indirect arguments filled by the GPU (for example Gareth Thomas, "Compute-based GPU Particle
Systems", AMD, GDC 2014). It matches "GPU particles (compute shaders)" in `autobattler.md`.

### Decisions

#### Common

| Topic | Decision |
|---|---|
| Module | `nv/vfx.h`, `engine/src/vfx.c`: the effect table, particles, segments (trails and beams), decals. The renderer draws it inside the scene pass when `NvRenderer.vfx` is set. HDR, tone mapping and bloom go in the renderer (`renderer.c`) |
| Time | `nv_vfx_update(vfx, dt)` advances the effects' clock. The app passes the game clock's dt, so a pause freezes effects and slow motion slows them (item 9 of `autobattler.md`) |
| Effects and emitters | An effect is a group of at most 8 emitters (an explosion = flash + sparks + smoke + debris). An emitter is one stream of particles, a row of `NvVfxEmitterDesc`: the count per burst, lifetime range, speed range and cone, gravity, drag, turbulence, ground collision (height, restitution, friction), start and end size, three colors (start, middle, end; HDR values allowed), shape, blend mode, stretching along velocity, a start delay within the effect. At most 1,024 effects (`NV_VFX_MAX_EFFECTS`) and 4,096 emitters (`NV_VFX_MAX_EMITTERS`). The emitter table is one GPU storage buffer (about 128 bytes per emitter, 512 KB at most), so raising the counts costs little. The app registers them at start. Data files are item 4 of `autobattler.md` (data-driven definitions) |
| Calls | `nv_vfx_burst(vfx, effect, position, direction, scale)`: one burst. `nv_vfx_emit(vfx, effect, from, to, count)`: spread evenly along the stretch moved this tick (a missile's smoke trail). No handles and nothing to free: everything disappears when its lifetime ends |
| Draw order | Inside the scene pass: opaque meshes → decals → debug lines → segments → alpha particles → additive particles. All test depth and none write it |
| Sorting | None. Additive blending does not depend on order. Alpha blending (smoke) is used at low opacity so order errors do not show (see "Sorting" under Particles). Order-independent transparency (OIT) is out of scope |
| Lighting and shadows | Effects are unlit, cast and receive no shadows, and are not pickable |
| MSAA and resolution | Effects are drawn in the scene pass, so they follow the scene's resolution and sample count (`resolution.md`, `msaa.md`) |
| Third-party | None |

#### Particles

| Topic | Decision |
|---|---|
| Capacity | The number of particles alive at once, all effects combined. Not a constant: the app passes it to `nv_vfx_init`, by default 2,097,152 (2M) on the desktop and 262,144 on a phone |
| Particle state | 32 bytes: position (three f32), age (f32), velocity and lifetime (four f16), emitter index and seed (u32), size and rotation (two f16). The f16 values go through WGSL core's `pack2x16float` and `unpack2x16float`, so the `shader-f16` feature is not needed |
| Buffers | The state; a free list (a stack of indices); living lists A and B (used in turn each frame); a visible list (what this frame draws, with a range per blend mode); counters and indirect arguments (free count, living counts, spawns dropped this frame, the arguments of `dispatchWorkgroupsIndirect` and `drawIndirect`). All live on the GPU only. At start the free list is filled with 0..N−1 |
| Memory | 48 bytes per particle (state 32, free list 4, the two living lists 8, the visible list 4): 96 MB for 2M, 12 MB for 256K. The state buffer must fit one binding, so with WebGPU's default `maxStorageBufferBindingSize` (128 MiB) the bound is about 4 million. When the app asks for more, `gpu.c` creates the device with the limit raised as far as the adapter allows |
| Spawning (emit pass) | When an effect fires, the CPU queues one burst record per emitter (emitter, position, direction, scale, seed, count; 48 bytes); an emitter with a delay waits in the CPU queue until its time. Each frame the CPU uploads the bursts and a job list (per workgroup: the burst index and the starting index within it), and dispatches directly, since it knows the spawn count. Each thread pops an index from the free list (atomic), writes the starting state from the emitter's settings and hash random numbers, and appends it to the living list. With no free index left, the spawn is dropped and counted: the oldest is not overwritten |
| Simulation (simulate pass) | An indirect dispatch over last frame's living list. Each thread adds dt to the age; past the lifetime it pushes the index back on the free list; otherwise it integrates the forces (semi-implicit Euler) and appends the particle to the next living list. Then frustum culling: when the particle's bounding sphere (position and size) is inside the scene camera's six frustum planes, it is also appended to the visible list's range for its blend mode. New particles join the same frame's list, so they show from the frame they are born |
| Forces | Per emitter: gravity, drag, turbulence (curl noise; strength and scale), ground collision (height, restitution, friction; a particle that nearly stops stays put). Global: wind (`NvVfx.wind`). Attractors and particles interacting with each other are out of scope |
| Frustum culling | Done inside simulate, as Wicked Engine does (above). Particles outside the camera keep simulating but cost no vertices or pixels. An RTS camera often sees only part of the battlefield, so this pays off. The renderer builds the frustum from the scene pass's camera and the scene resolution's aspect ratio. The shadow pass draws no particles, so no light frustum is needed |
| Fewer atomics | Every thread hitting one global counter contends. A workgroup (64 threads) counts in workgroup memory first and takes its range with one global atomic. 64 is a multiple of both wave32 and wave64, so it fills waves either way |
| Order | In one command encoder: emit → simulate → prepare (a one-thread pass that fills the indirect arguments) → shadow pass → scene pass. WebGPU synchronizes buffer use between passes, so there are no explicit barriers. The renderer records `NvRenderer.vfx`'s compute passes inside `nv_renderer_draw` |
| Time | With dt 0 (paused) simulate is skipped and the lists stay as they are. Slow motion is a small dt. A large dt (a tab hidden and back) is cut to 0.1 s |
| Drawing | Vertex pulling: one `drawIndirect` per blend mode (vertex count = 6 × visible count). The vertex shader finds the index in the visible list as `vertex_index / 6`. Small instances of 4 to 6 vertices can fill the GPU's vertex waves poorly, so this is used instead of instancing |
| Stats | The living and visible counts and dropped spawns live on the GPU, so they are copied to a small buffer and read asynchronously, like the timestamps (a frame or two late). The Effects section and the Stress tab show them |
| Cost | Compute reads and writes 32 bytes of state per living particle every frame, and drawing reads it again: about 190 MB per frame at 2M, about 11 GB/s at 60 fps. Fine for a desktop GPU, heavy for a phone (which is why the phone's default is 256K). The next limit is the fill rate of large overlapping additive particles. A timestamp pair is added for the compute passes; both are measured with the stress scene |
| Shapes | The first step draws them in the shader with no texture: a soft disc, a ring (shockwave), a streak stretched by velocity (sparks), a noisy puff (smoke). A sprite atlas comes later in the same shape-number slots |
| Facing | Quads face the camera. Emitters with stretching are lengthened along the state's velocity taken to screen space |
| Sorting | The living list's order changes every frame with the atomics, so overlapping unsorted smoke can flicker. Low opacity hides it; if it shows, GPU sorting (bitonic or radix, in compute) is the next step |

#### Trails and beams

| Topic | Decision |
|---|---|
| Form | Both are camera-facing segment instances: two end points, width, color, birth time, lifetime. Capacity set by the app (262,144 by default; 48 bytes a segment, 12 MB). A ring buffer: the CPU uploads only new segments and, when full, overwrites the oldest. Nothing is simulated, so no compute |
| Trails | `nv_vfx_trail(vfx, style, from, to)`: the app calls it for each stretch a missile moved this tick. Each stretch adds a segment that thins and fades over its lifetime. A smoke trail adds `nv_vfx_emit` on the same stretch |
| Beams | `nv_vfx_beam(vfx, style, from, to, seconds)`: a segment held between two points for a time, like a laser; the shader scrolls and flickers it. When it must follow moving units, the app calls it again each tick with a short lifetime |

#### Decals

| Topic | Decision |
|---|---|
| Form | Quad instances on the ground plane: position, rotation, size, shape, color, birth time, lifetime (fading at the end). Capacity set by the app (16,384 by default). The same ring buffer as segments |
| Scope | Flat ground only (`autobattler.md`: the battlefield is flat). Decals projected onto meshes or uneven terrain are out of scope: they need to read the depth buffer, and a multisampled depth cannot be read in the same pass |
| Depth | They overlap the ground, so a depth bias draws them on top. With reverse Z the bias's sign is the opposite of the standard one |

#### HDR, tone mapping and bloom

| Topic | Decision |
|---|---|
| Scene color | The scene color target and the 4-sample color target become `RGBA16Float`. WebGPU core can render, blend, multisample and resolve it. Values are stored linear, so `msaa.md`'s "made like the canvas" (the sRGB view) is no longer needed for this target |
| Memory | Color targets go from 4 to 8 bytes per pixel. At 1/2 on a 3× phone (585 × 500), the 4-sample color goes from 4.5 MB to 9 MB, the scene color from 1.1 MB to 2.3 MB |
| Tone mapping | Done in the upscale pass: multiply by the exposure, tone map, write to the canvas's sRGB view. Choices: **Clamp** (today's look: cut at 1), **PBR Neutral** (Khronos; nearly unchanged below 0.76, only the bright end is compressed smoothly), **ACES** (Narkowicz's fit; a contrasty film look). The default is decision 2 below |
| Bloom | Jimenez 2014: six downsample steps halving from the scene color (a 13-tap filter, with a Karis average on the first step against fireflies), then summed back up with a tent filter. The upscale pass mixes the result in by an intensity (0.04 by default). No threshold: only places with large HDR values spread visibly. Each step is a small render pass (no compute) |
| Bloom targets | An `RGBA16Float` mip chain starting at 1/2 of the scene's resolution, rounded up to 64 and kept while large enough, like the scene targets |
| Cost shown | One more timestamp pair around the bloom passes (`NV_TIMESTAMP_COUNT` 6 → 10 with the particle compute passes' pair). The Stress tab and the benchmark show it |
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
    f32 turbulence, turbulence_scale; // curl noise: m/s^2, feature size in meters
    f32 size_start, size_end;   // meters
    f32 colors[3][4];           // start, middle, end; linear, may exceed 1 (bloom)
    f32 ground;                 // collision height; -inf = none
    f32 restitution, friction;  // at the ground: share of normal speed kept, of tangential speed lost
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
    u32 bursts;                // queued per frame
    u32 segments, decals;
} NvVfxCapacity;

typedef struct NvVfxEffectId { u32 index; } NvVfxEffectId; // 0 = none

void          nv_vfx_init(NvVfx* vfx, NvGpu* gpu, NvVfxCapacity capacity, NvArena* arena);
NvVfxEffectId nv_vfx_add_effect(NvVfx* vfx, const NvVfxEffectDesc* desc);
void nv_vfx_update(NvVfx* vfx, f32 dt);  // game time: pauses and slow motion follow it
// The renderer records the emit, simulate and prepare passes in nv_renderer_draw.
NvVfxStats nv_vfx_stats(const NvVfx* vfx); // living, dropped: read back, a frame or two late
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

1. **Sprites:** shapes drawn by the shader in the first step (recommended, no assets) or a texture atlas such as Kenney's
   Particle Pack (CC0).
2. **Tone mapping default:** PBR Neutral (recommended: keeps today's showcase nearly as it looks while compressing bright
   effects smoothly), Clamp (exactly today's look) or ACES.
3. **Scope:** the five parts (HDR and tone mapping, bloom, particles, trails and beams, decals) in this one spec
   (recommended) or separately.
4. **Default capacity:** 2M particles alive at once on the desktop, 256K on a phone (recommended; revisited with the
   stress scene's measurements).

The particle simulation is decided: compute (2026-10-01).

### Changes

- **Engine.** `nv/vfx.h`, `engine/src/vfx.c` (the effect and emitter tables, the burst queue, particle buffers, the emit, simulate and prepare compute passes, segment and decal rings, drawing);
  `nv/renderer.h` and `renderer.c` (`RGBA16Float` scene targets, `NvPostSettings`, exposure, tone mapping and the bloom
  composite in the upscale pass, the bloom chain and its passes, 10 timestamps, the first compute pipelines); `engine/src/gpu.c` (device limits raised for large particle capacities); `engine/src/imgui.c` (float texture previews
  checked).
- **App.** The View tab's Post-processing and Effects sections (`app/ui.c`), the showcase's test effect definitions
  (`app/main.c`), the save tags (`app/save.c`, `save.md`), the stress workload, stats and benchmark (`app/stress.c`), Korean
  strings (`app/strings.c`), Debug exports.
- **Tests.** `tests/vfx_test.c` (Node): splitting bursts into workgroup jobs (delays, edges), allocating and overwriting in the segment and decal rings. The GPU side (emit, simulate, lists) is checked in browser tests.
- **Docs.** `msaa.md` and `resolution.md` (the scene target format), `textures.md`, `stress.md`, `save.md`, `AGENTS.md`, README.

### Phases

1. **HDR and tone mapping:** the `RGBA16Float` scene targets, exposure and tone mapping in the upscale pass, the View tab
   rows and save tags. Checked: with Clamp and exposure 1 a still frame equals the one before the change (pixel comparison,
   MSAA on and off); with PBR Neutral pixels below 0.76 are nearly equal; the Textures tab shows the float targets; resizing
   and switching resolution modes raise no WebGPU errors.
2. **Bloom:** the chain, its passes, the composite, the settings, the timestamps. Checked: a glow appears around a bright
   (HDR) test object and goes away when off; works from 1/1 to 1/4 and in Fixed mode; the chain is not remade every frame
   (the log).
3. **Particles:** compute pipelines, the particle buffers and free list, emit, simulate, prepare, indirect drawing, the effect
   and emitter tables, both blend modes, the forces (gravity, drag, turbulence, wind, ground collision), shader shapes,
   stretching, async stats, the test buttons, freezing when paused (dt 0). Checked: screenshots of each test effect; the
   living count rises by what was fired and returns to 0 when lifetimes end (the free list does not leak); spawns past
   capacity are dropped and their count shows; a paused frame stays the same; debris bounces on the ground and stops; the stress scene
   holds a million particles and more at desktop and phone size, and the frame times are recorded.
4. **Trails, beams and decals:** the segment and decal rings, test buttons (Missile, Laser, Scorch). Checked: screenshots,
   fading at the end of a lifetime, no flicker of decals on the ground (z-fighting).
5. **Cost and docs:** the stress workload, stats, benchmark, a phone-size check, the documents above.

Every phase is checked in Release and Debug in headless Chromium, at desktop and phone size. SwiftShader's timings mean
nothing; real costs come from devices.

### Out of scope

- Attractors, particles interacting with each other, collisions other than the ground (units, terrain).
- GPU sorting (a next step), order-independent transparency (OIT), soft particles (they need to read depth).
- Lit particles, particles casting shadows, heat distortion (refraction).
- Decals projected onto meshes and terrain.
- An effect editor and effect data files (item 4 of `autobattler.md`).
- Motion blur, depth of field, color grading (LUTs), FSR.

### References

- **Design foundation:** Gareth Thomas (AMD), "Compute-Based GPU Particle Systems", GDC 2014
  ([GDC Vault](https://gdcvault.com/play/1020002/Advanced-Visual-Effects-with-DirectX)): the particle pool, free list, living
  list, emit and simulate kernels, depth-buffer collisions, bitonic sorting, tiled rendering.
- **Implementation reference:** the Wicked Engine source ([GitHub](https://github.com/turanszkij/WickedEngine), MIT):
  `wiEmittedParticle.cpp`, `shaders/emittedparticle_emitCS.hlsl`, `emittedparticle_kickoffUpdateCS.hlsl`,
  `emittedparticle_simulateCS.hlsl`, `emittedparticle_finishUpdateCS.hlsl`. The same structure plus living lists used in turn,
  one-thread indirect-argument passes, frustum culling inside simulate and a GPU radix sort (`wiGPUSortLib`). Only the
  structure is followed; no code is taken. Differences: buffers and dispatches per emitter (ours: one global pool), and a
  global atomic per thread (ours: per workgroup).
- **Later steps:** AMD FidelityFX Parallel Sort ([GPUOpen](https://gpuopen.com/fidelityfx-parallel-sort/), MIT; GPU sorting),
  Bill Rockenbeck, "Blowing from the West: Simulating Wind in Ghost of Tsushima", GDC 2021
  ([GDC Vault](https://gdcvault.com/play/1027124/Blowing-from-the-West-Simulating); wind fields), Brandon Whitley, "The
  Destiny Particle Architecture", SIGGRAPH 2017 ([summary](https://80.lv/articles/siggraph-the-destiny-particle-architecture);
  data-driven authoring).

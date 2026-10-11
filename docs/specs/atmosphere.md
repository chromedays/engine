# Atmospheric scattering spec

상태: 제안 (2026-10-11). 공부도 목적이다("공부를 위한 구성"). 아래 "합의할 것"이 정해지면 구현한다. 이 스펙의 변경은 먼저 합의한다.

## 목표

샌드박스의 씬에 물리 기반 대기를 더한다.

- **하늘**: 지금의 단색 배경(`NvRenderer.clear_color`) 대신 해의 방향에 따라 바뀌는 하늘. 낮에는 파랗고, 해가 낮으면 지평선이
  붉어지고, 해 주변에 후광(Mie)이 있고, 해 원반이 보인다. 지평선 아래는 행성의 지면이다.
- **대기 원근(aerial perspective)**: 먼 물체가 하늘 색으로 흐려지고 어두워진다(안개처럼 보이지만 해의 방향에 따라 색이 바뀐다).
- **햇빛의 색**: 해가 지평선에 가까울수록 지면에 닿는 햇빛이 대기를 오래 지나 붉고 어두워진다.
- **하늘의 주변광**(3단계): 고정된 `ambient` 0.15 대신 하늘에서 오는 빛.

해는 지금처럼 씬의 첫 directional light다(`shadows.md`와 같다). 그 노드를 기즈모로 돌리면 시각이 바뀌고, 그 노드에 `spin`을 달면
Play에서 하루가 흐른다. 새 시간 개념은 없다.

## 지금

- 씬 패스는 `clear_color`(0.02, 0.02, 0.035)로 지우고 메시를 그린다(`engine/renderer.c`의 `nv_renderer_draw`).
- 메시 셰이더의 조명은 `frame.ambient.rgb + frame.light_color.rgb * diffuse`이다. 확산 항에 1/π가 없다: 빛의 세기 1은 법선 방향의
  흰 면을 1로 비춘다.
- 씬은 선형 HDR(`NV_SCENE_FORMAT`, `RGBA16Float`)이고 노출, 톤 매핑, bloom이 업스케일 패스에 있다(`vfx.md`). 하늘이 1을 넘어도 된다.
- 깊이는 reverse Z다: 0으로 지우고 `Greater`로 비교한다(`msaa.md`). 하늘처럼 "가장 먼 곳"은 깊이 0이다.
- 카메라의 `far`는 쇼케이스와 스트레스 모두 100 m, 쇼케이스의 바닥은 60 m 정사각형이다. 실제 지구 대기에서 100 m의 대기 원근은
  1% 남짓이라 거의 보이지 않는다(아래 "대기 원근의 거리 배율").
- compute 패스는 파티클이 쓴다(`vfx.c`). 이 스펙은 렌더러에 compute 패스를 더 둔다.

## 서드파티 후보 (AGENTS.md에 따라 먼저 찾음)

| 후보 | 무엇인가 | 언어, 라이선스 | 이 엔진에 맞음 | 장단점 |
|---|---|---|---|---|
| **직접 작성, Hillaire 2020 기법** (권장) | 아래 "접근법"의 LUT 네 장과 하늘 그리기를 C와 WGSL로 쓴다. 논문과 저자의 참조 구현(sebh/UnrealEngineSkyAtmosphere)을 따른다 | C17, WGSL | 렌더러의 패스, MSAA, 자체 해상도, reverse Z, HDR 규칙에 그대로 들어간다. 메시와 이펙트 셰이더에 대기 원근을 직접 넣을 수 있다. 약 800~1,000줄로 추정 | 우리가 쓰고 시험해야 한다. 참조 구현의 코드를 옮기면 MIT 고지를 남긴다(옮기지 않고 논문의 식으로 쓰는 것이 기본) |
| sebh/UnrealEngineSkyAtmosphere | 위 기법의 저자 참조 구현(Unreal Engine의 SkyAtmosphere와 같은 방법) | C++/HLSL(D3D11), MIT | 그대로는 못 쓴다: D3D11 데모다 | 식, 상수, LUT 매개화의 기준으로 쓴다 |
| webgpu-sky-atmosphere (JolifantoBambla) | 위 기법의 WebGPU 포팅. 렌더러 객체가 LUT를 만들고 하늘을 후처리로 그린다 | TypeScript/WGSL, MIT | JS에서 같은 `GPUDevice`를 받아 돌릴 수는 있다 | 장치와 패스가 wasm(emdawnwebgpu) 밖 JS에 생기고, 그리기 순서, MSAA 대상, 자체 해상도, reverse Z를 맞추는 접착 코드가 크다. 후처리 방식이라 깊이를 쓰지 않는 파티클과 데칼은 대기 원근이 틀린다. WGSL은 참고 자료로 쓸 수 있다 |
| Bruneton 2017, precomputed_atmospheric_scattering | 4D 산란 LUT를 미리 계산하는 방법의 참조 구현 | C++/GLSL, BSD | 정확한 다중 산란, 우주에서 보는 시점까지 | 대기 값을 바꿀 때마다 무거운 재계산(데스크톱에서도 수백 ms 이상)과 큰 3D 텍스처. 슬라이더로 대기를 바꾸는 편집기와 폰에 맞지 않는다 |
| Hosek-Wilkie 2012 analytic sky | 하늘 색을 맞춘 해석식(계수 표) | C, BSD-3 | 가장 싸다 | 지면 관찰자의 하늘 색뿐이다. 대기 원근, 햇빛의 색, 해가 지평선 아래인 경우가 없고, 대기 값을 바꿀 수 없다(탁도 하나) |
| Preetham 1999 | 더 오래된 해석식 | 공식 | 단순 | 해질녘이 틀리고, Hosek-Wilkie의 단점을 모두 가진다 |

권장: **직접 작성, Hillaire 2020.** 새 라이브러리는 없다. 기법과 참고한 구현을 코드 주석과 이 스펙의 "참고 자료"에 남긴다.

## 접근법

| 접근법 | 어떻게 | 장점 | 단점 |
|---|---|---|---|
| **LUT 네 장, Hillaire 2020** (권장) | 투과율 LUT와 다중 산란 LUT는 대기 값이 바뀔 때만, 하늘 뷰 LUT와 대기 원근 볼륨은 매 프레임 compute로 만든다. 하늘과 대기 원근은 그 LUT를 읽는다 | 프레임당 비용이 작고 해상도와 무관하다(논문: PS4에서 LUT와 하늘 합쳐 약 0.3 ms). 다중 산란이 근사로 들어가 해질녘과 두꺼운 연무가 그럴듯하다. 모든 값을 실시간으로 바꿀 수 있다 | LUT 해상도가 낮아 해 원반 주변 같은 고주파는 따로 그린다(해 원반은 하늘 패스에서 직접). 대기 원근 볼륨은 거리 방향 32 칸이라 먼 곳의 경계가 부드럽다 |
| 픽셀마다 레이 마칭 (Nishita식 단일 산란) | 하늘 픽셀마다 시선을 따라 적분한다 | 코드가 가장 짧다 | 픽셀마다 수십 걸음 × 해 방향 적분. 폰과 큰 해상도에 비싸고, 다중 산란이 없어 해질녘이 어둡다 |
| 미리 계산한 4D 산란 (Bruneton) | 위 후보 | 정확 | 위 후보의 단점 |
| 해석식 하늘 (Hosek-Wilkie) | 위 후보 | 가장 쌈 | 대기 원근과 햇빛 색이 없다 |

## 공부를 위한 구성

이 기능은 처음 구현하는 기술이라 공부도 목적이다. 그래서 순서와 도구를 다음처럼 정한다.

- **느린 정답부터.** 첫 단계는 LUT 없이 픽셀마다 시선을 적분하는 하늘(기준, reference)이다. 식이 코드에 한 줄씩 그대로 보인다.
  LUT는 그 다음에 하나씩, **같은 답을 빠르게 내는 최적화**로 넣는다. LUT를 하나 넣을 때마다 기준과 화면을 비교해 그 LUT가
  무엇을 바꿨는지(또는 틀렸는지) 본다.
- **기준 모드는 남긴다.** 다 만든 뒤에도 View 탭에서 하늘을 기준(픽셀마다 적분)으로 그릴 수 있다. 다중 산란을 빼면 기준과
  LUT 결과가 가까워야 하고(시험), 다중 산란이 무엇을 더하는지는 둘을 번갈아 보면 보인다.
- **성분 보기.** View 탭의 "Atmosphere view"가 하늘과 대기 원근의 한 항만 보여 준다: 최종, 투과율, 단일 산란, 다중 산란,
  Rayleigh만, Mie만. 저장하지 않는 편집기 도구다.
- **LUT를 눈으로.** Textures 탭이 투과율, 다중 산란, 하늘 뷰 LUT를 보여 준다. 각 LUT의 축이 무엇인지 스펙의 표와 셰이더 주석에 적는다.
- **식과 코드를 잇는다.** 셰이더의 각 함수 위 주석에 아래 "이론 요약"의 번호와 논문의 해당 절을 적는다.
- **CPU에도 한 벌.** 투과율은 C로도 있어(`nv_atmosphere_transmittance`) ctest로 값을 손으로 계산한 답과 맞춘다. 셰이더를 의심할 때
  비교할 기준이다.

## 이론 요약

기호: 시선 위의 점 x, 거리 t, 높이 h(x). σ_s 산란 계수, σ_a 흡수 계수, σ_t = σ_s + σ_a 소멸 계수(1/km). 각 성분의 계수는 지면 값 ×
밀도 d(h)이다: Rayleigh와 Mie는 d = exp(−h / H)(H는 각각 8 km, 1.2 km), 오존은 max(0, 1 − |h − 25| / 15).

1. **투과율.** 두 점 사이를 지나며 남는 빛의 비율. T(a, b) = exp(−∫ₐᵇ σ_t(x) dx). 적분 안의 값을 광학 깊이라 한다.
   파랑의 Rayleigh 계수가 빨강의 약 6배라 긴 경로(해질녘)에서는 빨강이 남는다.
2. **위상 함수.** 빛이 θ만큼 꺾여 산란될 확률 밀도. Rayleigh: p_R(θ) = 3/(16π) (1 + cos²θ). Mie: Cornette-Shanks
   p_M(θ) = 3/(8π) · (1 − g²)(1 + cos²θ) / ((2 + g²)(1 + g² − 2g cosθ)^{3/2}). g가 클수록 앞쪽(해 주변)에 몰린다.
3. **단일 산란.** 시선 위 각 점에서 햇빛이 한 번 꺾여 눈으로 오는 빛의 합.
   L₁ = ∫₀ᵗᵐᵃˣ T(c, x) · Σ_i σ_s,i(x) p_i(θ) · T(x, 해) · E_sun · V(x) dt. V는 행성이 해를 가리는지(0 또는 1).
   기준 모드가 이것을 그대로 적분한다(시선 위 수십 걸음, 각 걸음에서 T(x, 해)는 투과율로).
4. **다중 산란 (Hillaire 근사).** 두 번 이상 꺾인 빛은 방향에 거의 무관하다고 보고(등방), 한 점에서 모든 방향으로 한 번 더
   산란되는 비율 f_ms가 매번 같다고 보면 무한 등비급수가 된다: Ψ_ms = L₂ / (1 − f_ms). 다중 산란 LUT가 높이와 해의 천정각마다
   이 값을 담고, 3번의 적분에 σ_s(x) · Ψ_ms 항이 더해진다(논문의 다중 산란 절).
5. **대기 원근.** 물체까지의 시선에 대해 3, 4번을 적분한 산란 S와 투과율 T. 화면 색은 color · T + S.
6. **지면.** 지평선 아래 시선은 행성에서 끝나고, 그곳의 지면이 반사율 × (햇빛 + 하늘) / π로 빛난다.

LUT는 이 적분들의 결과를 낮은 해상도로 미리 담는 것이다: 투과율 LUT는 1번(해 방향 T를 걸음마다 다시 적분하지 않게), 다중
산란 LUT는 4번, 하늘 뷰 LUT는 3+4번을 시선 방향마다, 대기 원근 볼륨은 5번을 화면 칸과 거리마다.

## 결정 (제안)

### 모델

| 주제 | 결정 |
|---|---|
| 행성 | 반지름 6360 km의 공, 대기의 꼭대기 6460 km. 월드의 y = 0이 지면이고 행성의 중심은 (0, −6360 km, 0)이다. 씬의 1 단위는 1 m |
| 카메라 높이 | 6360 km + max(카메라 y, 0) / 1000 + 0.01 km. 0.01 km(10 m)를 더해 지면에 붙은 카메라에서 LUT 매개화가 무너지지 않게 한다(참조 구현의 `PLANET_RADIUS_OFFSET`) |
| 매질 | Rayleigh(지수 분포, 높이 8 km), Mie(지수 분포, 높이 1.2 km, 산란과 흡수), 오존(흡수만, 25 km를 중심으로 폭 30 km의 삼각 분포). 계수는 엔진 구조체에 물리 값(1/km)으로 둔다. 아래 "지구 기본값" |
| 위상 함수 | Rayleigh: 3/(16π)(1 + cos²θ). Mie: Cornette-Shanks, `g` 하나 |
| 다중 산란 | Hillaire의 근사: 등방 2차 이상 산란을 32 × 32 LUT 하나로(해의 천정각, 높이) |
| 해 | 첫 directional light. 방향은 그 노드의 −Z, 해의 조도는 `light.color × light.intensity`. 그런 빛이 없으면 해가 없는 대기다(하늘이 검다) |
| 단위 | 하늘의 radiance에 **π를 곱한다.** 엔진의 확산 조명에 1/π가 없으므로, 그래야 흰 면(albedo 1)과 하늘이 같은 단위로 비교된다. 하늘이 너무 밝거나 어두운 것은 노출로 맞춘다 |
| 해 원반 | 하늘 패스가 직접 그린다: 시직경 안이면 해의 조도 × 투과율 / 원반의 입체각(× π, 위와 같은 단위), 가장자리 어두워짐(limb darkening) 포함. 밝기가 매우 커서 bloom이 빛나게 한다 |

**지구 기본값** (Hillaire 2020, 참조 구현과 같음; 1/km)

| 값 | R | G | B |
|---|---|---|---|
| Rayleigh 산란 | 5.802e-3 | 13.558e-3 | 33.1e-3 |
| Mie 산란 | 3.996e-3 | 3.996e-3 | 3.996e-3 |
| Mie 흡수 | 0.444e-3 | 0.444e-3 | 0.444e-3 |
| 오존 흡수 | 0.650e-3 | 1.881e-3 | 0.085e-3 |

Mie `g` 0.8, 지면 반사율 0.3.

### LUT와 패스

모든 LUT는 `RGBA16Float`(compute에서 쓰기 전용 스토리지 텍스처로 쓰고, 셰이더에서는 선형 필터로 읽는다. 둘 다 WebGPU 코어에서 된다).

| LUT | 크기 | 매개화 | 언제 만드나 |
|---|---|---|---|
| 투과율 | 256 × 64 | (높이, 시선의 천정각), 참조 구현의 비선형 매핑 | 대기 값이 바뀐 프레임 |
| 다중 산란 | 32 × 32 | (해의 천정각, 높이). 화소마다 방향 64개를 workgroup 하나가 나눠 적분 | 대기 값이 바뀐 프레임, 투과율 다음 |
| 하늘 뷰 | 192 × 108 | (방위각, 지평선 근처를 촘촘히 한 고도각), 카메라 높이와 해 방향 기준 | 매 프레임 |
| 대기 원근 | 32 × 32 × 32 (3D) | 화면 uv × 거리 칸. rgb는 카메라부터 그 칸까지 산란된 빛, a는 투과율(회색 하나) | 매 프레임, 대기 원근이 켜진 동안 |

- "대기 값이 바뀌었다"는 `NvAtmosphere`의 매질 필드를 지난번과 비교해 안다(그림자 맵이 크기와 형식을 비교하는 것과 같다).
  해의 방향과 카메라는 투과율과 다중 산란에 들어가지 않으므로 해를 돌려도 그 둘은 다시 만들지 않는다.
- 네 compute 패스는 파티클 패스와 함께 그림자 패스 전에 기록한다. LUT는 렌더러가 대기를 처음 켤 때 만들고 다시 만들지 않는다
  (크기가 고정이다). 꺼진 동안은 1 × 1 텍스처가 묶인다(bloom의 `bloom_dummy`와 같다).
- 대기 원근의 거리 칸은 Hillaire 데모처럼 제곱 분포다(가까운 곳이 촘촘하다): 칸 k의 끝은 `aerial_distance × ((k + 1) / 32)²`.

### 씬 패스

그리기 순서: 불투명 메시, **하늘**, 데칼, 디버그 라인, 선분, 알파 파티클, 가산 파티클(`vfx.md`의 순서에 하늘이 끼어든다).

| 주제 | 결정 |
|---|---|
| 하늘 | 화면을 덮는 삼각형 하나, 깊이 0(가장 먼 곳)에 두고 비교 `GreaterEqual`, 깊이 쓰기 없음: 메시가 그리지 않은 픽셀만 남는다. 픽셀의 시선(역 view-projection)으로 하늘 뷰 LUT를 읽고 해 원반을 더한다. 대기가 꺼지면 그리지 않고 지금처럼 `clear_color`가 보인다 |
| 파이프라인 | 씬 패스의 다른 파이프라인처럼 `NV_SCENE_FORMAT`, `renderer->scene_samples`. MSAA가 바뀌면 함께 다시 만든다 |
| 대기 원근: 메시 | `fs_main`의 끝에서 `color × T + S`. (T, S)는 대기 원근 볼륨을 화면 uv(프래그먼트 위치 / 씬 해상도: 씬은 큰 타깃의 왼쪽 위에 그려지므로 타깃 크기가 아니다)와 카메라로부터의 거리로 읽는다. 첫 칸보다 가까우면 칸 0까지 선형으로 줄인다 |
| 대기 원근: 이펙트 | 알파 파티클, 데칼, 알파 선분은 `color × T + S × alpha`, 가산 파티클과 가산 선분은 `color × T`(빛이 더해지는 것이라 산란을 두 번 더하지 않는다). 디버그 라인은 그대로 둔다(편집 도구다) |
| 햇빛의 색 | 해의 조도에 카메라 높이에서 해 방향으로의 투과율을 곱해 `frame.light_color`에 넣는다. 그림자의 빛과 같은 값이다. CPU가 계산한다(아래 `nv_atmosphere_transmittance`): 씬이 100 m 안이라 높이에 따른 차이가 없고, 셰이더가 LUT를 읽지 않아도 되며 ctest로 시험할 수 있다 |
| 주변광 (3단계) | compute 패스 하나가 하늘 뷰 LUT를 위쪽 반구에서 코사인 가중으로 적분해 하늘 조도(색 하나)를, 지면 반사율 × (햇빛 + 하늘 조도) / π로 아래쪽 조도를 만든다. 메시는 법선의 y로 둘을 섞는다. 1, 2단계 동안 주변광은 지금의 `ambient`다 |
| 그림자 패스 | 바뀌지 않는다: `shadow_frame_group`은 LUT를 묶지 않는다 |
| 업스케일 패스 | 바뀌지 않는다. 노출, 톤 매핑, bloom이 하늘에도 그대로 적용된다 |

대기 원근의 거리 배율: 쇼케이스의 100 m는 실제 대기에서 거의 투명하다. `NvAtmosphere.aerial_scale`이 대기 원근에서만 거리에 곱해져
(Unreal의 "Aerial Perspective View Distance Scale"과 같다) 작은 씬에서도 먼 곳이 흐려진다. 하늘 자체는 배율과 무관하다.
볼륨이 덮는 거리 `aerial_distance`는 카메라의 `far × aerial_scale`이다(km로).

### 엔진

새 모듈 `engine/atmosphere.h`, `engine/atmosphere.c`: 설정 구조체, 지구 기본값, GPU가 필요 없는 수학(ctest 대상). GPU 쪽(LUT,
파이프라인, 패스)은 bloom처럼 `renderer.c`에 둔다.

```c
// engine/atmosphere.h
// Physical sky and aerial perspective (docs/specs/atmosphere.md). Zeroed: no atmosphere.
typedef struct NvAtmosphere {
    b32 on;
    f32 rayleigh_scattering[3]; // per km, at the ground
    f32 rayleigh_height;        // km, where the density falls to 1/e
    f32 mie_scattering;         // per km, at the ground
    f32 mie_absorption;         // per km, at the ground
    f32 mie_height;             // km
    f32 mie_anisotropy;         // Cornette-Shanks g, 0..0.99
    f32 ozone_absorption[3];    // per km, at the layer's peak
    f32 ground_albedo[3];
    f32 sun_disk;               // the sun's angular diameter, degrees; 0 = no disk
    f32 aerial_scale;           // distances count this many times in aerial perspective; 0 = no aerial perspective
} NvAtmosphere;

// Earth's values (Hillaire 2020), with `on` set.
NvAtmosphere nv_atmosphere_earth(void);

// The light that reaches a point `altitude` km above the ground from a direction `cos_zenith` from straight up,
// per channel, 0..1. 0 where the ray hits the planet.
NvVec3 nv_atmosphere_transmittance(const NvAtmosphere* atmosphere, f32 altitude, f32 cos_zenith);
```

`NvRenderer`에 `NvAtmosphere atmosphere`(앱이 매 프레임 설정, `shadows`, `post`와 같다), LUT 텍스처와 뷰, compute 파이프라인 넷,
하늘 파이프라인, 그리고 지난 매질 값이 생긴다. `FrameUniforms`에 `inverse_view_proj`와 `atmosphere`(x 켜짐, y 대기 원근 거리(m,
배율 포함), z 씬 폭, w 씬 높이)가 더해지고, 프레임 bind group에 대기 원근 볼륨과 그 샘플러가 더해진다. `vfx.c`의 이펙트
셰이더도 같은 두 바인딩을 받는다.

공부와 검증용 보기(`NvRenderer.atmosphere_view`, 앱이 설정, 0이 최종):

```c
typedef enum NvAtmosphereView {
    NV_ATMOSPHERE_VIEW_FINAL,         // the sky and aerial perspective as drawn
    NV_ATMOSPHERE_VIEW_REFERENCE,     // the sky integrated per pixel, no LUT (single scattering, then multiple from the LUT if on)
    NV_ATMOSPHERE_VIEW_TRANSMITTANCE, // T along each view ray, to the planet or the top of the atmosphere
    NV_ATMOSPHERE_VIEW_SINGLE,        // single scattering only
    NV_ATMOSPHERE_VIEW_MULTIPLE,      // multiple scattering only
    NV_ATMOSPHERE_VIEW_RAYLEIGH,      // the Rayleigh term only
    NV_ATMOSPHERE_VIEW_MIE,           // the Mie term only
    NV_ATMOSPHERE_VIEW_COUNT,
} NvAtmosphereView;
```

기준 모드의 레이 마칭은 하늘 패스의 셰이더 안 함수 하나이고(시선 위 32걸음, 걸음마다 해 방향 투과율), 하늘 뷰 LUT의 compute도
같은 함수를 부른다: 정답과 최적화가 같은 식을 쓴다. 성분 보기는 셰이더의 분기 하나로, 하늘과 메시의 대기 원근 모두에 적용된다
(메시는 성분 보기에서 S만 보인다).

타임스탬프: 대기 compute 패스에 쌍 하나(`NV_TIMESTAMP_COUNT` 12 → 14), `NvRenderer.gpu_atmosphere_ms`. Stress 탭의 GPU 줄과
벤치마크 표에 열 하나(`at`)가 더해진다.

### 샌드박스

| 주제 | 결정 |
|---|---|
| 어디에 적나 | **해 노드의 동작 `atmosphere`**(`scene_file.md`의 동작 종류에 하나 더함). 씬 파일이 정하고, 동작의 규칙(파라미터 표가 읽기, Inspector, 검색, 저장 `BHVR`, undo를 만든다)을 그대로 따른다. 해를 고르면 Inspector에 Light와 Atmosphere 섹션이 함께 보인다 |
| 제약 | 씬에 `atmosphere`는 많아야 하나이고, 그 노드는 씬의 첫 directional light여야 한다. 아니면 씬 파일 오류다(줄 번호와 함께, 다른 오류와 같다) |
| 매 프레임 | `behaviors.c`의 `atmosphere` 분기가 파라미터에서 `NvAtmosphere`를 만들어 `renderer.atmosphere`에 넣는다. 시간으로 움직이는 것이 없으므로 Edit와 Play가 같다. 동작이 없는 씬은 `atmosphere`를 0으로 둔다 |
| Play | 하루의 흐름은 해 노드의 `spin`이다(새 코드 없음). Stop이 해의 회전을 되돌린다(노드의 `ROT`가 저장에 있다) |
| 저장, undo | 새 태그 없음: 파라미터는 `BHVR`의 `PARM`으로 저장되고 Behaviors 범위로 undo된다. `SAVE_VERSION`은 그대로다. 옛 저장에 그 `BHVR`가 없으면 파일의 값이다 |
| 쇼케이스 | `sun` 노드에 `atmosphere`를 단다. `aerial_scale`과 노출은 구현할 때 보며 정하고 이 표에 적는다 |
| 스트레스 | 그대로(대기 없음). 벤치마크의 기존 결과와 비교할 수 있게 둔다 |
| View 탭 | "Atmosphere view" 콤보(위의 보기들). 편집기 도구라 저장하지 않고 undo도 없다. 보이는 씬에 대기가 없으면 숨긴다 |
| Textures 탭 | 투과율, 다중 산란, 하늘 뷰 LUT를 목록(`gather`)에 더한다. 대기 원근 볼륨은 3D라 미리보기가 없다("나중") |
| 문자열 | 새 UI 문자열은 `sandbox/strings.c`에 한국어 행 |

**`atmosphere` 동작의 키** (모두 파라미터, 모두 선택. 배율은 지구 기본값에 곱한다)

| 키 | 값 | 범위 | 기본 | 뜻 | Inspector 이름표 |
|---|---|---|---|---|---|
| `rayleigh <배율>` | | 0–10 | 1 | Rayleigh 산란: 하늘의 파란색, 해질녘의 붉은색 | "Rayleigh" |
| `mie <배율>` | | 0–20 | 1 | Mie 산란과 흡수: 연무, 해 주변의 흰 빛 | "Haze" |
| `mie_anisotropy <g>` | | 0–0.99 | 0.8 | 해 주변 후광이 얼마나 좁은가 | "Haze glow" |
| `ozone <배율>` | | 0–10 | 1 | 오존 흡수: 해질녘 천정의 파란색 | "Ozone" |
| `ground_albedo r g b` | | 0–1 각각 | 0.3 0.3 0.3 | 행성 지면의 반사율: 지평선 아래의 색, 다중 산란, 3단계의 아래쪽 주변광 | "Ground albedo" |
| `sun_disk <도>` | 도 | 0–10 | 0.545 | 해 원반의 시직경, 0이면 원반 없음 | "Sun disk" |
| `aerial_scale <배율>` | | 0–1000 | 1 | 대기 원근의 거리 배율, 0이면 대기 원근 없음 | "Aerial distance" |

검색 키워드: sky, atmosphere, scattering, fog, haze, sunset, rayleigh, mie, ozone.

```
node sun
    rotation ...
    light
        type directional
        color 1 0.96 0.9
        intensity 1.1
    atmosphere
        aerial_scale 50
```

### 디버그 내보내기와 시험

Debug 빌드가 내보낸다:

- `Module._sandbox_debug_set_atmosphere(rayleigh, mie, anisotropy, ozone, sun_disk, aerial_scale)`: 보이는 씬의 `atmosphere` 동작
  (`_set_grid` 등과 같은 규칙).
- `Module._sandbox_debug_atmosphere(n)`: 0 켜짐, 1 투과율/다중 산란 LUT를 만든 횟수, 2 하늘 뷰 LUT를 만든 횟수.
- `Module._sandbox_debug_set_sun(elevation_degrees, azimuth_degrees)`: 첫 directional light 노드의 회전을 바꾼다.
- `Module._sandbox_debug_set_atmosphere_view(view)`: `NvAtmosphereView`.

ctest (`tests/atmosphere_test.c`, `engine/atmosphere.c`만 링크):

- 지면에서 천정 방향 투과율이 지구 기본값의 광학 깊이로 계산한 값(약 0.940, 0.868, 0.762)과 1% 안에서 같다.
- 투과율은 고도각이 낮아질수록 줄고, 파랑이 빨강보다 빨리 준다. 행성을 맞히는 방향은 0이다.
- 대기 꼭대기 밖에서는 1이다.

Playwright (Release와 Debug 둘 다, swiftshader):

1. 쇼케이스: 뷰포트 위쪽(하늘) 픽셀이 파랗다(b > r). `_sandbox_debug_atmosphere(0)`이 1.
2. `_sandbox_debug_set_sun(2, 0)`: 해 쪽 지평선 픽셀이 붉어진다(r > b). 해를 돌려도 `_sandbox_debug_atmosphere(1)`은 그대로이고,
   `_sandbox_debug_set_atmosphere`로 `rayleigh`를 바꾸면 1 오른다.
3. `aerial_scale` 1과 1000에서 바닥의 먼 쪽 픽셀을 비교한다: 1000이 하늘 색에 더 가깝다.
4. 파라미터를 바꾼 뒤 `_sandbox_debug_undo_steps()`가 1 오르고, undo가 되돌리며, `_sandbox_debug_save_round_trip()`이 같다.
5. Play와 Stop 뒤 `_sandbox_debug_save_crc()`가 같다(해에 `spin`을 단 시험용 씬 텍스트 없이, 노드의 회전으로).
6. MSAA 끔/4x, 해상도 Scale 2, Fixed에서 1번이 같다. WebGPU 검증 오류가 없다(`_sandbox_debug_wgpu_error`).
7. **기준과 LUT 비교**: 해의 고도 45°, 10°, 2°마다 최종과 기준(`REFERENCE`)의 하늘 픽셀 몇 개(천정, 해 반대편, 지평선)가 상대 오차
   5% 안이다. 해 원반 근처는 빼고 본다(하늘 뷰 LUT의 해상도 한계). LUT를 넣는 단계마다 이 시험이 그 LUT를 검사한다.
8. 성분 보기마다 WebGPU 오류가 없고, `SINGLE` + `MULTIPLE`의 픽셀이 `FINAL`의 하늘 픽셀과 가깝다(해 원반 제외).

## 단계

각 단계는 화면에 무언가 새로 보이고, 앞 단계의 결과와 비교할 수 있게 나눈다. 단계마다 그 단계의 이론(위 "이론 요약"의 번호)을 적는다.

0. **바탕** (이론 1): `engine/atmosphere.c`, `NvAtmosphere`, 지구 기본값, CPU 투과율과 ctest. 햇빛의 색(해가 낮으면 바닥이 붉어진다).
   샌드박스의 `atmosphere` 동작과 Inspector, 쇼케이스. 하늘은 아직 `clear_color`다.
1. **기준 하늘** (이론 2, 3, 6): 하늘 패스, 픽셀마다 단일 산란을 적분한다. 해의 방향도 걸음마다 직접 적분한다(LUT 없음). 해 원반.
   성분 보기의 `TRANSMITTANCE`, `SINGLE`, `RAYLEIGH`, `MIE`. 시험 1, 2, 4, 5, 6. 느려도 된다: 이것이 정답이다.
2. **투과율 LUT** (이론 1): 해 방향의 적분을 LUT 한 번의 읽기로 바꾼다. Textures 탭에 보인다. 기준 모드는 계속 직접 적분해 둘을 비교한다.
3. **다중 산란 LUT** (이론 4): 하늘이 밝아지고 해질녘의 색이 바뀐다. `MULTIPLE` 보기. 이 단계부터 기준도 다중 산란 LUT를 더한다.
4. **하늘 뷰 LUT**: 하늘 패스가 픽셀마다 적분하지 않고 LUT를 읽는다. 기준은 `REFERENCE` 보기로 남는다. 시험 7, 8. 타임스탬프로 1단계와
   GPU 시간을 비교해 이 LUT가 얼마를 아꼈는지 "구현 결과"에 적는다.
5. **대기 원근** (이론 5): 볼륨 compute, 메시와 이펙트 셰이더. 시험 3.
6. **하늘의 주변광**: 반구 조도 compute와 메시 셰이더. 쇼케이스의 노출을 다시 맞춘다.

각 단계가 끝나면 `AGENTS.md`(모듈 목록, 규칙)와 이 스펙의 "구현 결과"를 고친다. "구현 결과"에는 단계마다 배운 것(틀렸던 곳, 기준과
달랐던 곳과 그 이유)도 짧게 적는다.

## 합의할 것

1. **직접 작성(Hillaire 2020), 새 라이브러리 없음.** (권장)
2. **씬 파일에서 어디에 적나.** 해 노드의 동작 `atmosphere`(권장: 저장, undo, Inspector, 검색이 공짜다), 또는 최상위 블록
   `atmosphere` + View 탭의 섹션(씬 설정으로는 더 자연스럽지만 새 저장 태그와 undo의 Scene settings 범위 행이 필요하다).
3. **대기 원근을 이펙트에도 넣나.** 넣는다(권장: 파티클이 먼 곳에서 떠 보이지 않는다), 또는 메시에만.
4. **주변광(3단계)을 이 스펙에 넣나.** 넣는다(권장: 해질녘에 그림자 속이 파랗게 남는 것이 대기의 큰 몫이다), 또는 "나중".
5. **스트레스 씬**: 대기 없음(권장, 벤치마크 비교 유지), 또는 켬.
6. **누가 코드를 쓰나.** 공부가 목적이므로: (가) 사용자가 단계마다 직접 쓰고 Claude는 설명, 리뷰, 시험을 맡는다, (나) Claude가 단계마다
   쓰고 사용자가 읽고 질문한 뒤 다음 단계로 간다, (다) 섞는다(예: 셰이더의 적분은 사용자가, 패스와 샌드박스 연결은 Claude가).

## 범위 밖 (나중)

- 볼류메트릭 그림자(빛줄기): 대기 원근 볼륨이 그림자 맵을 읽어야 한다.
- 구름, 별, 달, 밤하늘. 해가 지평선 아래면 하늘이 검다(다중 산란이 남기는 약한 빛뿐).
- 높이 안개(지면 근처의 짙은 안개), 우주에서 행성을 보는 시점(카메라 높이는 수 m~수 km를 가정한다).
- 대기 원근 볼륨의 Textures 탭 미리보기(3D 텍스처의 단면).
- Inspector의 해 고도/방위 슬라이더(지금은 기즈모의 회전).
- 하늘의 반사(반사 프로브): 엔진에 반사가 없다.

## 참고 자료

- Sébastien Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique", EGSR 2020. 참조 구현:
  https://github.com/sebh/UnrealEngineSkyAtmosphere (MIT).
- Eric Bruneton, Fabrice Neyret, "Precomputed Atmospheric Scattering", EGSR 2008; 구현 https://github.com/ebruneton/precomputed_atmospheric_scattering (BSD).
- webgpu-sky-atmosphere: https://github.com/JolifantoBambla/webgpu-sky-atmosphere (MIT), Hillaire 기법의 WebGPU 포팅.
- Lukas Hosek, Alexander Wilkie, "An Analytic Model for Full Spectral Sky-Dome Radiance", SIGGRAPH 2012.
- Unreal Engine 문서, "Sky Atmosphere Component" (Aerial Perspective View Distance Scale).

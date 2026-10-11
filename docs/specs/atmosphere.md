# Atmospheric scattering spec

상태: 결정됨, 구현 전 (2026-10-11). 구현은 사용자가 시작하라고 할 때 시작한다. 이 스펙의 변경은 먼저 합의한다.

이 스펙은 **공부가 첫 목적**이다. 사용자가 처음 구현해 보는 기술이므로, 기능을 빨리 끝내는 순서가 아니라 **개념을 하나씩 이해하며
쌓는 순서**로 짠다. 결과물(샌드박스의 하늘과 대기 원근)은 그 과정의 산물이다.

## 목차

1. 목표 — 무엇을 만들고, 무엇을 배우나
2. 공부의 원칙 — 이 스펙이 순서와 도구를 이렇게 정한 이유
3. 단계를 진행하는 방법 — 단계마다 Claude와 사용자가 하는 일
4. 이론 — 단계들이 가리키는 식 (0~10번)
5. 단계 — 0~8단계, 단계마다 배울 것, 만드는 것, 볼 것, 실험, 확인 질문
6. 공부 도구 — Study 섹션, 성분 보기, 차이 보기, 프로브, 그래프
7. 설계 — 모델, LUT, 패스, 엔진 API, 샌드박스 (구현의 결정)
8. 시험
9. 결정, 범위 밖, 용어, 읽을거리

---

## 1. 목표

### 만드는 것

샌드박스의 씬에 물리 기반 대기를 더한다.

- **하늘**: 지금의 단색 배경(`NvRenderer.clear_color`) 대신 해의 방향에 따라 바뀌는 하늘. 낮에는 파랗고, 해가 낮으면 지평선이
  붉어지고, 해 주변에 후광이 있고, 해 원반이 보인다. 지평선 아래는 행성의 지면이다.
- **햇빛의 색**: 해가 낮을수록 지면에 닿는 햇빛이 붉고 어두워진다.
- **대기 원근(aerial perspective)**: 먼 물체가 하늘 색으로 흐려진다.
- **하늘의 주변광**: 씬의 고정된 주변광(`lighting`의 `ambient`, chess_scene.md) 대신 하늘에서 오는 빛.

해는 씬의 첫 directional light다(`shadows.md`와 같다). 그 노드를 기즈모로 돌리면 시각이 바뀌고, 그 노드에 `spin`을 달면 Play에서
하루가 흐른다.

### 배우는 것

다 끝났을 때 사용자가 설명할 수 있어야 하는 것:

- 빛의 단위(radiance, irradiance)와 이 엔진의 단위가 그와 어떻게 다른가.
- 참여 매질(participating media)의 세 계수(산란, 흡수, 소멸)와 위상 함수, Beer-Lambert 법칙.
- 하늘이 왜 파랗고, 해질녘이 왜 붉고, 연무가 왜 희고, 해 주변이 왜 밝은가: 식으로.
- 산란 적분을 수치로 푸는 방법(레이 마칭)과 걸음 수가 정확도와 비용에 주는 영향.
- 다중 산란과 Hillaire의 근사가 무엇을 버리고 무엇을 얻는가.
- LUT(look-up table)로 적분을 미리 계산하는 방법: 대칭성으로 차원 줄이기, 매개화(parametrization), 해상도와 오차.
- 같은 적분이 하늘, 대기 원근, 주변광에 어떻게 다시 쓰이는가.
- GPU에서: compute 패스로 텍스처 만들기, 3D 텍스처, 프레임마다/값이 바뀔 때만 다시 계산하는 판단.

---

## 2. 공부의 원칙

1. **느린 정답부터, 최적화는 나중에.** 각 개념은 먼저 가장 곧은 형태(픽셀마다 적분)로 만든다. 식이 코드에 한 줄씩 그대로 보인다.
   LUT는 그 뒤에 하나씩, **같은 답을 빠르게 내는 최적화**로 넣는다.
2. **최적화는 끄고 켤 수 있다.** LUT마다 끄는 토글이 있어, 끄면 곧은 적분으로 돌아간다. 다 만든 뒤에도 남긴다. 토글과 차이
   보기로 그 LUT가 무엇을 바꿨는지(오차가 어디에 몰리는지)와 얼마를 아꼈는지(GPU 시간)를 직접 본다.
3. **정답의 사슬.** C 코드(CPU)가 첫 정답이다: 디버거로 따라가고, 값을 찍고, ctest로 손 계산과 맞출 수 있다. GPU의 곧은 적분은
   C와 맞추고(프로브), GPU의 LUT 경로는 GPU의 곧은 적분과 맞춘다(차이 보기). 어느 고리가 틀렸는지 늘 좁힐 수 있다.
4. **한 단계에 새 개념 하나.** 단계는 작게 나누고, 단계마다 화면에 새로 보이는 것이 있다.
5. **예측하고 확인한다.** 실험은 "이렇게 바꾸면 어떻게 될까?"를 먼저 적고 확인하는 형태다. 예측이 틀린 곳이 공부할 곳이다.
6. **식과 코드를 잇는다.** 함수마다 주석에 구현하는 식과 이 스펙 "이론"의 번호를 적는다. 이 모듈은 공부를 위해 코딩 표준보다
   주석이 많다: 식 자체를 주석으로 쓴다.

대가: 곧은 적분과 LUT 경로가 함께 있어 코드가 늘고(약 30%), 적분이 C와 WGSL 두 벌이다. 두 벌은 같은 구조와 이름으로 써서 나란히
읽을 수 있게 한다. 이 대가는 공부와 시험(정답의 사슬) 둘 다에 쓰이므로 받아들인다.

---

## 3. 단계를 진행하는 방법

정함(2026-10-11): **Claude가 단계마다 코드를 쓰고, 사용자가 읽고 질문한 뒤 다음 단계로 간다.** 한 단계는 이렇게 흐른다.

1. **미리 읽기.** Claude가 그 단계의 "배울 것"과 이론 번호, 읽을거리의 해당 부분, 실험에서 예측해 볼 질문을 짧게 보낸다.
   사용자가 원하면 여기서 먼저 질문한다.
2. **구현.** Claude가 그 단계를 구현한다. 커밋은 읽는 순서대로 나눈다: C의 수학과 ctest → WGSL → 렌더러 연결 → 샌드박스와 공부 도구.
3. **해설.** Claude가 이론과 코드의 대응(식 → 함수, 파일과 줄), 읽기 순서, 실행 방법을 보낸다. 배포된 Debug 빌드의 주소도 보낸다
   (CI가 브랜치마다 Pages에 올린다).
4. **실험과 질문.** 사용자가 실험을 해 보고 확인 질문에 답해 보며 묻는다. Claude가 답하고, 필요하면 실험 도구를 고친다.
5. **기록.** 그 단계에서 배운 것, 틀렸던 곳, 예측과 달랐던 실험, GPU 시간을 이 스펙의 "구현 결과"에 짧게 적는다.
6. **다음 단계.** 사용자가 넘어가자고 할 때 넘어간다.

---

## 4. 이론

단계들이 번호로 가리킨다. 단위는 길이 km, 계수 1/km.

**0. 빛의 단위.** radiance L은 한 방향에서 오는 빛의 세기(W/(m²·sr)), irradiance E는 면에 떨어지는 빛의 총량(W/m²). 해는 아주
작아서 E_sun 하나로 쓴다. 흰 Lambert 면이 E를 정면으로 받으면 L = E / π로 빛난다. 이 엔진의 메시 셰이더에는 1/π가 없다
(`light_color * diffuse`): 엔진의 값은 "π × radiance"다. 그래서 하늘의 radiance에도 π를 곱해야 메시와 같은 단위가 된다.

**1. 기하.** 행성은 중심이 (0, −R_g, 0)인 공(R_g = 6360 km), 대기는 반지름 R_t = 6460 km까지. 점 p에서 방향 v로 가는 광선과
반지름 r인 구의 교차는 t² + 2(p·v)t + (|p|² − r²) = 0의 근이다. 시선은 대기 안에 있는 구간만 적분하고, 행성에 닿으면 거기서 끝난다.
높이 h = |p| − R_g, 천정각의 코사인 μ = (p·v) / |p|.

**2. 매질.** 세 성분이 섞여 있다. 계수 = 지면의 값 × 밀도 d(h).
- Rayleigh(공기 분자): 산란만, d = exp(−h / 8). 파장에 크게 의존한다(λ⁻⁴ 근사: 파랑이 빨강의 약 6배).
- Mie(에어로졸, 연무): 산란과 약간의 흡수, d = exp(−h / 1.2). 파장에 거의 무관하다(회색).
- 오존: 흡수만, d = max(0, 1 − |h − 25| / 15). 초록에서 주황까지를 흡수하고 파랑은 거의 흡수하지 않는다.
H(8 km, 1.2 km)는 스케일 높이: H만큼 오를 때마다 밀도가 1/e가 된다.

**3. 투과율 (Beer-Lambert).** 두 점 사이를 지나며 남는 빛의 비율. σ_t = σ_s + σ_a(소멸 = 산란 + 흡수).
T(a, b) = exp(−τ), τ = ∫ₐᵇ σ_t(x) dx(광학 깊이). 천정 방향은 τ가 작고 지평선 방향은 Rayleigh 공기를 약 35배(√(πR_g / 2H)) 지나 τ가 크다: 파랑이 먼저
빠져 해질녘의 햇빛이 붉다.

**4. 위상 함수.** 산란된 빛이 각 θ만큼 꺾일 확률 밀도(모든 방향으로 적분하면 1).
- Rayleigh: p_R(θ) = 3 / (16π) · (1 + cos²θ). 앞뒤가 대칭이다.
- Mie(Cornette-Shanks): p_M(θ) = 3 / (8π) · (1 − g²)(1 + cos²θ) / ((2 + g²)(1 + g² − 2g·cosθ)^{3/2}). g가 클수록 앞쪽(해 주변)에 몰린다.

**5. 단일 산란.** 시선 위의 각 점 x에서 햇빛이 한 번 꺾여 눈으로 오는 빛을 더한다.
L₁ = ∫₀^{t_max} T(c, x) · Σ_i σ_s,i(x) p_i(θ) · T(x, 해) · V(x) · E_sun dt.
V(x)는 x에서 해가 행성에 가려지면 0. 수치 적분은 구간을 N걸음으로 나눠 각 걸음 가운데에서 값을 구한다(중점 규칙). 걸음마다
T(x, 해)도 적분이므로 곧은 구현의 비용은 픽셀 × N × M이다.

**6. 지면과 해 원반.**
- 지면(지평선 아래 시선): 행성의 표면이 반사율 a로 빛난다. L_ground = a / π · E_sun · T(지면, 해) · max(μ_sun, 0) (뒤에 하늘의 빛이 더해진다: 10번).
  그 빛이 눈까지 오며 T(c, 지면)만큼 줄고, 그 사이의 공기가 5번의 산란을 더한다.
- 해 원반: 시직경 α 안의 방향은 L_sun = E_sun · T(c, 해) / Ω, Ω = 2π(1 − cos(α / 2)). 가장자리로 갈수록 어둡게(limb darkening) 한다.
  Ω가 아주 작아 L_sun은 하늘보다 수만 배 밝다.

**7. 다중 산란 (Hillaire 2020).** 두 번 이상 꺾인 빛. 정확히 하려면 모든 방향의 적분이 겹겹이 필요하다. Hillaire의 근사:
- 2차 이상의 산란은 방향에 거의 무관하다(등방)고 본다.
- 한 점 주변이 다 같은 높이의 공기라고 보면, 빛이 한 번 더 산란될 때마다 같은 비율 f_ms가 돌아온다.
- 그러면 모든 차수의 합은 등비급수다: L₂ + L₂·f_ms + L₂·f_ms² + … = L₂ / (1 − f_ms).
다중 산란 LUT가 높이와 해의 천정각마다 Ψ_ms = L₂ / (1 − f_ms)를 담고, 5번의 적분 안에 σ_s(x) · Ψ_ms 항이 더해진다.
버리는 것: 다중 산란의 방향성. 그래서 연무가 아주 짙으면 틀린다(실험 4-d: 경로 추적과 비교).

**8. 대기 원근.** 물체까지의 시선에 5번(과 7번)을 t_max = 물체까지의 거리로 적분한 산란 S와 투과율 T. 화면 색 = 물체의 색 · T + S.
하늘은 "물체가 없는 시선"의 대기 원근이다: 같은 적분이다.

**9. LUT와 매개화.** 적분의 결과가 소수의 변수에만 달려 있으면 미리 계산해 텍스처에 담는다.
- 대칭성: 행성이 공이므로 투과율은 위치와 방향 전체가 아니라 (h, μ) 두 값에만 달려 있다. 다중 산란은 (h, μ_sun).
- 매개화: 텍스처의 uv를 그 값으로 어떻게 펼치나. 값이 빨리 변하는 곳(지평선 근처)에 텍셀을 많이 둔다. 균일하게 펼치면 지평선
  근처의 오차가 커진다(실험 3-c).
- 언제 다시 계산하나: 변수에 들어가지 않는 것이 바뀌면 다시 만든다. 투과율 LUT는 대기 값이 바뀔 때만, 하늘 뷰 LUT는 카메라 높이와
  해 방향이 들어가므로 매 프레임.

**10. 하늘의 조도(주변광).** 법선 n인 면이 하늘에서 받는 빛: E_sky(n) = ∫ L(ω) max(0, n·ω) dω(반구에서 코사인 가중 적분).
위를 보는 면은 하늘 전체를, 아래를 보는 면은 지면에서 튄 빛을 받는다. 그늘 속이 파랗게 보이는 이유다.

---

## 5. 단계

단계마다: **배울 것**, **이론**(4절의 번호), **만드는 것**, **볼 것**, **실험**(예측 → 확인), **확인 질문**(답할 수 있으면 이해한
것), **읽기 순서**, **다음 단계의 동기**. 실험의 값은 View 탭의 Atmosphere 섹션(대기 값)과 Atmosphere study 섹션(6절)에서 바꾼다.

### 0단계: 단위, 기하, 투과율 (CPU만)

- **배울 것**: 엔진의 빛 단위, 광선-구 교차, 밀도와 스케일 높이, 광학 깊이와 투과율, 해질녘이 붉은 이유.
- **이론**: 0, 1, 2, 3.
- **만드는 것**:
  - `engine/atmosphere.h`, `engine/atmosphere.c`: `NvAtmosphere`, 지구 기본값, 광선-구 교차, 밀도, 광학 깊이, `nv_atmosphere_transmittance`.
  - `tests/atmosphere_test.c`(ctest).
  - 렌더러: `frame.light_color`에 해 방향의 투과율을 곱한다(햇빛의 색).
  - 샌드박스: 씬 파일 `lighting` 블록 안의 `atmosphere` 블록(읽기, View 탭, 저장 `ATMO`, undo), 쇼케이스 파일에 블록. Study 섹션의
    그래프 둘: 높이별 밀도, 해의 고도별 투과율.
  - 하늘은 아직 `clear_color`다.
- **볼 것**: 기즈모로 해를 낮추면 바닥과 캐릭터가 주황으로, 지평선 아래로 가면 검게 바뀐다. 그래프에서 파랑이 먼저 떨어진다.
- **실험**:
  - a. 해의 고도 90° → 10° → 2°. 예측: 어느 고도에서 바닥이 눈에 띄게 주황이 될까? 투과율 그래프에서 그 고도의 R, G, B를 읽는다.
  - b. `rayleigh` 3배. 예측: 같은 고도에서 더 붉어질까, 더 어두워질까?
  - c. `mie` 10배. 예측: 색이 바뀔까, 밝기만 바뀔까? (Mie는 회색이다)
  - d. `ozone` 0. 예측: 바닥의 색에 차이가 클까? (작다: 오존은 6단계 하늘의 천정에서 보인다)
- **확인 질문**:
  1. 천정 방향 투과율이 (0.94, 0.87, 0.76)인 이유를 계수와 스케일 높이로 손 계산할 수 있나?
  2. 공기의 절반이 몇 km 아래에 있나? (8 · ln 2 ≈ 5.5 km)
  3. 해가 지평선에 닿기 전부터 바닥이 어두워지는 이유는?
  4. 엔진의 `light_color`가 radiance가 아니라 "π × radiance"의 단위라는 것은 무슨 뜻인가?
- **읽기 순서**: `NvAtmosphere`(필드의 단위) → 밀도 함수 → 광선-구 교차 → 광학 깊이 → `nv_atmosphere_transmittance` → ctest → 렌더러에서 쓰는 곳.
- **다음의 동기**: 해에서 지면까지 오는 빛은 알았다. 그 빛의 일부가 시선 쪽으로 꺾이는 것이 하늘이다.

### 1단계: 곧은 하늘, Rayleigh 단일 산란

- **배울 것**: 산란 적분, Rayleigh 위상 함수, 중점 규칙과 걸음 수, 곧은 구현의 비용(픽셀 × N × M).
- **이론**: 4(Rayleigh), 5.
- **만드는 것**:
  - C: 같은 적분의 CPU 판(`nv_atmosphere_sky`, 한 방향의 radiance와 성분).
  - 하늘 패스: 화면 삼각형, 픽셀마다 시선을 N걸음, 걸음마다 해 방향을 M걸음 적분한다(LUT 없음). 행성에 닿는 시선은 거기서 멈춘다(지면은 아직 검다).
  - Study 섹션: 걸음 수 슬라이더, 성분 보기(`FINAL`, `TRANSMITTANCE`, `SINGLE`), 프로브, 높이 오프셋, GPU 시간.
- **볼 것**: 파란 하늘, 천정보다 밝고 흰 지평선. 해를 낮추면 해 쪽 지평선이 붉어진다. 해 주변은 아직 특별히 밝지 않다.
- **실험**:
  - a. 걸음 수 64 → 4. 예측: 어디부터 틀어질까? (지평선 근처: 공기를 오래 지나는 시선)
  - b. 프로브를 천정과 지평선에 두고 GPU 값과 C 값을 비교한다. 차이가 1% 안이어야 한다(정답의 사슬 첫 고리).
  - c. 높이 오프셋 0 → 10 → 40 km. 예측: 하늘의 밝기와 색은? (어두워지고 짙은 파랑으로)
  - d. 해상도 Scale 1과 4에서 GPU 시간. 예측: 비용이 픽셀 수에 비례할까?
- **확인 질문**:
  1. 같은 Rayleigh 산란으로 하늘은 파랗고 해질녘의 지평선은 붉다. 둘을 한 식으로 설명할 수 있나?
  2. 지평선이 천정보다 밝고 흰 이유는?
  3. 왜 `T(x, 해)`가 적분 안에 있나? 빼면 무엇이 틀리나?
  4. 1080p에서 N = 32, M = 16이면 프레임당 지수 함수를 몇 번 계산하나?
- **읽기 순서**: C의 `nv_atmosphere_sky` → WGSL의 같은 이름의 함수(나란히) → 하늘 패스의 정점/프래그먼트 → 렌더러 연결.
- **다음의 동기**: 해 주변의 후광, 연무, 지평선 아래가 없다. 그리고 비싸다(실험 d).

### 2단계: Mie, 오존, 해 원반, 지면

- **배울 것**: Mie 위상 함수와 비등방성 g, 산란과 흡수의 차이, 오존 층, 해 원반의 radiance, 지면의 반사.
- **이론**: 2, 4(Mie), 6.
- **만드는 것**: 적분에 Mie와 오존, 해 원반, 지면. C와 WGSL 둘 다. 성분 보기 `RAYLEIGH`, `MIE`. 그래프: 위상 함수(각도별, 로그 축).
- **볼 것**: 해 주변의 흰 후광, 해 원반(bloom으로 빛난다), 지평선 아래의 지면.
- **실험**:
  - a. `mie_anisotropy` 0 → 0.95. 예측: 후광의 크기와 밝기는? 위상 함수 그래프와 비교한다.
  - b. `mie` 10배. 예측: 하늘이 더 파래질까, 희어질까?
  - c. 해 고도 2°에서 `ozone` 1과 0의 천정 색. 예측: 오존이 없으면 해질녘 천정은 무슨 색일까?
  - d. `sun_disk` 0.545° → 5°. 예측: 원반이 커지면 원반의 밝기는? (같은 E를 큰 Ω로 나눈다)
- **확인 질문**:
  1. 연무가 하늘을 희게 만드는 이유를 계수의 파장 의존으로 설명할 수 있나?
  2. 오존은 흡수만 하는데 왜 해질녘에만 눈에 띄나?
  3. 해 원반의 radiance가 하늘보다 수만 배 큰 이유는?
  4. 지면의 식에 왜 1/π가 있고, 엔진 단위에서는 어떻게 되나?
- **다음의 동기**: 정답이 생겼다. 이제 같은 답을 빠르게: 가장 안쪽의 적분(해 방향 투과율)부터.

### 3단계: 투과율 LUT

- **배울 것**: 미리 계산, 대칭성으로 차원 줄이기, (h, μ)의 매개화, 선형 필터의 오차, LUT를 다시 만드는 조건.
- **이론**: 3, 9.
- **만드는 것**: 투과율 LUT compute 패스, 토글 "Transmittance LUT", 차이 보기, Textures 탭 항목, 대기 compute의 타임스탬프. 공부용
  분기 "균일 매핑"(지평선을 촘촘히 하지 않는 매핑).
- **볼 것**: 그림은 거의 같고 GPU 시간이 준다. 차이 보기에서 오차가 지평선 근처에 모인다. Textures 탭에서 LUT의 모양.
- **실험**:
  - a. 토글을 끄고 켜며 GPU 시간. 예측: 얼마나 줄까? (M번의 지수 계산이 텍스처 한 번)
  - b. 차이 보기의 배율을 키워 오차가 어디 있는지 본다.
  - c. "균일 매핑"을 켠다. 예측: 오차가 어디서 커질까?
  - d. 해를 돌린다 / `rayleigh`를 바꾼다. Study 섹션의 "LUT를 만든 횟수"가 언제 오르나?
- **확인 질문**:
  1. 투과율이 위치 3개와 방향 2개가 아니라 (h, μ) 두 값에만 달린 이유는?
  2. 해를 돌려도 LUT를 다시 만들지 않아도 되는 이유는?
  3. LUT의 v축(μ)을 지평선 근처에서 촘촘히 하는 이유는?
- **다음의 동기**: 하늘이 실제보다 어둡다(특히 해에서 먼 쪽, 해가 진 뒤). 한 번만 꺾인 빛만 셌기 때문이다.

### 4단계: 다중 산란 LUT

- **배울 것**: 산란의 차수, 등방 근사, 등비급수, 근사의 한계.
- **이론**: 7.
- **만드는 것**: 다중 산란 LUT compute 패스(텍셀마다 방향 64개를 workgroup이 나눠 적분), 적분에 Ψ_ms 항, 토글 "Multiple scattering",
  성분 보기 `MULTIPLE`. C 판에도 같은 항. C의 몬테카를로 경로 추적(`nv_atmosphere_path_trace`, Debug 빌드의 Study 섹션).
- **볼 것**: 하늘이 밝아진다. 해가 진 직후에도 하늘이 완전히 검지 않다. 짙은 연무에서 차이가 크다.
- **실험**:
  - a. 토글을 끄고 켠다. 예측: 어느 쪽 하늘이 가장 많이 바뀔까?
  - b. `mie` 20배에서 토글. 예측: 단일 산란만으로는 짙은 안개가 어떻게 보일까?
  - c. 해 고도 −3°에서 `SINGLE`과 `MULTIPLE` 보기.
  - d. **경로 추적과 비교**(Debug 빌드): 프로브 방향에서 C의 몬테카를로 경로 추적(모든 차수의 산란, 근사 없음)을 돌려 근사와 비교한다.
    값이 프레임마다 쌓이며 수렴하는 것을 본다(잡음은 경로 수의 제곱근에 반비례한다). 예측: `mie` 1과 20에서 근사의 오차는?
- **확인 질문**:
  1. 2차 이상의 산란을 등방이라 봐도 되는 이유는? 언제 안 되나?
  2. f_ms는 무엇의 비율이고, 1 / (1 − f_ms)는 왜 나오나?
  3. 이 LUT가 해의 천정각과 높이 두 값에만 달린 이유는?
- **다음의 동기**: 하늘은 맞지만 여전히 픽셀마다 N걸음을 적분한다.

### 5단계: 하늘 뷰 LUT

- **배울 것**: 시점에 따른 LUT, 지평선을 촘촘히 한 위도 매핑, 방위각을 해 기준으로 재는 이유, 고주파(해 원반)를 LUT 밖에 두는 이유,
  비용이 해상도와 무관해지는 것.
- **이론**: 9.
- **만드는 것**: 하늘 뷰 LUT compute(192 × 108, 매 프레임), 하늘 패스가 LUT를 읽는다. 토글 "Sky-view LUT". 하늘 뷰 LUT를 만드는
  compute와 곧은 하늘 패스는 같은 WGSL 함수를 부른다.
- **볼 것**: 그림은 거의 같고, GPU 시간이 해상도에 덜 의존한다.
- **실험**:
  - a. 토글을 끄고 켜며 Scale 1과 4에서 GPU 시간(1단계 실험 d와 비교).
  - b. 차이 보기: 오차가 해 주변과 지평선에 모이는지.
  - c. 높이 오프셋 40 km에서 차이 보기. 예측: 매개화가 높은 곳에서도 맞을까?
- **확인 질문**:
  1. 이 LUT는 왜 매 프레임 다시 만드나? 무엇이 변수에 들어가나?
  2. 해 원반을 LUT에 넣으면 어떻게 될까?
  3. 방위각을 북쪽이 아니라 해 기준으로 재면 무엇이 좋아지나?
- **다음의 동기**: 하늘은 끝났다. 하지만 물체는 대기 밖에 있는 것처럼 선명하다.

### 6단계: 대기 원근

- **배울 것**: 유한한 거리까지의 같은 적분, color · T + S, 프러스텀 볼륨(froxel)과 깊이 분포, 거리 배율.
- **이론**: 8.
- **만드는 것**: 먼저 프래그먼트마다 곧은 적분, 그 다음 32 × 32 × 32 볼륨 compute와 메시 셰이더. 토글 "Aerial volume",
  `aerial_scale` 파라미터. 쇼케이스의 `aerial_scale`과 노출을 정한다.
- **볼 것**: 먼 바닥과 행성 모형이 하늘 색으로 흐려진다. 해질녘엔 붉게.
- **실험**:
  - a. `aerial_scale` 1 → 100 → 1000. 예측: 1에서 보일까? (100 m의 실제 공기는 거의 투명하다)
  - b. 토글로 곧은 적분과 볼륨 비교, 차이 보기.
  - c. 해를 등지고 볼 때와 해를 향해 볼 때 먼 곳의 색.
- **확인 질문**:
  1. 하늘과 대기 원근이 "같은 적분"이라는 것은 무슨 뜻인가?
  2. 볼륨이 투과율을 RGB가 아니라 회색 하나로 담는 근사는 무엇을 잃나?
  3. 거리 칸을 제곱 분포로 나누는 이유는?
- **다음의 동기**: 그늘 속이 여전히 씬의 고정된 주변광(`lighting`의 `ambient`)이다. 해의 높이와 상관없이 같은 색이다.

### 7단계: 하늘의 주변광

- **배울 것**: radiance에서 irradiance로(코사인 가중 반구 적분), 지면에서 튀는 빛.
- **이론**: 10.
- **만드는 것**: 하늘 뷰 LUT를 반구 적분하는 compute(위 조도, 아래 조도), 메시 셰이더가 법선의 y로 섞는다. 토글 "Sky ambient"(끄면
  씬의 `lighting`/`ambient`). 그래프 대신 Study 섹션에 두 조도의 색 견본.
- **볼 것**: 한낮에 그늘이 푸르스름하고, 해질녘엔 전체가 붉고 어두워진다.
- **실험**:
  - a. 토글을 끄고 켜며 그늘 속 색. 예측: 한낮의 그늘은 무슨 색일까?
  - b. `ground_albedo` 0 → 1. 예측: 무엇의 어느 쪽이 밝아질까?
- **확인 질문**:
  1. 위를 보는 면과 옆을 보는 면의 하늘 조도가 다른 이유는?
  2. 하늘의 radiance에 π를 곱한 것(0번)이 여기서 왜 맞아떨어지나?

### 8단계: 마무리 (새 개념 없음)

이펙트(알파 파티클, 데칼, 선분)의 대기 원근, 쇼케이스 다듬기, AGENTS.md 정리, "구현 결과"에 단계별 GPU 시간 표. 공학 작업이라
7단계를 마친 뒤 할지 정한다(결정 3).

---

## 6. 공부 도구

모두 View 탭의 **Atmosphere study** 섹션에 있다(Atmosphere 섹션 바로 아래, 데스크톱과 폰 같다). 보이는 씬에 대기가 없으면 두
섹션 모두 숨는다. 편집기 도구라 저장하지 않고 undo도 없다. 다 만든 뒤에도 남긴다(결정 8).
엔진 쪽 설정은 `NvAtmosphereStudy`이고 0이 "모든 최적화 켬, 최종 보기"다.

| 도구 | 하는 일 | 생기는 단계 |
|---|---|---|
| 그래프: 밀도 | 높이 0~60 km의 Rayleigh, Mie, 오존 밀도(`igPlotLines`, C 함수로 그린다) | 0 |
| 그래프: 투과율 | 해의 고도 −5°~90°에 따른 R, G, B 투과율, 지금 해의 고도 표시 | 0 |
| 그래프: 위상 함수 | 각도 0~180°의 p_R, p_M(지금의 g), 로그 축 | 2 |
| 걸음 수 | 곧은 적분의 N(시선), M(해 방향): 4~128 | 1 |
| 높이 오프셋 | 대기 계산에서만 카메라를 0~50 km 올린다 | 1 |
| 성분 보기 | `FINAL`, `TRANSMITTANCE`, `SINGLE`, `MULTIPLE`, `RAYLEIGH`, `MIE`, `DIFFERENCE` | 1부터 하나씩 |
| 최적화 토글 | Transmittance LUT, Multiple scattering, Sky-view LUT, Aerial volume, Sky ambient. 끄면 곧은 적분(다중 산란과 주변광은 끄면 빠진다) | 3부터 하나씩 |
| 차이 보기 | 하늘 픽셀마다 LUT 경로와 곧은 경로를 둘 다 계산해 \|차이\| / 값 × 배율을 보인다(배율 1~1000) | 3 |
| 프로브 | "Place probe"를 누른 뒤 뷰포트를 탭하면 그 픽셀에 십자가 남는다. 그 픽셀의 시선(해 기준 고도, 방위), GPU의 HDR 값(노출 전, 씬 색 텍셀 하나를 읽어 온다), 같은 시선의 C 값과 성분(Rayleigh, Mie, 다중), 상대 차이. 메시가 있는 픽셀은 GPU 값만 | 1 |
| LUT를 만든 횟수 | 투과율/다중 산란, 하늘 뷰, 볼륨 | 3 |
| GPU 시간 | 대기 compute 패스, 씬 패스(하늘 포함) | 1 |
| 경로 추적 | C의 몬테카를로 경로 추적으로 프로브 방향의 radiance. 프레임마다 약 5 ms어치 경로를 더해 화면을 멈추지 않고 수렴한다. 경로 수, 평균, 표준 오차, 근사와의 차이. Debug 빌드만 | 4 |

- 프로브의 읽기는 타임스탬프와 같은 규칙을 따른다: 다른 읽기가 진행 중이 아닐 때 시작하고, 값은 몇 프레임 늦게 온다.
- 프로브의 십자는 뷰포트에 그리는 편집기 UI의 예외다(빌드 표시, 기즈모와 같이). AGENTS.md에 예외로 적는다.
- 프로브가 기다리는 동안의 탭은 선택하지 않는다(배지 탭처럼 `pick`이 먼저 가로챈다).

---

## 7. 설계

### 서드파티 (AGENTS.md에 따라 먼저 찾음)

| 후보 | 무엇인가 | 언어, 라이선스 | 장단점 |
|---|---|---|---|
| **직접 작성, Hillaire 2020** (결정 1) | 이 스펙 | C17, WGSL | 공부가 목적이므로 가장 맞다: 모든 식을 우리가 쓴다. 렌더러의 패스, MSAA, 자체 해상도, reverse Z, HDR에 그대로 들어간다 |
| sebh/UnrealEngineSkyAtmosphere | 저자의 참조 구현 | C++/HLSL(D3D11), MIT | 직접 쓰지는 못한다. 식, 상수, 매개화를 확인하는 자료. 코드를 옮기면 MIT 고지를 남긴다(기본은 옮기지 않고 식으로 쓴다) |
| webgpu-sky-atmosphere (JolifantoBambla) | Hillaire 기법의 WebGPU 포팅 | TypeScript/WGSL, MIT | 장치와 패스가 wasm 밖 JS에 생기고, 후처리 방식이라 파티클의 대기 원근이 틀린다. 무엇보다 공부가 되지 않는다. WGSL은 참고 자료 |
| Bruneton 2017, precomputed_atmospheric_scattering | 4D 산란 LUT를 미리 계산 | C++/GLSL, BSD | 값을 바꿀 때마다 무거운 재계산. 해설이 아주 좋은 코드라 읽을거리로 쓴다 |
| Hosek-Wilkie 2012, Preetham 1999 | 해석식 하늘 | C, BSD-3 / 공식 | 하늘 색뿐이고 물리를 배우지 못한다 |

### 모델

| 주제 | 결정 |
|---|---|
| 행성 | 반지름 6360 km, 대기의 꼭대기 6460 km. 월드의 y = 0이 지면, 행성의 중심은 (0, −6360 km, 0). 씬의 1 단위는 1 m |
| 카메라 높이 | 6360 km + max(카메라 y, 0) / 1000 + 0.01 km + Study의 높이 오프셋. 0.01 km는 지면에 붙은 카메라에서 매개화가 무너지지 않게 한다(참조 구현의 `PLANET_RADIUS_OFFSET`) |
| 매질 | 이론 2. 계수는 엔진 구조체에 물리 값(1/km)으로 둔다 |
| 위상 함수 | 이론 4 |
| 다중 산란 | 이론 7, 32 × 32 LUT |
| 해 | 첫 directional light. 방향은 노드의 −Z, E_sun = `light.color × light.intensity`. 그런 빛이 없으면 하늘이 검다 |
| 단위 | 이론 0: 하늘의 radiance에 π를 곱한다. 밝기는 노출로 맞춘다 |

**지구 기본값** (Hillaire 2020; 1/km): Rayleigh 산란 (5.802, 13.558, 33.1)e-3, Mie 산란 3.996e-3, Mie 흡수 0.444e-3,
오존 흡수 (0.650, 1.881, 0.085)e-3, Mie g 0.8, 지면 반사율 0.3.

### LUT와 패스

LUT는 `RGBA16Float`: compute에서 쓰기 전용 스토리지 텍스처로 쓰고, 셰이더에서 선형 필터로 읽는다(둘 다 WebGPU 코어).

| LUT | 크기 | 변수 | 언제 만드나 | 단계 |
|---|---|---|---|---|
| 투과율 | 256 × 64 | (h, μ), 지평선 근처를 촘촘히 | 대기 값이 바뀐 프레임 | 3 |
| 다중 산란 | 32 × 32 | (h, μ_sun) | 대기 값이 바뀐 프레임, 투과율 다음 | 4 |
| 하늘 뷰 | 192 × 108 | (해 기준 방위각, 지평선 근처를 촘촘히 한 고도각), 카메라 높이와 해 방향 | 매 프레임 | 5 |
| 대기 원근 | 32 × 32 × 32 | 화면 uv × 거리 칸(제곱 분포). rgb 산란, a 투과율(회색) | 매 프레임 | 6 |
| 하늘 조도 | 버퍼, 색 둘 | 위 반구, 아래 반구 | 매 프레임 | 7 |

- "대기 값이 바뀌었다"는 `NvAtmosphere`의 매질 필드를 지난번과 비교해 안다(그림자 맵이 크기와 형식을 비교하는 것과 같다).
- compute 패스는 파티클 패스와 함께 그림자 패스 전에 기록한다. 대기가 꺼진 동안은 1 × 1 텍스처가 묶인다(`bloom_dummy`와 같다).
- 토글로 꺼진 LUT는 만들지 않는다(비용 비교가 정직하도록).

### 씬 패스

그리기 순서: 불투명 메시, **하늘**, 데칼, 디버그 라인, 선분, 알파 파티클, 가산 파티클.

| 주제 | 결정 |
|---|---|
| 하늘 | 화면 삼각형, 깊이 0(reverse Z의 가장 먼 곳), 비교 `GreaterEqual`, 깊이 쓰기 없음: 메시가 그리지 않은 픽셀만 남는다. 시선은 역 view-projection으로. 대기가 꺼지면 그리지 않고 `clear_color`가 보인다 |
| 파이프라인 | `NV_SCENE_FORMAT`, `renderer->scene_samples`. MSAA가 바뀌면 함께 다시 만든다 |
| 햇빛의 색 | `frame.light_color` = E_sun × 카메라 높이에서 해 방향의 투과율(C가 계산). 씬이 100 m 안이라 높이 차이는 버린다 |
| 대기 원근: 메시 | `fs_main`의 끝에서 color · T + S. 볼륨을 화면 uv(프래그먼트 위치 / 씬 해상도: 씬은 큰 타깃의 왼쪽 위에 그려진다)와 거리로 읽는다 |
| 대기 원근: 이펙트 (8단계) | 알파: color · T + S · alpha, 가산: color · T. 디버그 라인은 그대로 |
| 거리 배율 | `aerial_scale`이 대기 원근에서만 거리에 곱해진다(Unreal의 "Aerial Perspective View Distance Scale"). 볼륨이 덮는 거리는 카메라 `far` × 배율 |
| 그림자, 업스케일 패스 | 바뀌지 않는다 |

### 엔진

새 모듈 `engine/atmosphere.h`, `engine/atmosphere.c`: 설정, 지구 기본값, CPU의 적분(정답, ctest와 프로브와 그래프가 쓴다). GPU 쪽은
bloom처럼 `renderer.c`에 둔다. WGSL의 적분 함수는 C 함수와 같은 이름, 같은 순서로 쓴다.

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

typedef enum NvAtmosphereView {
    NV_ATMOSPHERE_VIEW_FINAL,
    NV_ATMOSPHERE_VIEW_TRANSMITTANCE, // T along each view ray
    NV_ATMOSPHERE_VIEW_SINGLE,        // single scattering only
    NV_ATMOSPHERE_VIEW_MULTIPLE,      // multiple scattering only
    NV_ATMOSPHERE_VIEW_RAYLEIGH,
    NV_ATMOSPHERE_VIEW_MIE,
    NV_ATMOSPHERE_VIEW_DIFFERENCE,    // |LUT path - direct path| / direct path, times difference_gain
    NV_ATMOSPHERE_VIEW_COUNT,
} NvAtmosphereView;

// Study tools (docs/specs/atmosphere.md, "공부 도구"). Zeroed: every optimization on, the final image.
typedef struct NvAtmosphereStudy {
    NvAtmosphereView view;
    b32 no_transmittance_lut;   // integrate toward the sun at every step instead
    b32 uniform_transmittance;  // map the transmittance LUT evenly, without crowding the horizon
    b32 no_multiple_scattering;
    b32 no_sky_view_lut;        // integrate every sky pixel
    b32 no_aerial_volume;       // integrate every fragment's ray
    b32 no_sky_ambient;         // NvRenderer.ambient (the scene's) instead
    u32 view_steps, sun_steps;  // of the direct integrals; 0 = 32, 16
    f32 altitude;               // km added to the camera's, for the atmosphere only
    f32 difference_gain;        // 0 = 10
} NvAtmosphereStudy;

// One view ray's light, split as the study tools show it (radiance times pi: the engine's units).
typedef struct NvSkySample {
    NvVec3 rayleigh, mie, multiple, ground, sun_disk;
    NvVec3 transmittance;
} NvSkySample;

NvAtmosphere nv_atmosphere_earth(void); // Earth's values (Hillaire 2020), with `on` set

// The light that reaches a point `altitude` km above the ground from a direction `cos_zenith` from straight up, 0..1 per
// channel; 0 where the ray hits the planet.
NvVec3 nv_atmosphere_transmittance(const NvAtmosphere* atmosphere, f32 altitude, f32 cos_zenith);

// The sky along `view` (unit, world) seen from `altitude` km with the sun toward `sun` (unit) giving `sun_illuminance`: the
// direct integral the GPU's reference path does too.
NvSkySample nv_atmosphere_sky(const NvAtmosphere* atmosphere, f32 altitude, NvVec3 view, NvVec3 sun, NvVec3 sun_illuminance,
                              u32 view_steps, u32 sun_steps);

// Debug study tool (step 4): adds `paths` Monte Carlo paths along `view`, every order of scattering and no approximation, to
// `sum` and `sum_squares` (for the mean and its standard error). Called a few hundred paths a frame, so the page keeps running.
void nv_atmosphere_path_trace(const NvAtmosphere* atmosphere, f32 altitude, NvVec3 view, NvVec3 sun, NvVec3 sun_illuminance,
                              u32 paths, u64* random_state, NvVec3* sum, NvVec3* sum_squares);
```

`NvRenderer`에 `NvAtmosphere atmosphere`, `NvAtmosphereStudy atmosphere_study`(앱이 매 프레임 설정, `shadows`, `post`와 같다), LUT,
파이프라인, 프로브 요청과 결과, 만든 횟수, `gpu_atmosphere_ms`가 생긴다. `FrameUniforms`에 `inverse_view_proj`와 대기 값이 더해진다.
타임스탬프 쌍 하나(`NV_TIMESTAMP_COUNT` 12 → 14).

### 샌드박스

결정 2: 대기는 씬 설정이다. 체스 씬이 만든 `lighting` 블록(씬의 빛 설정: `ambient`, chess_scene.md)과 같은 길을 간다: 파일이
기본값을 주고, View 탭에서 바꾸고, `SCNE` 안에 저장되고, undo의 Scene settings 범위에 든다.

| 주제 | 결정 |
|---|---|
| 어디에 적나 | `lighting` 블록 안의 하위 블록 `atmosphere`. 없으면 그 씬에 대기가 없다. 키는 `SceneField` 표 하나(`scene_file.c`의 `atmosphere_fields`)이고, 모두 파라미터다: 같은 행이 읽기, View 탭의 위젯, 검색, 저장, undo 이름표를 만든다(동작의 파라미터와 같은 방식) |
| 데이터 | `SceneDesc.lighting.atmosphere`(`AtmosphereDesc`: 아래 키의 값과 `on`)를 `SceneDoc.atmosphere`가 복사해 시작한다 |
| 해 | 씬의 첫 directional light. 제약 없음: 그런 빛이 없으면 하늘이 검다(오류 아님) |
| 매 프레임 | 주변광처럼(`sandbox/main.c`가 `renderer.ambient`를 넣는 곳) 보이는 씬의 `SceneDoc.atmosphere`로 `renderer.atmosphere`를 만든다. 시간으로 움직이는 것이 없어 Edit와 Play가 같다 |
| View 탭 | Ambient 행 아래 **Atmosphere** 섹션(파라미터 행), 그 아래 **Atmosphere study** 섹션(6절). 보이는 씬에 대기가 없으면 둘 다 숨는다(대기를 켜고 끄는 것은 파일이 정한다) |
| 저장 | `SCNE` 안의 새 컨테이너 `ATMO`: 파라미터마다 `PARM`{`NAME`, `VALU`}(`BHVR`와 같은 모양, 이름으로 찾으므로 키가 늘어도 옛 저장이 읽힌다). 없으면 파일의 값. 기존 태그의 뜻은 그대로이므로 `SAVE_VERSION`은 그대로다. `save.md`에 행을 더한다 |
| undo | Scene settings 범위(`SAVE_SCOPE_SCENE`)에 `ATMO`. 이름표는 "Atmosphere " + 행의 이름표. `undo.md`에 행을 더한다 |
| Study | `Sandbox.atmosphere_study`(저장 안 함, 씬마다가 아니라 하나)를 `renderer.atmosphere_study`에 넣는다 |
| Play | 하루의 흐름은 해 노드의 `spin`. Stop이 해의 회전을 되돌린다 |
| 씬 | 쇼케이스: 대기 있음, `aerial_scale`과 노출은 6단계에서 정한다. 스트레스: 없음(결정 5, 벤치마크 비교 유지). 체스: 없음(조명과 그림자의 기준 씬이라 그대로 둔다. 원하면 파일에 한 줄) |
| Textures 탭 | 투과율, 다중 산란, 하늘 뷰 LUT(`gather`) |
| 문서 | 0단계에서 `scene_file.md`(`lighting`의 하위 블록), `save.md`(`ATMO`), `undo.md`, AGENTS.md(프로브 십자의 예외는 1단계) |
| 문자열 | 새 UI 문자열은 `sandbox/strings.c`에 한국어 행 |

**`atmosphere` 블록의 키** (`lighting` 안. 모두 파라미터, 모두 선택. 배율은 지구 기본값에 곱한다)

| 키 | 범위 | 기본 | 뜻 | 이름표 | 단계 |
|---|---|---|---|---|---|
| `rayleigh <배율>` | 0–10 | 1 | Rayleigh 산란 | "Rayleigh" | 0 |
| `mie <배율>` | 0–20 | 1 | Mie 산란과 흡수 | "Haze" | 0 |
| `ozone <배율>` | 0–10 | 1 | 오존 흡수 | "Ozone" | 0 |
| `mie_anisotropy <g>` | 0–0.99 | 0.8 | 해 주변 후광의 좁기 | "Haze glow" | 2 |
| `ground_albedo r g b` | 0–1 | 0.3 0.3 0.3 | 행성 지면의 반사율 | "Ground albedo" | 2 |
| `sun_disk <도>` | 0–10 | 0.545 | 해 원반의 시직경, 0이면 없음 | "Sun disk" | 2 |
| `aerial_scale <배율>` | 0–1000 | 1 | 대기 원근의 거리 배율, 0이면 없음 | "Aerial distance" | 6 |

```
lighting
    ambient 0.15 0.15 0.15      # used where the sky's ambient is off (step 7)
    atmosphere
        aerial_scale 50
```

검색 키워드: sky, atmosphere, scattering, fog, haze, sunset, rayleigh, mie, ozone.

---

## 8. 시험

Debug 빌드가 내보낸다: `_sandbox_debug_set_atmosphere(rayleigh, mie, anisotropy, ozone, sun_disk, aerial_scale)`(보이는 씬의 `atmosphere`
블록), `_sandbox_debug_set_sun(elevation_degrees, azimuth_degrees)`, `_sandbox_debug_set_atmosphere_study(view, flags, view_steps,
sun_steps, altitude)`, `_sandbox_debug_atmosphere(n)`(0 켜짐, 1~3 LUT를 만든 횟수), `_sandbox_debug_set_probe(x, y)`,
`_sandbox_debug_probe(i)`(0 준비됨, 1~3 GPU 값, 4~6 C 값).

**ctest** (`tests/atmosphere_test.c`, `engine/atmosphere.c`만; 씬 파일은 `tests/scene_file_test.c`):

- 씬 파일: `lighting` 안의 `atmosphere`를 읽는다. 키를 빼면 기본값, 범위 밖의 값, 모르는 키, `atmosphere`를 두 번 쓰면 줄 번호와 함께 오류.
  `atmosphere`가 없으면 대기가 없다. (0단계)

- 천정 투과율이 손 계산(약 0.940, 0.868, 0.762)과 1% 안. 고도가 낮을수록 줄고 파랑이 먼저 준다. 행성을 맞히면 0, 대기 밖은 1. (0단계)
- 하늘: 천정이 파랗고(b > r), 해가 2°일 때 해 쪽 지평선이 붉고(r > b), 걸음 수를 늘리면 값이 수렴한다(64와 128의 차이 < 0.5%). (1단계)
- 위상 함수를 구 위에서 적분하면 1(수치 적분, 1%). Mie가 있으면 해 쪽이 반대쪽보다 밝다. (2단계)

**Playwright** (Release와 Debug, swiftshader). 단계마다 그 단계의 줄이 더해진다.

| 시험 | 단계 |
|---|---|
| 해를 낮추면 바닥 픽셀이 붉어진다. 파라미터를 바꾸면 undo 한 걸음(Scene settings), 저장 왕복이 같다, 다시 불러오면 값이 남는다. Play와 Stop 뒤 `save_crc`가 같다. 대기가 없는 씬(스트레스, 체스)은 그림이 전과 같다 | 0 |
| 하늘 픽셀이 파랗다. **프로브: GPU의 곧은 적분과 C가 하늘 픽셀 셋(천정 근처, 해 반대편, 지평선)에서 1% 안** | 1, 2 |
| MSAA 끔/4x, Scale 2, Fixed에서 같은 결과, WebGPU 오류 없음, 성분 보기마다 오류 없음 | 1 |
| **LUT 경로와 곧은 경로가 해의 고도 45°, 10°, 2°에서 하늘 픽셀 셋에서 상대 5% 안**(해 원반 근처 제외). 해를 돌려도 투과율 LUT 횟수가 그대로, `rayleigh`를 바꾸면 1 오름 | 3, 4, 5 |
| `aerial_scale` 1000에서 먼 바닥이 1보다 하늘 색에 가깝다. 볼륨과 프래그먼트별 곧은 적분이 5% 안 | 6 |

정답의 사슬: ctest(손 계산 ↔ C) → 프로브 시험(C ↔ GPU 곧은 적분) → 차이 시험(GPU 곧은 적분 ↔ GPU LUT).

---

## 9. 결정

모두 정함 (2026-10-11).

1. **직접 작성(Hillaire 2020), 새 라이브러리 없음.**
2. **씬 설정으로 둔다**: `lighting` 블록 안의 `atmosphere` 블록, View 탭에서 편집, `ATMO`로 저장, Scene settings 범위로 undo(7절 "샌드박스").
   해 노드의 동작으로 두는 안은 고르지 않았다.
3. **8단계(이펙트의 대기 원근)는 7단계를 마친 뒤 할지 정한다.**
4. **7단계(하늘의 주변광)를 둔다.**
5. **스트레스 씬은 대기 없음.** (체스 씬도 없음: 7절)
6. **Claude가 단계마다 코드를 쓰고, 사용자가 읽고 질문한 뒤 다음 단계로 간다**(3절). 구현은 사용자가 시작하라고 할 때 시작한다.
7. **C의 몬테카를로 경로 추적을 만든다**(4단계, Debug 빌드의 공부 도구).
8. **공부 도구를 끝까지 남긴다.**
9. **프로브는 뷰포트를 탭해 고르고 십자를 그린다**: "편집기 UI는 도크 안에"의 예외로 AGENTS.md에 적는다.

## 범위 밖 (나중)

- 볼류메트릭 그림자(빛줄기), 구름, 별, 달, 밤하늘, 높이 안개.
- 대기 밖(우주)에서 보는 시점: 높이 오프셋은 50 km까지.
- 대기 원근 볼륨의 Textures 탭 미리보기(3D 텍스처의 단면).
- Inspector의 해 고도/방위 슬라이더(지금은 기즈모의 회전).

## 용어

| 한국어 | 영어 | 뜻 |
|---|---|---|
| 복사휘도 | radiance (L) | 한 방향에서 오는 빛의 세기 |
| 복사조도 | irradiance (E) | 면에 떨어지는 빛의 총량 |
| 참여 매질 | participating media | 빛을 흡수하고 산란하는 기체, 연무 |
| 산란 / 흡수 / 소멸 계수 | scattering / absorption / extinction coefficient (σ_s, σ_a, σ_t) | 단위 거리당 빛이 꺾이는 / 사라지는 / 둘을 합친 비율 |
| 광학 깊이 | optical depth (τ) | 경로를 따라 적분한 소멸 계수 |
| 투과율 | transmittance (T) | exp(−τ), 경로를 지나 남는 비율 |
| 위상 함수 | phase function (p) | 꺾이는 각도의 분포 |
| 비등방성 | anisotropy (g) | 앞쪽으로 꺾이는 정도 |
| 스케일 높이 | scale height (H) | 밀도가 1/e가 되는 높이 |
| 단일 / 다중 산란 | single / multiple scattering | 한 번 / 여러 번 꺾인 빛 |
| 대기 원근 | aerial perspective | 먼 물체가 대기에 흐려지는 것 |
| 레이 마칭 | ray marching | 시선을 걸음으로 나눠 적분하는 것 |
| 매개화 | parametrization | 변수를 텍스처 좌표로 펼치는 방법 |
| 프록셀 | froxel | 화면 칸 × 깊이 칸의 볼륨 셀 |

## 읽을거리 (단계별)

| 자료 | 무엇 | 언제 |
|---|---|---|
| Sebastian Lague, "Coding Adventure: Atmosphere" (YouTube, 2020) | 단일 산란 하늘을 처음부터 만드는 영상. 직관을 먼저 잡기에 좋다 | 0 전 |
| Scratchapixel, "Simulating the Colors of the Sky" | 이론 1~5의 단계별 해설과 코드 | 0, 1 |
| GPU Gems 2, 16장 "Accurate Atmospheric Scattering" (Sean O'Neil) | 실시간 단일 산란의 고전 | 1 |
| Physically Based Rendering 4판, 11장 "Volume Scattering" (pbr-book.org) | 참여 매질, 위상 함수의 엄밀한 정의 | 2 |
| Sébastien Hillaire, "Physically Based Sky, Atmosphere and Cloud Rendering in Frostbite" (SIGGRAPH 2016 course) | 매질 모델과 단위, 실무 | 2 |
| Eric Bruneton, "Precomputed Atmospheric Scattering: a New Implementation" (2017, 해설이 붙은 코드) | 투과율 LUT의 매개화(지평선을 촘촘히) | 3 |
| Sébastien Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique" (EGSR 2020) | 이 스펙의 기법: 다중 산란 근사, 하늘 뷰 LUT, 대기 원근 볼륨 | 4~6 |
| sebh/UnrealEngineSkyAtmosphere (MIT) | 위 논문의 참조 구현 | 3~6 |
| Unreal Engine 문서, "Sky Atmosphere Component" | 파라미터의 실무 의미, 거리 배율 | 6 |

## 구현 결과

(단계마다 채운다: 배운 것, 틀렸던 곳, 예측과 달랐던 실험, GPU 시간.)

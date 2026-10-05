# Directional shadows spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-09-28). 이 스펙의 변경은 먼저 합의한다.

### 목표

태양이 그림자를 드리운다: 캐릭터, 행성, 스트레스 씬의 큐브가 빛에서 보아 뒤에 있는 것을 어둡게 하고, 캐릭터가
지면 위에 떠 있지 않고 서 있다. 벤치마크가 이미 GPU를 한계로 발견한 폰에서도 감당할 만해야 한다.

지금 렌더러에는 씬 패스 하나가 있다 (`engine/renderer.c`): 메시 노드마다 그리기 하나이고, 프래그먼트 셰이더가 첫
방향광 (Lambert 더하기 ambient)으로 그림자 없이 비춘다.

### 접근법

| 접근법 | 무엇인가 | 맞음 | 장단점 |
|---|---|---|---|
| **뷰에 맞춘 그림자 맵 하나** (추천 첫 단계) | 빛에서 본 깊이를 카메라 뷰를 그림자 거리까지 덮는 텍스처 하나에 렌더링한다; 씬 패스가 그것과 비교한다 | 깊이만의 패스 하나 추가; 두 씬 모두에 동작; 안정되게 만들기 단순 | 해상도가 그림자 거리 전체에 퍼진다: 작은 씬 근처에서는 선명하고, 스트레스 그리드 위에서는 흐리다 |
| 계단식 그림자 맵 (캐스케이드 2–4개) | 각각 뷰 거리의 한 조각을 덮는 여러 맵 | 넓은 야외 뷰의 표준 | 캐스케이드마다 패스 하나 (그리기가 다시 곱해진다), 캐스케이드 사이 블렌딩. 맵 하나가 너무 흐리면 나중 단계 |
| 씬 주변의 고정 맵 | 고정된 월드 경계 위의 맵 하나 | 가장 단순 | 스트레스 그리드는 폭 약 150 m: 거기서는 너무 거칠다 |
| 레이 트레이싱이나 화면 공간 그림자 | | | WebGPU에 없거나, 폰에서 너무 비싸다 |
| 라이브러리 | 맞는 것이 없다: 그림자는 렌더러 자체의 패스와 셰이더다 | | |

추천: 뷰에 맞춘 맵 하나, 나중에 캐스케이드를 더할 수 있게 코드를 짠다 (빛 행렬과 맵은 처음부터 캐스케이드별이며,
캐스케이드는 하나).

### 결정

| 주제 | 결정 |
|---|---|
| 빛 | 지금처럼 첫 방향광. 다른 빛 종류는 그림자를 드리우지 않는다 |
| 맵 | 기본 2048², 터치 화면 (`NvImgui.ui_scale` > 1)에서 1024², 512, 1024, 2048 또는 꺼짐으로 설정 가능 |
| 형식 | `depth32float` (기본값)와 `depth16unorm` 둘 다, 설정에서 고른다. 바꾸면 맵과 그림자 파이프라인을 다시 만든다 (깊이 형식은 파이프라인의 일부다). `depth16unorm`은 맵의 메모리와 대역폭을 반으로 줄인다; `depth32float`는 빛의 깊이 범위가 커져도 더 높은 정밀도를 유지한다. Stress 탭의 그림자 패스 시간이 장치에서 그 선택의 비용을 보여 준다 |
| 맞춤 | 카메라의 near 평면부터 **그림자 거리** (기본 30 m)까지의 뷰를 구로 감싼다; 빛의 정사영 상자는 그 구의 정사각형이고, 뷰 뒤의 caster도 그림자를 드리우도록 빛 쪽으로 50 m 물러난다. 카메라가 돌아도 구의 반지름은 바뀌지 않고, 중심은 그림자 맵 텍셀 단위로 맞춰지므로, 공전하는 동안 그림자가 반짝이거나 기어 다니지 않는다 |
| Caster | 월드 상자가 빛의 상자 밖에 있는 것 (picking이 이미 쓰는 상자로 하는 CPU 검사)을 뺀 모든 메시 노드, static과 skinned (skinned 정점 셰이더의 스키닝을 재사용). 양면 머티리얼은 두 면 모두에서 드리운다 |
| Receiver | 모든 메시 노드. 그림자는 방향광 항만 어둡게 한다; ambient 항은 남는다 |
| 필터링 | 선형 필터링의 비교 샘플러, 그래서 각 조회가 하드웨어의 2×2 percentage-closer 필터다. **High**: 그런 조회 3×3 (부드러운 4×4 텍셀 가장자리). **Low**: 조회 하나. 기본 High, 터치 화면에서 Low |
| 여드름과 peter-panning | 그림자 파이프라인의 기울기 비례 깊이 바이어스와, 씬 셰이더의 노멀 오프셋 (조회가 표면 노멀을 따라 약 한 텍셀 움직인다). 상수 바이어스는 형식마다 다른 뜻이므로 (아래), 각 형식은 파이프라인과 함께 설정되는 자체 조정 바이어스 값을 가진다; 기울기 배율과 노멀 오프셋은 공유된다. 모두 쇼케이스와 스트레스 씬에서 조정한다 |
| 거리 너머 | 그림자 없음, 마지막 10%에 걸쳐 흐려져 가장자리가 선이 되지 않는다 |
| 설정 | View 탭의 **Shadows** 섹션: 크기 (꺼짐, 512, 1024, 2048), 형식 (32비트 float, 16비트), 필터 (Low, High), 거리 (5–100 m), "Show light box" (맞춘 상자를 디버그 라인으로). 에디터 설정과 함께 저장 (`save.md`에 따른 새 `EDIT` 태그) |
| 비용 표시 | Stress 탭에 그림자 그리기와 그림자 패스의 GPU 시간 (두 번째 타임스탬프 쌍)이 생긴다. 벤치마크 표에 실행 때의 그림자 설정 (크기, 형식, 필터)이 생긴다 |
| 서드파티 | 없음 |

### GPU 쪽에 관한 메모

이것들은 브라우저가 어떤 GPU나 드라이버 위에서 돌든 명세된 WebGPU에 대해 성립한다.

- **깊이만의 패스는 싸다.** 그림자 파이프라인에는 프래그먼트 단계가 없고 discard하지 않으므로, 패스는 깊이만 쓰고
  픽셀 셰이딩을 하지 않는다.
- **하드웨어 PCF.** 비교 함수와 선형 필터링이 있는 샘플러는 `textureSampleCompare` 한 번이 가장 가까운 텍셀 네 개를
  비교하고 결과를 섞게 한다: 조회 하나의 비용으로 2×2 percentage-closer 필터. High 필터는 그런 조회 아홉 개다.
- **두 형식.** 모든 WebGPU 구현이 둘 다에 렌더링하고 둘 다를 비교 샘플러로 샘플링할 수 있으므로, 둘 다 항상 제공한다.
  - `depth32float`는 깊이 범위 조정이 필요 없다: 그림자 거리와 빛 쪽으로 당긴 50 m가 무엇이든 정밀도가 넉넉하다.
    2048²에서 16 MB다.
  - `depth16unorm`은 빛의 깊이 범위에 걸쳐 고른 65,536 단계를 가진다 (정사영 투영은 깊이를 선형으로 펼친다), 100 m에
    약 1.5 mm. 2048²에서 8 MB이고, 쓰고 읽는 대역폭이 반이다.
- **깊이 바이어스는 형식에 따른다.** 파이프라인의 상수 `depthBias`는 깊이 형식의 가장 작은 단계를 센다. 그 단계는
  `depth16unorm` 같은 정규화 형식에서는 고정이지만 (범위의 1/65,536), `depth32float`에서는 삼각형 자체의 깊이에 따른다.
  그래서 상수 하나가 둘 다에 맞을 수 없다: 각 형식의 파이프라인이 자체 조정 값을 가진다.
- **한 패스가 다른 패스가 쓴 것을 읽는다.** 그림자 맵은 같은 커맨드 버퍼의 그림자 패스에서 쓰고 씬 패스에서
  샘플링한다; WebGPU가 두 패스의 순서를 정하므로 명시적 배리어가 필요 없다.

### 엔진 변경

- **렌더러 (`engine/renderer.h`, `engine/renderer.c`).**
  - `NvShadowSettings` (크기, 형식, 필터, 거리)와 `nv_renderer_set_shadows`.
  - 크기나 형식이 바뀌면 다시 만드는 그림자 맵 텍스처와 그 뷰; 비교 샘플러.
  - `FrameUniforms`에 빛의 view-projection 행렬, 맵의 텍셀 크기, 필터, 거리가 생긴다; 프레임 bind group에 맵과 샘플러가
    생긴다.
  - 고른 형식을 위한 깊이만의 파이프라인 (static과 skinned, 각각 양면 변형과 함께), 그 형식의 바이어스와 함께, 형식이
    바뀌면 다시 만든다. 빛의 행렬로 변환하는 새 정점 진입점을 쓴다.
  - `nv_renderer_draw`는 그림자 패스를 먼저, 그다음 씬 패스를 기록한다; 프래그먼트 셰이더가 방향광 항에 그림자를
    적용한다.
  - 요청하면 빛 상자를 디버그 라인으로; 그림자 그리기와 그림자 패스 GPU 시간 통계.
- **샌드박스.** View 탭의 Shadows 섹션, 그 저장 태그, 터치 화면 기본값, 벤치마크의 그림자 열.

### 단계

1. **그림자:** 맵 (`depth32float`), 맞춘 빛 상자, static과 skinned 메시를 위한 깊이만의 파이프라인, 기울기 비례
   바이어스와 하드웨어 PCF 조회 하나, View 탭의 켜기/끄기. 확인: 쇼케이스에서 캐릭터와 행성의 그림자가 지면에 떨어짐;
   스트레스 씬이 최대 워크로드에서 assert 없이 그려짐.
2. **품질:** 3×3 필터, 노멀 오프셋, 텍셀 맞춤 (공전하는 동안 반짝임 없음, 스크린샷에서 프레임끼리 비교), 거리와 그
   흐려짐, caster 컬링, 빛 상자 라인, 자체 바이어스가 있는 `depth16unorm` 옵션 (어느 형식에서도 여드름이나
   peter-panning 없음, 스크린샷에서 비교), 저장된 설정과 그 터치 기본값.
3. **비용과 문서:** 그림자 통계와 GPU 시간, 벤치마크 열, 폰 크기 확인, `AGENTS.md`와 README.

만든 결과로는, 2단계의 엔진 쪽 (3×3 필터, 노멀 오프셋, 텍셀 맞춤, 흐려짐, 빛 상자)이 같은 코드에서 각각 몇 줄이라서
1단계와 함께 왔다; 2단계는 그다음 caster 컬링, 저장된 설정, `depth16unorm` 확인을 더했다. 폰 크기에서 터치 기본값
(1024, Low)은 눈에 보이게 계단진 그림자 가장자리를 준다; High는 아홉 번의 조회로 그것을 부드럽게 한다.

모든 단계는 헤드리스 Chromium에서 Release와 Debug로, 데스크톱과 폰 크기에서 확인한다. SwiftShader의 시간은 의미가
없다; 실제 비용은 장치에서 나온다.

## English

Status: implemented (2026-09-28). Changes to this spec are agreed first.

### Goal

The sun casts shadows: the character, the planet and the stress scene's cubes darken what is
behind them from the light, and the character stands on the ground instead of floating over it.
It has to stay affordable on a phone, where the benchmark already found the GPU to be the limit.

Today the renderer has one scene pass (`engine/renderer.c`): every mesh node is one draw, and
the fragment shader lights it with the first directional light (Lambert plus ambient), with no
shadows.

### Approaches

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **One shadow map, fitted to the view** (recommended first step) | Render depth from the light into one texture that covers the camera's view up to a shadow distance; the scene pass compares against it | One extra depth-only pass; works for both scenes; simple to get stable | Resolution is spread over the whole shadow distance: sharp near a small scene, soft over the stress grid |
| Cascaded shadow maps (2 to 4 cascades) | Several maps, each covering a slice of the view distance | The standard for large outdoor views | One pass per cascade (the draws multiply again), and blending between cascades. A later step if the single map is too soft |
| A fixed map around the scene | One map over fixed world bounds | Simplest | The stress grid is about 150 m wide: far too coarse there |
| Ray-traced or screen-space shadows | | | Not available in WebGPU, or far too costly on phones |
| A library | None fits: shadows are the renderer's own passes and shaders | | |

Recommendation: one map fitted to the view, with the code shaped so cascades can be added later
(the light matrix and the map are per cascade from the start, with one cascade).

### Decisions

| Topic | Decision |
|---|---|
| Light | The first directional light, as today. Other light types cast no shadows |
| Map | 2048² by default, 1024² on touch screens (`NvImgui.ui_scale` > 1), settable to 512, 1024, 2048 or off |
| Format | Both `depth32float` (the default) and `depth16unorm`, chosen in the settings. Changing it remakes the map and the shadow pipelines (the depth format is part of a pipeline). `depth16unorm` halves the map's memory and bandwidth; `depth32float` keeps more precision if the light's depth range grows. The Stress tab's shadow pass time shows what the choice costs on a device |
| Fitting | The camera's view from its near plane to the **shadow distance** (30 m by default) is enclosed in a sphere; the light's orthographic box is that sphere's square, pulled back toward the light by 50 m so casters behind the view still cast. The sphere's radius does not change as the camera turns, and its center is snapped to whole shadow-map texels, so shadows do not shimmer or crawl while orbiting |
| Casters | Every mesh node, static and skinned (the skinned vertex shader's skinning is reused), except those whose world box is outside the light's box (a CPU test with the boxes picking already uses). Double-sided materials cast from both faces |
| Receivers | Every mesh node. The shadow darkens only the directional light's term; the ambient term stays |
| Filtering | A comparison sampler with linear filtering, so each lookup is a 2×2 percentage-closer filter in hardware. **High**: 3×3 such lookups (a soft 4×4-texel edge). **Low**: one lookup. High by default, Low on touch screens |
| Acne and peter-panning | A slope-scaled depth bias in the shadow pipeline, and a normal offset in the scene shader (the lookup moves along the surface normal by about one texel). The constant bias means different things per format (below), so each format has its own tuned bias values, set with its pipelines; the slope scale and the normal offset are shared. All are tuned in the showcase and the stress scene |
| Beyond the distance | Unshadowed, faded out over the last 10% so the edge is not a line |
| Settings | A **Shadows** section in the View tab: size (off, 512, 1024, 2048), format (32-bit float, 16-bit), filter (Low, High), distance (5 to 100 m), and "Show light box" (the fitted box as debug lines). Saved with the editor settings (new `EDIT` tags, per `save.md`) |
| Cost shown | The Stress tab gains shadow draws, and the GPU time of the shadow pass (a second timestamp pair). The benchmark table gains the shadow settings it ran with (size, format, filter) |
| Third-party | None |

### Notes on the GPU side

These hold for WebGPU as specified, whatever GPU or driver the browser runs on.

- **A depth-only pass is cheap.** The shadow pipelines have no fragment stage and never discard,
  so the pass writes depth only and does no pixel shading.
- **Hardware PCF.** A sampler with a comparison function and linear filtering makes one
  `textureSampleCompare` compare the four nearest texels and blend the results: a 2×2
  percentage-closer filter for the cost of one lookup. The High filter is nine such lookups.
- **The two formats.** Every WebGPU implementation can render to both and sample both with a
  comparison sampler, so both are always offered.
  - `depth32float` needs no depth range tuning: precision stays ample whatever the shadow
    distance and the 50 m pulled toward the light. It is 16 MB at 2048².
  - `depth16unorm` has 65,536 even steps over the light's depth range (the orthographic
    projection spreads depth linearly), about 1.5 mm over 100 m. It is 8 MB at 2048², and half
    the bandwidth to write and read.
- **Depth bias depends on the format.** The pipeline's constant `depthBias` counts the depth
  format's smallest step. That step is fixed for a normalized format like `depth16unorm`
  (1/65,536 of the range), but for `depth32float` it depends on the triangle's own depth. So one
  constant cannot suit both: each format's pipelines carry their own tuned value.
- **One pass reads what the other wrote.** The shadow map is written in the shadow pass and
  sampled in the scene pass of the same command buffer; WebGPU orders the two passes, so no
  explicit barrier is needed.

### Engine changes

- **Renderer (`engine/renderer.h`, `engine/renderer.c`).**
  - `NvShadowSettings` (size, format, filter, distance) and `nv_renderer_set_shadows`.
  - The shadow map texture and its views, remade when the size or the format changes; a
    comparison sampler.
  - `FrameUniforms` gains the light's view-projection matrix, the map's texel size, the filter
    and the distance; the frame bind group gains the map and the sampler.
  - Depth-only pipelines (static and skinned, each with the double-sided variant) for the chosen
    format, with that format's bias, remade when the format changes. They use new vertex entry
    points that transform by the light's matrix.
  - `nv_renderer_draw` records the shadow pass first, then the scene pass; the fragment shader
    applies the shadow to the directional term.
  - The light box as debug lines on request; stats for shadow draws and the shadow pass's GPU
    time.
- **Sandbox.** The View tab's Shadows section, its save tags, the touch-screen defaults, and the
  benchmark's shadow column.

### Phases

1. **Shadows:** the map (`depth32float`), the fitted light box, the depth-only pipelines for static and skinned
   meshes, one hardware PCF lookup with a slope-scaled bias, and on/off in the View tab. Checked:
   the character's and the planet's shadows land on the ground in the showcase; the stress scene
   draws without asserts at its maximum workloads.
2. **Quality:** the 3×3 filter, the normal offset, texel snapping (no shimmer while orbiting,
   compared frame to frame in screenshots), the distance and its fade, caster culling, the light
   box lines, the `depth16unorm` option with its own bias (no acne or peter-panning in either
   format, compared in screenshots), the settings saved and their touch defaults.
3. **Cost and docs:** the shadow stats and GPU time, the benchmark column, a phone-size check,
   `AGENTS.md` and README.

As built, the engine side of phase 2 (the 3×3 filter, the normal offset, texel snapping, the fade
and the light box) arrived with phase 1, since they are a few lines each in the same code; phase 2
then added caster culling, the saved settings and the `depth16unorm` checks. At phone size the
touch defaults (1024, Low) give visibly stepped shadow edges; High smooths them at nine lookups.

Every phase is checked in Release and Debug in headless Chromium, at desktop and phone size.
SwiftShader's timings mean nothing; real costs come from devices.

# MSAA spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-10-01). 이 스펙의 변경은 먼저 합의한다.

### 목표

4× 다중 샘플 안티앨리어싱 (MSAA)으로 씬의 삼각형 가장자리를 부드럽게 한다. 지금 씬 패스는 픽셀당 샘플 하나로
캔버스에 바로 그리므로, 모든 윤곽 (캐릭터, 큐브, 지면의 지평선)과 모든 디버그 라인이 계단 모양이고, 폰에서 가장
잘 보인다.

엔진은 포워드 렌더러이고, MSAA에 맞는 경우다: 프래그먼트 셰이더는 여전히 삼각형마다 픽셀당 한 번 돈다; 샘플마다
유지되는 것은 커버리지와 깊이뿐이고, 패스 끝에서 샘플을 평균 내어 (resolve) 캔버스에 넣는다.

### 접근법

| 접근법 | 무엇인가 | 맞음 | 장단점 |
|---|---|---|---|
| **씬 패스의 MSAA 4×** (추천) | 씬 패스가 4-샘플 색 텍스처와 4-샘플 깊이 텍스처에 렌더링한다; WebGPU가 패스 끝에서 색을 캔버스로 resolve한다 (`resolveTarget`) | WebGPU에 내장 (`sampleCount` 4는 모든 구현이 지원하는 유일한 수). 삼각형 안쪽의 셰이딩 비용은 같다; 가장자리 픽셀만 두 번 이상 셰이딩한다. `renderer.c`에 수십 줄 | 메모리: 두 텍스처가 단일 샘플의 4배 크기 (아래). 셰이더 안에서 생긴 앨리어싱 (날카로운 스펙큘러, 텍스처 디테일)에는 아무것도 하지 않는데, 이 렌더러에는 지금 거의 없다 |
| FXAA 또는 SMAA (후처리 패스) | 완성된 이미지에서 가장자리를 찾아 그것을 따라 흐린다 | 싼 전체 화면 패스 하나; 셰이더 앨리어싱도 부드럽게 한다 | 먼저 씬이 오프스크린 텍스처에 있어야 한다 (없다: 씬은 캔버스에 그린다), 글자 같은 디테일과 가는 선을 흐리고, 움직일 때 기어 다닌다. SMAA는 조회 테이블 텍스처도 필요하다 |
| TAA (시간적) | 매 프레임 카메라를 흔들고 지난 프레임과 섞는다 | 큰 엔진에서 비용 대비 최고 품질 | 모션 벡터, 이력 텍스처, 고스팅 수정이 필요하다; 지금 이 렌더러에는 너무 많다 |
| 슈퍼샘플링 | 2배 크기로 렌더링하고 줄인다 | 이해하기 가장 단순 | 모든 곳에서 셰이딩 비용 4배; 폰은 이미 GPU 한계다 |
| 라이브러리 | 없음: MSAA는 API 자체의 것이고, 후처리 필터는 우리가 옮길 셰이더 코드다 | | |

추천: Off를 설정으로 남긴 MSAA 4×. 서드파티 라이브러리 없음.

### 결정

| 주제 | 결정 |
|---|---|
| 샘플 수 | Off (1)와 4×. WebGPU는 렌더 어태치먼트에 1과 4만 보장하므로 2×와 8×는 제공하지 않는다 |
| 기본값 | 모든 장치에서 4×. Stress 탭이 장치마다 비용을 보여 주고 (아래), 설정으로 끌 수 있다 |
| 설정 | View 탭의 Shadows 섹션 옆, **Anti-aliasing** 콤보 (Off, MSAA 4×), 두 UI 모두 |
| 저장 | 새 `EDIT` 태그 `MSAA` (u32: 1 또는 4; 다른 값은 4로 불러옴). 없으면: 기본값 |
| 엔진 API | `NvRenderer.msaa` (샘플 수), `NvRenderer.shadows`처럼 앱이 설정. `nv_renderer_draw`는 시그니처를 유지한다: 받은 캔버스 뷰가 resolve 타깃이 된다 |
| 색 타깃 | 캔버스 크기와 그 렌더 형식 (sRGB 뷰 형식, 그래서 샘플이 선형 공간에서 평균되어 가장자리가 어두워지지 않음)의 4-샘플 텍스처. Store op Discard: resolve된 캔버스만 남는다. 깊이 버퍼처럼 캔버스 크기, 형식, 샘플 수가 바뀌면 다시 만든다 |
| 깊이 타깃 | 기존 `depth32float` 타깃도 4-샘플이 된다 (한 패스의 어태치먼트는 샘플 수를 공유한다). Reverse Z는 바뀌지 않는다. WebGPU는 깊이를 resolve할 수 없으므로, Textures 탭이 유지하지 않는 한 (아래) Discard로 남는다 |
| 파이프라인 | 씬 패스에서 쓰는 모든 파이프라인 (메시, static과 skinned, 단면과 양면, 디버그 라인)이 `multisample.count` = 설정을 받고, 그것이 바뀌면 다시 만든다. 그림자 패스와 ImGui 패스는 단일 샘플로 남는다: 자체 어태치먼트가 있다 |
| 뷰포트 | 씬 패스는 이미 캔버스 안에 뷰포트와 시저를 설정한다; resolve는 캔버스 전체를 쓰는데, 이것은 지금 패스의 clear가 하는 일이고, 에디터 창은 나중에 나머지 위에 그린다 |
| 알파 | 변경 없음. 알파 테스트 (`discard`)를 쓰는 것이 없으므로 alpha to coverage는 필요 없다. 블렌딩되는 디버그 라인은 샘플마다 블렌딩한다 |
| Textures 탭 | 깊이 타깃을 4-샘플 텍스처로 보여 준다: 두 번째 깊이 미리보기 파이프라인이 `textureLoad(…, sample 0)`로 `texture_depth_multisampled_2d`를 읽는다. 4-샘플 색 텍스처도 미리보기 없이 나열하므로 (매 프레임 버려진다) Render targets 합계가 그 메모리를 센다; 크기는 샘플 수를 곱한다 |
| Picking, 기즈모, 그림자 | 영향 없음: picking은 CPU 광선, 기즈모는 ImGui 패스에서 그리고, 그림자 맵에는 자체 패스가 있다 |
| 비용 표시 | 시작 로그의 색과 깊이 타깃 줄이 각각의 샘플 수와 메모리를 준다. Stress 탭에 "MSAA" 줄이 생기고, 벤치마크 표는 실행 때의 설정을 기록한다 |
| 서드파티 | 없음 |

### 메모리

4-샘플 타깃은 각각 단일 샘플의 네 배다: `bgra8unorm-srgb`와 `depth32float` 모두 픽셀당 4바이트 × 4샘플.

| 캔버스 (프레임버퍼 픽셀) | 색, 4× | 깊이, 4× (지금 1×) | MSAA가 더하는 양 |
|---|---|---|---|
| 1280 × 800 (데스크톱) | 16 MB | 16 MB (4 MB) | 28 MB |
| 1920 × 1080 | 32 MB | 32 MB (8 MB) | 56 MB |
| 1170 × 2532 (3× pixel ratio의 폰) | 45 MB | 45 MB (11 MB) | 79 MB |

모바일 GPU는 패스의 샘플을 칩 위의 타일 메모리에 두고 resolve된 픽셀만 내보내므로 그쪽 대역폭 비용은 작지만,
메모리는 여전히 할당된다: 브라우저는 WebGPU 페이지에 "transient attachment"를 노출하지 않는다. 타깃을 캔버스 전체
대신 씬 뷰포트 (폰 높이의 60%) 크기로 잡으면 그만큼 아낄 수 있지만, resolve 뒤 캔버스로의 복사가 든다; Stress 탭이
그것이 중요한지 보여 줄 때까지 나중으로 남긴다.

### 변경

- **렌더러 (`nv/renderer.h`, `engine/src/renderer.c`).** `NvRenderer.msaa`; 4-샘플 색 텍스처와 그 뷰; 샘플 수로 깊이
  타깃을 만드는 `update_depth_buffer`; 그것으로 만든 씬 파이프라인 (바뀌면 다시 만듦); 캔버스를 `resolveTarget`으로
  한 4-샘플 뷰를 가리키는 씬 패스의 색 어태치먼트, Off일 때는 캔버스를 바로 가리킴; 샘플 수와 크기가 든 로그 줄.
- **ImGui 미리보기 (`engine/src/imgui.c`).** 미리보는 깊이 텍스처의 샘플이 둘 이상일 때 고르는 다중 샘플 깊이
  미리보기 파이프라인과 bind group layout.
- **앱.** View 탭의 Anti-aliasing 콤보 (`app/ui.c`), `MSAA` 태그 (`app/save.c`와 `save.md`), Stress 탭의 줄과
  벤치마크의 열 (`app/stress.c`).
- **문서.** `textures.md` (다중 샘플 깊이), `stress.md`, `AGENTS.md`, README.

### 단계

1. **MSAA:** 렌더러의 설정, 4-샘플 색과 깊이 타깃, 씬 파이프라인과 resolve, 기본 4×인 View 탭 콤보. 확인: 정지
   프레임의 가장자리에 4×에서는 섞인 픽셀이 있고 Off에서는 없다 (캐릭터의 윤곽과 큐브의 가장자리를 따라 스크린샷
   비교), 삼각형 안쪽은 바뀌지 않음, 실행 중 전환과 창 크기 조절에서 WebGPU 오류 없음, picking과 기즈모가 여전히
   동작.
2. **주변:** `MSAA` 저장 태그 (새로고침이 설정을 유지; 옛 저장은 4×를 받음), Textures 탭의 다중 샘플 깊이
   미리보기, 메모리가 든 로그 줄.
3. **비용과 문서:** Stress 탭 줄과 벤치마크 열 (스트레스 씬의 가장 큰 워크로드에서 Off와 4×의 GPU 씬 패스 시간),
   폰 크기 확인, 위의 문서들.

모든 단계는 헤드리스 Chromium에서 Release와 Debug로, 데스크톱과 폰 크기에서 확인한다. SwiftShader의 시간은 의미가
없다; 실제 비용은 장치에서 나온다.

만든 결과로는, 콤보, 저장 태그, Textures 항목이 단계 순서대로 왔다; 아래 메모는 빌드가 가르쳐 준 것이다:

- **색 타깃은 캔버스처럼 만든다**: 캔버스 자체 형식 (`BGRA8Unorm`)에 sRGB 렌더 형식을 뷰 형식으로 두고, sRGB 뷰를
  통해 렌더링한다. sRGB 형식으로 직접 만든 텍스처는 SwiftShader 위의 Chromium에서 캔버스로 resolve될 때 약 한 단계
  어둡게 나왔다 (캔버스가 인코딩된 값을 기대하는 곳에 resolve가 선형 값을 썼다); 캔버스처럼 만들면 4×와 Off
  이미지의 색이 같다.
- `resolution.md` 이후로 4-샘플 색과 깊이 타깃은 캔버스가 아니라 씬의 해상도 (64로 올림)이고, 4-샘플 색은 씬 색
  타깃으로 resolve된다; 업스케일 패스가 그것을 캔버스에 넣는다. 위의 메모리 표는 캔버스 크기 타깃 기준이다.
- `NvRenderer.scene_samples`는 파이프라인과 깊이 타깃이 가진 것이다; `NvRenderer.msaa`는 앱이 요청하는 것이고,
  `update_msaa` (`update_depth_buffer` 전)가 매 프레임 둘을 맞춘다. 엔진의 기본값은 1이다; 앱이 4를 설정한다.
- Debug 빌드는 테스트가 UI 없이 전환하는 데 쓰는 `_app_debug_msaa`와 `_app_debug_set_msaa`를 export한다.
- 확인: 큐브 가장자리를 따라 4×에서 24개의 서로 다른 색, Off에서 4개, 삼각형 안쪽의 같은 색, 실행 중 전환과 크기
  조절에서 WebGPU 오류 없음, 스트레스 씬이 4×로 그려짐, Textures 탭이 다중 샘플 깊이를 미리 봄, 새로고침이 설정을
  유지, 저장 안의 지원하지 않는 수는 4×로 불러옴, 태그 없는 저장은 4×를 줌.
- SwiftShader에서 스트레스 씬의 GPU 씬 패스는 4×일 때 Off보다 약 2.5배 오래 걸렸다 (기본 워크로드에서 144 ms 대
  365 ms); 실제 GPU에 대해서는 아무것도 말해 주지 않는다.

### 범위 밖

- 2×와 8× (WebGPU가 보장하지 않음), FXAA, SMAA, TAA.
- 뷰포트 크기의 타깃 (메모리 참고).
- 머티리얼이 알파 테스트를 쓸 때까지 alpha to coverage.

## English

Status: implemented (2026-10-01). Changes to this spec are agreed first.

### Goal

Smooth the scene's triangle edges with 4× multisample anti-aliasing (MSAA). Today the scene pass
draws straight into the canvas with one sample per pixel, so every silhouette (the character,
the cubes, the ground's horizon) and every debug line is stair-stepped, most visibly on a phone.

The engine is a forward renderer, the case MSAA suits: the fragment shader still runs once per
pixel per triangle; only coverage and depth are kept per sample, and the samples are averaged
(resolved) into the canvas at the end of the pass.

### Approaches

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **MSAA 4× in the scene pass** (recommended) | The scene pass renders into a 4-sample color texture and a 4-sample depth texture; WebGPU resolves the color into the canvas at the pass's end (`resolveTarget`) | Built into WebGPU (`sampleCount` 4 is the one count every implementation supports). Shading cost stays the same inside triangles; only edge pixels shade more than once. A few dozen lines in `renderer.c` | Memory: the two textures are 4× the size of a single-sample one (below). It does nothing for aliasing made inside a shader (sharp specular, texture detail), which this renderer barely has today |
| FXAA or SMAA (a post pass) | Finds edges in the finished image and blurs along them | One cheap full-screen pass; also softens shader aliasing | Needs the scene in an offscreen texture first (there is none: the scene draws into the canvas), blurs text-like detail and thin lines, and crawls in motion. SMAA also needs lookup-table textures |
| TAA (temporal) | Jitters the camera every frame and blends with past frames | Best quality per cost in large engines | Needs motion vectors, history textures and ghosting fixes; far too much for this renderer now |
| Supersampling | Renders at 2× size and scales down | Simplest to reason about | 4× the shading cost everywhere; the phone is already GPU-bound |
| A library | None: MSAA is the API's own, and the post-process filters are shader code we would port | | |

Recommendation: MSAA 4×, with Off kept as a setting. No third-party library.

### Decisions

| Topic | Decision |
|---|---|
| Sample counts | Off (1) and 4×. WebGPU guarantees only 1 and 4 for render attachments, so 2× and 8× are not offered |
| Default | 4× on every device. The Stress tab shows what it costs on each one (below), and the setting turns it off |
| Setting | An **Anti-aliasing** combo in the View tab (Off, MSAA 4×), beside the Shadows section, on both UIs |
| Saved | A new `EDIT` tag, `MSAA` (u32: 1 or 4; anything else loads as 4). Missing: the default |
| Engine API | `NvRenderer.msaa` (the sample count), set by the app like `NvRenderer.shadows`. `nv_renderer_draw` keeps its signature: the canvas view it is given becomes the resolve target |
| Color target | A 4-sample texture of the canvas's size and its render format (the sRGB view format, so samples are averaged in linear space and edges are not darkened). Store op Discard: only the resolved canvas is kept. Remade when the canvas size, the format or the sample count changes, like the depth buffer |
| Depth target | The existing `depth32float` target becomes 4-sample too (a pass's attachments share one sample count). Reverse Z is unchanged. WebGPU cannot resolve depth, so it stays Discard, unless the Textures tab keeps it (below) |
| Pipelines | Every pipeline used in the scene pass (the meshes, static and skinned, single- and double-sided, and the debug lines) gets `multisample.count` = the setting, and is remade when it changes. The shadow pass and the ImGui pass stay single-sample: they have their own attachments |
| Viewport | The scene pass already sets a viewport and scissor inside the canvas; the resolve writes the whole canvas, which is what the pass's clear does today, and the editor's windows draw over the rest afterwards |
| Alpha | No change. Nothing uses alpha testing (`discard`), so alpha to coverage is not needed. The blended debug lines blend per sample |
| Textures tab | It shows the depth target as a 4-sample texture: a second depth preview pipeline reads `texture_depth_multisampled_2d` with `textureLoad(…, sample 0)`. The 4-sample color texture is listed too, without a preview (it is discarded every frame), so the Render targets total counts its memory; sizes are multiplied by the sample count |
| Picking, gizmo, shadows | Unaffected: picking is a CPU ray, the gizmo draws in the ImGui pass, the shadow map has its own pass |
| Cost shown | The startup log's color and depth target lines give the sample count and the memory of each. The Stress tab gains an "MSAA" line, and the benchmark table records the setting it ran with |
| Third-party | None |

### Memory

Each 4-sample target is four times a single-sample one: 4 bytes × 4 samples per pixel for both
`bgra8unorm-srgb` and `depth32float`.

| Canvas (framebuffer pixels) | Color, 4× | Depth, 4× (today 1×) | Added by MSAA |
|---|---|---|---|
| 1280 × 800 (desktop) | 16 MB | 16 MB (4 MB) | 28 MB |
| 1920 × 1080 | 32 MB | 32 MB (8 MB) | 56 MB |
| 1170 × 2532 (a phone at 3× pixel ratio) | 45 MB | 45 MB (11 MB) | 79 MB |

Mobile GPUs keep a pass's samples in on-chip tile memory and write out only the resolved pixels,
so the bandwidth cost there is small, but the memory is still allocated: browsers expose no
"transient attachment" to WebGPU pages. Sizing the targets to the scene viewport instead of the
whole canvas (60% of a phone's height) would save that share, at the cost of a copy into the
canvas after the resolve; it is left for later, once the Stress tab shows whether it matters.

### Changes

- **Renderer (`nv/renderer.h`, `engine/src/renderer.c`).** `NvRenderer.msaa`; the 4-sample color
  texture and its view; `update_depth_buffer` creating the depth target with the sample count;
  the scene pipelines created with it (remade on change); the scene pass's color attachment
  pointing at the 4-sample view with the canvas as `resolveTarget`, or straight at the canvas when
  Off; log lines with the sample count and the sizes.
- **ImGui previews (`engine/src/imgui.c`).** A multisampled depth preview pipeline and bind group
  layout, chosen when the previewed depth texture has more than one sample.
- **App.** The View tab's Anti-aliasing combo (`app/ui.c`), the `MSAA` tag (`app/save.c` and
  `save.md`), the Stress tab's line and the benchmark's column (`app/stress.c`).
- **Docs.** `textures.md` (the multisampled depth), `stress.md`, `AGENTS.md` and README.

### Phases

1. **MSAA:** the setting in the renderer, the 4-sample color and depth targets, the scene
   pipelines and the resolve, the View tab combo with 4× by default. Checked: edges in a still
   frame have blended pixels with 4× and none with Off (screenshots compared along the
   character's silhouette and a cube's edge), the inside of triangles is unchanged, switching at
   runtime and resizing the window raise no WebGPU errors, picking and the gizmo still work.
2. **Around it:** the `MSAA` save tag (a reload keeps the setting; an old save gets 4×), the
   Textures tab's multisampled depth preview, the log lines with the memory.
3. **Cost and docs:** the Stress tab line and the benchmark column (the GPU scene pass time with
   Off and with 4× at the stress scene's largest workloads), a phone-size check, and the
   documents above.

Every phase is checked in Release and Debug in headless Chromium, at desktop and phone size.
SwiftShader's timings mean nothing; real costs come from devices.

As built, the combo, the save tag and the Textures entries arrived in the order of the phases; the
notes below are what the build taught:

- **The color target is made like the canvas**: the canvas's own format (`BGRA8Unorm`) with the sRGB
  render format as a view format, rendered through the sRGB view. A texture created directly in the
  sRGB format resolved into the canvas about a stop too dark in Chromium on SwiftShader (the
  resolve wrote linear values where the canvas expects encoded ones); made like the canvas, the 4×
  and Off images have the same colors.
- Since `resolution.md` the 4-sample color and depth targets are the scene's resolution (rounded up
  to 64), not the canvas's, and the 4-sample color resolves into the scene color target; the
  upscale pass puts that into the canvas. The memory table above is for the canvas-sized targets.
- `NvRenderer.scene_samples` is what the pipelines and the depth target have; `NvRenderer.msaa` is
  what the app asks for, and `update_msaa` (before `update_depth_buffer`) reconciles them each
  frame. The engine's default is 1; the app sets 4.
- Debug builds export `_app_debug_msaa` and `_app_debug_set_msaa`, which the tests use to switch
  without the UI.
- Checked: 24 distinct colors along a cube's edge with 4× against 4 without, equal colors inside
  triangles, switching at runtime and resizing raise no WebGPU errors, the stress scene draws at
  4×, the Textures tab previews the multisampled depth, a reload keeps the setting, an unsupported
  count in a save loads as 4×, and a save without the tag gives 4×.
- On SwiftShader the stress scene's GPU scene pass took about 2.5× longer with 4× than with Off
  (144 ms against 365 ms at the default workload); it says nothing about real GPUs.

### Out of scope

- 2× and 8× (not guaranteed by WebGPU), FXAA, SMAA and TAA.
- Targets sized to the viewport (see Memory).
- Alpha to coverage, until a material uses alpha testing.

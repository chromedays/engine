# Scene resolution spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-10-01). 이 스펙의 변경은 먼저 합의한다.

### 목표

씬이 자체 해상도로 렌더링되고, 뷰포트에 최근접 픽셀 필터로 보이게 한다. 두 모드 중 하나로:

- **Scale:** 뷰포트 픽셀의 정수 분의 1 (1/1, 1/2, 1/3, 1/4)로 뷰포트를 채운다. 1/1 아래에서는 GPU가 한계인 곳에서
  선명함을 속도와 바꾸는데, 스트레스 벤치마크가 폰에서 그것을 발견했다: pixel ratio 3×의 폰은 지금 뷰포트를 약
  1170 × 1000 픽셀로, CSS 픽셀당 아홉 픽셀로 렌더링한다.
- **Fixed:** 창과 상관없이 정확한 크기 (예: 1280 × 720), 자체 종횡비로, 가운데에, 이미지가 채우지 않는 쪽에 검은
  띠를 두고 보인다 (위아래는 레터박스, 좌우는 필러박스). 도크와 창에 상관없이 게임이 그 해상도로 보여 줄 것.

어느 쪽이든 렌더링된 모든 픽셀은 화면 픽셀의 고른 정사각 블록으로 보인다. 에디터 UI (도크, 기즈모, 빌드 라벨)는
캔버스의 전체 해상도로 남는다: 씬만 바뀐다.

### 지금

씬 패스는 뷰포트 사각형 안 (뷰포트와 시저)에서 캔버스에 바로 그리고, MSAA가 있으면 캔버스 전체 크기의 4-샘플
텍스처에 그려 캔버스로 resolve한다 (`msaa.md`). 깊이 타깃도 캔버스 크기다. 그래서 씬의 해상도는 항상 캔버스의
것이고, 종횡비는 뷰포트의 것이며, 타깃은 결코 그리지 않는 도크와 패널까지 덮는다.

### 접근법

| 접근법 | 무엇인가 | 맞음 | 장단점 |
|---|---|---|---|
| **오프스크린 씬 타깃과 업스케일 패스** (추천) | 씬 패스가 씬 해상도의 자체 색 텍스처에 렌더링하고 (MSAA가 그리로 resolve), 그다음 작은 패스가 ImGui 패스 전에 그 텍스처를 최근접 픽셀 필터로 캔버스의 사각형에 그린다 | 두 모드와 모든 크기에 경로 하나; 타깃이 씬 자체 크기로 줄어드는데, 이것은 `msaa.md`가 나중으로 남긴 것도 한다 (도크 아래 4-샘플 텍스처가 없다). 씬 색이 Textures 탭이 보여 줄 수 있는 샘플링 텍스처가 된다 | 1/1에서도 패스 하나, 뷰포트 전체 읽기와 쓰기가 더해진다 |
| 1/1에서는 캔버스에 바로, 그 외에만 오프스크린 | 경로 두 개 | 1/1에서 추가 패스를 아낀다 | 맞게 유지할 코드 경로 두 개; 1/1에서 타깃이 캔버스 크기로 남는다 |
| 캔버스 자체 해상도 낮추기 (CSS 픽셀당 프레임버퍼 픽셀 줄이기) | 페이지 전체가 낮은 pixel ratio로 렌더링 | 새 패스 없음 | UI 텍스트도 흐려지는데, 그것은 건드리면 안 된다; 고정 크기가 없다 |
| AMD FidelityFX Super Resolution 1 (FSR 1: EASU 업스케일 + RCAS 샤픈; MIT; GLSL/HLSL 셰이더) | 최근접보다 부드러운 공간 업스케일러 | WGSL로 옮기면 최근접 패스를 대신한다 | 패스 두 개와 유지할 포팅. 나중의 선택지 |
| 동적 해상도 (배율이 프레임 시간을 따름) | 프레임이 길어지면 배율을 낮춘다 | 안정된 프레임률 | 믿을 만한 GPU 시간이 필요한데, SwiftShader는 테스트에 그것을 줄 수 없다; 이것 위의 나중 단계 |

추천: 우리가 작성하는 오프스크린 씬 타깃과 최근접 픽셀 업스케일 패스. FSR 1과 동적 해상도는 나중으로 남기며 이
설계를 바꿀 필요가 없다: 업스케일 패스를 대신하고 배율을 설정할 뿐이다.

### 결정

#### 두 모드 모두

| 주제 | 결정 |
|---|---|
| 설정 | View 탭의 **Resolution** 섹션: **Mode** (Scale, Fixed), 그다음 모드 자체의 조작 (아래). 씬이 무엇으로 렌더링되는지 ("585 × 497")와 렌더링된 픽셀 하나가 화면에서 얼마나 큰지 ("2 × 2 screen pixels, 1.5 per CSS pixel")를 보여 준다 |
| 기본값 | 두 UI 모두 Scale: 데스크톱 UI에서 1/1, 폰 UI에서 1/2 (GPU가 한계인 곳; 3× 화면도 여전히 CSS 픽셀당 1.5 픽셀을 받는다). 설정으로 어느 쪽이든 바꾼다 |
| 업스케일 필터 | **최근접만**: 렌더링된 픽셀마다 화면 픽셀 정수 개의 블록이 되어, 선명하고 렌더링된 그대로다. 그래서 배율은 정수 분의 1이고, 고정 크기는 들어맞는 곳에서 정수 배로 보인다: 67% 같은 비율은 이미지에 걸쳐 1픽셀과 2픽셀 폭 블록을 섞는다. Scale 모드에서는 보이는 것보다 크게 렌더링하지 않는다: 최근접 필터에서 남는 픽셀은 평균되지 않고 버려진다 |
| 씬 타깃 | 씬 해상도의 단일 샘플 색 텍스처, 업스케일 패스가 샘플링한다. 캔버스처럼 만들어서 (그 형식에 sRGB 뷰 형식, sRGB 뷰로 렌더링하고 샘플링) MSAA resolve가 지금과 같은 색을 쓴다 (`msaa.md`는 sRGB 형식으로 직접 만든 텍스처가 너무 어둡게 resolve되는 것을 발견했다) |
| MSAA와 깊이 | 4-샘플 색 텍스처와 깊이 텍스처도 캔버스가 아니라 씬 해상도다. MSAA는 씬 타깃으로 resolve한다 |
| 크기와 크기 조절 | Scale 모드에서 타깃은 64 픽셀의 배수로 올려 할당하고 충분히 큰 동안 유지하므로, 분할선 드래그나 창 크기 조절이 매 프레임 다시 만들지 않는다; 필요한 픽셀의 두 배보다 많이 담으면 더 작게 다시 만든다. 씬은 왼쪽 위 부분 (뷰포트와 시저)에 렌더링하고, 업스케일 패스는 그 부분을 읽는다. Fixed 모드에서는 크기가 창을 전혀 따르지 않는다 |
| 한도 | 씬 해상도는 장치의 `maxTextureDimension2D` (WebGPU 기본값에서 8192)로, 그리고 최소 1 × 1로 제한된다 |
| 업스케일 패스 | 이미지 사각형의 뷰포트와 시저를 가진 전체 화면 삼각형, clamp-to-edge 샘플러로 씬 타깃을 샘플링. 먼저 캔버스를 검게 clear한다 (씬 패스는 더 이상 캔버스를 건드리지 않는다): 그 검정이 Fixed 모드의 띠이고, 나중에 그리는 도크가 나머지를 덮는다 |
| 이미지 사각형 | 앱이 뷰포트 안에서 이미지가 갈 곳을 계산한다 (`App.layout.image`, 프레임버퍼 픽셀): Scale 모드에서는 뷰포트 전체, Fixed 모드에서는 가운데 맞춤. 렌더러, picking, 기즈모, 팬이 그것을 쓴다; 뷰포트는 입력을 받는 영역으로 남는다 |
| 디버그 라인, 선택 상자 | 씬 패스에서 그리므로 씬과 함께 배율이 바뀐다 (씬 해상도에서 1 픽셀 폭). 기즈모와 빌드 라벨은 ImGui라서 선명하게 남는다 |
| Textures 탭 | **scene color** 타깃을 미리보기와 함께 나열하고 (어차피 샘플링된다), 깊이와 MSAA 타깃을 새 크기로 나열한다. 깊이 타깃은 더 이상 "씬 부분"을 잘라 낼 필요가 없다: 그것이 씬이다 |
| 비용 표시 | Stress 탭이 해상도 ("585 × 497, scale 1/2" 또는 "1280 × 720 fixed, shown ×1")와 업스케일 패스의 GPU 시간 (세 번째 타임스탬프 쌍)을 보여 준다; 벤치마크가 그것을 기록한다 |
| 서드파티 | 지금은 없음; 더 부드러운 업스케일이 필요하면 FSR 1 (MIT)이 후보다 |

#### Scale 모드

| 주제 | 결정 |
|---|---|
| 프리셋 | 뷰포트 프레임버퍼 픽셀의 변당 1/1, 1/2, 1/3, 1/4. 이미지가 뷰포트를 채운다; 종횡비는 지금처럼 뷰포트의 것이다 |
| 반올림 | 씬 크기는 뷰포트의 것을 나누어 올림한 것이고, 이미지 사각형은 씬 크기 × 제수를 뷰포트로 자른 것이라서, 오른쪽과 아래 가장자리의 블록만 부분일 수 있다 |

#### Fixed 모드

| 주제 | 결정 |
|---|---|
| 크기 | 프리셋 640 × 360, 1280 × 720, 1920 × 1080 (16:9 가로), 360 × 640, 720 × 1280 (9:16 세로, 폰용), 그리고 **Custom**: 폭과 높이 필드, 각각 16–4096 (그리고 장치 한도 안) |
| 종횡비 | 카메라 투영은 뷰포트가 아니라 고정 크기의 종횡비를 쓴다: 세로 시야각이 유지되므로 세로 크기는 더 좁은 시야를 보여 준다. 그래서 도크나 창이 움직여도 보이는 것이 바뀌지 않는다 |
| 맞춤 | **Fit** 설정이 이미지가 뷰포트와 만나는 방식을 고른다. **Whole multiples** (기본값): 들어맞는 가장 큰 정수 배 (×1, ×2, ...), 가운데, 그래서 모든 블록이 같은 크기다. ×1조차 들어맞지 않으면 (뷰포트보다 큰 크기) 종횡비를 유지한 채 들어맞게 줄이고, View 탭이 그렇다고 말한다 ("larger than the viewport: shown at 0.62×, some pixels dropped") |
| 뷰포트에 맞춤 | 종횡비를 유지하며 들어맞는 가장 큰 배율, 소수 (1320 × 832 뷰포트에서 1920 × 1080이 1.07×로 보임), 가운데: 한 축에만 띠, 그리고 최근접 필터 때문에 어떤 씬 픽셀은 다른 것보다 넓게 보인다 (1 또는 2 화면 픽셀). 뷰포트보다 크면 같은 방식으로 줄어든다 |
| 뷰포트로 늘림 | 이미지가 뷰포트 전체를 채우고, 띠가 사라지고, 종횡비가 뷰포트의 것으로 휜다 (`NvSceneOutput.pixel_width`와 `pixel_height`가 다르다). 카메라는 고정 크기의 종횡비를 유지하므로, 늘린 이미지처럼 그림이 왜곡된다 |
| 띠 | 검정, 업스케일 패스의 clear가 그린다; Whole multiples와 Fit to viewport에서만. 뷰포트의 나머지처럼 입력을 받는다 |
| 띠 위의 입력 | 드래그는 공전과 팬을, 휠이나 핀치는 줌을, 띠를 포함해 뷰포트 어디서든 한다. 띠를 탭하면 아무것도 고르지 않고 선택을 유지한다 (이미지의 빈 배경을 탭하면 지금처럼 여전히 선택이 지워진다). 빌드 라벨의 배지는 뷰포트 왼쪽 위 모서리에, 띠가 있으면 그 위에 남는다 |
| Picking과 기즈모 | 이미지 사각형과 고정 종횡비를 쓴다 (`nv_renderer_view_ray`, `nv_renderer_camera_matrices`, `ImGuizmo_SetRect`가 이미지 사각형을 받는다). 기즈모는 ImGui가 화면 해상도로 그리므로 띠 위로 넘어갈 수 있다 |
| 팬 | 드래그 한 픽셀은 공전 점을 그 거리에서 뷰가 덮는 높이 나누기 **이미지** 높이만큼 옮기므로, 씬이 여전히 손가락을 따라간다 |
| 메모리 | 타깃이 고정 크기이므로 큰 고정 크기는 창과 상관없이 같은 비용이 든다 (MSAA 4×의 1920 × 1080: 약 71 MB) |

#### 저장

| 태그 | 타입 | 무엇 | 없거나 잘못됨 |
|---|---|---|---|
| `RSMD` | u32 | 모드: 0 Scale, 1 Fixed | 0 |
| `RSCL` | u32 | Scale 모드의 제수: 1, 2, 3, 4 | 장치의 기본값 |
| `RSFT` | u32 | Fixed 모드의 맞춤: 0 정수 배, 1 뷰포트에 맞춤, 2 늘림 | 0 |
| `RSFW`, `RSFH` | u32 | Fixed 모드의 폭과 높이, 16..4096로 제한 | 1280 × 720 |

폰과 데스크톱의 기본값이 다르므로, 한 장치에서 쓰고 다른 장치에서 불러온 저장은 다른 장치의 기본값이 아니라
사용자의 선택을 유지한다.

### 메모리

3× 폰 (뷰포트 약 1170 × 1000)에서 1/2의 MSAA 4×로, 씬의 타깃은 약 585 × 500 픽셀을 담는다: 색 4.5 MB (4×), 깊이
4.5 MB (4×), 씬 타깃 1.1 MB, 지금 캔버스 크기 MSAA 타깃의 약 90 MB와 대비된다. 기본 도크의 1280 × 800 데스크톱
(뷰포트 680 × 552)에서 1/1은 약 13 MB 대 32 MB.

### 변경

- **렌더러 (`nv/renderer.h`, `engine/src/renderer.c`).** `nv_renderer_draw`가 뷰포트 사각형 하나 대신 씬의 해상도와
  이미지 사각형을 받는다; 투영의 종횡비는 해상도의 것이다. `update_scene_targets`가 (위의 반올림으로) 크기를 정하는
  씬 색 타깃과 MSAA와 깊이 타깃; 업스케일 파이프라인 (전체 화면 삼각형과 최근접 샘플러); 씬 타깃으로의 씬 패스,
  그다음 캔버스로의 업스케일 패스; 업스케일 패스를 위한 세 번째 타임스탬프 쌍. `nv_renderer_view_ray`와
  `nv_renderer_camera_matrices`가 이미지 사각형과 해상도를 받는다.
- **앱.** `App`의 해상도 설정; 모드로부터 씬 크기와 `App.layout.image`를 계산하는 `app_layout`; 이미지 사각형 위의
  picking, 팬, 기즈모, 띠 탭 무시 (`app/main.c`); View 탭의 Resolution 섹션 (`app/ui.c`); 장치별 기본값; 태그 네 개
  (`app/save.c`, `save.md`); Textures 탭 항목 (`app/textures.c`); Stress 탭 줄과 벤치마크 열 (`app/stress.c`). 씬
  크기와 이미지 사각형을 위한 Debug export.
- **문서.** `msaa.md` (씬 크기의 타깃), `viewport.md`와 `gizmo.md` (이미지 사각형), `textures.md`, `stress.md`,
  `AGENTS.md`, README.

### 단계

1. **오프스크린 씬:** 씬 타깃, 업스케일 패스, 반올림으로 뷰포트 크기에 맞춘 타깃, 씬 타깃으로 resolve하는 MSAA,
   아직 1/1. 확인: 정지 프레임이 변경 전과 같다 (픽셀 비교: 1/1에서 최근접 패스는 복사다), MSAA 켜고 끄고; 창 크기
   조절과 분할선 드래그가 매 프레임 타깃을 다시 만들지 않는다 (로그); picking과 기즈모가 동작.
2. **Scale 모드:** 제수, View 탭 섹션, 기본값, `RSMD`와 `RSCL`, Textures 탭 항목. 확인: 제수마다의 씬 크기 (debug
   export), 1/2은 고른 2 × 2 블록을 1/4은 고른 4 × 4 블록을 보여 줌, 1/3에서 picking과 기즈모, 새로고침이 설정을
   유지, 옛 저장은 장치 기본값을 받음.
3. **Fixed 모드:** 크기와 Custom, 고정 종횡비, 정수 배 맞춤과 축소, 검은 띠, picking, 팬, 기즈모를 위한 이미지
   사각형, 띠 탭, `RSFW`와 `RSFH`. 확인: 데스크톱 뷰포트의 1280 × 720 (도크가 움직이면 필러박스나 레터박스, 분할선을
   끄는 동안 보이는 씬은 그대로), 폰의 세로 크기, 뷰포트보다 큰 크기 (줄어들고 안내가 보임), 띠 탭이 선택을 유지,
   캐릭터 탭이 그것을 고름, 레터박스 이미지에서 기즈모 드래그.
4. **비용과 문서:** Stress 탭 줄, 업스케일 패스의 GPU 시간, 벤치마크 열, 폰 크기 확인, 문서.

모든 단계는 헤드리스 Chromium에서 Release와 Debug로, 데스크톱과 폰 크기에서 확인한다. SwiftShader의 시간은 의미가
없다; 실제 비용은 장치에서 나온다.

만든 결과:

- Fixed 모드의 코드 (크기, 정수 배 맞춤, 축소, 이미지 사각형)와 저장 태그는 씬 출력을 처음 계산한 1, 2단계와 함께
  왔다; 3단계가 View 탭 조작을 더하고 확인했다; 업스케일 패스의 타임스탬프 쌍은 1단계와 함께 왔다.
- 타깃은 충분히 크고 필요한 픽셀의 최대 두 배인 동안 할당된 채 남으므로, 고정 1280 × 720이 더 큰 뷰포트에서 남은
  타깃에 앉을 수 있다 (로그 줄이 무엇을 쥐고 있는지 말한다).
- 업스케일 셰이더는 샘플러가 아니라 `textureLoad`로 씬 색 타깃을 읽는다 (화면 픽셀 아래의 텍셀, 이미지 모서리부터
  `pixel_size` 블록 단위로 셈): 그것이 정확한 최근접 필터링이고, 줄어든 고정 크기가 픽셀을 고르게 버리게 하는 것이기도
  하다.
- 씬 색 타깃은 캔버스처럼 만들고 (`BGRA8Unorm`에 sRGB 뷰 형식), Textures 탭은 다른 `Unorm` 텍스처와 같은 경로로
  미리 본다 (그 바이트는 디스플레이 인코딩이다), 그리고 그것이 맞게 나왔다.
- Debug 빌드에서 1280 × 800, 1920 × 1080, 390 × 664 (터치)로 확인: 제수마다의 씬 크기; 1/2과 1/4에서 고른 2 × 2와
  4 × 4 블록; 1/1의 첫 프레임이 이전 빌드의 것과 정적 영역에서 같음, MSAA 켜고 끄고; 1320 × 832 뷰포트에서 검은 띠와
  함께 필러박스와 레터박스된 1280 × 720; ×2로 고른 블록으로 보인 640 × 360; 세로 크기; 종횡비를 유지하며 0.32로 줄어든
  4096 × 2160; 분할선 드래그 뒤 이미지 안의 씬이 동일; 캐릭터를 고르는 탭, 선택을 유지하는 띠 탭, 선택을 지우는 빈
  배경 탭, 기즈모 드래그와 띠에서 시작한 드래그의 공전; 두 모드를 유지하는 새로고침; 잘못된 제수와 없는 태그; 폰의
  기본값 1/2; Stress 탭의 해상도 줄.
- 나중에 추가: Fit to viewport와 Stretch to viewport가 있는 **Fit** 설정 (`App.resolution.fixed_fit`, `RSFT`로 저장).
  `NvSceneOutput`은 이제 `pixel_size` 하나 대신 `pixel_width`와 `pixel_height`를 가지고, 업스케일 셰이더의 블록은
  `vec2f`다. 1920 × 1080에서 확인: 2.06×로 맞춘 640 × 360, 높이로 1.3×에 맞춘 360 × 640, 캐릭터를 고르는 탭과 기즈모
  드래그와 함께 정확히 뷰포트로 늘린 것, 여전히 ×2인 정수 배, 맞춤을 유지하는 새로고침.
- 측정하지 않음: GPU에서 업스케일 패스와 각 배율의 실제 비용; SwiftShader의 숫자는 그것에 대해 아무것도 말하지 않는다.

### 범위 밖

- FSR 1이나 어떤 부드러운 업스케일러, 동적 해상도 (이 설계 위의 나중 단계).
- UI 배율 조절.
- 다른 색이나 무늬의 띠.

## English

Status: implemented (2026-10-01). Changes to this spec are agreed first.

### Goal

Let the scene render at a resolution of its own and show it in the viewport with the
nearest-pixel filter, in one of two modes:

- **Scale:** a whole fraction of the viewport's pixels (1/1, 1/2, 1/3, 1/4), filling the viewport.
  Below 1/1 it trades sharpness for speed where the GPU is the limit, which the stress benchmark
  found on phones: a phone with a 3× pixel ratio renders its viewport at about 1170 × 1000 pixels
  today, nine pixels per CSS pixel.
- **Fixed:** an exact size (e.g. 1280 × 720) whatever the window, shown at its own aspect ratio,
  centered, with black bars on the sides the image does not fill (letterbox above and below,
  pillarbox left and right). What a game would show at that resolution, independent of the docks
  and the window.

Either way every rendered pixel shows as an even square block of screen pixels. The editor UI (the
docks, the gizmo, the build label) stays at the canvas's full resolution: only the scene changes.

### Today

The scene pass draws straight into the canvas, inside the viewport rectangle (viewport and
scissor), and with MSAA it draws into a 4-sample texture of the whole canvas's size and resolves it
into the canvas (`msaa.md`). The depth target is the canvas's size too. So the scene's resolution
is always the canvas's, its aspect ratio is the viewport's, and its targets cover the docks and
panels it never draws.

### Approaches

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **An offscreen scene target and an upscale pass** (recommended) | The scene pass renders into its own color texture at the scene resolution (MSAA resolves into it); a small pass then draws that texture into its rectangle of the canvas with the nearest-pixel filter, before the ImGui pass | One path for both modes and every size; the targets shrink to the scene's own size, which also does what `msaa.md` left for later (no 4-sample texture under the docks). The scene color becomes a sampled texture the Textures tab can show | One more pass, a full-viewport read and write, even at 1/1 |
| Render straight into the canvas at 1/1, offscreen only otherwise | Two paths | Saves the extra pass at 1/1 | Two code paths to keep right; at 1/1 the targets stay canvas-sized |
| Lower the canvas's own resolution (fewer framebuffer pixels per CSS pixel) | The whole page renders at a lower pixel ratio | No new pass | Blurs the UI text too, which is what we must not touch; no fixed size |
| AMD FidelityFX Super Resolution 1 (FSR 1: EASU upscale + RCAS sharpen; MIT; GLSL/HLSL shaders) | A spatial upscaler, smoother than nearest | Ported to WGSL, it would replace the nearest pass | Two passes and a port to maintain. A later option |
| Dynamic resolution (the scale follows the frame time) | Lowers the scale when frames run long | Steady frame rate | Needs reliable GPU timings, which SwiftShader cannot give for tests; a later step on top of this one |

Recommendation: an offscreen scene target and a nearest-pixel upscale pass, written by us. FSR 1 and
dynamic resolution are left for later and need no change to this design: they replace the upscale
pass and set the scale.

### Decisions

#### Both modes

| Topic | Decision |
|---|---|
| Setting | A **Resolution** section in the View tab: **Mode** (Scale, Fixed), then the mode's own controls (below). It shows what the scene renders at ("585 × 497") and how large a rendered pixel is on screen ("2 × 2 screen pixels, 1.5 per CSS pixel") |
| Default | Scale on both UIs: 1/1 on the desktop UI, 1/2 on the phone UI (where the GPU is the limit; a 3× screen still gets 1.5 pixels per CSS pixel). The setting changes either |
| Upscale filter | **Nearest only**: each rendered pixel becomes a block of whole screen pixels, crisp and exactly what was rendered. So scales are whole fractions and fixed sizes are shown at whole multiples where they fit: a share like 67% would mix 1- and 2-pixel-wide blocks across the image. Nothing is rendered larger than it is shown in Scale mode: with nearest filtering, extra pixels would be dropped, not averaged |
| Scene target | A single-sample color texture at the scene resolution, sampled by the upscale pass. Made like the canvas (its format with the sRGB view format, rendered and sampled through the sRGB view), so the MSAA resolve writes the same colors it does today (`msaa.md` found a texture created directly in the sRGB format resolved too dark) |
| MSAA and depth | The 4-sample color texture and the depth texture are the scene resolution too, not the canvas's. MSAA resolves into the scene target |
| Sizes and resizing | In Scale mode the targets are allocated rounded up to multiples of 64 pixels and kept while they are large enough, so a splitter drag or a window resize does not remake them every frame; they are remade smaller when they hold more than twice the pixels needed. The scene renders into the top-left part (viewport and scissor), and the upscale pass reads that part. In Fixed mode the size does not follow the window at all |
| Limits | The scene resolution is clamped to the device's `maxTextureDimension2D` (8192 in WebGPU's defaults) and to at least 1 × 1 |
| Upscale pass | A full-screen triangle with the viewport and scissor of the image's rectangle, sampling the scene target with a clamp-to-edge sampler. It clears the canvas to black first (the scene pass no longer touches the canvas): that black is the bars in Fixed mode, and the docks drawn later cover the rest |
| Image rectangle | The app computes where the image goes inside the viewport (`App.layout.image`, framebuffer pixels): the whole viewport in Scale mode, the centered fit in Fixed mode. The renderer, picking, the gizmo and panning use it; the viewport stays the area that takes input |
| Debug lines, selection boxes | Drawn in the scene pass, so they scale with the scene (1 pixel wide at the scene resolution). The gizmo and the build label are ImGui and stay sharp |
| Textures tab | Lists the **scene color** target with a preview (it is sampled anyway), and the depth and MSAA targets at their new size. The depth target no longer needs "the scene's part" cut out: it is the scene |
| Cost shown | The Stress tab shows the resolution ("585 × 497, scale 1/2" or "1280 × 720 fixed, shown ×1") and the GPU time of the upscale pass (a third timestamp pair); the benchmark records them |
| Third-party | None now; FSR 1 (MIT) is the candidate when a smoother upscale is wanted |

#### Scale mode

| Topic | Decision |
|---|---|
| Presets | 1/1, 1/2, 1/3 and 1/4 of the viewport's framebuffer pixels per side. The image fills the viewport; its aspect ratio is the viewport's, as today |
| Rounding | The scene size is the viewport's divided and rounded up, and the image rectangle is the scene size times the divisor, cut to the viewport, so the blocks at the right and bottom edges are the only ones that can be partial |

#### Fixed mode

| Topic | Decision |
|---|---|
| Sizes | Presets 640 × 360, 1280 × 720, 1920 × 1080 (16:9 landscape), 360 × 640, 720 × 1280 (9:16 portrait, for phones), and **Custom**: width and height fields, 16 to 4096 each (and within the device limit) |
| Aspect ratio | The camera's projection uses the fixed size's aspect ratio, not the viewport's: the vertical field of view stays, so a portrait size shows a narrower view. What is visible therefore does not change when the docks or the window move |
| Fit | A **Fit** setting picks how the image meets the viewport. **Whole multiples** (the default): the largest whole multiple of its size that fits (×1, ×2, ...), centered, so every block is the same size. When even ×1 does not fit (a size larger than the viewport), it is shrunk to fit, keeping its aspect ratio, and the View tab says so ("larger than the viewport: shown at 0.62×, some pixels dropped") |
| Fit to viewport | The largest scale that fits keeping the aspect ratio, fractional (1920 × 1080 shown at 1.07× in a 1320 × 832 viewport), centered: bars on one axis only, and with the nearest filter some scene pixels show wider than others (1 or 2 screen pixels). Larger than the viewport it shrinks the same way |
| Stretch to viewport | The image fills the whole viewport, bars gone, its aspect ratio bent to the viewport's (`NvSceneOutput.pixel_width` and `pixel_height` differ). The camera keeps the fixed size's aspect ratio, so the picture is distorted, as stretched images are |
| Bars | Black, drawn by the upscale pass's clear; only with Whole multiples and Fit to viewport. They take input like the rest of the viewport |
| Input on the bars | Drags orbit and pan, and the wheel or a pinch zooms, anywhere in the viewport, bars included. A tap on a bar picks nothing and keeps the selection (a tap on the image's empty background still clears it, as today). The build label's badge stays in the viewport's top-left corner, over the bar if there is one |
| Picking and gizmo | Use the image rectangle and the fixed aspect ratio (`nv_renderer_view_ray`, `nv_renderer_camera_matrices` and `ImGuizmo_SetRect` get the image rectangle). The gizmo is drawn by ImGui at screen resolution, so it may reach over the bars |
| Panning | One pixel of drag moves the orbit point by the height the view covers at that distance divided by the **image's** height, so the scene still follows the finger |
| Memory | The targets are the fixed size, so a large fixed size costs the same whatever the window (1920 × 1080 with MSAA 4×: about 71 MB) |

#### Saved

| Tag | Type | What | Missing or invalid |
|---|---|---|---|
| `RSMD` | u32 | mode: 0 Scale, 1 Fixed | 0 |
| `RSCL` | u32 | Scale mode's divisor: 1, 2, 3 or 4 | the device's default |
| `RSFT` | u32 | Fixed mode's fit: 0 whole multiples, 1 fit to the viewport, 2 stretch | 0 |
| `RSFW`, `RSFH` | u32 | Fixed mode's width and height, clamped to 16..4096 | 1280 × 720 |

The phone's and the desktop's defaults differ, so a save written on one device and loaded on
another keeps the user's choice, not the other device's default.

### Memory

With MSAA 4× at 1/2 on a 3× phone (viewport about 1170 × 1000), the scene's targets hold about
585 × 500 pixels: color 4.5 MB (4×), depth 4.5 MB (4×) and the scene target 1.1 MB, against
about 90 MB for the canvas-sized MSAA targets today. At 1/1 on a 1280 × 800 desktop with the
default docks (viewport 680 × 552), about 13 MB against 32 MB.

### Changes

- **Renderer (`nv/renderer.h`, `engine/src/renderer.c`).** `nv_renderer_draw` takes the scene's
  resolution and the image rectangle instead of one viewport rectangle; the projection's aspect
  ratio is the resolution's. The scene color target and the MSAA and depth targets, sized by
  `update_scene_targets` (with the rounding above); the upscale pipeline (a full-screen triangle and
  a nearest sampler); the scene pass into the scene target, then the upscale pass into the canvas;
  a third timestamp pair for the upscale pass. `nv_renderer_view_ray` and
  `nv_renderer_camera_matrices` take the image rectangle and the resolution.
- **App.** The resolution settings in `App`; `app_layout` computing the scene size and
  `App.layout.image` from the mode; picking, panning and the gizmo on the image rectangle, and a
  tap on a bar ignored (`app/main.c`); the View tab's Resolution section (`app/ui.c`); the defaults
  by device; the four tags (`app/save.c`, `save.md`); the Textures tab's entries (`app/textures.c`);
  the Stress tab line and the benchmark columns (`app/stress.c`). Debug exports for the scene size
  and the image rectangle.
- **Docs.** `msaa.md` (targets sized to the scene), `viewport.md` and `gizmo.md` (the image
  rectangle), `textures.md`, `stress.md`, `AGENTS.md`, README.

### Phases

1. **Offscreen scene:** the scene target, the upscale pass, the targets sized to the viewport with
   the rounding, MSAA resolving into the scene target, still at 1/1. Checked: a still frame is the
   same as before the change (pixel comparison: at 1/1 the nearest pass is a copy), with MSAA on
   and off; resizing the window and dragging a splitter do not remake the targets every frame (the
   log); picking and the gizmo work.
2. **Scale mode:** the divisors, the View tab section, the defaults, `RSMD` and `RSCL`, the
   Textures tab entries. Checked: each divisor's scene size (debug exports), 1/2 shows even 2 × 2
   blocks and 1/4 even 4 × 4 blocks, picking and the gizmo at 1/3, a reload keeps the setting, an
   old save gets the device's default.
3. **Fixed mode:** the sizes and Custom, the fixed aspect ratio, the whole-multiple fit and the
   shrink, the black bars, the image rectangle for picking, panning and the gizmo, taps on the bars,
   `RSFW` and `RSFH`. Checked: 1280 × 720 in the desktop viewport (pillarboxed or letterboxed as the
   docks move, the visible scene unchanged while a splitter is dragged), a portrait size on the
   phone, a size larger than the viewport (shrunk, the note shown), a tap on a bar keeps the
   selection, a tap on the character picks it, the gizmo drags in a letterboxed image.
4. **Cost and docs:** the Stress tab line, the upscale pass's GPU time, the benchmark columns, a
   phone-size check, the documents.

Every phase is checked in Release and Debug in headless Chromium, at desktop and phone size.
SwiftShader's timings mean nothing; real costs come from devices.

As built:

- The Fixed mode's code (the size, the whole-multiple fit, the shrink, the image rectangle) and the
  save tags came with phases 1 and 2, where the scene output was first computed; phase 3 added its
  View tab controls and checked it; the upscale pass's timestamp pair came with phase 1.
- The targets stay allocated while they are large enough and at most twice the pixels needed, so a
  fixed 1280 × 720 can sit in targets left from a larger viewport (the log lines say what is held).
- The upscale shader reads the scene color target with `textureLoad` (the texel under the screen
  pixel, counted from the image's corner in blocks of `pixel_size`), not a sampler: that is exact
  nearest filtering, and also what lets a shrunk fixed size drop pixels evenly.
- The scene color target is made like the canvas (`BGRA8Unorm` with the sRGB view format), and the
  Textures tab previews it through the same path as other `Unorm` textures (their bytes are
  display-encoded), which came out right.
- Checked in a Debug build at 1280 × 800, 1920 × 1080 and 390 × 664 (touch): the scene size for each
  divisor; even 2 × 2 and 4 × 4 blocks at 1/2 and 1/4; the first frame at 1/1 equal to the
  previous build's in the static regions, with MSAA on and off; 1280 × 720 pillarboxed and
  letterboxed in a 1320 × 832 viewport with black bars; 640 × 360 shown at ×2 as even blocks; a
  portrait size; 4096 × 2160 shrunk to 0.32 keeping its aspect ratio; the scene inside the image
  identical after a splitter drag; a tap picking the character, a tap on a bar keeping the
  selection, a tap on the empty background clearing it, the gizmo dragging and a drag from a bar
  orbiting; a reload keeping both modes; a bad divisor and a missing tag; the phone's default of
  1/2; the Stress tab's resolution line.
- Added afterwards: the **Fit** setting with Fit to viewport and Stretch to viewport
  (`App.resolution.fixed_fit`, saved as `RSFT`). `NvSceneOutput` now has `pixel_width` and
  `pixel_height` instead of one `pixel_size`, and the upscale shader's block is a `vec2f`. Checked at
  1920 × 1080: 640 × 360 fitted at 2.06×, 360 × 640 fitted by height at 1.3×, stretched to exactly
  the viewport with a tap picking the character and the gizmo dragging, whole multiples still at
  ×2, a reload keeping the fit.
- Not measured: the real cost of the upscale pass and of each scale on a GPU; SwiftShader's numbers
  say nothing about it.

### Out of scope

- FSR 1 or any smoothing upscaler, and dynamic resolution (later steps on this design).
- Scaling the UI.
- Bars in another color or with a pattern.

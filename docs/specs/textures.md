# Texture viewer spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-09-30). 이 스펙의 변경은 먼저 합의한다.

### 목표

엔진이 쓰는 텍스처를 앱 안에서 본다: 캐릭터의 base color 맵, 그림자 맵, 씬의 깊이 타깃, ImGui의 글꼴 아틀라스.
**Textures** 탭이 썸네일, 크기, 형식, 밉 레벨, 메모리, 사용처와 함께 그것들을 나열한다; 하나를 고르면 밉 레벨, 채널,
깊이 범위 조작과 함께 크게 보여 준다. Inspector의 Mesh 섹션도 그 머티리얼의 텍스처를 보여 준다.

범위 밖: 텍셀 값을 CPU로 읽어 오기 (포인터 아래 값), 텍스처 편집이나 교체, 지난 프레임의 텍스처 (RenderDoc이 네이티브로
하는 캡처 없음).

### 지금 있는 것

| 텍스처 | 어디 | 형식 | 용도 | 지금 ImGui가 샘플링할 수 있나 |
|---|---|---|---|---|
| 머티리얼 텍스처 (glTF base color: `T_Hair_1_BaseColor`, `T_Eye_Brown`, `T_Superhero_Male_Dark`)와 1×1 흰색 기본값 (슬롯 0) | `NvRenderer.textures[256]`, `nv_renderer_add_texture` | `RGBA8UnormSrgb` (흰색: `RGBA8Unorm`), 전체 밉 체인 | `TextureBinding`, `CopyDst` | 예, 새 bind group을 통해; 하지만 렌더러가 이름, 크기, 형식, 밉 수를 보관하지 않는다 |
| 그림자 맵 | `NvRenderer.shadow_texture` | `Depth32Float` 또는 `Depth16Unorm`, 512²–2048², 꺼지면 1×1 자리 표시자 | `RenderAttachment`, `TextureBinding` | 아니요: ImGui의 셰이더는 필터링 샘플러와 `texture_2d<f32>`를 받고, 깊이 형식은 필터링할 수 없다 |
| 씬 색 타깃 | `NvRenderer.scene_color` | `BGRA8Unorm` (캔버스의 형식, 그 sRGB 뷰로 렌더링), 씬 해상도를 64로 올림 (`resolution.md`) | `RenderAttachment`, `TextureBinding` | 예: 업스케일 패스가 샘플링한다; 씬이 채운 부분이 보인다 |
| 씬 깊이 타깃 | `NvRenderer.depth_texture` | `Depth32Float`, 씬 해상도를 64로 올림, reverse Z, MSAA에서 4샘플 (`msaa.md`, `resolution.md`) | `RenderAttachment`; 탭이 보이는 동안 `TextureBinding` 추가 | 탭이 보이는 동안 예 (다중 샘플 텍스처의 샘플 0) |
| 스왑체인 (캔버스) | `NvGpu.current_texture` | `BGRA8Unorm`에 `BGRA8UnormSrgb` 뷰 (브라우저별) | `RenderAttachment` | 아니요, 그럴 수도 없다: ImGui 패스가 그 안에 그리고 있다 |
| ImGui 글꼴 아틀라스 | `NvImgui.textures[16]` | `RGBA8Unorm` | `TextureBinding`, `CopyDst` | 예 (ImGui 자체의 것) |

### 접근법

| 접근법 | 무엇인가 | 맞음 | 장단점 |
|---|---|---|---|
| **`igImage` 위의 자체 뷰어, `imgui.c`의 미리보기 파이프라인과 함께** (추천) | 탭이 ImGui의 `igImage`로 텍스처를 그린다. `imgui.c`에 미리보기 슬롯이 생긴다: 필요할 때 만드는 (텍스처, 밉, 모드)별 bind group, 채널 하나 또는 깊이 텍스처를 고른 범위의 회색으로 보여 줄 수 있는 작은 두 번째 파이프라인으로 그린다 | C17, 고정 슬롯, 새 의존성 없음; 약 400줄 (엔진과 앱) | 우리가 써야 한다. 텍셀 읽어 오기 없음 |
| imgui_tex_inspect (andyborrell, C++, MIT) | ImGui 텍스처 인스펙터: 줌, 팬, 텍셀별 값 주석, 채널 마스크 | 기능상 가장 가깝다 | C API가 없는 C++ (cimgui 바인딩 없음), 백엔드는 OpenGL과 DirectX 11만: WebGPU 백엔드 (셰이더와 텍셀 읽어 오기)는 어차피 우리가 써야 하고, 더하여 `anim.cpp` 같은 C++ 래퍼 |
| Dear ImGui만 (`igImage`, `igImageWithBg`) | 위젯, 현재 파이프라인으로 | 이미 있다 | 색 텍스처에는 충분하지만, 깊이 텍스처, 채널, 밉 레벨을 보여 주지 못한다 |
| WebGPU Inspector (Brendan Duncan, 브라우저 확장, MIT) | 프레임을 캡처하고 텍스처를 포함한 모든 GPU 객체를 나열한다 | 엔진에 더할 것이 없다 | 앱 밖: 확장이 있는 데스크톱 브라우저, 폰이 아님. 곁에 둘 좋은 도구이지 요청된 기능은 아니다 |

추천: `igImage` 위에 직접 작성. 서드파티 라이브러리 없음.

### 결정

| 주제 | 결정 |
|---|---|
| 탭 | 데스크톱 UI (`layout.md`): 오른쪽 도크에서 Inspector와 View 다음의 **Textures** 탭; 오른쪽 도크는 텍스처를 담을 만큼 높지만 하단 도크는 아니다. 폰 UI: View와 Console 사이의 탭, 그리고 탭 바가 ImGui의 축소 맞춤 (`ImGuiTabBarFlags_FittingPolicyShrink`)을 써서, 스크롤되는 탭 바 대신 다섯 탭 모두가 라벨이 짧게 잘린 채 보인다 |
| 목록과 고른 텍스처 | 탭이 넓은 곳 (700 px × `ui_scale` 이상)에서는 목록과 고른 텍스처가 나란히 놓인다. 그 밖 (오른쪽 도크, 폰)에서는 탭이 둘 중 하나를 보여 준다: 행을 고르면 텍스처가 보이고, **< Textures**가 목록으로 돌아간다 |
| 그룹 | 세 개의 접는 섹션: **Materials** (렌더러의 텍스처), **Render targets** (그림자 맵, 깊이 타깃, 스왑체인), **UI** (ImGui의 글꼴 아틀라스) |
| "사용 중" | 머티리얼 텍스처는 보이는 씬의 메시 노드가 그것을 `base_color_texture`로 가진 머티리얼을 가질 때 사용 중이다; 흰색 기본값은 텍스처 없는 머티리얼에 대해 센다. **In use only** 체크박스 (기본 켜짐)가 나머지를 숨긴다. 목록 위의 검색 상자 (`search.md`)가 텍스처 이름과 그룹으로 거른다 ("materials"는 그룹 전체를 나열); 고른 텍스처의 상세는 걸러지지 않고, 상세만 보여 주는 좁은 패널에는 상자가 없다. 렌더 타깃은 그것에 그리는 동안 사용 중이다 (그림자가 켜진 동안의 그림자 맵) |
| 행 | 썸네일 (48 px × `ui_scale`, 종횡비 유지, 알파가 보이도록 체커보드 위), 그다음 짧은 세 줄: 이름; 크기와 형식; 밉 레벨, 메모리, 사용자 ("11 mips, 5.3 MB, 1 user"). 화면에 있는 행만 썸네일을 만들므로 (`igIsItemVisible`), 목록이 아무리 길어도 미리보기 슬롯이 적게 남는다 |
| 메모리 | 폭 × 높이 × 텍셀당 바이트, 밉 체인이 있으면 × 4/3. 섹션 헤더가 그 합을, 탭의 맨 윗줄이 전체를 보여 준다 |
| 이름 | 머티리얼 텍스처는 glTF 이미지의 이름 (`T_Eye_Brown`)을, 없으면 `image N`을 받는다; 기본값은 `white`. WebGPU 라벨로도 설정하므로 브라우저 도구가 같은 이름을 보여 준다 |
| 상세 | 텍스처가 패널의 폭과 남은 높이 (최소 160 px × `ui_scale`)에 맞춰진다. **Zoom** (1×–16×, 슬라이더)과 이미지 위의 옆 드래그가 그것을 팬한다 (세로 드래그는 패널 스크롤을 유지). **Mip** (레벨 위의 슬라이더, 그 레벨의 크기와 함께). **Channels**: RGBA, RGB (알파 무시), R, G, B, A를 회색으로, 라디오 버튼 한 줄로 (콤보는 두 번 탭이 드는데 각 한 번). **Checkerboard** 뒤에, 기본 켜짐. 이미지 아래에 포인터의 텍셀 좌표와 UV, hover하거나 누르는 동안 |
| 사용처 | 상세 아래에 그것을 쓰는 노드; 하나를 탭하면 그것이 선택된다 (그리고 Scene 탭처럼 Inspector가 열린다) |
| Inspector | Mesh 섹션이 "Multiplied with a texture."를 텍스처의 썸네일과 이름으로 바꾼다; 그것을 탭하면 그것이 선택된 채 Textures 탭이 열린다 |
| 깊이 표시 | 깊이는 **범위**로부터 매핑한 회색으로 보인다: 그림자 맵은 날것의 0..1 (정사영이라 이미 선형이다); 깊이 타깃은 카메라의 near와 far 평면으로 뷰 거리로 되돌리고 (reverse Z: 1이 가까움), 카메라에서 검정이고 **White at**에서 흰색 (기본은 공전 카메라 거리의 두 배, far 평면까지의 슬라이더). 깊이 타깃은 그 중 씬 뷰포트 부분을 보여 준다. 깊이에서는 밉과 채널 조작이 숨는다 |
| 깊이 타깃 샘플링 | 깊이 타깃은 Textures 탭이 보이는 동안에만 `TextureBinding`과 함께 만든다 (`NvRenderer.depth_sampled`, 앱이 설정; 바뀌면 렌더러가 텍스처를 다시 만든다). 그 밖에는 `RenderAttachment`만 유지하므로, 배포된 프레임은 결코 그 비용을 치르지 않는다 (아래 GPU 메모). 샘플링 가능한 동안 씬 패스는 깊이도 저장한다 (`WGPUStoreOp_Store`); 그 밖에는 패스 뒤에 아무것도 읽지 않으므로 버린다 |
| 스왑체인 | 크기, 형식, present mode와 함께 미리보기 없이 나열한다: ImGui 패스가 그 안에 렌더링하고, 패스는 자체 어태치먼트를 샘플링할 수 없다. 그것을 미리 보려는 복사는 적은 이득을 위해 매 프레임 전체 화면 복사가 든다 |
| 미리보기 슬롯 | `imgui.c`는 ImGui의 16개 옆에 미리보기 슬롯 64개를 둔다. 슬롯은 (텍스처, 밉, 모드, 깊이 범위)로 찾거나 만들고 (그 밉의 텍스처 뷰, 작은 유니폼 버퍼, bind group), GPU가 아직 그것으로 그릴 수 있으므로 3프레임 쓰지 않은 뒤 해제한다. 슬롯의 뷰가 텍스처를 붙잡으므로, 렌더러가 다시 만든 텍스처 (설정 변경 때의 그림자 맵)는 슬롯이 아직 그것을 키로 쓰는 동안 같은 주소로 돌아올 수 없다. 한 번에 64개를 넘으면 assert한다: clipper가 보이는 썸네일을 그보다 훨씬 아래로 유지한다 |
| 미리보기 파이프라인 | 같은 정점 배치의 두 번째 파이프라인. 그 프래그먼트 셰이더가 슬롯의 모드를 적용한다: 채널 하나를 회색으로, RGB는 알파를 1로 강제, 또는 깊이 (`texture_depth_2d`를 픽셀의 텍셀에서 `textureLoad`로 읽음, 샘플러 없음, 그다음 범위 매핑). 색 모드는 같은 선형 샘플러로 샘플링한다. 그리기 명령은 텍스처 id의 범위로 파이프라인을 고르므로, ImGui 자체의 그리기는 바뀌지 않는다 |
| sRGB | sRGB 텍스처는 선형 값으로 샘플링되고 캔버스 뷰가 다시 sRGB로 인코딩하므로 저장된 대로 보인다; 회색으로 보이는 채널은 저장된 채널 값이다 |
| 저장 | 안 함. 탭이 보여 주는 것 (섹션 상태, 선택, 줌, 밉, 채널, 범위)은 Console의 필터처럼 보기 상태다 |
| Undo | 여기서 undo되는 것은 없고, 여기서 undo가 비교하는 것을 바꾸는 것도 없다 |
| 서드파티 | 없음 |

### 엔진 API 변경

- **`nv/renderer.h`**
  - `NvRenderTexture`에 `char name[64]`, `u32 width, height, mip_count`, `WGPUTextureFormat format`이 생긴다.
  - `nv_renderer_add_texture(renderer, name, width, height, rgba, srgb, scratch)`: 새 `name` 매개변수 (NULL = `texture N`);
    `gltf.c`가 이미지의 이름을 넘긴다.
  - `NvRenderer.depth_sampled` (b32): 깊이 타깃도 `TextureBinding`과 함께 만든다.
  - 메모리 열을 위해 `nv/gpu.h`의 `nv_gpu_format_name` 옆에 `nv_gpu_format_bytes(WGPUTextureFormat)`.
- **`nv/imgui.h`**
  - `typedef enum NvImguiPreview { NV_IMGUI_PREVIEW_RGBA, NV_IMGUI_PREVIEW_RGB, NV_IMGUI_PREVIEW_R, ..._G, ..._B, ..._A, NV_IMGUI_PREVIEW_DEPTH }`.
  - `ImTextureID nv_imgui_preview(NvImgui* imgui, WGPUTexture texture, u32 mip, NvImguiPreview mode, f32 range_min, f32 range_max, f32 near, f32 far)`:
    이번 프레임 `igImage`에 넘길 id. `near`와 `far`는 reverse-Z 깊이를 거리로 바꾼다; 0과 0은 깊이를 날것으로 읽는다.
  - UI 그룹이 그것을 보여 줄 수 있게 `ImTextureID nv_imgui_font_atlas(NvImgui*)`.

### 앱 변경

- `app/textures.c` (새로 생김): Textures 탭, 그 상태 (`App` 안의 `TextureView`), 보이는 씬의 노드를 도는 "사용 중"과
  "사용처" 탐색, 체커보드.
- `app/ui.c`: 탭, 좁은 패널의 축소 맞춤, Inspector의 Mesh 썸네일.
- `app/main.c`: `renderer.depth_sampled`가 지난 프레임에 Textures 탭이 보였는지를 따른다.

### GPU 메모

사용자 선호에 따라 드라이버 동작은 Mesa의 RADV (오픈 소스 AMD Vulkan 드라이버)를 참고로 설명한다; 브라우저는 Linux와
Android에서 Vulkan에 닿는다.

- **샘플링되는 깊이 타깃은 압축을 잃을 수 있다.** RADV는 이미지마다 깊이가 HTILE (깊이 테스트와 clear를 빠르게 하는
  계층적 깊이 메타데이터)을 받는지, 그리고 그것이 "TC-compatible", 즉 텍스처 유닛이 압축된 데이터를 바로 읽을 수 있는지를
  정한다. 깊이 이미지가 샘플링 용도도 가지고 TC-compatible HTILE을 쓸 수 없으면 (옛 GCN 칩, 일부 형식과 샘플 수 경우),
  그것을 읽으려면 먼저 압축 해제 패스가 필요하거나, HTILE을 끈다. 브라우저는 WebGPU의 `TextureBinding`을 그 샘플링
  용도로 내려보낸다. 뷰어가 깊이 타깃을 보여 주는 동안에만 그것을 더하면 모든 칩에서 정상 경로가 그대로 남는다.
- **`textureLoad`로 깊이 읽기.** WebGPU에서 깊이 형식은 필터링할 수 없다; 샘플러 없는 load 명령은 어차피 하드웨어가 이것에
  대해 하는 일이고 (AMD에서는 image load), 셰이더가 프래그먼트의 UV에서 텍셀을 고른다.
- **깊이는 읽히는 동안에만 저장된다.** 씬 패스는 뷰어가 샘플링하지 않으면 깊이에 대해 `WGPUStoreOp_Discard`로 끝난다:
  그러면 타일러 (폰 GPU)는 깊이를 메모리에 결코 쓰지 않고, 데스크톱 드라이버는 패스 끝에서 압축 해제를 건너뛸 수 있다.
- **미리보기 뷰는 싸다.** 밉 레벨 하나의 텍스처 뷰는 복사가 아니라 디스크립터다; 뷰어의 비용은 bind group과 보이는
  썸네일을 위한 추가 draw call이다.

### 테스트

- **Node**: `nv_gpu_format_bytes`와 메모리 합 (밉 체인 있음과 없음).
- **Playwright**, Release와 Debug, 데스크톱 마우스와 폰 터치 크기. Debug 빌드는 텍스처 수, 사용 중인 미리보기 슬롯, 선택된
  텍스처, 행과 조작이 그려진 곳 (Console 탭이 하듯)을 export한다.
  - 쇼케이스는 머티리얼 텍스처 네 개 (glTF 셋과 흰색)를 glTF 이름, 크기, 밉 수와 함께 나열한다; "In use only"는 사용 중인
    것을 보여 준다; 스트레스 씬도 같은 것을 나열한다 (그 crowd가 캐릭터의 텍스처를 쓴다).
  - 썸네일의 픽셀이 텍스처와 맞는다 (썸네일 위치의 스크린샷 대 디코딩된 이미지, 허용 오차 안); `T_Eye_Brown`의 채널 R이
    회색으로 보인다.
  - 그림자 맵이 각 크기와 형식에서, 그리고 형식 변경 뒤에 미리 보인다; 깊이 타깃은 탭이 보이는 동안에만 미리 보이고, 다른
    탭을 고르면 `depth_sampled`가 다시 꺼진다; 그림자를 끄면 미리보기 없이 1×1 자리 표시자 줄이 보인다.
  - Mip 슬라이더: 보이는 레벨의 크기가 단계마다 반으로 준다.
  - "사용처" 노드를 탭하면 그것이 선택되고 Inspector가 열린다; Inspector의 썸네일은 그 텍스처가 선택된 채 탭을 연다.
  - 목록을 위아래로 스크롤해도 미리보기 슬롯이 64 아래에 머물고, 탭을 떠나면 유휴 수로 돌아간다.
  - 위의 모든 것을 거치는 동안 Console 탭에 WebGPU 오류 없음; 저장 왕복과 undo 단계 수는 바뀌지 않음.

### 단계

1. **엔진:** 텍스처 이름과 크기, `nv_gpu_format_bytes`, 미리보기 슬롯과 미리보기 파이프라인 (색, 채널, 깊이),
   `depth_sampled`. 테스트 창에 그림자 맵과 머티리얼 텍스처를 그리는 Debug export로 확인 (대신 탭 자체로 했다).
2. **탭:** 세 그룹, clipper가 있는 행, 줌, 팬, 밉, 채널, 깊이 범위가 있는 상세, "사용처", Inspector 썸네일, 폰의 축소 맞춤.
3. **경계 사례와 문서:** 미리 보는 동안 바뀌는 그림자 설정, 깊이 타깃을 미리 보는 동안의 크기 조절, 스트레스 씬, 긴
   스크롤 뒤의 슬롯 수; `AGENTS.md`, README, 아무것도 추가하지 않았다는 의존성 메모.

모든 단계는 헤드리스 Chromium에서 Release와 Debug로, 데스크톱 크기의 마우스와 폰 크기의 터치로 확인한다.

## English

Status: implemented (2026-09-30). Changes to this spec are agreed first.

### Goal

See the textures the engine is using, inside the app: the character's base color maps, the shadow
map, the scene's depth target and ImGui's font atlas. A **Textures** tab lists them with a
thumbnail, size, format, mip levels, memory and what uses them; picking one shows it large, with
mip level, channel and depth-range controls. The Inspector's Mesh section shows its material's
texture too.

Out of scope: reading texel values back to the CPU (a value under the pointer), editing or
replacing textures, and textures of past frames (no capture, as RenderDoc does natively).

### What there is today

| Texture | Where | Format | Usage | Can ImGui sample it today |
|---|---|---|---|---|
| Material textures (glTF base color: `T_Hair_1_BaseColor`, `T_Eye_Brown`, `T_Superhero_Male_Dark`) and the 1×1 white default (slot 0) | `NvRenderer.textures[256]`, `nv_renderer_add_texture` | `RGBA8UnormSrgb` (white: `RGBA8Unorm`), full mip chain | `TextureBinding`, `CopyDst` | Yes, through a new bind group; but the renderer keeps no name, size, format or mip count |
| Shadow map | `NvRenderer.shadow_texture` | `Depth32Float` or `Depth16Unorm`, 512² to 2048², or a 1×1 placeholder when off | `RenderAttachment`, `TextureBinding` | No: ImGui's shader takes `texture_2d<f32>` with a filtering sampler, and a depth format cannot be filtered |
| Scene color target | `NvRenderer.scene_color` | `BGRA8Unorm` (the canvas's format, rendered through its sRGB view), the scene's resolution rounded up to 64 (`resolution.md`) | `RenderAttachment`, `TextureBinding` | Yes: the upscale pass samples it; the part the scene fills is shown |
| Scene depth target | `NvRenderer.depth_texture` | `Depth32Float`, the scene's resolution rounded up to 64, reverse Z, 4 samples with MSAA (`msaa.md`, `resolution.md`) | `RenderAttachment`; plus `TextureBinding` while the tab is shown | Yes while the tab is shown (sample 0 of a multisampled texture) |
| Swapchain (the canvas) | `NvGpu.current_texture` | `BGRA8Unorm` with a `BGRA8UnormSrgb` view (per browser) | `RenderAttachment` | No, and it cannot be: the ImGui pass is drawing into it |
| ImGui font atlas | `NvImgui.textures[16]` | `RGBA8Unorm` | `TextureBinding`, `CopyDst` | Yes (it is ImGui's own) |

### Approaches

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **Our own viewer on `igImage`, with a preview pipeline in `imgui.c`** (recommended) | The tab draws textures with ImGui's `igImage`. `imgui.c` gains preview slots: a bind group per (texture, mip, mode) made on demand, drawn by a small second pipeline that can show one channel, or a depth texture as gray over a chosen range | C17, fixed slots, no new dependency; about 400 lines (engine and app) | Ours to write. No texel readback |
| imgui_tex_inspect (andyborrell, C++, MIT) | An ImGui texture inspector: zoom, pan, per-texel value annotations, channel masks | The closest match in features | C++ with no C API (no cimgui binding), and backends only for OpenGL and DirectX 11: its WebGPU backend (a shader and texel readback) would be ours to write anyway, plus a C++ wrapper like `anim.cpp` |
| Dear ImGui alone (`igImage`, `igImageWithBg`) | The widgets, with the current pipeline | Already here | Enough for color textures, but shows no depth texture, no channel, no mip level |
| WebGPU Inspector (Brendan Duncan, browser extension, MIT) | Captures a frame and lists every GPU object, textures included | Nothing to add to the engine | Outside the app: a desktop browser with the extension, not a phone. A good tool alongside, not the feature asked for |

Recommendation: write it ourselves on `igImage`. No third-party library.

### Decisions

| Topic | Decision |
|---|---|
| Tab | Desktop UI (`layout.md`): a **Textures** tab in the right dock, after Inspector and View; the right dock is tall enough for a texture, where the bottom dock is not. Phone UI: a tab between View and Console, and the tab bar uses ImGui's shrink fitting (`ImGuiTabBarFlags_FittingPolicyShrink`), so all five tabs stay visible with their labels cut short instead of a scrolling tab bar |
| List and picked texture | Where the tab is wide (700 px times `ui_scale` or more), the list and the picked texture sit side by side. Otherwise (the right dock, a phone) the tab shows one or the other: picking a row shows the texture, and **< Textures** goes back to the list |
| Groups | Three collapsing sections: **Materials** (the renderer's textures), **Render targets** (shadow map, depth target, swapchain), **UI** (ImGui's font atlas) |
| "In use" | A material texture is in use when a mesh node of the shown scene has a material with it as `base_color_texture`; the white default counts for materials with none. An **In use only** checkbox (on by default) hides the rest. A search box above the list (`search.md`) filters it by texture name and group ("materials" lists the whole group); the picked texture's detail is not filtered, and a narrow panel showing only the detail has no box. Render targets are in use while they are drawn to (the shadow map while shadows are on) |
| Row | A thumbnail (48 px times `ui_scale`, aspect kept, on a checkerboard so alpha shows), then three short lines: the name; size and format; mip levels, memory and users ("11 mips, 5.3 MB, 1 user"). Only rows on screen make thumbnails (`igIsItemVisible`), so the preview slots stay few however long the list |
| Memory | Width × height × bytes per texel, × 4/3 with a mip chain. The section headers show their sums, and the tab's top line the total |
| Names | Material textures take the glTF image's name (`T_Eye_Brown`), or `image N` when it has none; the default is `white`. Also set as the WebGPU label, so browser tools show the same name |
| Detail | The texture fits the panel's width and the height left (at least 160 px times `ui_scale`). **Zoom** (1× to 16×, a slider) and a sideways drag on the image pan it (a vertical drag keeps scrolling the panel). **Mip** (a slider over its levels, with that level's size). **Channels**: RGBA, RGB (alpha ignored), R, G, B, A as gray, as a row of radio buttons (one tap each, where a combo takes two). **Checkerboard** behind, on by default. The pointer's texel coordinates and UV under the image, on hover or while pressed |
| Used by | Under the detail, the nodes using it; tapping one selects it (and opens the Inspector, as the Scene tab does) |
| Inspector | The Mesh section replaces "Multiplied with a texture." with the texture's thumbnail and name; tapping it opens the Textures tab with it selected |
| Depth display | Depth is shown as gray, mapped from a **range**: the shadow map raw 0..1 (it is orthographic, so already linear); the depth target turned back into view distance with the camera's near and far planes (reverse Z: 1 is near), black at the camera and white at **White at** (twice the orbit camera's distance by default, a slider up to the far plane). The depth target shows the scene viewport's part of it. Mip and channel controls are hidden for depth |
| Depth target sampling | The depth target is made with `TextureBinding` only while the Textures tab is shown (`NvRenderer.depth_sampled`, set by the app; the renderer recreates the texture when it changes). Otherwise it keeps `RenderAttachment` alone, so a shipped frame never pays for it (GPU notes below). While samplable, the scene pass also stores its depth (`WGPUStoreOp_Store`); otherwise it discards it, as nothing reads it after the pass |
| Swapchain | Listed with size, formats and present mode, without a preview: the ImGui pass renders into it, and a pass cannot sample its own attachment. A copy to preview it would cost a full-screen copy every frame for little gain |
| Preview slots | `imgui.c` keeps 64 preview slots beside ImGui's 16. A slot is found by (texture, mip, mode, depth range) or made (a texture view of that mip, a small uniform buffer, a bind group), and released after 3 frames unused, since the GPU may still draw with it. The slot's view holds its texture, so a texture the renderer recreates (the shadow map on a setting change) cannot come back at the same address while a slot is still keyed by it. More than 64 at once asserts: the clipper keeps the visible thumbnails far below that |
| Preview pipeline | A second pipeline with the same vertex layout. Its fragment shader applies the slot's mode: a channel as gray, alpha forced to 1 for RGB, or depth (`texture_depth_2d` read with `textureLoad` at the pixel's texel, no sampler, then the range mapping). Color modes sample with the same linear sampler. Draw commands pick the pipeline by their texture id's range, so ImGui's own draws are unchanged |
| sRGB | sRGB textures are sampled as linear values and the canvas view encodes back to sRGB, so they show as stored; a channel shown as gray is the stored channel value |
| Saved | No. What the tab shows (section states, selection, zoom, mip, channels, range) is view state, like the Console's filters |
| Undo | Nothing here is undoable, and nothing here changes what undo compares |
| Third-party | None |

### Engine API changes

- **`nv/renderer.h`**
  - `NvRenderTexture` gains `char name[64]`, `u32 width, height, mip_count`, `WGPUTextureFormat format`.
  - `nv_renderer_add_texture(renderer, name, width, height, rgba, srgb, scratch)`: the new `name`
    parameter (NULL = `texture N`); `gltf.c` passes the image's name.
  - `NvRenderer.depth_sampled` (b32): make the depth target with `TextureBinding` too.
  - `nv_gpu_format_bytes(WGPUTextureFormat)` next to `nv_gpu_format_name` in `nv/gpu.h`, for the
    memory column.
- **`nv/imgui.h`**
  - `typedef enum NvImguiPreview { NV_IMGUI_PREVIEW_RGBA, NV_IMGUI_PREVIEW_RGB, NV_IMGUI_PREVIEW_R, ..._G, ..._B, ..._A, NV_IMGUI_PREVIEW_DEPTH }`.
  - `ImTextureID nv_imgui_preview(NvImgui* imgui, WGPUTexture texture, u32 mip, NvImguiPreview mode, f32 range_min, f32 range_max, f32 near, f32 far)`:
    the id to pass to `igImage` this frame. `near` and `far` turn reverse-Z depth into distance;
    0 and 0 read depth raw.
  - `ImTextureID nv_imgui_font_atlas(NvImgui*)`, so the UI group can show it.

### App changes

- `app/textures.c` (new): the Textures tab, its state (`TextureView` in `App`), the "in use" and
  "used by" walk over the shown scene's nodes, and the checkerboard.
- `app/ui.c`: the tab, shrink fitting on narrow panels, and the Inspector's Mesh thumbnail.
- `app/main.c`: `renderer.depth_sampled` follows whether the Textures tab was shown last frame.

### GPU notes

Per the user preferences, driver behavior is described with Mesa's RADV (the open-source AMD
Vulkan driver) as the reference; browsers reach Vulkan on Linux and Android.

- **A sampled depth target can cost compression.** RADV decides per image whether depth gets
  HTILE (the hierarchical depth metadata that speeds up depth tests and clears) and whether it is
  "TC-compatible", meaning the texture unit can read the compressed data directly. When a depth
  image also has the sampled usage and TC-compatible HTILE cannot be used (older GCN chips, some
  format and sample-count cases), reading it needs a decompress pass first, or HTILE is left off.
  Browsers pass WebGPU's `TextureBinding` down as that sampled usage. Adding it only while the
  viewer shows the depth target keeps the normal path untouched on every chip.
- **Reading depth with `textureLoad`.** Depth formats cannot be filtered in WebGPU; a load
  instruction without a sampler is what the hardware does for this anyway (an image load on AMD),
  and the shader picks the texel from the fragment's UV.
- **Depth is stored only while it is read.** The scene pass ends with `WGPUStoreOp_Discard` for
  depth unless the viewer samples it: a tiler (phone GPUs) then never writes depth to memory, and
  a desktop driver may skip decompressing it at the end of the pass.
- **Preview views are cheap.** A texture view for one mip level is a descriptor, not a copy; the
  cost of the viewer is the bind groups and the extra draw calls for visible thumbnails.

### Tests

- **Node**: `nv_gpu_format_bytes` and the memory sum (with and without a mip chain).
- **Playwright**, Release and Debug, desktop mouse and phone touch sizes. Debug builds export the
  texture count, the preview slots in use, the selected texture, and where rows and controls were
  drawn (as the Console tab does).
  - The showcase lists four material textures (three glTF and white), with the glTF names, sizes and
    mip counts; "In use only" shows the ones in use; the stress scene lists the same (its crowd uses the
    character's textures).
  - A thumbnail's pixels match the texture (a screenshot at the thumbnail against the decoded image,
    within a tolerance); channel R of `T_Eye_Brown` shows gray.
  - The shadow map previews at each size and format, and after a format change; the depth target
    previews only while the tab is shown, and `depth_sampled` goes back off when another tab is
    picked; turning shadows off shows the 1×1 placeholder line without a preview.
  - Mip slider: the shown level's size halves each step.
  - Tapping a "used by" node selects it and opens the Inspector; the Inspector's thumbnail opens the
    tab with its texture selected.
  - Scrolling the list up and down keeps preview slots under 64, and they return to the idle count
    after leaving the tab.
  - No WebGPU errors in the Console tab through all of the above; the save round trip and the undo
    step count are unchanged.

### Phases

1. **Engine:** texture names and sizes, `nv_gpu_format_bytes`, preview slots and the preview
   pipeline (color, channels, depth), `depth_sampled`. Checked with a Debug export that draws the
   shadow map and a material texture in a test window (done with the tab itself instead).
2. **Tab:** the three groups, rows with the clipper, detail with zoom, pan, mip, channels and depth
   range, "used by", the Inspector thumbnail, shrink fitting on phones.
3. **Edge cases and docs:** shadow settings changing while previewed, a resize while the depth
   target is previewed, the stress scene, the slot count after long scrolling; `AGENTS.md`, README
   and the Dependencies note that none was added.

Every phase is checked in Release and Debug in headless Chromium, with the mouse at desktop size
and with touch at phone size.

# D3D12 포팅 스펙 (Windows 네이티브 빌드, 엔진의 GPU API)

상태: 초안 (2026-10-12). "합의할 것"은 모두 정했다(2026-10-12). 이 스펙의 변경은 먼저 합의한다. **단계 0–6은 끝났고 검증했다**(웹은 단계 7을 기다린다)("찾은 것"); 단계 3부터는 Windows PC에서 한다.

### 목표

샌드박스와 오토배틀러를 **Windows x64 네이티브 실행 파일**로도 빌드하고, 그 빌드는 **Direct3D 12**로 그린다. 웹 빌드(WebGPU,
Emscripten)는 그대로 남고 지금처럼 주 배포물이다(CI, Pages; 합의함, 2026-10-12). 두 빌드는 한 소스에서 나온다. 네이티브 빌드는
어디에도 내놓지 않는다: CI 아티팩트도 GitHub 릴리스도 없고, 개발자가 자기 PC에서 빌드하고 시험한다(합의함, 2026-10-12).

- 렌더러, 효과, ImGui 그리기는 한 번만 쓴다. 그 코드는 WebGPU도 D3D12도 직접 부르지 않고 엔진의 GPU API(`engine/gpu.h`, 아래
  "GPU API")를 부른다. 그 아래에 백엔드가 둘 있다: `gpu_webgpu.c`와 `gpu_d3d12.c`. 빌드마다 하나만 컴파일한다.
- 브라우저에 기대던 것(창, 입력, 저장, 파일 대화 상자, 이미지 디코딩, 로그)은 Win32로 다시 쓴다(아래 "플랫폼 계층").
- **화면과 규칙은 바뀌지 않는다.** 씬 파일, 저장 형식(`state.nvs`), 실행 취소, Edit/Play, 셰이더 소스가 두 빌드에서 같다. 그림은
  웹 빌드와 허용 오차 안에서 같고, 효과의 덤프는 일치한다(아래 "검증").
- 이 포팅은 [Slang 포팅](slang_shader_port.md)이 "범위 밖"으로 미뤄 둔 세 가지(RHI, D3D12 백엔드, 네이티브 빌드)를 정한다. Slang
  스펙의 열린 질문(DXIL, `register`/`space` 규칙)의 답도 여기 있다.

범위 밖:

- Vulkan, Metal, D3D11, Linux와 macOS 네이티브, Windows on Arm.
- 네이티브 빌드의 터치 입력과 폰 UI. 네이티브는 항상 데스크톱 UI다(`Sandbox.ui_mode`).
- D3D12에만 있는 기능(메시 셰이더, 레이 트레이싱, 바인드리스, 웨이브 연산, 향상된 배리어). GPU API는 그것들을 막지 않게 짓지만,
  이 포팅은 1:1이다. 나중에 쓸 때는 `NvGpu`에 `has_timestamps` 같은 플래그를 두고 웹에는 대체 경로를 둔다.
- 멀티스레드 명령 기록, 비동기 컴퓨트, 큐 여럿. 지금처럼 큐 하나, 스레드 하나다.
- HDR 디스플레이 출력(scRGB 스왑체인), 티어링 끄기, 설치 프로그램, 코드 서명, 스토어 배포.
- 셰이더 핫 리로드, 런타임 셰이더 컴파일.

### 지금 웹에 묶인 곳 (2026-10-12, 커밋 `e6de84c`)

| 영역 | 파일 | 무엇 |
|---|---|---|
| GPU 호출 | `renderer.c` 115, `vfx.c` 57, `imgui.c` 41, `gpu.c` 28, `sandbox/main.c` 10, `sandbox/textures.c` 8, `autobattler/main.c` 5 | `wgpu*` 호출. 서로 다른 함수 78개 |
| GPU 타입 | `gpu.h`, `renderer.h`, `vfx.h`, `imgui.h` | `WGPU*` 타입이 공개 헤더에 있어서 앱도 본다(`WGPUTextureFormat`, `WGPUCommandEncoder` 등) |
| 창과 루프 | `window.c` | 캔버스 크기, `requestAnimationFrame`, 페이지 숨김, 시간 |
| 입력 | `imgui.c` (Emscripten 콜백 43곳) | 키, 마우스, 휠, 터치, 포커스, 텍스트 에이전트(IME), 클립보드, 커서 |
| 저장 | `storage.c` | IDBFS(IndexedDB), `EM_ASYNC_JS` |
| 로컬 파일 | `local_file.c` | File System Access API, 파일 입력, 내려받기 |
| 이미지 디코딩 | `gltf.c` | 브라우저의 `createImageBitmap`. 에셋의 이미지는 모두 JPEG다(chess.glb 33장, character.glb 3장) |
| 그 밖 | `log.c`(브라우저 콘솔, 페이지 큐), `strings.c`(브라우저 언어), `gpu.c`(`navigator`의 시스템 정보), `sandbox/shortcuts.c`(Mac 여부), `sandbox/stress.c`(user agent) | `EM_JS` |
| 디버그 내보내기 | `sandbox/*.c`, `autobattler/*.c` | `EMSCRIPTEN_KEEPALIVE` 93개. Playwright가 부른다 |
| 빌드 | `CMakeLists.txt` | Emscripten이 아니면 `FATAL_ERROR`. `nv_setup_executable`의 `.data` 묶기, Node에서 도는 ctest |
| 컴파일러 | `base.h`(`__builtin_trap`), `log.h`, `text.h`, `scene_file.c`, `autobattler/main.c`(`__attribute__((format))`) | clang/GCC 문법. MSVC `cl`은 받지 않으므로 매크로로 감싼다(아래 "빌드") |

### 합의할 것

AGENTS.md의 "Third-party libraries" 규칙에 따라 후보를 적고 추천했다. 네 가지 모두 정했다(합의함, 2026-10-12):

| 항목 | 정한 것 |
|---|---|
| 1. GPU API | 직접 작성(`engine/gpu.h`) |
| 2. 플랫폼 계층 | Win32 직접 |
| 3. DXIL 컴파일러 | DXC, 빌드 시점 도구로만 |
| 4. 그 밖 | **Agility SDK를 들인다**(추천과 다름). 나머지(D3D12 Memory Allocator, WinPixEventRuntime, stb_image)는 들이지 않는다 |

받아들인 것은 단계 3에서 실제로 더할 때 `CODING_STANDARD.md`의 의존성 절에 까닭과 함께 적는다.

#### 1. GPU API (RHI)

| 후보 | 무엇 | 언어, 라이선스 | 판단 |
|---|---|---|---|
| **직접 작성 (추천)** | `engine/gpu.h`에 엔진이 쓰는 것만 담은 API. 개념은 WebGPU를 따른다(버퍼, 텍스처, 뷰, 샘플러, 바인드 그룹 레이아웃, 바인드 그룹, 파이프라인, 패스). 백엔드는 컴파일 시점에 고른다(함수 포인터와 vtable 없음) | C17 | 엔진은 지금 WebGPU의 개념으로 쓰여 있고, WebGPU는 D3D12, Vulkan, Metal 위에 구현되도록 설계된 모델이라 모든 개념이 D3D12로 옮겨진다. 엔진이 쓰는 함수는 78개라 API가 작다. 코딩 표준(단순한 데이터, 직접 호출)에 맞는다. 대가: D3D12 백엔드(추정 3,000줄 안팎)를 직접 쓰고 디버깅한다 |
| Dawn 네이티브 | Chrome의 WebGPU 구현을 Windows에서 링크(D3D12 위의 WebGPU) | C++ (C API), BSD-3 | 렌더러 코드가 거의 그대로다. 그러나 D3D12를 쓰는 것이 아니라 WebGPU를 쓰는 것이라, Slang을 고른 까닭("D3D12의 기능을 그대로 쓸 수 있다", `CODING_STANDARD.md`)과 맞지 않는다. 런타임에 WGSL을 Tint로 HLSL로 바꾸고 DXC로 컴파일한다. 매우 큰 의존성이고, 릴리스 묶음에 링크할 라이브러리가 있는지 확인하지 않았다 |
| SDL3 GPU | SDL의 GPU API. D3D12, Vulkan, Metal | C, zlib | 네이티브 쪽은 잘 맞지만 WebGPU 백엔드가 없다(확인한 범위에서). 그러면 웹은 지금처럼 WebGPU를 직접 부르고, 엔진에 GPU API가 둘 생긴다. 타임스탬프 쿼리도 없다 |
| Diligent Engine | D3D11/12, Vulkan, Metal, WebGPU 백엔드를 가진 RHI | C++ (C 인터페이스 있음), Apache-2.0 | 두 플랫폼을 다 덮지만 크고, C++ 객체 모델과 자체 셰이더 처리를 갖는다. 코딩 표준과 멀다 |

#### 2. 플랫폼 계층

| 후보 | 판단 |
|---|---|
| **Win32 직접 (추천)** | 창, 메시지 루프, 키보드, 마우스, IME, 클립보드, 커서, DPI, 파일 대화 상자 모두 OS API로 된다. Handmade Hero의 방식 그대로다. 추정 1,500줄 안팎 |
| SDL3 (zlib) | 창, 입력, IME, 클립보드, 대화 상자를 준다. 그러나 웹 쪽은 지금의 `EM_JS` 계층(텍스트 에이전트, IDBFS, File System Access API, 터치 판정)을 바꿀 까닭이 없으므로 Windows 하나를 위해 큰 라이브러리를 들이는 셈이다 |
| GLFW (zlib) | IME 조합 창 위치, 파일 대화 상자가 없다. 한국어 입력에 모자란다 |

#### 3. 셰이더: DXIL 컴파일러

D3D12는 DXIL(셰이더 모델 6) 또는 DXBC(셰이더 모델 5.1) 바이트코드를 받는다. Slang 묶음에는 DXC가 없다(Slang 스펙 "확인한 것").

| 후보 | 판단 |
|---|---|
| **DXC (추천)** | Microsoft의 DirectXShaderCompiler. 공식 GitHub 릴리스의 미리 빌드된 묶음(`dxc.exe`, `dxcompiler.dll`, 서명용 `dxil.dll`)을 Slang, Tint와 같은 조건으로 쓴다: **빌드 시점 도구로만 실행하고 어디에도 링크하지 않으며, 버전과 SHA-256을 고정한다.** 라이선스는 `dxil.dll`이 Microsoft 라이선스, 나머지가 LLVM(University of Illinois)이다. 묶음 안의 라이선스 파일을 단계 3에서 확인해 `CODING_STANDARD.md`에 적는다. 셰이더 모델 6.6을 쓴다 |
| FXC (Windows SDK의 `d3dcompiler_47`) | SM 5.1까지만 컴파일한다. 최소 사양이 SM 6.6이므로(아래 "최소 사양") 쓸 수 없다 |

#### 4. 그 밖 (정함: Agility SDK만 들인다)

| 후보 | 판단 |
|---|---|
| D3D12 Memory Allocator (MIT, C++) | 리소스가 적고(텍스처 최대 256개, 타깃 십여 개, 버퍼 수십 개) 크기가 정해져 있어 커밋된 리소스(`CreateCommittedResource`)로 충분하다. C++이라 래퍼도 필요하다 |
| **Agility SDK (들인다, 합의함 2026-10-12)** | Microsoft의 D3D12 런타임(`D3D12Core.dll`, Debug의 `d3d12SDKLayers.dll`)을 실행 파일 옆에 두어, OS에 들어 있는 런타임 대신 쓴다. 그래서 런타임의 기능(셰이더 모델, 디버그 레이어)이 Windows 업데이트가 아니라 우리가 고정한 버전으로 정해진다. 아래 "Agility SDK" |
| WinPixEventRuntime | 패스 이름은 `ID3D12GraphicsCommandList::BeginEvent`로 PIX에 보인다. 형식 있는 마커가 필요해지면 다시 본다 |
| stb_image (공개 도메인/MIT) | JPEG 디코딩은 Windows Imaging Component(WIC, OS 내장)로 된다. 웹에서 브라우저가 디코딩하듯 네이티브는 OS가 한다. 스크린숏의 PNG 인코딩도 WIC로 된다 |

플랫폼이 요구하는 것으로 의존성 절에 더하는 것: Windows SDK(DXGI, WIC 헤더), Visual Studio의 MSVC(아래 "빌드"). D3D12 헤더는
Agility SDK의 것을 쓴다(런타임과 버전이 맞는다).

#### Agility SDK

- **받기:** NuGet 패키지 `Microsoft.Direct3D.D3D12`(`.nupkg`는 zip이다)를 `cmake/shaders.cmake`가 DXC처럼 받는다: 버전과 SHA-256을
  고정하고, `NV_TOOLS_DIR`에 풀고, `NV_AGILITY_SDK`로 대신할 수 있다. Windows 호스트의 네이티브 빌드에서만 받는다. 버전은 단계 3에서
  그때의 마지막 정식(프리뷰가 아닌) 버전으로 고정했다: **1.619.6**(2026-10-12; `D3D12SDKVersion` 619). DXC는 **v1.9.2609**
  (`dxc_2026_09_29.zip`).
- **쓰기:** 헤더(`include/d3d12.h` 등)를 Windows SDK의 것보다 먼저 찾게 한다. 실행 파일이 런타임에게 두 기호를 내보낸다
  (`engine/gpu_d3d12.c`):

  ```c
  // The D3D12 loader reads these from the executable and loads D3D12Core.dll from that folder instead of the OS's runtime.
  __declspec(dllexport) extern const UINT D3D12SDKVersion = NV_AGILITY_SDK_VERSION;
  __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
  ```

  `NV_AGILITY_SDK_VERSION`은 CMake가 고정한 버전에서 정의한다. 헤더와 DLL과 이 숫자가 한 버전이다.
- **설치:** `nv_setup_executable`이 `bin/x64/D3D12Core.dll`을 실행 파일 옆의 `D3D12\`에 복사한다. Debug 빌드는 `d3d12SDKLayers.dll`도
  (디버그 레이어가 런타임과 버전이 맞아야 한다). 링크하는 것은 없다: 런타임이 그 폴더에서 읽는다.
- **확인:** 시작할 때 런타임이 우리 `D3D12Core.dll`을 읽었는지 로그에 남긴다(`ID3D12SDKConfiguration` 또는 그 모듈의 경로). 읽지
  못하면 OS의 런타임으로 돌아가는 대신 알리고 끝낸다: 두 런타임에서 달리 도는 것을 디버깅하지 않기 위해서다.
- **라이선스:** Microsoft의 라이선스(DirectX SDK EULA). 실행 파일과 함께 재배포를 허용한다. 네이티브 빌드는 어디에도 내놓지 않지만,
  단계 3에서 패키지 안의 라이선스 파일을 읽고 `CODING_STANDARD.md`에 적는다.
- **바뀌지 않는 것:** 최소 사양(셰이더 모델 6.6, 기능 수준 12_2)과 1:1 포팅의 범위. Agility SDK가 셰이더 모델 6.8(`SV_StartInstanceLocation`)과
  향상된 배리어를 열어 주지만 그것들은 드라이버도 받아야 하는 기능이므로 쓰지 않는다: first instance는 아래 루트 상수 그대로이고, 배리어는
  기존 전이 배리어다. 런타임 쪽 셰이더 모델 6.6이 보장되므로 "기본 런타임이 6.6을 받는가"라는 열린 질문은 없어진다(드라이버는 여전히
  `CheckFeatureSupport`로 확인한다).

### 최소 사양 (합의함, 2026-10-12)

| 항목 | 최소 |
|---|---|
| OS | Windows 11 x64 |
| D3D12 기능 수준 | 12_2 (DirectX 12 Ultimate) |
| 셰이더 모델 | 6.6 |

- **까닭:** 네이티브 빌드는 개발 도구다(PIX와 RenderDoc 디버깅, 실제 GPU 시간, 네이티브 디버거). 나중에 일반 사용자에게 내놓을 수도
  있고, D3D12에만 있는 기능을 더할 수도 있다. 기준을 처음부터 높게 두면, 그런 기능을 더할 때 D3D12 안에서 하드웨어마다 갈라지는 경로가
  생기지 않는다.
- **기능 수준 12_2가 보장하는 것** (Microsoft DirectX 블로그, "New in DirectX — Feature Level 12_2", 2020): 셰이더 모델 6.5, 리소스
  바인딩 티어 3, 루트 시그니처 1.1, 레이 트레이싱(DXR) 1.1, 메시 셰이더 티어 1, 가변 셰이딩 비율 티어 2, 샘플러 피드백 티어 0.9, 타일
  리소스 티어 3, 보수적 래스터화 티어 3. **셰이더 모델 6.6은 보장하지 않으므로 따로 확인한다.** 6.6이 더하는 것은 동적 리소스(바인드리스
  `ResourceDescriptorHeap`), 64비트 아토믹, 웨이브 크기 지정 등이다.
- **대상 하드웨어:** 처음 발표된 것은 NVIDIA Turing(RTX 20)과 Ampere, AMD RDNA 2(RX 6000)였고, 그 뒤의 세대들이다. 그보다 오래된
  GPU(GTX 10, RX 500/Vega 등)에서는 네이티브 빌드가 돌지 않는다. 그런 사용자는 웹 빌드를 쓴다.
- **시작할 때 확인:** `D3D12CreateDevice`를 `D3D_FEATURE_LEVEL_12_2`로, `CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL)`이 6.6 이상인지.
  하나라도 안 되면 어댑터 이름과 무엇이 모자란지를 로그에 남기고, 메시지 상자로 알린 뒤 끝낸다(`T()`로, 한국어 행 포함). 웹의 "WebGPU
  없음"과 같은 자리다.
- **어댑터 고르기** (합의함, 2026-10-12): 언제나 자동이고 고르는 옵션은 없다. `IDXGIFactory6::EnumAdapterByGpuPreference`를
  `DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE`로 불러 외장 GPU부터 차례로 보고, 위 확인을 통과하는 첫 어댑터를 쓴다. 외장 GPU가 없거나
  통과하지 못하면 내장 GPU로 넘어간다. **WARP(소프트웨어, `DXGI_ADAPTER_FLAG_SOFTWARE`)는 쓰지 않는다**: 목록에서 건너뛰고, 통과하는
  하드웨어 어댑터가 없으면 위처럼 알리고 끝낸다. 고른 어댑터와 건너뛴 어댑터(와 그 까닭)를 로그에 남긴다.
- **설계에 주는 것:**
  - 리소스 바인딩 티어 3이라 D3D12 쪽의 디스크립터 한도와 "테이블의 모든 자리가 유효해야 한다"는 티어 1 규칙을 신경 쓰지 않는다.
  - **함께 쓰는 셰이더의 한도는 여전히 WebGPU의 것이다.** 웹이 주 배포물이므로, 두 백엔드가 같이 쓰는 셰이더와 바인드 그룹은 WebGPU
    기본 한도를 넘지 않는다. 예: `vfx_compute.slang`의 읽고 쓰는 버퍼는 지금 8개(`particles`부터 `draw_args`까지)이고, WebGPU의
    `maxStorageBuffersPerShaderStage` 기본값이 8이다.
  - **D3D12 전용 기능**(나중에 더한다면)은 `NvGpu`의 플래그로 알린다(`has_timestamps`처럼). 최소 사양이 높으므로 그 플래그는 D3D12
    빌드에서 언제나 켜지고 웹에서 꺼진다. 그래서 갈림길은 백엔드 둘뿐이고, 웹에는 대체 경로가 있어야 한다.
  - DXIL은 `-profile sm_6_6`으로 컴파일한다. 그래서 FXC(SM 5.1까지)는 후보에서 빠진다.
- **개발 PC:** NVIDIA GeForce RTX 5070 Laptop GPU(Blackwell)가 조건을 채우고, 어댑터 고르기가 언제나 이것을 고른다. 함께 있는 AMD
  Radeon 내장 GPU는 쓰이지 않는다(세대도 확인하지 않았다).
- **일반 사용자에게 내놓는 것은 여전히 범위 밖이다.** 그때 이 최소 사양을 다시 볼지 정한다.

### GPU API (`engine/gpu.h`)

#### 원칙

- **WebGPU의 부분집합이다.** 엔진 코드가 지금 쓰는 것만 담는다. 웹 백엔드는 거의 일대일로 `wgpu*`를 부르고, D3D12 백엔드는
  WebGPU가 숨겨 둔 일(리소스 상태, 배리어, 디스크립터 힙, 프레임 동기화, 업로드)을 맡는다. **엔진 코드는 배리어를 쓰지 않는다.**
- **핸들은 인덱스와 세대다.** `typedef struct NvGpuTexture { u32 index; u32 generation; } NvGpuTexture;`처럼 인덱스 0이 "없음"인
  구조체(코딩 표준의 핸들 규칙). 백엔드가 고정 용량의 풀을 갖고, 넘치면 assert한다. 해제한 칸을 다시 쓸 때 세대가 오르고, 세대가 맞지
  않는 핸들을 쓰면 assert한다. **세대가 필요한 까닭:** ImGui의 미리보기 슬롯(`imgui.c`의 `nv_imgui_preview`)은 텍스처를 그 정체로
  찾고(`same_preview`), 지금은 `wgpuTextureAddRef`로 붙잡아 같은 포인터가 다른 텍스처에 다시 쓰이지 않게 한다. 인덱스만 있으면 깊이
  타깃을 다시 만든 뒤 같은 칸의 새 텍스처가 옛 슬롯과 같아 보여 옛 뷰를 그린다. 그래서 참조 횟수(`AddRef`) 대신 세대로 정체를 가린다.
  공개 헤더는 `<webgpu/webgpu.h>`도 `<d3d12.h>`도 포함하지 않는다.
- **백엔드의 상태는 백엔드 파일 안에 있다.** `NvGpu`는 공통 필드(창, 크기, 포맷, `has_timestamps`, 백엔드 이름)만 갖는다. 장치는
  프로세스에 하나다.
- **포맷은 엔진의 열거형이다.** `NvGpuFormat`: `BGRA8`, `BGRA8_SRGB`, `RGBA8`, `RGBA8_SRGB`, `RGBA16F`, `DEPTH32F`, `DEPTH16`.
  `nv_gpu_format_name`, `nv_gpu_format_bytes`, `NV_SCENE_FORMAT`이 그것을 쓴다(`gpu_format_test`도).
- **명령 목록은 프레임에 하나다.** `nv_gpu_begin_frame`이 열고 `nv_gpu_end_frame`이 제출한다(D3D12는 제시도 한다). 앱이
  인코더를 만들고 제출하던 코드(`sandbox/main.c`, `autobattler/main.c`)가 없어지고, `nv_renderer_draw`와 `nv_imgui_render`는
  인코더를 받지 않는다.
- **콜백 대신 폴링이다.** `wgpuBufferMapAsync`의 콜백(타임스탬프, 효과 통계)은 읽기 요청과 매 프레임의 폴로 바뀐다. 결과는 지금처럼
  몇 프레임 늦게 온다. 앞의 읽기가 끝나지 않았으면 요청이 거짓을 돌려주므로, 렌더러와 효과가 따로 갖던 상태 플래그
  (`timestamp_copied`/`timestamp_mapping`, `stats_copied`/`stats_mapping`)가 없어진다.
- **쓰기는 언제든 된다.** 버퍼와 텍스처 쓰기는 프레임 밖(초기화, `nv_renderer_init`의 흰 텍스처)에서도, 패스를 기록하는 중간에도
  (`record_bloom`은 씬 패스 뒤에 bloom 유니폼을 쓴다) 불린다. 의미는 WebGPU의 큐 쓰기 그대로다: 다음 제출의 모든 명령보다 먼저
  적용된다. 그래서 한 버퍼를 한 프레임에 두 번 쓰면 마지막 것만 남는다(bloom이 패스마다 동적 오프셋을 쓰는 까닭).

#### 모양 (초안)

```c
typedef struct NvGpuBuffer   { u32 index, generation; } NvGpuBuffer;
typedef struct NvGpuTexture  { u32 index, generation; } NvGpuTexture;
typedef struct NvGpuView     { u32 index, generation; } NvGpuView;     // 기본 뷰, mip 하나, 또는 깊이 aspect
typedef struct NvGpuSampler  { u32 index, generation; } NvGpuSampler;
typedef struct NvGpuLayout   { u32 index, generation; } NvGpuLayout;   // 바인드 그룹 레이아웃
typedef struct NvGpuGroup    { u32 index, generation; } NvGpuGroup;    // 바인드 그룹 (만든 뒤 바뀌지 않는다)
typedef struct NvGpuPipeline { u32 index, generation; } NvGpuPipeline; // 렌더 또는 컴퓨트

NvGpuBuffer   nv_gpu_create_buffer(NvGpu* gpu, const NvGpuBufferDesc* desc);
void          nv_gpu_write_buffer(NvGpu* gpu, NvGpuBuffer buffer, u64 offset, const void* bytes, umm size);
NvGpuTexture  nv_gpu_create_texture(NvGpu* gpu, const NvGpuTextureDesc* desc);
// 한 mip의 (x, y)부터 width x height. ImGui는 글꼴 아틀라스의 바뀐 사각형만 자기 행 간격(pitch)으로 올린다.
void          nv_gpu_write_texture(NvGpu* gpu, NvGpuTexture texture, u32 mip, u32 x, u32 y, u32 width, u32 height,
                                   const void* texels, u32 bytes_per_row);
NvGpuTextureDesc nv_gpu_texture_desc(NvGpu* gpu, NvGpuTexture texture); // 만들 때의 크기, mip 수, 포맷, 샘플 수, 쓰임
NvGpuView     nv_gpu_create_view(NvGpu* gpu, NvGpuTexture texture, const NvGpuViewDesc* desc);
NvGpuSampler  nv_gpu_create_sampler(NvGpu* gpu, const NvGpuSamplerDesc* desc);
NvGpuLayout   nv_gpu_create_layout(NvGpu* gpu, const NvGpuLayoutEntry* entries, u32 count); // 항목마다 단계, 종류, stride
NvGpuGroup    nv_gpu_create_group(NvGpu* gpu, NvGpuLayout layout, const NvGpuGroupEntry* entries, u32 count); // 버퍼는 offset, size
NvGpuPipeline nv_gpu_create_render_pipeline(NvGpu* gpu, const NvGpuRenderPipelineDesc* desc);   // 그룹 레이아웃 배열을 받는다
NvGpuPipeline nv_gpu_create_compute_pipeline(NvGpu* gpu, const NvGpuComputePipelineDesc* desc);
void          nv_gpu_release_buffer(NvGpu* gpu, NvGpuBuffer buffer); // ... 종류마다 하나

void nv_gpu_begin_render_pass(NvGpu* gpu, const NvGpuRenderPassDesc* desc); // 색 하나(+리졸브), 깊이, 타임스탬프 자리 둘
void nv_gpu_begin_compute_pass(NvGpu* gpu, const NvGpuComputePassDesc* desc);
void nv_gpu_end_pass(NvGpu* gpu);
void nv_gpu_set_pipeline(NvGpu* gpu, NvGpuPipeline pipeline);
void nv_gpu_set_group(NvGpu* gpu, u32 index, NvGpuGroup group, const u32* dynamic_offsets, u32 dynamic_count);
void nv_gpu_draw_indexed(NvGpu* gpu, u32 index_count, u32 first_index, s32 base_vertex, u32 first_instance);
// set_vertex_buffer (슬롯 0), set_index_buffer (u16, u32), set_viewport, set_scissor, draw, draw_indirect, dispatch,
// dispatch_indirect, copy_buffer_to_buffer

// 패스 시간. 쿼리 자리 NV_GPU_MAX_TIMESTAMPS개를 장치가 갖고, 패스가 시작과 끝에 쓸 자리 둘을 고른다.
typedef struct NvGpuTimestamps { b32 ok; u64 nanoseconds[NV_GPU_MAX_TIMESTAMPS]; } NvGpuTimestamps;
b32             nv_gpu_resolve_timestamps(NvGpu* gpu, u32 count); // 이 프레임의 자리를 읽기로. 앞의 것이 읽히는 중이면 거짓
NvGpuTimestamps nv_gpu_poll_timestamps(NvGpu* gpu);              // 준비되면 한 번 ok, 나노초

// 버퍼 읽기 (효과 통계). 자리마다 하나씩 진행된다.
typedef struct NvGpuReadback { b32 ok; const void* bytes; umm size; } NvGpuReadback;
b32           nv_gpu_copy_for_readback(NvGpu* gpu, u32 slot, NvGpuBuffer source, u64 offset, umm size); // 그 자리가 바쁘면 거짓
NvGpuReadback nv_gpu_poll_readback(NvGpu* gpu, u32 slot); // 준비되면 한 번 ok

#if !defined(NDEBUG)
void nv_gpu_debug_raise_error(NvGpu* gpu); // 검증 오류를 일부러 낸다: _sandbox_debug_wgpu_error와 그 D3D12 짝
#endif
```

`NvGpu`는 백엔드가 채우는 한도도 갖는다: `max_buffer_bytes`, `max_storage_binding_bytes`. 효과(`nv_vfx_init`)가 파티클 용량을 그것에
맞춘다(지금은 `wgpuDeviceGetLimits`).

- 이름과 필드는 구현하며 바뀔 수 있다. 바뀌지 않는 것은 원칙이다.
- **레이아웃은 명시한다.** `wgpuRenderPipelineGetBindGroupLayout`(자동 레이아웃, `imgui.c` 2곳, `renderer.c` 1곳)은 없어진다. D3D12는
  루트 시그니처를 미리 알아야 한다.
- **구조화 버퍼의 원소 크기:** D3D12의 `StructuredBuffer` SRV/UAV는 원소 크기가 필요한데 WebGPU의 바인드 그룹 항목에는 없다.
  레이아웃 항목에 `stride`를 둔다(웹 백엔드는 무시한다). `shader_layout_test`가 리플렉션과 비교한다.
- **셰이더:** `nv_add_shader`가 백엔드마다 다른 헤더를 만든다. 웹은 지금의 WGSL 텍스트 하나, D3D12는 엔트리 포인트마다 DXIL 바이트.
  둘 다 `const NvGpuShaderCode <name>_shader`로 같은 이름이라, 파이프라인을 만드는 코드는 백엔드를 모르고 엔트리 포인트 이름으로
  찾는다(이름은 지금 그대로).

#### 엔진이 쓰는 것 (전부, 2026-10-12 커밋 `e6de84c`의 `renderer.c`, `vfx.c`, `imgui.c`)

GPU API는 이 표만 구현한다. 표에 없는 값은 두 백엔드 모두 assert한다. 새로 필요해지면 표에 행을 더하고 두 백엔드를 함께 고친다.

| 무엇 | 쓰는 것 |
|---|---|
| 텍스처 | 2D만. mip 1개 이상(재질 텍스처, bloom 6단), 샘플 1 또는 4. 쓰임: 샘플링, 렌더 타깃(깊이 포함), 복사 대상. 깊이 타깃은 Textures 탭이 보일 때만 샘플링 쓰임을 갖는다 |
| 포맷 | `RGBA8`, `RGBA8_SRGB`(재질), `RGBA16F`(씬, bloom), `DEPTH32F`(깊이, 그림자), `DEPTH16`(그림자), 캔버스 `BGRA8`/`RGBA8`과 그 sRGB 뷰 |
| 뷰 | 기본(모든 mip), mip 하나(bloom의 단마다, 미리보기), 깊이 aspect(미리보기) |
| 샘플러 | 반복 또는 가장자리 고정, 선형 확대/축소, mip 선형 또는 최근접, 비교(`LessEqual`, 그림자) |
| 바인딩 | 유니폼 버퍼(동적 오프셋은 bloom의 하나뿐), 읽기 전용 저장 버퍼, 읽고 쓰는 저장 버퍼(아토믹 포함), 2D 텍스처(float, depth, 다중 샘플 depth), 샘플러(필터링, 비교). 그룹 0과 1만, 그룹 하나에 항목 12개까지, 샘플러 하나까지 |
| 정점 입력 | 버퍼 슬롯 0 하나, 정점마다(인스턴스 스텝 없음). `Float32x2`, `Float32x3`, `Float32x4`, `Uint16x4`(관절), `Unorm8x4`(ImGui 색) |
| 인덱스 | `u16`(ImGui), `u32`(메시) |
| 렌더 파이프라인 | 색 타깃 0개(그림자) 또는 1개. 깊이 `DEPTH32F`/`DEPTH16` 또는 없음, 스텐실 없음. `TriangleList`, `LineList`(디버그 선). 컬 없음 또는 뒷면, 앞면 CCW. 깊이 비교 `Greater`(리버스 Z), `Less`(그림자), `Always`(디버그 선). 깊이 바이어스(상수, 기울기: 그림자). 블렌드 셋: 알파(`SrcAlpha`, `OneMinusSrcAlpha`), 더하기(`One`, `One`), bloom 업샘플(색 더하기, 알파 유지). 샘플 1 또는 4 |
| 패스 | 색 첨부 하나(`Clear`/`Load`, `Store`/`Discard`, 리졸브 타깃 하나: MSAA), 깊이(`Clear`, `Store`/`Discard`), 타임스탬프 자리 둘(bloom은 첫 패스의 시작과 마지막 패스의 끝으로 여러 패스를 잰다) |
| 그리기 | `Draw`, `DrawIndexed`(ImGui는 base vertex, 메시는 **first instance = 오브젝트 번호**), `DrawIndirect`(인덱스 없음: 파티클) |
| 컴퓨트 | `Dispatch`, `DispatchIndirect`(시뮬레이션). 한 컴퓨트 패스에 디스패치 다섯: `prepare_a`가 쓴 `sim_args`를 같은 패스의 `simulate`가 간접 인자로 읽는다 |
| 복사 | 버퍼에서 버퍼(타임스탬프, 효과 통계: API 안으로 들어간다), 큐 쓰기(버퍼, 텍스처의 mip과 부분 사각형) |
| 없는 것 | 텍스처 복사, 스텐실, 여러 색 타깃, 인스턴스 정점 버퍼, 3D/배열/큐브 텍스처, 저장 텍스처, 인덱스 간접 그리기, 렌더 번들, 오클루전 쿼리, 자동 레이아웃 |

#### D3D12 백엔드 (`engine/gpu_d3d12.c`)

C17에서 `COBJMACROS`로 D3D12와 DXGI의 C 인터페이스를 부른다(`ID3D12Device_CreateCommittedResource(device, ...)`). C++ 도우미
(`d3dx12.h`)는 쓰지 않는다.

| WebGPU의 것 | D3D12에서 | 메모 |
|---|---|---|
| 어댑터, 장치 | DXGI 팩토리, `D3D12CreateDevice`, 최소 사양(위 "최소 사양") 확인 | 외장 GPU를 먼저, 없으면 내장 GPU. WARP는 쓰지 않는다(위 "어댑터 고르기") |
| 캔버스 서피스 | 플립 모델 스왑체인(`FLIP_DISCARD`), 버퍼 3개, `BGRA8_UNORM`, RTV는 `BGRA8_UNORM_SRGB` | 웹과 똑같은 구조다: 캔버스는 선형 포맷이고 sRGB 뷰로 그린다. 플립 모델은 sRGB 스왑체인 포맷을 받지 않지만 sRGB RTV는 받는다 |
| `Fifo` 제시 | `Present(1, 0)` | 웹의 "`wgpuSurfacePresent`를 부르지 않는다"는 웹 백엔드의 규칙으로 남는다 |
| 큐에 쓰기(`writeBuffer`, `writeTexture`) | 프레임마다 업로드 링(고정 용량)에 복사하고, 그 프레임의 명령 앞에 `CopyBufferRegion`/`CopyTextureRegion` | 의미는 WebGPU와 같다: 제출 전에 쓴 것은 그 제출의 명령 전에 적용된다. 텍스처 행은 256바이트로 맞춰 다시 담는다. 링에 맞지 않으면(씬을 불러올 때) 지금까지를 제출하고 기다린 뒤 링을 비운다 |
| 프레임 동기화 | 진행 중인 프레임 2개, 펜스 | 웹에는 없던 것. 업로드 링, 읽기, 지연 해제가 이것에 기댄다 |
| 리소스 해제 | 지연 해제: 그 프레임의 펜스가 지난 뒤 | WebGPU는 GPU가 쓰는 중에 해제해도 되므로 엔진 코드는 그렇게 쓴다 |
| 리소스 상태 | 백엔드가 **서브리소스(mip)마다** 현재 상태를 갖는다. 그리기와 디스패치 직전에, 바뀐 그룹, 정점, 인덱스, 간접 인자 버퍼가 쓰는 서브리소스를 보고 전이 배리어를 넣는다. 패스를 시작할 때는 첨부를 | mip마다인 까닭: bloom은 한 텍스처의 mip i에 그리면서 mip i+1을 샘플링한다. 그리기마다인 까닭: 렌더 패스는 `BeginRenderPass`가 아니라 `OMSetRenderTargets`로 하므로 그리기 사이에 배리어를 넣을 수 있고, 컴퓨트에는 패스가 없어 WebGPU처럼 디스패치마다가 사용 범위다(`sim_args`가 같은 패스 안에서 UAV에서 간접 인자로 바뀐다). 디스패치 사이에는 UAV 배리어도 넣는다. 충돌하는 쓰임은 WebGPU가 이미 막으므로 그 검사는 하지 않는다 |
| first instance | 렌더 루트 시그니처마다 끝에 루트 상수 하나(`b0, space8`). `nv_gpu_draw_indexed`가 그 값이 바뀔 때 `SetGraphicsRoot32BitConstant`로 쓴다 | **D3D12의 `SV_InstanceID`는 `StartInstanceLocation`을 더하지 않는다**(WebGPU의 `instance_index`는 더한다). 메시와 그림자 셰이더는 오브젝트 번호를 그것으로 읽으므로(`mesh.slang`, `shadow.slang`의 `vs_static`, `vs_skinned`) 그대로면 모든 오브젝트가 0번 행렬로 그려진다. 아래 "셰이더 바인딩". `SV_StartInstanceLocation`은 셰이더 모델 6.8이라 쓰지 않는다. `SV_VertexID`의 같은 차이는 문제가 없다: 셰이더가 정점 번호를 읽는 그리기는 모두 first vertex가 0이다 |
| 작은 버퍼 | 버퍼마다 커밋된 리소스 | 커밋된 리소스는 64 KB 단위다. 재질마다 16바이트 유니폼(최대 256개)과 미리보기 매개변수(최대 64개)로 많아야 약 20 MB를 쓴다. 단순함을 위해 받아들인다 |
| 바인드 그룹 레이아웃 | 루트 시그니처의 디스크립터 테이블 | 그룹 g는 `space g`. 그룹마다 CBV/SRV/UAV 테이블 하나와 (있으면) 샘플러 테이블 하나. 동적 오프셋 유니폼(bloom)은 루트 CBV |
| 바인드 그룹 | 셰이더에서 보이는 힙에 만들 때 한 번 써 둔다(크기 등급 1, 2, 4, 8, 16의 자유 목록) | 그룹은 바뀌지 않으므로 그리기마다 복사하지 않는다. 샘플러는 몇 개뿐이라 샘플러 힙에 영구히 두고, 그룹에는 샘플러가 하나까지다(assert). 루트 시그니처는 1.1이고, 디스크립터에 `DATA_STATIC_WHILE_SET_AT_EXECUTE` 같은 플래그를 단다 |
| 유니폼 버퍼 | CBV. 크기는 256바이트 배수로 올려 만든다 | 동적 오프셋은 이미 256 정렬이다(WebGPU의 기본 `minUniformBufferOffsetAlignment`도 256) |
| 깊이 텍스처를 샘플링 | `R32_TYPELESS`(`R16_TYPELESS`) 리소스, DSV `D32_FLOAT`, SRV `R32_FLOAT` | 깊이 미리보기와 그림자 맵 |
| MSAA 리졸브 | 패스 끝에 `ResolveSubresource` | 4x는 기능 수준 11_0부터 모든 렌더 타깃 포맷에 보장된다 |
| 간접 그리기와 디스패치 | `ExecuteIndirect`, 명령 시그니처 둘 | 인자의 바이트 배치가 WebGPU와 같다(`D3D12_DRAW_ARGUMENTS`, `D3D12_DISPATCH_ARGUMENTS`). 셰이더는 바뀌지 않는다 |
| 타임스탬프 | 타임스탬프 쿼리 힙, 패스 시작과 끝의 `EndQuery`, 리드백 버퍼로 `ResolveQueryData` | 틱을 `GetTimestampFrequency`로 나노초로 바꿔 `nv_gpu_poll_timestamps`가 돌려준다. 렌더러는 지금처럼 나노초를 읽는다 |
| `MapAsync` 읽기 | 리드백 힙 버퍼, 펜스가 지나면 준비 | `nv_gpu_poll_readback` |
| 깊이 타깃의 쓰임 | 샘플링하지 않을 때는 `D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE`와 `D32_FLOAT`, 할 때만 `R32_TYPELESS` | 웹이 Textures 탭을 볼 때만 샘플링 쓰임을 주는 까닭(압축)과 같다 |
| 레이블 | `ID3D12Object::SetName`, 패스는 `BeginEvent`/`EndEvent` | PIX와 디버그 레이어가 같은 이름을 보인다 |
| 검증 오류 | Debug 빌드: 디버그 레이어와 GPU 기반 검증. `ID3D12InfoQueue`의 메시지를 `nv_log(NV_LOG_ERROR, "d3d12", ...)`로 | 웹의 "WebGPU 오류 0개"에 해당하는 관문이다 |
| 장치 분실 | `DXGI_ERROR_DEVICE_REMOVED`를 로그하고(DRED가 있으면 그 이유도) 프레임을 멈춘다 | 웹의 `on_device_lost`처럼 |

좌표계는 바꿀 것이 없다: WebGPU와 D3D12 모두 NDC의 y가 위, 프레임버퍼와 텍스처 좌표의 원점이 왼쪽 위, 클립 z가 0..1이다.
리버스 Z(`reverse_depth`)도 그대로다.

#### 셰이더 바인딩 (Slang 스펙의 열린 질문)

- 모든 리소스에 지금의 `[[vk::binding(b, g)]]`에 더해 `: register(<종류><b>, space<g>)`를 붙인다. 종류는 유니폼 `b`, 읽기 전용
  버퍼와 텍스처 `t`, 읽고 쓰는 버퍼 `u`, 샘플러 `s`. 번호는 지금의 binding 번호 그대로다. 그래서 C 쪽 레이아웃 표 하나가 두 백엔드에
  맞는다.
- Slang의 HLSL 출력은 `register`가 없으면 차례로 번호를 매긴다(Slang 스펙 "확인한 것"). 그래서 빠진 것은 `shader_layout_test`가 DXIL
  리플렉션에서 잡는다.
- **오브젝트 번호:** 셰이더는 `SV_InstanceID`를 직접 쓰지 않고 `renderer_frame.slang`의 함수로 읽는다. D3D12 빌드는 `slangc`에
  `-DNV_D3D12`를 준다:

  ```slang
  #if defined(NV_D3D12)
  // IMPORTANT: D3D12's SV_InstanceID leaves out StartInstanceLocation; the backend passes it as a root constant.
  struct FirstInstance { uint value; };
  ConstantBuffer<FirstInstance> first_instance : register(b0, space8);
  uint object_index(uint instance_id) { return instance_id + first_instance.value; }
  #else
  uint object_index(uint instance_id) { return instance_id; }
  #endif
  ```

  단계 3에서 넣는다. 웹은 `#else` 쪽이라 WGSL이 바뀌지 않는다. `shader_layout_test`가 DXIL 리플렉션에서 `space8`이 이 상수 하나뿐인지 본다.
- 정점 입력: Slang의 HLSL은 `TEXCOORD<n>` 시맨틱을 쓰므로 D3D12 입력 레이아웃은 `SemanticName "TEXCOORD"`, `SemanticIndex =
  location`이다. AGENTS.md의 "location은 `TEXCOORD<n>`의 번호" 규칙이 그대로 이어진다.
- 컴파일: `nv_add_shader`가 D3D12 빌드에서는 `slangc -target dxil -profile sm_6_6`으로 엔트리 포인트마다 DXIL을 쓴다(DXC를
  `slangc`에 알려 준다). WGSL과 Tint 검사는 웹 빌드에서만 돈다. 두 빌드 모두 지금의 HLSL 검사는 남는다.

### 플랫폼 계층

#### 파일 나누기

플랫폼에 따라 다른 모듈은 헤더 하나와 플랫폼마다 소스 하나다: `window.h` + `window_web.c` + `window_win32.c`. CMake가 빌드에 맞는
것을 고른다. 이것은 코딩 표준의 "모듈은 헤더 하나와 소스 하나"의 예외로 그 절에 적는다.

| 모듈 | 웹 | Windows |
|---|---|---|
| `gpu` | `gpu_webgpu.c` | `gpu_d3d12.c` |
| `window` | `window_web.c` | `window_win32.c` |
| `imgui` | `imgui.c`(공통: 프레임, 그리기, 미리보기, 빌드 라벨) + `imgui_web.c`(입력, 텍스트 에이전트, 클립보드) | `imgui.c` + `imgui_win32.c` |
| `storage` | `storage_web.c` | `storage_win32.c` |
| `local_file` | `local_file_web.c` | `local_file_win32.c` |
| `gltf`의 이미지 디코딩 | `image_web.c` | `image_win32.c` (WIC) |
| `log`, `strings`, `file` | 작은 차이라 한 파일 안의 `#if defined(__EMSCRIPTEN__)` | |

`renderer.c`, `vfx.c`, `scene.c`, `anim.cpp`, `gltf.c`의 나머지, 샌드박스와 오토배틀러의 코드는 플랫폼을 모른다. 플랫폼을 모르는
파일에 `__EMSCRIPTEN__`이 남지 않는 것이 단계 2의 끝 조건이다(디버그 내보내기는 아래 "디버그 내보내기").

#### Windows에서

| 무엇 | 어떻게 |
|---|---|
| 창 | `CreateWindowExW`, 처음 1600x900 DIP. 앱 매니페스트에 Per-Monitor V2 DPI와 UTF-8 코드 페이지(아래 "경로의 글자"), 지원 OS는 Windows 11. `pixel_ratio`는 `GetDpiForWindow / 96`: 웹의 "CSS 픽셀"이 DIP가 되므로 레이아웃 코드(`nv_window_framebuffer_rect_from_css`)는 그대로다 |
| 루프 | `nv_window_run`이 `PeekMessageW`와 프레임 호출을 돈다. 제시의 vsync가 속도를 정한다. 창을 닫으면 숨김 콜백을 부르고 GPU를 기다린 뒤 프로세스를 끝낸다. "돌아오지 않는다"는 약속은 그대로다 |
| 숨김 콜백 | 최소화, 앱 비활성화(`WM_ACTIVATEAPP`), 닫기, 세션 종료(`WM_ENDSESSION`). 최소화 중에는 프레임을 건너뛴다(크기 0) |
| 시간 | `QueryPerformanceCounter` |
| 키보드 | `WM_KEYDOWN`/`WM_KEYUP`/`WM_SYSKEY*`를 ImGui 키로. 웹의 `on_key`처럼 글자를 치는 키는 글자로, 단축키 표(`shortcuts.c`)는 그대로. `NvImgui.claims_key`는 브라우저가 없으니 의미가 없다 |
| 텍스트와 IME | `WM_CHAR`의 UTF-16(서로게이트 쌍 포함)을 `ImGuiIO_AddInputCharacterUTF16`로. 한국어 조합은 OS IME 창이 보이고, ImGui의 IME 위치 콜백이 `ImmSetCompositionWindow`로 그 창을 커서 옆에 둔다. 조합이 끝난 글자만 `WM_CHAR`로 온다 |
| 마우스 | `WM_*BUTTON*`, `WM_MOUSEMOVE`, `WM_MOUSEWHEEL`, `SetCapture`. 뷰포트에서 시작한 입력이 ImGui를 건너뛰는 규칙(`NvImgui.view_rect`, `view_grab`)은 공통 코드에 있다 |
| 커서, 클립보드 | `SetCursor`, `OpenClipboard`/`CF_UNICODETEXT` |
| 파일(에셋) | 웹의 `/assets/...`, `/data/...` 경로를 실행 파일 옆의 `assets\`, `data\`로 읽는다. `nv_file_read`가 맨 앞의 `/`를 실행 파일 폴더로 바꾼다. 설치 규칙이 그 폴더들을 복사한다 |
| 저장 | `%LOCALAPPDATA%\nv\<target>\`. 임시 파일에 쓰고 `MoveFileExW(MOVEFILE_REPLACE_EXISTING \| MOVEFILE_WRITE_THROUGH)`. 쓰기가 바로 디스크로 가므로 `nv_storage_flush`는 할 일이 없고, `nv_storage_flush_then_reload`는 자기 자신을 다시 실행하고 끝낸다 |
| 경로의 글자 | 앱 매니페스트의 `activeCodePage` UTF-8(Windows 10 1903부터 되므로 Windows 11에서는 언제나 된다). 그래서 `fopen`이 UTF-8 경로를 받고, 사용자 이름이 한글인 `%LOCALAPPDATA%`도 열린다. Win32 호출은 `W` 함수와 UTF-8 변환으로 |
| 로컬 파일 | `GetOpenFileNameW`/`GetSaveFileNameW`. 대화 상자는 모달이지만 결과는 다음 `nv_local_file_poll`로 알린다(API 그대로). 붙잡아 둔 파일은 경로이고, 바깥 수정 보호는 `GetFileAttributesExW`의 마지막 쓰기 시각으로 |
| 이미지 | WIC로 JPEG와 PNG를 RGBA8로 디코딩(색 변환 없음, 웹의 `colorSpaceConversion: "none"`처럼). 동기라서 웹의 Asyncify 대기가 그냥 호출이 된다 |
| 로그 | `OutputDebugStringW`와 표준 오류. 로그 링과 Console 탭은 그대로 |
| 언어 | `GetUserDefaultUILanguage`가 한국어면 한국어 |
| 시스템 정보 | CPU 이름(레지스트리 `ProcessorNameString`), 코어 수, 메모리(`GlobalMemoryStatusEx`), GPU(`DXGI_ADAPTER_DESC1`: 이름, 전용 메모리), 모니터, Windows 버전. 웹이 숨겨서 못 적던 것을 적는다. 벤치마크 표의 user agent 자리에는 OS와 GPU |
| 빌드 라벨 | 빌드 종류 옆에 백엔드 이름(`D3D12`). 내려받은 크기 대신 빈 칸 |

### 빌드

- **컴파일러는 MSVC(`cl`)이고, 빌드 도구는 Visual Studio 제너레이터다** (합의함, 2026-10-12; 제너레이터는 단계 6 뒤에 Ninja에서 바꿨다). 구성은 아무
  프롬프트에서나: `cmake -S . -B build-win -G "Visual Studio 18 2026" -A x64`, 빌드는 `cmake --build build-win --config Release`(또는 `Debug`).
  제너레이터가 MSVC와 Windows SDK를 스스로 찾으므로 "x64 Native Tools" 프롬프트가 필요 없다. 단계 3–6에서는 Ninja(`-G Ninja
  -DCMAKE_BUILD_TYPE=Release`, Native Tools 프롬프트)로 만들고 검증했다. 단계 3에서 쓴 것: Visual Studio 2026 Community
  18.9.2, MSVC 14.51.36231(`cl` x64), Windows SDK 10.0.26100.0, CMake 4.4.3. Emscripten이 아니면 MSVC만 받고,
  그 밖의 네이티브 호스트와 컴파일러는 지금처럼 `FATAL_ERROR`다. 디버그 레이어를 쓰려면 Windows의 선택 기능 "그래픽 도구"(Graphics
  Tools)가 필요하다("찾은 것").
- **Visual Studio 제너레이터는 Windows 빌드에만 쓴다 (바꿨고 검증했다, 2026-10-12).** 웹 빌드는 Emscripten과 Ninja(`CMAKE_BUILD_TYPE`)를 그대로 쓴다. 제너레이터는 여러 구성을 한 빌드 폴더에 두므로
  (`CMAKE_BUILD_TYPE`이 비어 있다) 바꾼 곳: `NV_BUILD_NAME`은 Windows에서 `$<CONFIG>`, 실행 파일 옆의 `D3D12\`는 `cmake/copy_d3d12_runtime.cmake`가 채운다(디버그 레이어 DLL은 Debug만; 구성에 따라
  명령을 빼는 일은 한 프로젝트에 모든 구성이 든 빌드에서 생성기 식으로는 안 된다), `local_file_test`는 모든 구성에서 만들고 `CONFIGURATIONS Debug`로 Debug에서만 돌린다(`ctest -C Debug`).
  실행 파일은 `build-win\sandbox\Release\sandbox.exe`처럼 구성 폴더 안에 생기고, `capture_native.ps1`은 `-Config`(기본 Debug)로 그 폴더를 고른다. 한 `build-win`에 Debug와 Release가 함께 있다.
  Ninja로 만든 단계 3–6의 폴더는 `build-win-ninja`와 `build-win-ninja-release`로 옮겨 두었고, `-DNV_TOOLS_DIR`로 받아 둔 도구를 다시 쓰면 새 폴더에서 다시 받지 않는다.
  검증: Debug와 Release 모두 경고 없이 만들어지고(타사 ozz의 `D9025`, `/W4`가 그쪽 `/w`와 겹치는 알림은 제외), ctest가 Debug 13개, Release 12개를 통과하고, `capture_native.ps1`의 22번 실행이
  디버그 레이어 메시지 0개로 끝나고, `compare --blocks`가 50건 모두 같다고 하고, B1이 같은 라운드를 돈다.
- 옵션 (우리 코드): `/std:c17`(C), `/std:c++17`(`anim.cpp`), `/W4`, `/utf-8`, `/Zc:preprocessor`. 외부 라이브러리는 `/w`.
  `CMakeLists.txt`의 `add_compile_options(-Wall -Wextra)`와 외부 라이브러리의 `-w`는 컴파일러에 따라 고른다.
  - `/utf-8`이 없으면 MSVC는 BOM 없는 소스를 시스템 코드 페이지(한국어 Windows에서는 CP949)로 읽어 한글 문자열을 깨뜨린다. 한글이 든
    소스: `sandbox/strings.c`, `autobattler/strings.c`, `engine/imgui.c`, `sandbox/ui.c`, `autobattler/battle.h`, `autobattler/battle_view.c`.
  - 경고는 `/W4`에서 0개다. 표준 C17인데 MSVC만 경고하는 것(예: 이름 없는 구조체의 C4201)은 CMake에서 그 번호만 끄고 그 줄에 까닭을
    적는다. 그 밖의 경고는 코드를 고친다.
- **clang/GCC 전용 문법 다섯 곳**을 `engine/base.h`의 매크로로 감싼다:

  ```c
  #if defined(_MSC_VER)
  #    define NV_TRAP() __debugbreak()                 // 디버거가 그 자리에서 멈춘다
  #    define NV_PRINTF_FORMAT(format_index, first_arg)
  #else
  #    define NV_TRAP() __builtin_trap()
  #    define NV_PRINTF_FORMAT(format_index, first_arg) __attribute__((format(printf, format_index, first_arg)))
  #endif
  ```

  `NV_ASSERT`는 `NV_TRAP()`을 쓰고, `log.h`, `text.h`, `sandbox/scene_file.c`, `autobattler/main.c`의 `__attribute__((format(...)))`는
  `NV_PRINTF_FORMAT(...)`가 된다. MSVC 빌드는 printf 형식을 검사하지 않지만, 같은 코드가 웹 빌드(clang)에서 검사된다.
- VLA, GNU 확장(빈 초기화자 `{}`, `case` 범위, 이진 리터럴, 문 표현식)은 지금 코드에 없다(확인함, 2026-10-12). 앞으로도 쓰지 않는다:
  코딩 표준의 "언어와 파일"에 "우리 코드는 emcc(clang)와 MSVC 둘 다에서 컴파일된다"고 적는다.
- `nv_setup_executable`이 네이티브에서는 실행 파일과 매니페스트, `assets/`(또는 `PRELOAD`의 폴더)를 설치한다. `.data`, `.js`, 페이지는
  웹에서만 만든다.
- `cmake/shaders.cmake`가 Windows 호스트에서 DXC 묶음과 Agility SDK를 받는다(버전과 SHA-256 고정, `NV_DXC`, `NV_AGILITY_SDK`로
  대신할 수 있다. 위 "Agility SDK").
- ctest: 네이티브 빌드에서는 C 테스트가 그냥 실행 파일로 돈다(Node 없이). `.mjs` 테스트(`strings_test`, `shader_layout_test`)는 Node를
  찾으면 돌고, 없으면 경고하고 건너뛴다. 웹 CI가 어차피 돌린다.
- CI는 지금처럼 웹만 빌드한다. 네이티브 빌드는 CI에서 빌드하지도 내놓지도 않는다(합의함, 2026-10-12). 그래서 웹만 고친 변경이
  네이티브 빌드를 깨도 CI는 모른다: 플랫폼을 모르는 파일과 `gpu.h`를 바꾼 커밋은 네이티브로도 빌드해 본 뒤 올린다.

### 디버그 내보내기와 네이티브 테스트

- `EMSCRIPTEN_KEEPALIVE` 함수는 C 함수이므로 네이티브에서도 컴파일된다. 매크로 `NV_DEBUG_EXPORT`(웹은 `EMSCRIPTEN_KEEPALIVE`,
  네이티브는 비어 있음)로 바꾼다.
- 네이티브 Debug 빌드는 `--script <file> [--out <dir>]`을 받는다(`engine/debug_script.h`: 샌드박스와 오토배틀러가 같은 실행기를 쓴다). 한 줄에 명령
  하나: `size <w> <h>`(클라이언트 영역을 그 픽셀로, 픽셀 비율 1로), `call <함수> <숫자>...`, `frames <n>`, `screenshot <png> [viewport | x y w h]`,
  `move`, `mouse down|up`, `wheel`, `key`, `modifiers`, `text`(WM_CHAR로 넣는 글자), `close`(닫기 단추: 저장한다), `quit`. 앱이 더하는 명령(`NvDebugScriptDesc.command`):
  샌드박스는 `wait-fixed <n>`(고정 시계가 n프레임 돌 때까지), `case <id> <dt> <counters>`, `case-frame <frame> <full|counters>`, `case-end`(효과의 덤프를
  웹 하네스와 같은 JSON으로), `benchmark <file>`(Stress 씬의 Run을 돌려 Copy 단추와 같은 표를 쓴다). `call`은 이름에서 함수로 가는 표로 찾는다
  (샌드박스는 `sandbox/main.c`의 `sandbox_debug_calls`, 오토배틀러는 `autobattler/main.c`의 `battle_debug_calls`: `NV_DEBUG_EXPORT` 함수와 그 인자 모양). 같은
  경우 목록이 Playwright(웹)와 스크립트(네이티브) 둘을 몬다. 끝낼 때 로그의 오류 수를 종료 코드로 돌려준다. 입력 명령은 창의 메시지
  처리기로 보내는 메시지이고 진짜 포인터가 아니므로, 이 실행에서는 포인터가 창을 떠났다는 알림(WM_MOUSELEAVE)을 무시한다. 환경 변수(Debug만):
  `NV_STORAGE_DIR`(저장 폴더), `NV_LOCAL_FILE`(대화 상자 대신 쓸 경로), `NV_D3D12_VALIDATION=0`(디버그 레이어와 GPU 기반 검증을 끈다: 시간을 재는 실행용).
- 파일 대화 상자는 사람이 있어야 하므로, Debug 빌드는 환경 변수 `NV_LOCAL_FILE`이 있으면 대화 상자를 건너뛰고 그 경로를 대답으로 쓴다
  (`tests/local_file_test.c`가 그렇게 `nv_local_file_*`를 시험한다).
- 스크린숏은 스왑체인 버퍼를 리드백해 WIC로 PNG를 쓴다.

### 검증

Slang 포팅의 하네스(S1–S10 화면, V1–V12 효과, Git 기록의 커밋 `217d1c2`–`8ef41ff` 무렵)를 `tools/d3d12_port/`로 되살려 쓴다. 이 포팅이
끝나면 지난번처럼 지운다.

- **필수 관문:** 각 씬을 보이고 Play를 하는 동안 오류 0개. 오토배틀러도 한 라운드.
  - 웹(WebGPU 오류, `_sandbox_debug_wgpu_error`의 경로)은 **단계 0–2에서만** 본다.
  - 네이티브 Debug(D3D12 디버그 레이어와 GPU 기반 검증의 메시지)는 단계 3–6에서 본다.
- **단계 3–6에서는 웹을 검사하지 않는다** (합의함, 2026-10-12). 그 단계들이 공통 코드(`gpu.h`, `renderer.c`, 셰이더)를 바꿔도 웹
  하네스를 돌리지 않는다. 웹 검사는 단계 6이 끝난 뒤 **사용자가 명령할 때** 한 번 한다(아래 "단계"의 7). CI는 지금처럼 푸시마다 웹을
  빌드하고 ctest를 돌린다(바꾸지 않는다).
- **단계 1, 2 (웹만 바뀜):** 바뀌는 것은 코드의 모양뿐이므로 웹 빌드가 기준선과 **픽셀까지 같아야** 한다(Slang 포팅 때처럼 가장 큰
  채널 차이 0/255). 효과의 덤프도 정확히 같다.
- **단계 4, 5 (D3D12): SwiftShader 기준선과 비교한다** (합의함, 2026-10-12). 기준선은 하나뿐이다: 단계 0에서 클라우드의 SwiftShader로
  만든 것. 개발 PC에서 웹 빌드를 돌려 기준선을 만들지 않는다. 우리 D3D12 빌드는 개발 PC의 GPU(어댑터 고르기가 고른 것, 지금은 RTX
  5070 Laptop)에서 같은 경우를 찍고, 그 그림을 SwiftShader 기준선과 비교한다. WARP는 쓰지 않는다(합의함, 2026-10-12).
  - 래스터라이저가 다르므로(소프트웨어와 실제 GPU) 픽셀까지의 비교가 아니다. 가장자리, 텍스처 필터링의 정밀도, 초월 함수가 조금씩
    다르다. 그래서 **블록 평균으로 비교한다:** 그림을 8x8 블록으로 나눠 채널 평균을 내고, 블록마다 가장 큰 채널 차이를 본다. 출발
    허용 오차는 4/255이고, 넘는 블록이 하나라도 있으면 실패다(단계 4에서 이유를 찾아 넓혔다: "찾은 것"의 "얇은 선"). 하네스는 차이 그림(블록마다 차이를 밝기로)을 함께 남겨 눈으로도 본다.
  - 허용 오차는 **원인을 찾아 여기 "찾은 것"에 적은 뒤에만** 그 경우에 한해 넓힌다. 이 비교는 같은 GPU 비교보다 느슨하므로, 작은
    차이가 버그인지 래스터라이저 차이인지는 경우를 줄여(한 메시, 한 패스) 따로 본다.
  - 단계 1, 2도 같은 SwiftShader 기준선을 쓴다: 그것은 웹 빌드끼리의 비교이고, 같은 소프트웨어 렌더러라 픽셀까지 같은 비교가 된다.
- **효과:** 개수(alive, visible, dropped, segments, decals)는 정확히 같아야 한다(V9의 dropped 53,248 포함). 파티클 값은 상대 오차
  1e-4에서 출발한다. 초월 함수(컬 노이즈의 `sin`, `cos`)를 두 컴파일 경로가 다르게 풀어 쌓이는 경우(V4의 연기가 후보)는 분포
  비교(평균, 최소, 최대)로 낮추고 어느 경우인지 적는다.
- **하네스는 두 곳에서 돈다.** 손으로(또는 에이전트가) 돌리고, CI에서는 돌리지 않는다.
  - **클라우드(웹 빌드):** 헤드리스 Chromium과 SwiftShader(AGENTS.md "Testing without a display"). 기준선을 만들고(단계 0), 단계 1, 2의
    픽셀 비교를 한다. 그 뒤로는 단계 7(사용자가 명령할 때)에만 돈다.
  - **개발 PC(D3D12 빌드):** `tools/d3d12_port/capture_native.ps1`(PowerShell: 경우마다 스크립트를 만들어 네이티브 Debug 빌드의 `--script`로
    돌린다; 효과의 경우는 웹 하네스와 같은 `cases.json`)이 스크린숏과 효과 덤프를 만들고, `compare --blocks`가 커밋된 SwiftShader 기준선과 블록
    평균으로 비교한다(단계 4, 5). 시험하는 D3D12 장치는 어댑터 고르기가 고른 것 하나(지금은 RTX 5070 Laptop)다.
  - 기준선과 비교 도구는 `tools/d3d12_port/`에 커밋되어 두 곳이 같은 것을 쓴다. 비교 도구(`compare.c`)는 C 라이브러리만 쓰는 C17 한 파일이라(PNG 읽기와 쓰기, inflate, JSON 읽기도 자기 것) Windows에서는 MSVC로, 클라우드에서는 아무 C 컴파일러로 컴파일해 쓴다. Node도 브라우저도 필요 없고, 같은 도구가 웹의 기준선 비교(`--exact`, 기본)와 네이티브의 비교(`--blocks`)를 둘 다 한다.
- **저장 호환:** 웹에서 만든 `state.nvs`를 네이티브 저장 폴더에 넣으면 같은 상태로 열리고, 그 반대도 된다(`_sandbox_debug_save_round_trip`
  에 해당하는 검사를 스크립트로).
- Release와 Debug 두 빌드 모두.

### 단계

**모든 작업은 `d3d12-support` 브랜치에서 한다** (합의함, 2026-10-12). `master`에서 그 브랜치를 만들고, 단계마다의 커밋은 그 브랜치에
올린다. **`master`에는 사용자가 합치라고 할 때만 합친다** (합의함, 2026-10-12): 마지막 단계까지 끝난 것을 사용자가 확인하고
PR로 올린다. 그 전에는 에이전트가 `master`에 합치거나 PR을 만들지 않는다. 지금의 CI 규칙대로 그 브랜치를 푸시하면 웹 Debug 빌드가 Pages의
`d3d12-support/`(오토배틀러는 `autobattler/d3d12-support/`)에 올라가므로, 단계 1, 2의 웹 빌드를 실제 브라우저에서도 볼 수 있다.

각 단계는 한 커밋 이상이고, 각 커밋은 그 단계의 검증을 통과한 뒤 올린다. 단계 0–2는 웹만 바뀌고 웹에서 검증된다. 단계 3–6 동안 브랜치의 웹 빌드는 검사하지 않으므로 깨져 있을 수 있고, 단계 7에서
고친다.

0. **기준선:** 하네스와 그것이 쓰는 디버그 내보내기를 Git 기록에서 되살리고, 지금의 웹 빌드로 S1–S10, V1–V12의 기준선을 SwiftShader로
   만든다(모든 단계가 이것 하나를 쓴다). 자기와의 비교를 확인한 뒤 커밋한다.
1. **플랫폼 나누기 (웹만):** 위 표대로 `*_web.c`로 나누고, `NV_DEBUG_EXPORT`, `NvGpuFormat`. CMake가 플랫폼별 소스를 고른다. 기준선과
   픽셀까지 같다.
2. **GPU API (웹만):** `engine/gpu.h`와 `gpu_webgpu.c`. `renderer.c`, `vfx.c`, `imgui.c`, `sandbox/textures.c`, 두 앱의 `main.c`를 옮긴다.
   끝 조건: `gpu_webgpu.c` 밖에 `wgpu`와 `WGPU`가 없다. 기준선과 픽셀까지 같고 효과 덤프도 같다. **가장 큰 단계이고, 여기까지는 D3D12
   없이 웹에서 모두 검증된다.**
3. **Windows 껍데기:** MSVC 빌드(`NV_TRAP`, `NV_PRINTF_FORMAT`), 매니페스트, Win32 플랫폼 계층 전부, DXC와 DXIL 헤더, Agility SDK(받기, 내보내는 기호, `D3D12\` 설치), `register` 주석. GPU는 장치, 스왑체인과
   ImGui(`imgui.slang`)만. 확인: 데스크톱 UI가 마우스, 키보드, 한국어 IME, 클립보드로 동작하고, 저장과 다시 열기, 로컬 파일 열기와 저장이
   되고, 디버그 레이어 오류가 0개다.
4. **D3D12 렌더러:** 메시, 그림자, 디버그 선, 업스케일, bloom, MSAA, 타임스탬프, 텍스처 미리보기. S1–S10.
5. **D3D12 컴퓨트와 효과:** 간접 그리기와 디스패치, UAV 배리어, 리드백. V1–V12. 그 뒤 stress 씬의 벤치마크를 웹과 나란히 돌려 표를
   남긴다(성능 목표는 두지 않는다: 기록만).
6. **정리:** 오토배틀러 네이티브(한 라운드, `battle_test`는 이미 GPU가 없다). AGENTS.md(네이티브 빌드와 실행 방법, "There is no native build" 고치기, 구조도), `CODING_STANDARD.md`(의존성,
   파일 규칙의 예외), 이 스펙의 "만든 결과", GPU를 가리키는 다른 스펙(`msaa.md`, `shadows.md`, `textures.md`, `vfx.md`,
   `resolution.md`)을 고친다.
7. **웹 검사 (사용자가 명령할 때만):** 단계 6이 끝나도 에이전트가 먼저 시작하지 않는다. 명령을 받으면 클라우드에서 웹 하네스를 돌린다:
   SwiftShader 기준선과 픽셀까지 같은지, 효과 덤프가 같은지, WebGPU 오류 0개, Release와 Debug 둘 다. 다른 것은 고치고 원인을 "찾은
   것"에 적는다. 통과하면 `tools/d3d12_port/`와 하네스 전용 디버그 내보내기를 지운다(웹 검사가 그것을 쓰므로 이 단계의 끝에서).

### 위험

| 위험 | 대응 |
|---|---|
| D3D12의 C 인터페이스에서 구조체를 돌려주는 메서드(`GetCPUDescriptorHandleForHeapStart`, `GetDesc`)의 호출 규약 | 최근 Windows SDK의 C 선언은 결과를 포인터로 받는 모양이다. 단계 3에서 고정할 SDK 버전으로 확인한다 |
| 배리어 추적의 실수가 어떤 GPU에서는 보이지 않고 다른 GPU에서만 깨진다 | Debug 빌드는 GPU 기반 검증을 켠다. 개발 PC의 GPU 하나만 시험하므로, 다른 GPU에서 깨진 보고가 오면 GPU 기반 검증으로 먼저 본다 |
| 웹에서 "해제해도 되는" 순간에 의존한 코드(크기 바뀔 때 타깃 다시 만들기, 미리보기 슬롯) | 백엔드의 지연 해제가 덮는다. 스왑체인 크기 바꾸기만 GPU를 기다린다(드물다) |
| SwiftShader와 실제 GPU의 차이가 진짜 버그를 가린다 | 블록 평균의 허용 오차는 원인을 적은 경우에만 넓히고, 차이 그림을 눈으로도 본다. 효과의 개수는 언제나 정확히 비교한다. 의심스러우면 경우를 줄여 따로 본다 |
| 내장 GPU로 넘어가는 경로를 개발 PC에서 시험할 수 없다(외장 GPU가 있으므로 언제나 그것을 고른다) | 그 경로는 어댑터 목록을 도는 것뿐이고, 로그가 무엇을 왜 건너뛰었는지 남긴다 |
| 단계 3–6의 웹 회귀가 단계 7에서야 한꺼번에 보인다 | 단계 7에서 다른 경우가 나오면 브랜치의 커밋들로 이분 탐색(`git bisect`)해 바뀐 커밋을 찾는다. 그래서 공통 코드를 바꾸는 커밋은 작게 나눈다 |
| 2단계의 리팩터링이 크다(GPU를 부르는 약 2,500줄) | 웹에서 픽셀까지 같다는 기준이 있어서 파일 하나씩 옮기고 그때마다 비교한다 |

### 열린 질문

없음.

### 찾은 것

(구현하면서 찾은 버그, 웹과 D3D12의 차이, 넓힌 허용 오차와 그 원인을 적는다.)

- **기준선이 두 번 찍어 모두 같지는 않다 (단계 0).** SwiftShader 위의 같은 빌드를 두 번 캡처해 비교한 결과, S1–S9와 대부분의 효과 덤프는
  정확히 같다. 같지 않은 것은 셋이다: (1) UI 화면 S10은 글자의 픽셀 약 10개(채널 차이 5/255 이하, 0.001%), (2) 효과의 화면 V1–V4, V11은
  겹치는 반투명 파티클을 정렬하지 않으므로 겹치는 순서가 달라 픽셀이 다르다(16x16 블록 평균은 옛 허용 오차 안), (3) Play 안에서 도는
  V12-250k는 실시간에 따라 `simulateArgs`가 3831 또는 3985로 갈린다(옛 Slang 포팅의 기준선과 비교해도 같은 두 값이다). 그래서
  `compare --exact`는 S1–S9를 0/255로, 효과 덤프를 정확히 비교하고, 위 셋은 각각 S10 0.01%, 블록 평균, 제외로 둔다.
- **파티클 패스의 시간이 지금까지 한 번도 기록되지 않았다 (단계 2).** 옛 `renderer.c`는 파티클 컴퓨트 패스의 타임스탬프를
  `timestamp_copied`로 정했는데, 그 값은 그리기 중간에야 정해지고 프레임 끝에 0으로 돌아가므로 그 자리에서는 언제나 0이었다. 그래서
  `gpu_particles_ms`는 언제나 0이었다. GPU API로 옮기며 모든 패스가 시간을 쓰고 읽기만 하나씩 가져가게 되어, 타임스탬프가 있는 GPU에서는
  이제 파티클 시간이 나온다(SwiftShader에는 타임스탬프가 없어 비교에 보이지 않는다).
- **단계 1, 2의 검증 (2026-10-12).** 단계 2의 웹 Debug 빌드(단계 1을 포함한다)를 SwiftShader에서 전체 하네스로 돌려 단계 0의 기준선과
  `compare --exact`로 비교했다: 씬 화면 S1–S9는 모든 채널이 0/255로 같고, S10, 효과의 화면과 덤프도 위의 기준 안에서 같고, WebGPU 오류는
  0개다. 웹 Release와 Debug 빌드와 ctest 15개는 CI에서도 통과했다(커밋 `72907db`). 비교한 50개 중 하나, V12-1M의 25번째 프레임이 한 번
  `simulateArgs`가 14997이 아니라 14683이었는데, 같은 케이스를 두 빌드에서 세 번씩 다시 돌리면 모두 14997이었다(기준선 파일도). V12 케이스는 Play
  안에서 실시간에 따라 갈리는 것으로 보고(V12-250k가 기준선에서 갈리는 것과 같다), `--exact`가 V12를 하나 놓치면 같은 케이스를 다시 돌려 본다.
- CMake 3.30 이상이 필요하다(ozz-animation). 클라우드 이미지의 apt는 3.28이라 `pip install cmake`를 가상 환경에 설치했다.
- **셰이더의 구조화 버퍼 원소 크기가 WGSL과 HLSL에서 다르다 (단계 3).** `renderer_frame.slang`의 `Object`(행렬 하나와 `uint` 하나)는 WGSL에서
  구조체 크기가 정렬(16)로 올려져 80바이트이고, HLSL에서는 68바이트다. C의 `NvObjectData`는 80바이트이고 SRV의 `StructureByteStride`도 80으로
  만들었는데, 셰이더는 DXIL에 구워진 68로 색인해서 첫 오브젝트 말고는 쓰레기 행렬을 읽었다(상자가 사라지거나 거대하게 그려졌다).
  GPU 기반 검증이 "Structured buffer byte stride mismatch: 80, 68"로 짚었다. `uint` 셋(`uint3`가 아니다: WGSL의 vec3는 80에서 시작해 구조체를
  96으로 만든다)을 명시적으로 더해 두 쪽을 80으로 맞췄다. 다른 구조화 버퍼(`Particle` 32, `Emitter` 128, `Burst` 64, `Segment` 80, `Decal` 48,
  `uint2`, `float4x4`)는 두 쪽이 같은 크기이고, 검증이 더는 말하지 않는다.
- **디버그 레이어는 Windows의 "그래픽 도구"가 있어야 뜬다 (단계 3).** Agility SDK의 `d3d12SDKLayers.dll`을 `D3D12Core.dll` 옆에 두어도(내보내는
  기호와 `ID3D12DeviceFactory` 둘 다) `D3D12GetDebugInterface`가 `DXGI_ERROR_SDK_COMPONENT_MISSING`(0x887A002D)을 돌려줬다. 선택 기능 "그래픽
  도구"를 설치하자 뜬다. 그 기능이 없는 PC에서는 로그에 한 줄 경고하고 검증 없이 돈다. `DXGI_CREATE_FACTORY_DEBUG`는 쓰지 않는다(DXGI 디버그
  레이어도 그 기능의 것이다).
- **디버그 레이어는 루트 서술자 테이블을 놓는 순간 자원의 상태를 본다 (단계 3).** 서술자 범위를 `DATA_STATIC_WHILE_SET_AT_EXECUTE`(기본)로
  두면 `SetGraphicsRootDescriptorTable`에서 이미 상태를 검사하므로, 배리어를 큐에 넣고 그리기 직전에야 내보내면 "상태가 맞지 않는다"
  오류가 난다. `prepare_command`는 배리어를 먼저 내보내고 그다음에 루트 인자를 놓는다.
- **WIC의 PNG 인코더는 RGBA가 아니라 BGRA를 저장한다 (단계 3).** `SetPixelFormat`에 RGBA를 주면 BGRA를 돌려주고(인자가 입출력이다) 그
  모양으로 써야 한다. 스크린숏의 빨강과 파랑이 바뀌어 있었다.
- **C 라이브러리의 `sinf`, `cosf`, `tanf`, `atan2f`가 플랫폼마다 마지막 비트가 다르다 (단계 3).** `battle_test`의 황금 라운드는 틱 수, 결과, 남은
  적 수가 MSVC에서도 같지만 해시(유닛의 float 비트)가 다르다. 그 해시 비교는 웹에서만 하고, 네이티브는 틱 수, 결과, 남은 적 수만 비교한다.
  같은 규칙이 두 빌드에서 같은 라운드를 두는 것은 맞고, 비트까지 같은 것은 아니다.
- **ozz-animation은 기본이 정적 C 런타임(`/MT`)이다 (단계 3).** `ozz_build_msvc_rt_dll`을 켜서 우리 코드와 같은 동적 런타임(`/MD`)으로 맞췄다.
- **MSVC의 C++은 복합 리터럴 `(T){...}`를 받지 않는다 (단계 3).** `anim.cpp`가 포함하는 헤더(`math.h`, `window.h`, `mesh.h`)의 것을
  `NV_LITERAL(T){...}`로 바꿨고, 지정 초기화(`.field =`)는 C++17에 없어 `nv_mesh_builder_data`를 일반 대입으로 고쳤다.
- **GetModuleFileName 같은 호출에는 `UNICODE`가 필요하다 (단계 3).** `IDC_ARROW` 같은 자원 이름이 `UNICODE` 없이는 `char*`다. Windows 빌드는
  `UNICODE`와 `_UNICODE`를 정의한다. Windows 헤더의 매크로(`SC_MOVE` 같은)가 샌드박스의 열거형 이름과 부딪히므로 `windows.h`를 포함하는
  파일은 플랫폼 코드뿐이고(`engine/win32.h`), 샌드박스의 스크립트 실행기는 명령줄과 입력을 `engine/window_win32.h`를 통해 얻는다.
- **단계 3의 검증 (2026-10-12).** Debug(디버그 레이어와 GPU 기반 검증 켬)와 Release 빌드가 `/W4`에서 경고 0개로 만들어지고 ctest 12개가
  통과한다(`strings_test`와 `shader_layout_test`는 Node가 없어 건너뜀: "열린 일"). 스크립트로 확인한 것: 스크립트 실행의 로그에 오류 0개, 경고
  0개; 쇼케이스 화면이 기준선의 S10과 눈으로 같다(색, 그림자, 선택 상자, 기즈모, 글자); Textures 탭이 깊이 미리보기까지 그린다; 마우스(탭
  누르기, 검색 상자 누르기, 왼쪽 드래그로 궤도 회전(yaw 0.35 → -0.61), 휠로 줌(거리 5 → 3.285, 되돌리면 5), 뷰포트 밖의 휠은 줌하지
  않음); 키(Ctrl+A, Ctrl+C, Backspace, Ctrl+V); 글자 입력("char"가 Scene 탭을 거르고 한글 "한글"이 보인다); 클립보드(시스템 클립보드의
  텍스트와 "char한글"이 오간다); 저장과 다시 열기(msaa, 톤, 노출, 블룸이 새 프로세스에서 같고 `save_round_trip`이 참); 로컬 파일 열기, 저장,
  제자리 저장, 바깥에서 바뀐 파일의 거부(`local_file_test`). **사람이 해야 하는 것: 진짜 한국어 입력기(IME)의 조합 창이 입력란 옆에 뜨는지**
  (`WM_CHAR`로 들어오는 글자까지는 위에서 시험했다), Alt+Tab과 화면 배율 바뀜.
- **얇은 선은 두 래스터라이저가 다르게 그린다 (단계 4).** 선택 상자와 기즈모의 디버그 선(1픽셀)이 SwiftShader에서는 샘플로 안티앨리어싱된
  1픽셀 폭이고, GPU의 D3D12에서는 안티앨리어싱 없는 1픽셀 선이다. `MultisampleEnable`을 켜면 안티앨리어싱되지만 폭이 1.4픽셀인 사각형이 되어 더
  멀어진다(블록이 89개에서 122개로 늘었다). 그래서 끄고 둔다(`gpu_d3d12.c`의 주석). 이 차이가 쇼케이스 화면(선택 상자가 있다)에서 블록 평균 4/255를
  넘는 블록 67–97개(전체의 1.5–2.2%, 가장 큰 것 26/255)를 만든다: 차이 그림에서 그 블록들은 상자와 기즈모를 그대로 따라 그린다. 그래서
  `--blocks`는 4/255를 넘는 블록을 전체의 3%까지, 가장 큰 블록 차이를 40/255까지 허용한다. 진짜 오류는 이보다 훨씬 많은 블록이나 큰 차이를
  만든다(상자가 사라진 경우를 보았다: 셰이더 구조체 크기 문제는 아래). 장면을 낮은 해상도로 그려 최근접으로 늘린 화면(S7, S8)은 선이 두세 배
  굵어지므로 그 두 배(6%, 70/255)를 허용한다.
- **체스 화면의 몇 블록 (단계 4).** 체스 화면마다 4–6블록이 4/255를 조금(가장 큰 것 7.05/255) 넘는다. 자리는 판 아래쪽 금색 테두리 근처
  (368, 344)이고, MSAA를 끈 S2에서도 한 블록(6.25, 픽셀은 190까지)이 남는다: 1픽셀보다 가는 테두리 지오메트리가 두 구현에서 다른 표본에 걸리는
  것으로 보인다(표본 위치가 구현마다 다르다). 위 3%의 안이고, 더 좁히지 않았다.
- **바인드 그룹은 자기가 묶은 자원을 붙잡아야 한다 (단계 4).** WebGPU는 앱이 텍스처를 놓아도 그것을 묶은 바인드 그룹이 붙잡고 있다. 같은 프레임에
  UI가 미리보기(그룹)를 만들고 렌더러가 깊이 타깃을 다시 만들며 놓으면(Textures 탭에서 MSAA를 바꿀 때), 그 프레임의 그리기가 놓인 텍스처를 쓴다.
  D3D12 백엔드는 자원마다 그룹 참조 수를 세어 마지막 그룹이 놓일 때까지 자원을 살려 둔다(`group_references`).
- **효과의 파티클 패스 시간이 이제 나온다 (단계 4).** 웹은 `gpu_particles_ms`가 언제나 0이었다("찾은 것", 단계 2). D3D12에서는 Stress 탭의 "GPU
  particle passes"가 0이 아니다.
- **단계 4의 검증 (2026-10-12).** `capture_native.ps1 -Cases S1..S10`이 만든 27장을 `compare --blocks`로 기준선과 비교해 27장 모두 통과한다(S1–S6
  각 3개 씬, S7, S8 셋, S9 셋, S10 둘). 실행의 로그에 디버그 레이어와 GPU 기반 검증의 메시지가 0개다. S10의 폰 UI(터치)는 네이티브에 없으므로
  비교하지 않는다. GPU 시간(장면, 그림자, 업스케일, 블룸, 파티클)이 Stress 탭에 나온다.
- **HLSL의 `f32tof16`은 0 쪽으로 자른다 (단계 5).** 파티클의 속도와 크기는 half float 둘을 한 워드에 담는다(`pack_half2`). WGSL의 `pack2x16float`
  (SwiftShader)는 가장 가까운 값으로, 같은 거리면 짝수로 반올림하고, HLSL의 `f32tof16`은 0 쪽으로 자른다. 파티클마다 속도가 half float의 한 단위까지
  어긋나 V2–V11의 값이 69군데 달랐다. `vfx_common.slang`의 `half_bits`가 D3D12에서만(`NV_D3D12`) 가장 가까운 값으로 반올림해 3군데로 줄었다(남은 것은
  `exp`와 `sin`, `cos`의 GPU 정밀도 차이로 보이는 한 단위 어긋남).
- **효과의 허용 오차 (단계 5, 잰 값).** 개수와 인자(`counters`, `drawArgs`, `capacity`, `segments`, `decals`)는 정확히 같다. 파티클은 (emitter,
  seed)로 정렬해 필드마다 절대 오차로 비교한다: 위치와 속도(`vx`, `vy`, `vz`)와 회전은 0.02, 나이는 1e-4, 수명과 크기는 0.005. 잰 가장 큰 오차는 위치 8 mm, 속도 0.0078이다
  (V2–V11, 파티클 수천 개). 그보다 큰 오차는 없고, 있다면 버그다.
- **보이는 개수는 GPU마다 조금 다르다 (단계 5).** `counters`의 보이는 파티클 수(3, 4, 9번)와 `drawArgs`의 그리는 정점 수는 파티클의 구가 절두체에 닿는지로
  센다. 위치가 GPU의 `sin`, `cos`, `exp`에서 나오므로 면 가까이의 파티클이 몇 개 반대편으로 넘어간다. V12(파티클 25만–200만 개)에서 잰 차이는 0.05–1.8%이고,
  생존(alive)과 free 수는 기준선과 정확히 같다. `compare --blocks`는 이 슬롯에 한해 3%(그리고 8개)까지 받고, `--exact`는 받지 않는다. 네이티브는 같은
  경우를 두 번 돌려 같은 값을 냈다(결정적): 기준선이 V12에서 실시간에 따라 갈리는 것("단계 1, 2의 검증")과 다르다.
- **`simulateArgs`는 한 프레임 어긋날 수 있다 (단계 5).** 시뮬레이트의 워크그룹 수는 그것을 쓴 프레임이 시작할 때의 생존 수로 `prepare_a`가 매 프레임
  다시 쓴다. 생존 수가 64의 배수를 넘는 프레임(V1의 23번, V4의 300번)에서는 덤프를 한 프레임 먼저 또는 나중에 읽는 것만으로 두 값(예: 2와 1) 중 하나가
  나오고, 끝나지 않은 Play(V12)에서는 수천씩 다른 값이 나온다. 개수는 모두 같다. `--blocks`는 앞의 두 단어가 같고 첫 단어가 1 또는 5% 안이면 받는다.
- **효과 화면은 반투명 순서가 다르다 (단계 5).** 알파 파티클은 일부러 정렬하지 않는다(`sandbox/effects.c`의 `// TODO:`). 그리는 순서는 GPU 스레드가 생존
  목록에 넣은 순서이므로, 같은 연기에서 어느 색이 위에 오는지가 GPU마다 다르다(V4의 180번 프레임: 기준선은 주황이 위, 우리는 흰색이 위). 큰 구름이 블록
  수백 개를 크게 바꾼다(최대 99.9/255, 블록의 12.9%). 그래서 `--blocks`는 효과 화면(V*)에 한해 넘는 블록의 몫을 5배(15%), 가장 큰 블록을 2.75배(110/255)까지
  받는다. 이 화면들이 효과의 올바름을 보이는 것은 덤프이고, 화면은 그림이 크게 틀어지지 않았는지만 본다.
- **스크립트의 순서 (단계 5).** 덤프나 `case-end`, `quit`가 앞선 명령의 끝을 기다리지 않아 파일이 비거나 일찍 끝났다. 스크립트는 화면 찍기, 고정 시계 기다리기
  (`wait-fixed`), 덤프를 기다린 뒤에만 다음 줄로 간다.
- **벤치마크 (단계 5, 기록만).** `benchmark <파일>` 스크립트 명령(`sandbox/debug_script.c`)이 stress 씬의 Run을 돌려 Copy 단추와 같은 표를 파일로 쓴다. 성능을
  재려면 디버그 레이어와 GPU 기반 검증을 꺼야 하므로(`NV_D3D12_VALIDATION=0`, Debug 빌드만) 그렇게 돌렸다. **Debug(`/Od`) 빌드**라 CPU 단계(anim, scene,
  draw, ui)는 Release보다 느리다. GPU 시간은 그대로 읽을 수 있다. 웹 쪽 표는 단계 7에서 사용자가 명령할 때 나란히 놓는다.
  개발 PC(AMD Ryzen AI 7 350, GPU NVIDIA GeForce RTX 5070 Laptop, Windows 11 26200), 캔버스 1280x720, 장면 680x472, MSAA 4x, 그림자 2048:

  ```
  step            frames  avg ms  worst ms  load %  anim  scene  draw    ui    gpu  shadow  upscale  bloom  particles  alive
  Cubes 250          988    3.04      9.43      23  0.00   0.06  0.24  0.39   0.03    0.01     0.01   0.02       0.01      0
  Cubes 1000         989    3.04      5.28      34  0.00   0.17  0.45  0.42   0.04    0.02     0.01   0.03       0.01      0
  Cubes 4000         986    3.04      8.57      76  0.01   0.58  1.34  0.39   0.07    0.03     0.01   0.03       0.02      0
  Cubes 8000         667    4.51      6.55      92  0.02   1.09  2.50  0.51   0.12    0.03     0.05   0.02       0.00      0
  Cubes 16000        355    8.47     13.20      94  0.04   2.34  4.84  0.74   0.23    0.03     0.09   0.03       0.01      0
  Crowd 8            988    3.04      9.56      31  0.24   0.01  0.22  0.46   0.06    0.01     0.01   0.05       0.02      0
  Crowd 32           988    3.04      8.40      55  0.78   0.02  0.47  0.42   0.13    0.06     0.01   0.03       0.03      0
  Crowd 60           984    3.05      7.71      86  1.42   0.03  0.72  0.44   0.21    0.14     0.01   0.02       0.02      0
  Crowd 120          534    5.63      7.59      92  2.93   0.07  1.53  0.64   0.40    0.30     0.08   0.02       0.01      0
  Crowd 200          341    8.81     10.46      94  5.03   0.12  2.47  0.67   0.64    0.52     0.08   0.02       0.01      0
  Particles 250k     979    3.07     10.70      24  0.02   0.12  0.13  0.46   0.31    0.01     0.01   0.02       0.04  210652
  Particles 1M       981    3.06     10.31      48  0.02   0.13  0.16  0.40   1.09    0.01     0.01   0.03       0.34  843442
  Particles 2M       863    3.48      4.64      88  0.03   0.17  0.18  0.63   2.32    0.01     0.01   0.03       0.67  1648010
  Missiles 500       791    3.80      5.64      94  0.02   0.16  0.19  0.66   3.43    0.00     0.01   0.03       0.09  289113
  Missiles 2000      305    9.85     15.87      99  0.03   0.18  0.33  0.99   9.35    0.01     0.01   0.03       0.39  726780
  Beams 500          981    3.06     10.56      26  0.02   0.12  0.16  0.48   0.32    0.01     0.01   0.03       0.01      0
  ```

  프레임 시간 3.04 ms(약 330 Hz)가 가벼운 단계에서 바닥으로 보인다: 프레임 속도가 거기서 더 오르지 않는 것은 프레젠트(디스플레이의 주사율일 것이다)에 묶였기 때문으로 보이며, 이유를 따로 확인하지는 않았다. 파티클 단계의 `gpu`는
  장면 패스(파티클 그리기 포함)이고 `particles`는 컴퓨트 패스의 시간이다.
- **단계 5의 검증 (2026-10-12).** `capture_native.ps1 -Cases V1..V12`가 만든 효과 12개(JSON 덤프와 화면 8장)를 `compare --blocks`로 기준선과 비교해 모두
  통과한다(위의 허용 안에서). 실행의 로그에 디버그 레이어와 GPU 기반 검증의 메시지가 0개다.
- **스크립트 실행기는 엔진에 있다 (단계 6).** 오토배틀러도 같은 `--script`로 돌려야 하므로 실행기(`size`, `call`, `frames`, `screenshot`, 입력 명령, `quit`)를
  `sandbox/debug_script.c`에서 `engine/debug_script.c/.h`로 옮겼다. 앱은 `call`이 찾을 표와, 자기만의 명령(`command`), 기다릴 일이 있는지(`busy`)를 준다. 샌드박스의
  `case`, `wait-fixed`, `benchmark`는 `sandbox/debug_script.c`에 남았다. 옮긴 뒤에 같은 효과 케이스(V1, V3)와 S10이 기준선과 그대로 통과한다.
- **오토배틀러의 한 라운드 (단계 6).** `capture_native.ps1 -Cases B1`이 `autobattler.exe`를 스크립트로 돌린다: 기본 프로젝트에 플레이어 부대 다섯을
  놓고(`call deploy`), 시작해, 라운드가 끝날 때까지 돌린다(`call run`). 이 배치는 패배로 끝난다(754틱, 플레이어 0, 적 45: 6유닛 부대 다섯이 열 부대에 진다).
  로그에 오류와 `[d3d12]` 메시지가 0개이고 그림 셋(`autobattler/battle-*.png`)에서 부대, 탄, 효과, 패널이 제대로 그려진다. 웹에는 이 그림이 없어 비교하지
  않는다. 규칙은 그대로 GPU가 없는 `battle.c`이고 `battle_test`가 이미 돈다.
- **단계 6의 검증 (2026-10-12).** Release와 Debug 빌드가 `/W4`에서 경고 0개로 만들어지고, ctest가 Release 12개, Debug 13개(`local_file_test` 포함)를 통과한다
  (Node가 없어 `strings_test`와 `shader_layout_test`는 건너뛴다: 웹 CI가 돌린다). 샌드박스와 오토배틀러가 디버그 레이어와 GPU 기반 검증의 메시지 0개로 돈다. 웹 빌드는
  단계 3–6에서 하기로 한 대로 검사하지 않았다(단계 7).

### 만든 결과

2026-10-12 단계 0–6의 끝에서 (웹은 단계 2에서 검증하고 그 뒤로는 검사하지 않았다):

- **GPU:** `engine/gpu.h`(핸들, 버퍼, 텍스처, 바인드 그룹, 파이프라인, 패스, 컴퓨트, 간접 호출, 타임스탬프, 리드백)와 두 백엔드 `gpu_webgpu.c`, `gpu_d3d12.c`.
  D3D12 백엔드는 자원 상태 추적, 업로드 링과 펜스, 뷰와 샘플러 힙, 루트 시그니처 캐시, 지연 해제를 한다.
- **플랫폼:** `window`, `imgui`의 입력, `storage`, `local_file`, `image`가 플랫폼마다 소스 하나(`*_web.c`, `*_win32.c`). 플랫폼을 모르는 파일에는 `__EMSCRIPTEN__`이 없다.
- **셰이더:** 같은 Slang 소스가 웹에서는 WGSL, Windows에서는 DXIL(SM 6.6)이 된다. 빌드가 DXC와 Agility SDK를 받는다.
- **빌드:** `cmake -S . -B build-win -G "Visual Studio 18 2026" -A x64`(MSVC)로 `sandbox.exe`, `autobattler.exe`. CI는 웹만 빌드한다.
- **시험:** 네이티브 C 테스트(ctest), `--script`로 도는 Debug 실행, `tools/d3d12_port/`의 SwiftShader 기준선과 `compare`(하나의 비교 구현), `capture_native.ps1`(S1–S10, V1–V12, B1).
- **문서:** `AGENTS.md`(Windows 빌드와 시험), `docs/CODING_STANDARD.md`(MSVC와 emcc 둘 다, 플랫폼별 소스의 예외, 의존성), 그리고 이 스펙과 GPU를 가리키던 `msaa.md`, `shadows.md`,
  `textures.md`, `resolution.md`, `vfx.md`의 참고.
- **남은 것:** 단계 7(웹을 클라우드에서 다시 검사: 사용자가 명령할 때). 사람이 해야 하는 검사: 실제 한국어 IME 조합, DPI가 다른 모니터 사이의 이동, Alt+Tab. 이 스펙이 정한 성능 목표는 없고,
  벤치마크 표는 위에 기록만 했다(웹 쪽 표는 단계 7).

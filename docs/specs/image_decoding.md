# 이미지 디코딩 스펙 (stb_image: 모든 플랫폼에서 재 보고, Linux에서만 쓴다)

상태: 끝남 (2026-11-07). 이 스펙의 변경은 먼저 합의한다. 단계 0–4를 했다. 단계 3에서 계획을 바꿨다: 아래 "정한 것".

### 정한 것 (단계 3, 2026-11-07)

측정("찾은 것")을 보고 사용자가 정했다: **stb_image는 Linux 네이티브 빌드에서만 쓰고, stb_image의 힙 할당은 모두 아레나로 바꾼다.**
웹은 브라우저 디코더(`image_web.c`), Windows는 WIC(`image_win32.c`)를 그대로 둔다.

- 까닭: 웹에서 stb_image는 실제 GPU로 약 1.5배 느렸고(chess가 245 ms 늦게 보인다), 디코딩 중의 임시 버퍼(2048x2048 progressive JPEG
  한 장에 52 MB)가 wasm 힙에 잡혀 wasm 메모리가 49 MB 늘고 줄지 않았다. SIMD는 효과가 없었다.
- 그래서 단계 2(모든 플랫폼에서 stb_image)의 웹과 Windows 쪽은 되돌렸다. 브랜치를 정리하며 단계 1–3의 커밋을 합쳤으므로, 단계 2의
  코드와 시간 로그는 Git 기록에 없다. 아래의 "목표", "바뀌는 것"은 단계 2의 계획
  그대로 기록으로 남긴다. D3D12 포팅의 결정("네이티브는 OS가 디코딩한다")은 바뀌지 않는다: Windows에서는 그대로이고, Linux에는 OS
  디코더가 없다.
- **아레나:** `CMakeLists.txt`가 만드는 stb_image의 구현에서 `STBI_MALLOC`, `STBI_REALLOC_SIZED`, `STBI_FREE`가 `image_linux.c`의
  `nv_image_stb_malloc`, `_realloc`, `_free`다. 디코딩 하나 동안 `nv_image_decode`가 받은 아레나에서 블록을 차례로 밀어 넣고(16바이트
  정렬, 0으로 채우지 않음), 하나씩 놓지 않는다. 가장 새 블록은 제자리에서 자란다(PNG의 데이터와 zlib 출력이 그렇게 자란다). 끝나면
  픽셀을 디코딩이 시작한 곳으로 옮기고 그 뒤를 모두 버린다: 아레나에는 픽셀만 남는다. 자리가 모자라면 NULL을 돌려주고 stb가 실패한다
  (Release에서는 `NV_ASSERT`가 꺼지므로 `nv_arena_push`를 쓰지 않는다). 그 로그는 어떤 크기의 블록이 들어가지 못했는지 말한다.
- **시간 로그**(`gltf.c`, 단계 1)는 측정이 끝나서 지웠다. `nv_gltf_load_model`이 `nv_image_decode` 하나하나를 `nv_time_seconds`로
  재어, 모델마다 장 수, 합, 최댓값, 가장 큰 크기를 info 로그 한 줄로 남긴 것이다(아래 "시간 로그"). 다시 잴 때는 그렇게 다시 넣는다.
- **아레나가 가져야 할 자리**(Linux 네이티브, `-O2`, 디코딩이 되는 가장 작은 아레나를 이분 탐색): baseline JPEG는 픽셀의 1.75배
  (2048x2048에서 28 MB), progressive JPEG는 3.25배(52 MB), character의 1024x1024는 1.39배. 샌드박스의 scratch 아레나(128 MB)에는
  chess.glb(43 MB)와 함께 들어간다. 한 장의 디코딩은 baseline 11–12 ms, progressive 28–34 ms다.

### 목표

`nv_image_decode`(`engine/image.h`)를 모든 플랫폼에서 같은 C 코드 하나로 한다: stb_image로 JPEG와 PNG를 RGBA8로 디코딩한다.
지금은 플랫폼마다 따로 한다.

| 플랫폼 | 파일 | 디코더 |
|---|---|---|
| 웹 | `engine/image_web.c` (39줄) | 브라우저의 `createImageBitmap`. `EM_ASYNC_JS`라서 Asyncify로 기다린다 |
| Windows | `engine/image_win32.c` (146줄 중 디코딩 약 60줄) | WIC(COM) |
| Linux | `engine/image_linux.c` | 없다. 오류만 남긴다 |

바꾸는 까닭:

- **Linux에는 디코더가 없다.** Linux 빌드에서는 텍스처가 모두 빠진다(씬은 단색으로 그려지고, 이미지마다 오류가 남는다). 어차피 C 디코더가 하나 든다.
- **모든 플랫폼에서 같은 픽셀.** 브라우저의 JPEG 디코더와 WIC는 IDCT와 크로마 업샘플링이 달라 채널마다 조금씩 다를 수 있다.
  디코더가 하나면 텍스처가 같다.
- **코드 경로가 하나.** 브라우저 JS와 WIC/COM 디코딩이 없어지고, 웹에서 이미지 때문에 Asyncify로 기다리는 일도 없어진다.

IMPORTANT: 웹에서는 브라우저의 네이티브 디코더를 wasm 디코더로 바꾸는 것이라 느려질 수 있다. 그래서 **바꾸기 전과 뒤를 웹 빌드에서
측정하고**(아래 "측정"), 그 결과를 보고 사용자가 정한다.

이것은 D3D12 포팅의 결정(`d3d12_port.md`, "그 밖", 2026-10-12: stb_image를 들이지 않고 "웹에서 브라우저가 디코딩하듯 네이티브는 OS가
한다")을 바꾼다. 합의하면 그 스펙에도 바뀐 것을 적는다.

범위 밖:

- JPEG와 PNG 밖의 형식(WebP, KTX, Basis 등). 지금 에셋은 모두 JPEG다. 브라우저 디코더는 WebP도 읽었지만 쓰는 곳이 없다.
- PNG 쓰기(`nv_image_write_png`). Windows는 WIC, Linux는 `stb_image_write` 그대로다. 합칠 수 있지만 따로 정한다.
- 디코딩을 다른 스레드로 옮기기, GPU 압축 텍스처, 밉맵 생성.
- 색 관리(ICC, 감마). 지금처럼 하지 않는다.

### 에셋 (2026-11-07)

| 파일 | 이미지 | 크기 | 종류 | 디코딩 뒤 |
|---|---|---|---|---|
| `assets/quaternius/character.glb` (showcase, stress) | JPEG 3장 | 1024x1024 2장, 256x256 1장 | baseline, 4:2:0 | 약 8.3 MB |
| `assets/chess/chess.glb` (chess) | JPEG 33장 중 13장을 디코딩한다(재질의 base color만 쓴다), 압축 4.6 MB | 모두 2048x2048 | 13장 중 4장 progressive, 모두 4:4:4 | 장마다 16 MB, 모두 약 208 MB(한 장씩 올리고 버린다) |

chess가 측정의 주인공이다. progressive JPEG는 stb_image가 계수를 모두 메모리에 두고 디코딩하므로(2048x2048 세 채널이면 약
24 MB) 느리고 메모리를 더 쓴다.

### 의존성

stb_image (public domain / MIT, 단일 헤더). `stb_image_write`와 같은 저장소, 같은 고정 커밋(`4e9b368`)이라 새로 받는 것이 없다. 지금
`CMakeLists.txt`는 stb를 Linux에서만 받으므로(`if(NV_LINUX)`), 받는 것은 모든 플랫폼으로 넓히고 `stb_image_write` 라이브러리만 Linux에
남긴다. 다른 후보: libjpeg-turbo + libpng(시스템 라이브러리 둘, `setjmp` 오류 처리 등 API가 장황하다), Wuffs(메모리 안전한 디코더지만 생성된
수만 줄짜리 C 파일 하나, 새 의존성), GdkPixbuf(GLib에 기대고 LGPL이다).
`docs/CODING_STANDARD.md`의 의존성 절에 까닭과 함께 적는다.

### 바뀌는 것

NOTE: 단계 2의 계획이다. 단계 3에서 웹과 Windows 쪽은 되돌렸다("정한 것").

| 파일 | 무엇 |
|---|---|
| `engine/image.c` (새 파일) | `nv_image_decode`: `stbi_load_from_memory(bytes, size, &w, &h, &n, 4)`. 결과를 아레나로 복사하고 `stbi_image_free`. 실패하면 `stbi_failure_reason()`과 `mime`을 로그에 남긴다. 형식은 stb가 바이트로 알아보므로 `mime`은 로그에만 쓴다 |
| `engine/image.h` | 시그니처는 그대로다(오토배틀러가 따라올 것이 없다). 주석의 "On the web this waits for the browser's decoder (Asyncify)"를 고친다 |
| `engine/image_web.c` | 지운다. 웹에는 `nv_image_write_png`가 없으므로 남는 것이 없다 |
| `engine/image_win32.c` | 디코딩을 지우고 `nv_image_write_png`(WIC)만 남긴다 |
| `engine/image_linux.c` | 디코딩 스텁을 지우고 `nv_image_write_png`만 남긴다 |
| `CMakeLists.txt` | stb를 모든 플랫폼에서 받는다. `stb_image` 라이브러리: 구현을 경고를 끈 별도 번역 단위에 둔다(`stb_image_write`, cgltf와 같다). 정의: `STB_IMAGE_IMPLEMENTATION`, `STBI_ONLY_JPEG`, `STBI_ONLY_PNG`, `STBI_NO_STDIO`(메모리에서만 읽는다) |
| `engine/CMakeLists.txt` | `nv`가 모든 플랫폼에서 `stb_image`를 PRIVATE로 링크한다. `image.c`는 공통 소스, `image_web.c`는 빠진다 |
| `engine/gltf.c` | 모델마다 디코딩 시간을 info 로그로 남긴다(아래 "측정"). NOTE: 측정이 끝난 뒤 사용자가 정해 지웠다("정한 것") |

픽셀의 뜻은 바뀌지 않는다: 8비트 RGBA, 위쪽 행부터, 색 변환 없음, 알파를 곱하지 않음. 지금 웹(`colorSpaceConversion: "none"`,
`premultiplyAlpha: "none"`)과 WIC(`GUID_WICPixelFormat32bppRGBA`, 색 관리 없음)가 하는 것과 같다. stb_image는 ICC와 감마를 적용하지 않고,
16비트 PNG는 8비트로, 회색과 팔레트는 RGBA로 바꾼다.

### 측정

바꾸기 전(브라우저 디코더)과 뒤(stb_image)를 같은 방법으로 잰다. 기준선을 먼저 재야 하므로, 시간 로그를 넣는 커밋을 바꾸는 커밋보다
먼저 올린다(아래 "단계").

**시간 로그** (`gltf.c`, 모든 플랫폼):

```
[nv] chess.glb: 13 images decoded in 427.9 ms (max 55.9 ms, largest 2048x2048)
```

`nv_time_seconds`로 `nv_image_decode` 하나하나를 재고, 모델 하나에 대해 합과 최댓값을 남긴다.

**무엇을 재나:**

| 항목 | 어떻게 |
|---|---|
| chess의 디코딩 시간 | 위 로그. 샌드박스를 띄우고 chess 씬을 처음 보일 때(`_sandbox_debug_show_scene`) |
| showcase의 디코딩 시간 | 같은 로그. 시작할 때 |
| chess 씬이 처음 보일 때까지 걸린 시간 전체 | 씬을 고른 때부터 그 씬이 그려진 첫 프레임까지. 디코딩 밖의 차이(Asyncify 양보가 없어짐)도 보인다 |
| wasm 메모리 | chess를 보인 뒤 `wasmMemory.buffer.byteLength`. wasm 메모리는 줄지 않으므로 디코딩 중의 최대가 그대로 남는다. 브라우저 디코더는 픽셀을 JS 쪽에서 만들었지만 stb는 wasm 힙 안에서 만든다 |
| 다운로드 크기 | Release `sandbox.wasm`의 바이트, gzip과 brotli로 압축한 바이트 |
| 픽셀 차이 | 같은 JPEG를 두 디코더로 디코딩해 채널마다의 최대 차이와 평균 차이. 측정용 임시 코드로 하고 커밋하지 않는다 |

**어디서 재나:**

- **headless Chromium**(Playwright, AGENTS.md의 SwiftShader 플래그): 디코딩은 CPU 일이라 GPU가 없어도 의미가 있다. 매번 새 브라우저로
  5번 재고 중앙값을 쓴다. 기계가 같고 자동이라 전과 뒤의 비교가 공정하다.
- **사용자의 데스크톱 Chrome**: 개발 서버에서 Release 빌드를 열고 Console 탭에서 로그를 읽는다. 실제 숫자다.
- **(고를 수 있음) 폰**: 폰의 브라우저 디코더와 wasm의 차이가 가장 클 곳이다. 사용자가 할 수 있으면.
- **(고를 수 있음) Windows**: WIC와 stb_image를 같은 로그로 비교한다. Windows Release에서 사용자가 한다.

**변형:** stb_image의 SSE2 경로를 wasm SIMD로 컴파일한 것(그 번역 단위에만 `-msimd128 -msse2`; Emscripten이 SSE2 내장 함수를 wasm SIMD로
옮긴다)도 잰다. NOTE: wasm SIMD를 쓰면 모듈 전체가 SIMD를 지원하는 브라우저에서만 열린다. WebGPU가 되는 브라우저는 모두 지원하므로
잃는 것은 없다고 본다. 측정에서 확인한다.

**결과:** 이 스펙의 "찾은 것"에 표로 적는다(전, 뒤, 뒤+SIMD; 환경마다). 정해진 합격선은 두지 않는다. 표를 보고 사용자가 정한다:

1. 모든 플랫폼에서 stb_image(이 스펙대로).
2. 1과 같고 웹에서는 SIMD.
3. 웹은 브라우저 디코더를 남기고, 네이티브(Windows, Linux)만 stb_image. 웹이 크게 느려졌을 때의 대안이다. 그러면 `image_web.c`가 남고
   "모든 플랫폼에서 같은 픽셀"은 잃는다.

### 위험

| 위험 | 대응 |
|---|---|
| 웹에서 chess 로딩이 눈에 띄게 느려진다 | 측정하고, 결과에 따라 SIMD나 대안 3 |
| 디코딩이 메인 스레드를 오래 막는다. 지금은 이미지 사이마다 Asyncify로 브라우저에 양보했지만, stb는 chess를 다 만들 때까지 양보하지 않는다 | "처음 보일 때까지 걸린 시간 전체"로 본다. 몇 초를 넘어 브라우저가 "응답 없음"을 띄우면 이미지 사이에 양보를 넣을지 정한다 |
| progressive JPEG의 계수 버퍼로 wasm 힙이 커지고, 줄지 않는다 | 측정의 wasm 메모리 항목. 힙은 2 GB까지 자란다(`ALLOW_MEMORY_GROWTH`) |
| stb_image가 못 읽는 JPEG(산술 부호화, 12비트, CMYK) | 지금 에셋에는 없다(위 표). 실패는 로그에 이유와 함께 남는다 |
| 브라우저/WIC와 픽셀이 조금 달라 그림이 바뀐다 | 측정의 픽셀 차이. 시각 회귀 테스트는 텍스처를 쓰지 않으므로 영향이 없다 |

### 검증

- **새 테스트** `tests/image_test.c`(ctest, 웹은 Node, Windows는 네이티브): 작은 PNG(정확한 픽셀을 비교)와 작은 JPEG(크기와 대략의 색)를
  바이트 배열로 넣고 디코딩한다. 잘못된 바이트는 실패하고 아레나에 아무것도 남지 않는다. 디코딩이 플랫폼을 모르게 되므로 처음으로
  테스트할 수 있다.
- **웹** Release와 Debug: 세 씬 모두 텍스처가 바르게 보이고(바꾸기 전의 스크린숏과 눈으로 비교), WebGPU 오류와 로그 오류가 없다.
- **Windows** Release와 Debug: 같은 확인. 사용자가 한다(AGENTS.md: 플랫폼을 모르는 파일을 바꾸면 Windows도 빌드한다).
- **Linux**: 창 빌드와 헤드리스 빌드가 빌드된다. 실제 씬에서 보는 것은 Linux에서 샌드박스가 돌 때다(별도의 작업).

### 단계

**작업은 `chromedays/linux-native-groundwork` 브랜치에서 한다**(Linux 샌드박스가 이것을 기다린다). 커밋은 단계마다 하나 이상이고,
`master`에 합치거나 PR을 만드는 것은 사용자가 말할 때만이다.

0. **합의:** 이 스펙.
1. **기준선:** `gltf.c`의 시간 로그만 커밋하고, 지금의 브라우저 디코더로 웹을 잰다(headless 5번, 사용자 Chrome). 다운로드 크기와 wasm
   메모리도.
2. **바꾸기:** 위 "바뀌는 것"과 `tests/image_test.c`. 같은 방법으로 다시 재고, SIMD 변형과 픽셀 차이도 잰다. "찾은 것"에 표를 적는다.
3. **정하기:** 사용자가 표를 보고 정했다: 3의 변형("정한 것"). 웹과 Windows는 되돌리고, Linux의 stb_image는 아레나에서만 할당한다.
4. **정리:** AGENTS.md(구조도의 `image_linux.c`), `docs/CODING_STANDARD.md`의 의존성(stb_image, Linux만), 이 스펙. `image_web.c`와 Asyncify에 대한 AGENTS.md의 말은 그대로 맞다.

### 찾은 것

#### 측정 (2026-11-07)

Linux 개발 PC, Google Chrome stable headless(`playwright-core`로 띄움), Release 빌드, 매번 새 브라우저로 5번 잰 중앙값. chess는 명령
팔레트(F1, "chess", Enter)로 골랐다: Release에는 디버그 내보내기가 없다. 바꾸기 전은 브라우저 디코더에 시간 로그만 더한 빌드다.

| | 브라우저 디코더 | stb_image | stb_image + `-msimd128 -msse2` |
|---|---|---|---|
| 실제 GPU: chess 디코딩(13장) | 427.9 ms (한 장 최대 55.9) | 652.6 ms (최대 86.3) | 633.4 ms (최대 82.6) |
| 실제 GPU: chess를 고른 뒤 그려질 때까지 | 538 ms | 783 ms | 756 ms |
| 실제 GPU: showcase 디코딩(3장) | 24.0 ms | 19.1 ms | 17.5 ms |
| SwiftShader: chess 디코딩 | 1254.2 ms | 795.4 ms | 772.0 ms |
| SwiftShader: chess를 고른 뒤 그려질 때까지 | 2470 ms | 1577 ms | 1453 ms |
| SwiftShader: showcase 디코딩 | 298.3 ms | 22.6 ms | 19.3 ms |
| wasm 메모리(chess 뒤) | 242 MB | 291 MB | 291 MB |
| `sandbox.wasm` 원본 / gzip / brotli | 1,668,464 / 637,172 / 494,228 B | 1,727,324 / 658,965 / 510,896 B | 1,738,261 / 663,168 / 514,174 B |
| `sandbox.js` gzip | 42,422 B | 42,168 B | 42,168 B |

- **실제 GPU에서 stb_image는 브라우저보다 약 1.5배 느리다.** chess가 처음 보일 때 약 245 ms 늦다. 작은 이미지(showcase)는 오히려 빠르다:
  브라우저 경로에는 이미지마다 드는 고정 비용(`createImageBitmap`, 캔버스, `getImageData`, Asyncify 양보)이 있다.
- **SwiftShader 숫자는 디코더 비교에 쓰지 않는다.** 브라우저 경로의 `drawImage`와 `getImageData`가 소프트웨어 GPU를 지나서 브라우저 쪽이
  느려 보인다. NOTE: "어디서 재나"는 SwiftShader로 재기로 했지만, 이 때문에 실제 GPU(`--enable-unsafe-webgpu --enable-features=Vulkan`)로도
  쟀다.
- **SIMD는 효과가 없다.** stb_image는 SSE2 경로를 `__x86_64__`나 `__i386`에서만 켜므로(`STBI__X86_TARGET`) wasm32에서는 꺼져 있다. 남은
  3%는 컴파일러의 자동 벡터화와 측정 오차다. 웹에서 SIMD를 쓰려면 stb를 고쳐야 하므로 선택지 2는 이대로는 없다.
- **wasm 메모리가 49 MB 늘고 줄지 않는다.** stb가 디코딩 중의 결과(16 MB)와 progressive JPEG의 계수 버퍼를 wasm 힙에서 잡는다. 브라우저
  경로는 그것을 JS 쪽에서 했다.
- **다운로드는 wasm이 원본 +58.9 KB, gzip +21.8 KB, brotli +16.7 KB.** JS 로더는 0.8 KB 줄었다.
- **픽셀 차이**(실제 GPU, 같은 바이트를 두 디코더로 디코딩한 측정용 임시 빌드, 커밋하지 않음): 16장 모두 채널 차이가 최대 2–3(255 중),
  평균 0.001–0.033, 다른 채널 값 0.1–3.3%.
- **스크린숏**(SwiftShader, 바꾸기 전과 뒤): showcase 0.17%, chess 0.20%의 픽셀이 2% 넘게 다르다. 눈으로는 같다.
- **오류:** 실제 GPU headless에서는 두 빌드 모두 `[wgpu] reading a buffer back failed (4)`가 남는다. 바꾸기 전에도 나므로 이 변경과
  무관하다(headless Chrome의 실제 GPU 환경). SwiftShader에서는 두 빌드 모두 경고와 오류가 없다.

#### 단계 2의 확인 (2026-11-07)

- ctest 16개 통과(웹 Release와 Debug, 새 `image_test` 포함).
- 웹 Release: showcase와 chess의 텍스처가 바르게 보인다(스크린숏). Console의 경고 0, 오류 0. 웹 Debug: chess까지 오류 없음.
- Linux 창 빌드와 헤드리스 빌드가 빌드된다. `just visual` 합격.
- Windows 빌드는 아직이다(사용자가 한다).

#### 단계 3의 확인 (2026-11-07)

- `nm -u libstb_image.a`의 바깥 기호에 `malloc`, `realloc`, `free`, `calloc`이 없다: stb_image는 `nv_image_stb_*`로만 할당한다.
- `tests/image_test.c`(Linux 디코더): 위의 검사에 더해, 512x512 PNG를 `stb_image_write`로 만들어 정확히 되돌아오는지(zlib 출력과
  데이터가 자라는 경로), 디코딩 뒤 아레나에 픽셀만 시작 자리에 남는지, 픽셀 크기만큼의 아레나(576바이트)에서는 stb의 상태(18,568바이트)가
  들어가지 않아 실패하고 아레나가 그대로인지. AddressSanitizer와 UndefinedBehaviorSanitizer로, `-Wall -Wextra` 경고 없이 통과.
  그때 Linux 빌드는 테스트를 빌드하지 않아 손으로 컴파일해 돌렸다. 그 뒤 Linux에서 ctest를 켰다(`nv_add_test`가 Linux에서 `libm`을
  링크한다): `image_test`를 포함한 15개가 창 빌드(Debug, Release)와 헤드리스 빌드에서 통과한다.
- 웹 Release: stb가 빌드에 없고, `sandbox.wasm`이 단계 1과 같은 크기다(빌드 라벨의 커밋 글자만큼 다르다). ctest 15개 통과.
- `engine/image_web.c`, `engine/image_win32.c`가 이 작업 전(`93e8e79`)과 같다.
- Linux 창 빌드와 헤드리스 빌드가 빌드된다.

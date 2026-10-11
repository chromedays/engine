# Slang 셰이더 포팅 스펙 (WGSL → Slang, 빌드 시점 컴파일)

상태: 제안 (2026-10-11). 이 스펙의 변경은 먼저 합의한다.

### 목표

엔진의 모든 셰이더(지금 C 문자열 안의 WGSL, 약 970줄)를 Slang 소스로 옮긴다. 빌드 시점에 `slangc`가 Slang에서 WGSL을 만들고,
`tint`가 그 WGSL을 검사한다. 엔진은 그 WGSL을 지금처럼 `wgpuDeviceCreateShaderModule`에 넘긴다. **런타임 동작과 화면은 바뀌지
않는다.** 이것은 나중의 D3D12 백엔드(직접 작성)로 가는 첫 단계다: 같은 소스가 HLSL로도 컴파일되도록 지금부터 지킨다.

범위 밖:

- D3D12 백엔드, RHI(Rendering Hardware Interface, 렌더링 하드웨어 인터페이스) 설계, 네이티브 빌드. 각자 따로 스펙을 쓴다.
- 런타임 셰이더 컴파일, 핫 리로드. `slangc`와 `tint`는 실행 파일에 들어가지 않는다.
- 셰이더의 기능 변경과 버그 수정. 포팅은 1:1이다. 포팅하다가 찾은 버그는 따로 커밋하고 여기 "찾은 것"에 적는다.
- DXIL(DirectX Intermediate Language) 컴파일과 검증. 이 단계는 HLSL 텍스트까지 만든다(아래 "열린 질문").

### 의존성 (승인됨, 2026-10-11)

두 도구 모두 같은 조건이다: **빌드 시점 도구로만 실행하고 어디에도 링크하지 않는다. 공식 릴리스의 미리 컴파일된 바이너리만 쓰고,
소스에서 빌드하지 않는다.** 버전과 SHA-256을 고정한다. 둘 다 `docs/CODING_STANDARD.md`의 의존성 절에 기록한다.

#### Slang (`slangc`)

| 항목 | 결정 |
|---|---|
| 무엇 | Slang 컴파일러 `slangc`(shader-slang/slang). Apache 2.0 with LLVM exception |
| 쓰는 방식 | **빌드 시점 도구로만 실행한다. 엔진, 샌드박스, 오토배틀러, 테스트 어느 것에도 링크하지 않는다.** 릴리스 묶음의 `lib/`은 `slangc`가 실행될 때 쓰는 것일 뿐이다 |
| 받는 방식 | 공식 GitHub 릴리스의 미리 빌드된 바이너리를 **버전을 고정해** 내려받는다. 소스에서 빌드하지 않는다 |
| 버전 | `v2026.19` (2026-10-11 기준 최신 태그). 올리는 것은 의도적인 변경이고, 아래 "검증"을 다시 통과해야 한다 |
| 기록 | `docs/CODING_STANDARD.md`의 의존성 절 |

호스트별 묶음 (`slang-<version>-<host>.tar.gz`, 릴리스에 있는 것을 확인함). `slangc`는 **빌드를 하는 호스트**에서 일반 프로그램으로
돈다. 그래서 각 호스트마다 네이티브 묶음이 있어야 하고, Emscripten으로 빌드하지 않는다. 타깃(WGSL, HLSL, DXIL)은 호스트와 별개다:
`slangc`는 텍스트를 쓰므로 결과는 컴파일한 곳과 무관하다.

| 호스트 | 묶음 | 필요한 곳 |
|---|---|---|
| Linux x86_64 | `linux-x86_64` (약 80 MB, 풀면 약 250 MB) | CI(`ubuntu-24.04`), 클라우드 세션. **필수** |
| Windows x64 | `windows-x86_64` | Windows에서 개발하는 사람, 나중의 D3D12 작업 |
| macOS arm64 | `macos-aarch64` | Apple Silicon에서 개발하는 사람 |
| Linux arm64 | `linux-aarch64` | arm64 Linux에서 개발하는 사람 |
| Windows arm64 | `windows-aarch64` | Windows on Arm에서 개발하는 사람 |

검증 상태: 다섯 묶음 모두 `v2026.19` 릴리스에 있음을 확인했다(묶음 존재 확인). 내려받아 푼 것은 `linux-x86_64` 하나뿐이고, 나머지
넷의 내용(실행 여부, 호환성)은 아직 확인하지 않았다. 각 호스트에서 `slangc -v`와 시험 컴파일이 통과하는 것을 단계 1에서 확인한다.

#### Tint (`tint`)

`slangc`는 자기가 쓴 WGSL을 검사하지 않는다(아래 "확인한 것"). 잘못된 WGSL이 빌드를 통과하고 브라우저에서야 거절되므로, 빌드에서
Chrome과 같은 WGSL 컴파일러로 검사한다.

| 항목 | 결정 |
|---|---|
| 무엇 | Dawn(google/dawn)의 WGSL 컴파일러 `tint`. BSD 3-Clause. Chrome과 Edge의 WebGPU가 쓰는 바로 그 컴파일러라서, 브라우저가 거절하는 WGSL을 똑같이 거절한다 |
| 쓰는 방식 | 생성된 WGSL을 읽어 검사만 한다. 출력은 엔진에 쓰지 않고 "검사를 통과했다"는 표시로만 남긴다(아래 "빌드"). 링크하지 않는다 |
| 받는 방식 | Dawn 공식 GitHub 릴리스의 묶음(`Dawn-<commit>-<host>-Release.tar.gz`)에서 `bin/tint` 하나만 꺼내 쓴다. **미리 컴파일된 바이너리만 쓰고 소스에서 빌드하지 않는다** |
| 버전 | `v20261009.190412` (commit `c943ea32b614ff53e4fe410fc8f4968b6b8fe46d`, 2026-10-11 기준 최신). 올리는 것은 의도적인 변경이다 |
| naga를 쓰지 않는 이유 | naga는 Firefox와 wgpu의 컴파일러라서 Chrome과 판정이 다를 수 있고, 공식 미리 컴파일된 바이너리를 확인하지 못했다(보통 `cargo install naga-cli`, 즉 소스 빌드). Firefox를 지원하게 되면 그때 함께 쓰는 것을 다시 본다 |

| 호스트 | 묶음 | 상태 |
|---|---|---|
| Linux x86_64 | `ubuntu-latest-Release` (약 35 MB, `bin/tint`는 약 15 MB) | 내려받아 돌려 봄. 시스템 라이브러리 밖의 공유 라이브러리가 필요 없다. SHA-256 `8530b52e5ef5abeb788ab5c3552b261882d9b4d400d7c85c9dc45255b74d48ea` |
| macOS arm64 / x86_64 | `macos-latest-Release` / `macos-15-intel-Release` | 릴리스에 있음. 내용은 확인하지 않음 |
| Windows x64 | `windows-latest-Release` | 릴리스에 있음. 내용은 확인하지 않음 |
| Linux arm64, Windows arm64 | 없음 | 검사를 건너뛰고 구성 시점에 경고한다. 브라우저 검증(아래 "검증")이 그 호스트에서도 잡는다 |

### 빌드

- `cmake/slang.cmake`가 구성(configure) 시점에 호스트(`CMAKE_HOST_SYSTEM_NAME`, `CMAKE_HOST_SYSTEM_PROCESSOR`)에 맞는 묶음을
  `FetchContent`로 받는다. `URL_HASH SHA256=...`로 묶음마다 해시를 고정한다(해시 표는 그 파일에 둔다). Emscripten 크로스 빌드에서도
  호스트 도구로 실행된다.
- 캐시 변수 `NV_SLANGC`에 경로를 주면 내려받지 않고 그 `slangc`를 쓴다(오프라인 개발). 그 버전이 고정 버전과 다르면 경고한다
  (`slangc -v`).
- `tint`도 같은 방식이다: `cmake/tint.cmake`가 Dawn 묶음을 받아 `bin/tint`를 쓰고, `NV_TINT`로 대신할 수 있다. 묶음이 없는 호스트에서는
  위 2단계를 건너뛴다.
- CI(`build.yml`)는 내려받은 묶음을 `actions/cache`에 버전 키로 둔다. Release와 Debug 두 빌드 디렉터리가 한 번만 받도록
  `FETCHCONTENT_BASE_DIR`을 공유한다.
- 소스 하나(`engine/shaders/<name>.slang`)에서 WGSL 모듈 하나가 나온다. 그 파일의 `[shader(...)]` 엔트리 포인트가 모두 한 모듈에
  들어간다(지금 모듈 하나에 엔트리 포인트가 여럿인 것과 같다).
- 셰이더마다 명령 세 개가 차례로 돈다. 앞의 것이 실패하면 뒤의 것은 돌지 않고 빌드가 실패한다:
  1. `slangc` → 빌드 디렉터리의 `shaders/<name>.wgsl`. 이 파일은 남긴다: 브라우저나 Tint의 오류는 이 파일의 줄과 열을 가리킨다.
  2. `tint --format=wgsl -o shaders/<name>.tint.wgsl shaders/<name>.wgsl` → 검사. 0이 아닌 종료 코드면 빌드가 실패하고 Tint의 메시지가
     빌드 로그에 나온다. 출력 파일은 "검사를 통과했다"는 표시로만 쓴다(`tint`의 `--format=none`은 이 버전에서 받지 않는다).
  3. `cmake -P cmake/embed_text.cmake` → C 헤더 `shaders/<name>_wgsl.h`(`const char <name>_wgsl[]`). 검사를 통과한 WGSL만 헤더가
     된다. (`slangc`의 `-source-embed-style text`도 헤더를 쓸 수 있지만, 그러면 Tint가 읽을 `.wgsl`이 따로 없다.)
- C 쪽은 지금의 문자열 자리를 `#include "shaders/<name>_wgsl.h"`로 바꾼다. `create_shader`와 `create_module`의 모양은 그대로다.
  vfx의 이어 붙이기(`create_module`의 `snprintf`, 32 KB 버퍼)는 없어진다.
- 명령은 `add_custom_command`로 만들고 `-depfile`의 의존성 목록을 `DEPFILE`로 넘긴다. 그래서 `import`한 공용 모듈이 바뀌어도 다시
  만든다.
- 옵션: `-target wgsl -matrix-layout-column-major -warnings-as-errors all`.
- 같은 소스를 `-target hlsl`로도 컴파일한다(결과는 쓰지 않고, 실패하면 빌드가 실패한다). D3D12로 갈 때 HLSL로 안 되는 코드가 쌓이지
  않게 하기 위해서다.

### 파일과 모듈

셰이더 소스는 `engine/shaders/`에 둔다. 공용 선언은 Slang 모듈(`import`)로 나눈다. 지금의 문자열 이어 붙이기(`FRAME_WGSL`,
`PREVIEW_COMMON`, `vfx_common_wgsl` + `vfx_shapes_wgsl`)를 대신한다.

| 지금 (C 문자열) | Slang | 엔트리 포인트 (이름 그대로) | 바인딩 |
|---|---|---|---|
| `FRAME_WGSL` (`renderer.c`) | `frame.slang` (공용) | 없음 | group 0: 0 `frame`, 1 `objects`, 2 `skin` |
| `mesh_shader` | `mesh.slang` | `vs_static`, `vs_skinned`, `fs_main` | group 0: 0–4 (3 `shadow_map`, 4 `shadow_sampler`), group 1: 0–2 (재질) |
| `shadow_shader` | `shadow.slang` | `vs_static`, `vs_skinned` | group 0: 0–2 |
| `debug_shader` | `debug_lines.slang` | `vs_main`, `fs_main` | group 0: 0 |
| `upscale_shader` | `upscale.slang` | `vs_main`, `fs_main` | group 0: 0–3 |
| `bloom_shader` | `bloom.slang` | `vs_main`, `fs_down_first`, `fs_down`, `fs_up` | group 0: 0–2 |
| `imgui_shader` (`imgui.c`) | `imgui.slang` | `vs_main`, `fs_main` | group 0: 0–2 |
| `PREVIEW_COMMON` | `preview_common.slang` (공용) | 없음 | group 0: 0 `u`, 3 `p` |
| `preview_color_shader` | `preview_color.slang` | `vs_main`, `fs_main` | group 0: 0–3 |
| `preview_depth_shader` | `preview_depth.slang` | `vs_main`, `fs_main` | group 0: 0, 2 `d` (깊이, `textureLoad`), 3 |
| `preview_depth_ms_shader` | `preview_depth_ms.slang` | `vs_main`, `fs_main` | group 0: 0, 2 `d` (멀티샘플 깊이), 3 |
| `vfx_common_wgsl` (`vfx.c`) | `vfx_common.slang` (공용) | 없음 | group 0: 0 `frame` |
| `vfx_compute_wgsl` | `vfx_compute.slang` | `init_free`, `emit`, `prepare_a`, `simulate`, `prepare_b` | group 0: 1–11 |
| `vfx_shapes_wgsl` | `vfx_shapes.slang` (공용) | 없음 | 없음 |
| `vfx_render_wgsl` | `vfx_particles.slang` | `vs_add`, `vs_alpha`, `fs_add`, `fs_alpha` | group 0: 1–3 |
| `vfx_marks_wgsl` | `vfx_marks.slang` | `vs_segment`, `fs_segment`, `vs_decal`, `fs_decal` | group 0: 1–2 |

- **엔트리 포인트 이름은 그대로다.** 파이프라인을 만드는 C 코드는 엔트리 포인트 이름으로 찾으므로 바뀌지 않는다. `slangc`는 엔트리
  포인트 이름을 유지하고, 다른 이름에는 `_0` 같은 꼬리를 붙인다(확인함). C는 그 이름을 쓰지 않는다.
- **바인딩은 고정이다.** 모든 리소스에 `[[vk::binding(binding, group)]]`을 붙여 지금의 `@group`/`@binding`과 똑같이 둔다. C 쪽 바인드
  그룹 레이아웃은 바뀌지 않는다. D3D12의 `register(..., space...)` 규칙은 RHI 스펙에서 정한다.
- 주석과 이름은 옮긴다(WGSL의 `particles`는 Slang에서도 `particles`). 셰이더 코드도 `docs/CODING_STANDARD.md`를 따른다.

### 대응 규칙

| WGSL | Slang | 메모 |
|---|---|---|
| `var<uniform> x: T` | `ConstantBuffer<T> x` | 레이아웃은 아래 "구조체 레이아웃" |
| `var<storage, read> a: array<T>` | `StructuredBuffer<T> a` | |
| `var<storage, read_write> a: array<T>` | `RWStructuredBuffer<T> a` | |
| `array<atomic<i32>>` (storage) | `RWStructuredBuffer<Atomic<int>>`, `.add()`, `.load()`, `.store()` | 출력이 `array<atomic<i32>>`와 `atomicAdd`인 것을 확인함 |
| `var<workgroup> wg: array<atomic<u32>, 8>` | `groupshared Atomic<uint> wg[8]` | 출력이 `var<workgroup> ... array<atomic<u32>, 8>`인 것을 확인함 |
| `workgroupBarrier()` | `GroupMemoryBarrierWithGroupSync()` | 출력이 `workgroupBarrier()`인 것을 확인함 |
| `texture_depth_2d` | `DepthTexture2D` | **`Texture2D<float>`를 쓰면 `texture_2d<f32>`가 나와, WebGPU가 비교 샘플링과 깊이 텍스처 바인딩을 거절한다.** 확인함 |
| `texture_depth_multisampled_2d` | `DepthTexture2DMS` | `Texture2DMS<float>`는 `texture_multisampled_2d<f32>`가 된다. 위와 같은 이유로 쓰지 않는다 |
| `sampler_comparison` | `SamplerComparisonState` | |
| `textureSampleCompare` | `SampleCmp` / `SampleCmpLevelZero` | |
| `textureLoad` (멀티샘플) | `.Load(coord, sample)` | |
| `mat4x4f`, `m * v` | `float4x4`, `mul(m, v)` | `-matrix-layout-column-major`. `NvMat4`(`f32 e[16]`)의 메모리 순서 그대로. 지금 셰이더에 열 인덱싱(`m[3].xyz`)은 없다(확인함). 생기면 Slang에서 `m[i]`는 행이라는 것에 주의 |
| `pack2x16float` / `unpack2x16float` | `f32tof16` / `f16tof32` | 출력이 `pack2x16float(vec2f(x, 0))` 두 번과 OR이 된다. 비트는 같다. 하나로 나오는 내장 함수가 있으면 그것을 쓴다(스파이크에서 확인) |
| `bitcast<u32>(x)` | `asuint(x)` | |
| `@interpolate(flat)` | `nointerpolation` | |
| `@builtin(front_facing)` | `SV_IsFrontFace` | |
| `@builtin(vertex_index)`, `instance_index` | `SV_VertexID`, `SV_InstanceID` | |
| `@builtin(global_invocation_id)`, `local_invocation_index`, `workgroup_id` | `SV_DispatchThreadID`, `SV_GroupIndex`, `SV_GroupID` | |
| `@compute @workgroup_size(64)` | `[shader("compute")] [numthreads(64, 1, 1)]` | |
| `mix`, `select` | `lerp`, `?:` (또는 `select`) | 스칼라와 벡터 모두 |

### 구조체 레이아웃

C 구조체와 셰이더 구조체의 바이트 배치가 한 바이트라도 다르면 화면이 조용히 깨진다. `slangc`의 WGSL 출력은 uniform 구조체를 std140
규칙의 `@align(16)` 구조체로 바꾸고, 행렬을 `_MatrixStorage_..._ColMajorstd140` 래퍼로 감싼다(확인함). 지금 쓰는 구조체는 대부분 같은
배치가 되겠지만 추측하지 않고 시험한다:

- 각 모듈을 `-reflection-json`으로도 컴파일한다. 리플렉션에는 리소스의 group/binding, 구조체의 크기, 필드마다 오프셋이 있다(확인함).
- 새 ctest `shader_layout_test`가 그 JSON을 C 쪽 `sizeof`/`offsetof`와 비교한다. 대상: renderer의 frame, object, material, upscale,
  bloom 매개변수; imgui의 uniforms, preview 매개변수; vfx의 `VfxFrameUniforms`, 파티클(32바이트), `NvVfxGpuEmitter`(128),
  `NvVfxBurst`(64), `NvVfxGpuSegment`(80), `NvVfxGpuDecal`(48). 바인딩 번호도 C의 바인드 그룹 레이아웃 표와 비교한다.
- 지금 있는 `_Static_assert`(`vfx.c`)는 그대로 둔다.

### 검증

포팅 전의 WGSL 결과를 **기준선**으로 먼저 저장하고, 포팅한 것을 그것과 비교한다. Release와 Debug 두 빌드 모두, 헤드리스 Chromium
SwiftShader(AGENTS.md "Testing without a display")에서 한다.

**이 회귀 테스트는 포팅이 끝날 때까지만 쓴다** (2026-10-11 합의). 상시 CI 작업은 만들지 않는다: CI는 지금처럼 ctest만 돌리고, 그 안의
빌드 검사(Slang, HLSL, Tint)와 `shader_layout_test`는 포팅 뒤에도 남는다. 브라우저 검사는 단계마다 손으로(또는 에이전트가) 돌린다.

#### 스크립트와 기준선

- 스크립트는 `tools/shader_port/`의 Node 스크립트(`.mjs`)다. Playwright는 저장소에 설치하지 않고 npx로 버전을 고정해 쓴다
  (`npx -y -p playwright@1.56.1 ...`, AGENTS.md의 tools 규칙). Chromium은 Playwright의 것을 쓴다. `tests/`는 "브라우저가 필요 없는
  테스트"라서 거기 두지 않는다.
- 이미지 비교는 별도 라이브러리 없이 Chromium 안에서 한다: 두 PNG를 `<canvas>`에 그려 `getImageData`로 픽셀을 비교한다.
- 기준선은 **커밋한다** (2026-10-11 합의). 단계 0에서 지금의 WGSL로 만든 결과를 `tools/shader_port/baseline/`에 둔다. 포팅한 빌드의
  결과는 `<build>/shader_port/current/`에 쓰고 커밋하지 않는다.
  - 스크린샷: `baseline/screens/<경우>.png` (예: `S3-chess.png`, `V2-f030.png`). PNG는 이미 `.gitattributes`에서 Git LFS로 간다.
    Release 빌드에서 찍고, Debug 빌드도 같은 기준선과 비교한다(`NV_ASSERT`는 화면을 바꾸지 않는다). 약 35장.
  - VFX 덤프: `baseline/vfx/<경우>.json`, 일반 Git(텍스트, diff가 보인다). 비교하는 것만 담는다: 덤프한 프레임마다 counters와 살아 있는
    파티클을 (emitter, seed)로 정렬한 목록(위치, 속도, 나이, 크기, 회전; float는 왕복이 정확한 9자리). 용량 전체의 버퍼(2,097,152 × 32
    바이트)는 담지 않는다. 불변식은 실행할 때 검사하므로 저장하지 않는다. V9, V12는 counters만.
  - 기준선을 두 번 만들어 자기와의 비교를 통과한 쪽을 커밋한다. 기준선을 다시 만드는 것은 의도적인 변경이고, 커밋 메시지에 까닭을 쓴다.
- CI는 기준선 PNG를 받지 않는다: `build.yml`의 `git lfs pull`과 LFS 캐시 키(`.lfs-objects`)에서 `tools/shader_port/baseline/`을
  뺀다(`--exclude`). CI는 브라우저 검사를 돌리지 않으므로 필요 없다.
- 단계 6에서 `tools/shader_port/`(스크립트와 기준선)와 이 검증만을 위한 디버그 내보내기(`_sandbox_debug_fixed_dt`,
  `_sandbox_debug_vfx_dump`)를 지운다. 기준선은 Git 기록에 남는다.

#### 공통 검사 (모든 단계)

1. 빌드: WGSL과 HLSL이 경고 없이 컴파일되고(`-warnings-as-errors all`), 모든 WGSL이 Tint 검사를 통과한다.
2. `shader_layout_test` 통과.
3. **필수 관문:** 시작부터 각 씬을 보이고 Play를 하는 동안 WebGPU 오류가 0개(`_sandbox_debug_wgpu_error`). 모든 파이프라인이 만들어진다.
   오토배틀러도 한 라운드 돈다. Tint는 셰이더 하나만 보므로 셰이더와 바인드 그룹 레이아웃의 불일치는 여기서만 잡힌다. 어느 단계도
   이것 없이 끝나지 않는다.
4. 화면 비교: Edit 모드(아무것도 움직이지 않음)에서 아래 경우마다 스크린샷을 기준선과 비교한다. 허용 오차: 채널 차이 최대 2/255, 그
   넘는 픽셀은 0.1% 이하. 그보다 크면 원인을 찾을 때까지 다음 단계로 가지 않는다.

| 경우 | 씬 | 설정 |
|---|---|---|
| S1 | showcase, stress, chess | 기본값 |
| S2 | 세 씬 | MSAA 1 |
| S3 | 세 씬 | 톤 매핑 Clamp |
| S4 | 세 씬 | 톤 매핑 ACES (기본값 PBR Neutral은 S1이 본다) |
| S5 | 세 씬 | bloom 끔 |
| S6 | 세 씬 | 그림자 끔 |
| S7 | showcase | 해상도 Scale 2 |
| S8 | showcase | 해상도 Fixed 320x180, 맞춤 방식 셋(정수 배, Fit, Stretch) |
| S9 | showcase | Textures 탭: 컬러 미리보기, 깊이 미리보기(MSAA 1), 멀티샘플 깊이 미리보기(MSAA 4) |
| S10 | showcase | ImGui만: 데스크톱 UI의 패널과 메뉴, 폰 UI(터치 에뮬레이션) |

S2–S6은 한 번에 하나만 바꾼다(조합 전부는 너무 많고, 셰이더 경로는 하나씩 바꿔도 다 지난다).

#### VFX 검사 (철저히)

VFX의 계산 패스는 아토믹으로 슬롯을 나눠 가지므로 실행 순서에 따라 파티클이 놓이는 슬롯이 바뀐다. 그래서 버퍼를 바이트로 비교하지 않고,
**순서와 상관없는 값**과 **불변식**으로 비교한다.

- **결정적 실행:** 새 디버그 내보내기 `_sandbox_debug_fixed_dt(dt)`(0이면 끔)로 게임 클럭을 고정 dt로 돌린다. 경우마다 페이지를 새로
  열어(빈 저장소, 같은 시작 상태) showcase 씬에서 Play를 누르고, 프레임 0에 `_sandbox_debug_vfx_fire`로 발사한다. showcase에는
  `effects` 행동이 없어서 발사한 것 말고는 파티클이 없다(V9의 정확한 수가 이것에 기댄다).
- **덤프:** 새 디버그 내보내기 `_sandbox_debug_vfx_dump()`가 파티클 버퍼, free 리스트, 두 alive 리스트, visible 리스트, counters,
  `sim_args`, `draw_args`를 읽어 돌려준다(맵 읽기는 Asyncify로 기다린다).
- **불변식** (덤프할 때마다):
  - free 수 + alive 수 = 용량.
  - free 리스트와 alive 리스트에 같은 인덱스가 두 번 나오지 않고, 둘을 합치면 0..용량-1 전부다.
  - visible(additive + alpha) ≤ alive. 살아 있는 파티클은 모두 `age < life`.
  - `sim_args`, `draw_args`가 counters와 맞는다(`prepare_b`: 정점 수 = additive × 6, alpha × 6, 인스턴스 1; `prepare_a`의 워크그룹 수).
  - dropped는 줄지 않는다.
- **기준선과 비교** (덤프할 때마다): 살아 있는 파티클을 (emitter, seed)로 정렬해 위치, 속도, 나이, 크기, 회전을 비교한다(상대 오차 1e-4,
  half float로 담긴 값은 half의 1 ulp). 개수(alive, visible, dropped, segments, decals)는 정확히 같아야 한다.

경우 (수명은 `sandbox/effects.c`의 값: Sparks 0.3–0.9 s, Explosion 최대 3.2 s + 지연 0.05 s, Swarm 2.0–3.0 s, Smoke 3.0–5.0 s; 기본
용량 `NV_VFX_DEFAULT_PARTICLES` = 2,097,152):

| 경우 | 프레임 0에 발사 | dt | 덤프하는 프레임 | 보는 것 |
|---|---|---|---|---|
| V1 | Sparks 1 | 1/60 | 0–60 매 프레임 | 워크그룹 하나(70개). 첫 emit, 첫 simulate, alive 리스트 교대, 마지막 하나가 죽어 alive 0 (54프레임 무렵) |
| V2 | Explosion 1 | 1/60 | 1, 2, 3, 4, 5, 30, 120, 200 | 이미터 다섯, 지연된 버스트(0.05 s, 3프레임째), 바닥 충돌(`ground`, `restitution`, `friction`), 200에 alive 0 |
| V3 | Swarm 1 | 1/60 | 1, 2, 3, 60, 120, 200 | 한 버스트가 워크그룹 여럿(512개 = 8)으로: `emit`의 워크그룹 아토믹(`groupshared`). 200에 alive 0 |
| V4 | Smoke 1 | 1/60 | 1, 60, 180, 300, 320 | 긴 수명, 난류(curl noise), 크기와 회전의 변화. 320에 alive 0 |
| V5 | Sparks 1 + Explosion 1 | 1/30 | 1, 2, 15, 50, 100 | 큰 dt |
| V6 | Sparks 1 + Explosion 1 | 1/240 | 1, 2, 60, 240, 800 | 작은 dt, 많은 프레임 |
| V7 | Sparks 10 + Explosion 5 + Swarm 3 | 1/60 | 1, 2, 3, 60, 200 | 한 프레임에 버스트 여럿(작업 분할, 서로 다른 이미터) |
| V8 | Sparks 1을 5프레임마다, 60프레임 동안 (13번) | 1/60 | 30, 60, 61, 120 | emit과 죽음이 섞인 프레임, free 리스트 재사용 |
| V9 | Swarm 4,200 | 1/60 | 1, 2, 200 | **넘침.** counters만 본다(전체 덤프는 너무 크다). 1, 2에서 alive = 2,097,152, free = 0, dropped = 4,200 × 512 − 2,097,152 = **53,248** (정확히). 200에서 alive 0, free = 용량, dropped 그대로 |
| V10 | V9 뒤 같은 페이지에서 Sparks 1 | 1/60 | 1, 60 | 한 번 가득 찼다가 빈 free 리스트에서 다시 emit |
| V11 | Missile 3 | 1/60 | 30, 90, 200 | 궤적, 빔, 데칼(링 버퍼). segments, decals 개수가 정확히 같다 |
| V12 | stress 씬의 `effects` 행동을 파일의 값 셋(particles 250,000, 1,000,000, 2,000,000)으로 (`_sandbox_debug_set_effects`) | 1/60 | 10, 60, 120 | 부하. counters만 보고 불변식은 개수로(free + alive = 용량, visible ≤ alive). 마지막에 벤치마크를 한 번 돌려 오류 0 |

화면 (V-화면): 아래 프레임에서 dt를 0으로 멈추고 스크린샷을 기준선과 비교한다(공통 검사 4와 같은 허용 오차). alpha 파티클은 일부러
정렬하지 않으므로(`sandbox/effects.c`의 `// TODO:`) 겹치는 부분의 순서가 다를 수 있다. 그런 차이가 보이면 alpha 효과는 하나씩 따로
찍는다.

| 경우 | 프레임 |
|---|---|
| V1 | 10, 40 |
| V2 | 4, 30, 120 |
| V3 | 60 |
| V4 | 180 |
| V11 | 30, 90 (궤적, 빔, 데칼) |

**먼저 자기와의 비교:** 단계 0에서 기준선을 같은 빌드로 두 번 만들어 V1–V11의 비교가 스스로와 일치하는지 본다. 일치하지 않으면
(SwiftShader에서도 결정적이지 않으면) 그 경우의 "기준선과 비교"는 개수와 분포(평균, 최소, 최대)의 비교로 낮추고, 어느 경우가 그런지
여기 적는다. V9의 53,248은 결정성과 상관없이 정확해야 한다.

### 단계

0. **기준선** (셰이더 변경 없음): 디버그 내보내기(`_sandbox_debug_fixed_dt`, `_sandbox_debug_vfx_dump`)와 `tools/shader_port/`의
   스크립트를 더하고, 지금의 WGSL로 S1–S10, V1–V12를 두 번 돌려 자기와의 비교를 확인한 뒤 기준선(`tools/shader_port/baseline/`)을
   커밋한다. `build.yml`의 LFS 제외도 여기서 한다. 이 커밋이 이후 모든 비교의 기준이다.
1. **빌드 통합 + 가장 쉬운 것**: `cmake/slang.cmake`, `cmake/tint.cmake`, `cmake/embed_text.cmake`, CI 캐시, `shader_layout_test`의 틀,
   `imgui.slang`. 공통 검사 1–4 (S10 중심). 일부러 잘못 쓴 셰이더(깊이 텍스처를 `Texture2D<float>`로)가 Tint 단계에서 빌드를 멈추는지도 본다.
2. **가장 어려운 것**: `vfx_common.slang`, `vfx_compute.slang`. V1–V12의 덤프와 불변식(화면은 단계 3). **여기서 계속할지 정한다.** Slang의 WGSL 출력으로 같은
   결과가 안 나오면 멈추고 무엇이 막혔는지 보고한다.
3. `vfx_shapes.slang`, `vfx_particles.slang`, `vfx_marks.slang`. V-화면, V11.
4. renderer: `frame.slang`, `mesh.slang`, `shadow.slang`, `debug_lines.slang`, `upscale.slang`, `bloom.slang`. S1–S8.
5. imgui 미리보기: `preview_*.slang`. S9.
6. 정리: 마지막으로 S1–S10, V1–V12를 모두 다시 돌린다. C에 WGSL 문자열이 남지 않는다. `tools/shader_port/`와 두 디버그 내보내기를
   지운다. AGENTS.md(셰이더가 `engine/shaders/*.slang`이라는 것, "a matching entry point in
   `shadow_shader`"는 `shadow.slang`으로), 이 스펙의 "만든 결과", 셰이더를 가리키는 다른 스펙(`vfx.md`, `shadows.md`, `msaa.md`,
   `textures.md`)을 고친다.

단계마다 한 커밋 이상이고, 각 커밋은 그 단계의 검증을 통과한 뒤 올린다.

### 확인한 것 (2026-10-11, slangc 2026.19, Dawn v20261009.190412, linux-x86_64)

- 묶음은 `bin/slangc`와 그것이 쓰는 `lib/libslang-compiler.so` 등이다. DXC(`dxcompiler`)는 들어 있지 않다. 그래서 DXIL은 따로 DXC가
  있어야 한다.
- 위 "대응 규칙"의 "확인함" 행들: 작은 시험 소스를 WGSL과 HLSL로 컴파일해 출력을 읽었다. 브라우저에서 돌려 보지는 않았다(단계 1, 2).
- HLSL 출력에서 `vk::binding`은 무시되고 레지스터가 차례로(`u0`, `u1`) 붙는다. D3D12 바인딩은 RHI 스펙에서 정한다.
- `-source-embed-style text`는 C 헤더를, `-depfile`은 Makefile 형식의 의존성 목록을 쓴다.
- **`slangc`는 잘못된 WGSL을 오류도 경고도 없이 쓴다.** `Texture2D<float>`를 `SampleCmpLevelZero`로 쓴 시험 소스가 그랬다. Slang의 설계
  문서(`target-pipelines/wgsl.md`)도 "bare `WGSL` target은 텍스트에서 멈추고 Slang 안에서 검사하지 않는다"고 한다. Slang이 Tint를
  부르는 것은 `wgsl-spirv` 타깃뿐이고, 그것도 릴리스 묶음에 Tint(`slang-tint`)가 없어 돌지 않는다(`failed to load dynamic library
  'slang-tint'`).
- 그 WGSL을 헤드리스 Chromium에 넣으면 `createShaderModule`에서 `:28:116 error: no matching call to 'textureSampleCompareLevel(...)'`로
  거절된다. Dawn 릴리스의 `tint`도 같은 메시지를 내고 종료 코드 1로 끝난다. 올바른 WGSL에서는 0이다.
- 파이프라인 검증 오류(셰이더와 레이아웃의 바인딩 타입 불일치)는 `@group(0) @binding(6)`과 엔트리 포인트 이름으로 나온다. `_0`이 붙은
  이름은 나오지 않는다.
- Slang의 WGSL 성숙도 (2026-10-11): 제목에 WGSL이 있는 이슈가 열림 40, 닫힘 128. 최근 열린 것 다수가 잘못된 WGSL 출력이다(#13571 행렬
  부정, #13566 `f(a < b, a > b)`가 템플릿 목록으로 읽힘 등). 지금 우리 셰이더에는 그 두 패턴이 없다. Slang CI에서 WebGPU로 도는 테스트
  파일은 143개(Vulkan 1,361, D3D12 463)이고, CI가 실패를 예상하는 19개 중 9개가 WebGPU이며 그중 4개가 `groupshared`다. VFX `emit`이
  워크그룹 아토믹을 쓰므로 단계 2를 계속할지 정하는 지점으로 두는 이유가 이것이다.

### 열린 질문

- DXIL: 언제 DXC로 컴파일과 검증을 시작할지(DXC도 빌드 도구 의존성이므로 따로 승인). RHI 스펙에서 정하자는 것이 제안이다.
- D3D12의 `register`/`space` 규칙: RHI 스펙.
- 우리가 고정한 Tint와 사용자의 Chrome 안의 Tint는 버전이 다를 수 있다. 판정이 갈리면 브라우저가 기준이다. Tint를 올릴 때는 그때의
  Chrome stable에 가까운 Dawn 릴리스를 고른다.

### 찾은 것

(포팅하면서 찾은 버그와 차이를 적는다.)

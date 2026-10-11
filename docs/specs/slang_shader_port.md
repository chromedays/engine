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

공통 (모든 모듈):

1. 빌드: WGSL과 HLSL이 경고 없이 컴파일되고(`-warnings-as-errors all`), 모든 WGSL이 Tint 검사를 통과한다.
2. `shader_layout_test` 통과.
3. **필수 관문:** 시작부터 각 씬을 보이고 Play를 하는 동안 WebGPU 오류가 0개(`_sandbox_debug_wgpu_error`). 모든 파이프라인이 만들어진다.
   오토배틀러도 한 라운드 돈다. Tint는 셰이더 하나만 보므로 셰이더와 바인드 그룹 레이아웃의 불일치는 여기서만 잡힌다. 어느 단계도
   이것 없이 끝나지 않는다.
4. 화면 비교: Edit 모드(아무것도 움직이지 않음)에서 각 씬(showcase, stress, chess)의 스크린샷을 기준선과 비교한다. 조합: MSAA 1과 4,
   톤 매핑 3개(Clamp, PBR Neutral, ACES), bloom 켜고 끄기, 그림자 켜고 끄기, 해상도 Scale과 Fixed. Textures 탭의 미리보기(컬러, 깊이,
   멀티샘플 깊이)와 ImGui 자체도 찍는다. 허용 오차: 채널 차이 최대 2/255, 그 넘는 픽셀은 0.1% 이하. 그보다 크면 원인을 찾을 때까지
   다음 단계로 가지 않는다.

VFX (철저히):

VFX의 계산 패스는 아토믹으로 슬롯을 나눠 가지므로 실행 순서에 따라 파티클이 놓이는 슬롯이 바뀐다. 그래서 버퍼를 바이트로 비교하지 않고,
**순서와 상관없는 값**과 **불변식**으로 비교한다.

1. 결정적 실행: 새 디버그 내보내기 `_sandbox_debug_fixed_dt(dt)`(0이면 끔)로 게임 클럭을 고정 dt로 돌린다. 같은 씬, 같은 발사
   (`_sandbox_debug_vfx_fire`), 같은 프레임 수.
2. 덤프: 새 디버그 내보내기 `_sandbox_debug_vfx_dump()`가 파티클 버퍼, free 리스트, 두 alive 리스트, visible 리스트, counters,
   `sim_args`, `draw_args`를 읽어 돌려준다(맵 읽기는 Asyncify로 기다린다).
3. 불변식 (프레임마다, 기준선과 새 것 모두):
   - free 수 + alive 수 = 용량.
   - free 리스트와 alive 리스트에 같은 인덱스가 두 번 나오지 않고, 둘을 합치면 0..용량-1 전부다.
   - visible(additive + alpha) ≤ alive. 살아 있는 파티클은 모두 `age < life`.
   - `sim_args`, `draw_args`가 counters와 맞는다(`prepare_b`: 정점 수 = additive × 6, alpha × 6, 인스턴스 1; `prepare_a`의 워크그룹 수).
   - dropped는 줄지 않는다.
4. 기준선과 비교: 살아 있는 파티클을 (emitter, seed)로 정렬해 위치, 속도, 나이, 크기, 회전을 비교한다(상대 오차 1e-4, half float로
   담긴 값은 half의 1 ulp). 개수(alive, visible, dropped)는 정확히 같아야 한다.
5. 경계 조건: 용량을 넘는 발사(dropped가 늘어야 함), 64개를 넘는 한 번의 버스트(워크그룹 여럿, `emit`의 워크그룹 아토믹), 한 프레임에
   버스트 여럿, 파티클이 모두 죽어 alive가 0이 되는 프레임, 바닥 충돌(`ground`, `restitution`, `friction`), 난류(curl noise).
6. 부하: stress 씬의 `effects` 행동을 최대로(`_sandbox_debug_set_effects`) 두고 벤치마크를 한 번 돌린다. 오류 0, 불변식 유지.
7. 화면: 고정 dt로 N 프레임 뒤 dt 0(멈춤)에서 additive, alpha 파티클, 궤적, 빔, 데칼의 스크린샷을 기준선과 비교한다(위와 같은 허용
   오차, 단 alpha 파티클은 일부러 정렬하지 않으므로(`sandbox/effects.c`의 `// TODO:`) 겹치는 부분의 순서 차이가 나올 수 있다. 그런
   차이가 보이면 alpha 효과는 하나씩 따로 찍는다).

먼저 기준선을 같은 빌드로 두 번 돌려 4의 비교가 스스로와 일치하는지 본다. 일치하지 않으면(SwiftShader에서도 결정적이지 않으면) 4는
개수와 분포(평균, 최소, 최대)의 비교로 낮추고, 그 사실을 여기 적는다.

### 단계

0. **기준선** (셰이더 변경 없음): 디버그 내보내기(`_sandbox_debug_fixed_dt`, `_sandbox_debug_vfx_dump`)와 Playwright 스크립트를
   더하고, 지금의 WGSL로 스크린샷과 덤프를 저장한다. 이 커밋이 이후 모든 비교의 기준이다.
1. **빌드 통합 + 가장 쉬운 것**: `cmake/slang.cmake`, `cmake/tint.cmake`, `cmake/embed_text.cmake`, CI 캐시, `shader_layout_test`의 틀,
   `imgui.slang`. 공통 검증 1–4. 일부러 잘못 쓴 셰이더(깊이 텍스처를 `Texture2D<float>`로)가 Tint 단계에서 빌드를 멈추는지도 본다.
2. **가장 어려운 것**: `vfx_common.slang`, `vfx_compute.slang`. VFX 검증 1–6. **여기서 계속할지 정한다.** Slang의 WGSL 출력으로 같은
   결과가 안 나오면 멈추고 무엇이 막혔는지 보고한다.
3. `vfx_shapes.slang`, `vfx_particles.slang`, `vfx_marks.slang`. VFX 검증 7.
4. renderer: `frame.slang`, `mesh.slang`, `shadow.slang`, `debug_lines.slang`, `upscale.slang`, `bloom.slang`.
5. imgui 미리보기: `preview_*.slang`.
6. 정리: C에 WGSL 문자열이 남지 않는다. AGENTS.md(셰이더가 `engine/shaders/*.slang`이라는 것, "a matching entry point in
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

# 시각 회귀 테스트 스펙 (Linux Wayland 창, 삼각형 씬 하나)

상태: 합의됨 (2026-11-06). 이 스펙의 변경은 먼저 합의한다.

### 목표

렌더러가 그린 그림이 기준 그림과 달라졌는지를 명령 하나로 확인한다. 숫자로는 맞는 것처럼 보이는
셰이더/톤 매핑/업스케일 회귀를 그림으로 잡는 것이 목적이다. 이번 범위는 케이스 하나, 삼각형 렌더
씬뿐이다. 케이스를 늘리는 것은 이 하나가 반복 가능함을 보인 다음의 일이다.

합의된 결정:

- 캡처 대상은 Linux Dawn 네이티브 WebGPU의 **창 있는(Wayland)** 빌드다. 브라우저/SwiftShader도,
  Windows D3D12도 아니다. 삭제된 D3D12 포트 회귀 하네스(`tools/d3d12_port/`, Git 기록 `4ecbb2e`)는
  복원하지 않고 처음부터 쓴다.
- 대상 앱은 샌드박스가 아니라 전용 실행 파일 `visual_test`다.
- 비교 도구는 [odiff](https://github.com/dmtrKovalenko/odiff)(MIT, Zig로 쓴 네이티브 바이너리)다.
  `slangc`/`tint`처럼 버전과 SHA-256을 고정해 내려받고, 빌드/테스트 때만 실행하며 어디에도 링크하지 않는다.
- CI와 PR 게이트는 없다. `ctest`에도 넣지 않는다. 실행은 `justfile` 레시피로 명시적으로만 한다.

범위 밖:

- 케이스 추가(MSAA, 톤 매핑 변형, 그림자, 해상도, UI). 구조는 케이스 표를 늘리기 쉽게 두되 이번에는 행이 하나다.
- 샌드박스의 Linux 실행. 이미지 디코더와 샌드박스 타깃 활성화가 필요하므로 별도 스펙의 일이다.
- 창 입력(키보드/포인터/IME/클립보드)과 창 크기 변경 대응. 이 창 백엔드는 테스트가 필요한 만큼만 한다.
- 다른 기계에서 만든 기준 그림과의 일치 보장. 기준 그림은 만든 기계의 GPU/드라이버에서 유효하다.
  허용치로 흡수하는 범위는 "합격 규칙"에 적는다.

### 의존성

- odiff 4.5.0 (MIT): 픽셀 단위 비교와 diff 그림 생성을 맡는다. 직접 쓰면 PNG 디코더와 비교기가
  필요하고, 그것은 이 테스트의 목적이 아니다. 고정 방식은 `docs/CODING_STANDARD.md`의 의존성에 기록한다.
- 시스템 라이브러리 `wayland-client`와 `xdg-shell` 프로토콜: Linux 창이 요구하는 플랫폼 라이브러리로,
  Windows SDK와 같은 종류다. `wayland-scanner`가 빌드 때 프로토콜 코드를 생성한다.
- stb의 `stb_image_write` (public domain / MIT, 단일 헤더): Linux의 PNG 쓰기(`nv_image_write_png`)에 쓴다.
  Windows는 WIC를 쓰고, Linux에 같은 자리의 시스템 라이브러리는 없어 단일 헤더를 들인다. 도입 방식은
  cgltf와 같다: 저장소 커밋을 고정해 FetchContent로 받고, 구현은 경고를 끈 별도 번역 단위에 둔다.
- Node: 비교 러너(`tools/visual/compare.mjs`)를 돌린다. `tools/dev_server.mjs`와 같은 전제다.

### 구성

```
just visual ─▶ cmake 구성/빌드 (build-visual, Debug, NV_HEADLESS 끔 + NV_BUILD_VISUAL_TEST)
            ─▶ visual_test <out>     창을 열고 3프레임을 그리고 <out>/triangle.png를 쓴다
            ─▶ compare.mjs           odiff로 tests/visual/baseline/triangle.png와 비교한다
                                     report.html/report.json을 쓰고 마지막 줄에 그 링크를 출력한다 (visual_report.md)
just visual-update-ref ─▶ 같은 캡처 뒤 기준 그림을 덮어쓴다 (검토는 사람의 몫이다)
```

| 파일 | 역할 |
|---|---|
| `engine/window_linux.c`, `engine/window_linux.h` | Wayland 창 백엔드. 헤더는 엔진 내부 전용으로 `gpu_webgpu.c`에 `wl_display`/`wl_surface`를 넘긴다 |
| `engine/gpu_webgpu.c` | 창 빌드에서 Wayland 서피스 생성, `wgpuSurfacePresent`(네이티브에서만), Debug 스크린샷 리드백 |
| `engine/image_linux.c` | stb_image_write로 `nv_image_write_png` 구현 (디코드는 여전히 미구현으로 오류를 남긴다) |
| `tests/visual/visual_test.c` | 전용 앱. 삼각형 씬 하나를 렌더러로 그린다 |
| `tests/visual/baseline/triangle.png` | 기준 그림. Git LFS (`*.png` 패턴이 이미 있다) |
| `cmake/odiff.cmake` | odiff 고정 다운로드. `NV_ODIFF`가 있으면 그것을 쓴다 |
| `tools/visual/compare.mjs` | odiff 실행, 합격 규칙 적용, 결과 출력 |
| `tools/visual/report.mjs` | 결과 페이지(`report.html`, 그림을 안에 담은 파일 하나)와 `report.json`. `docs/specs/visual_report.md` |

CMake 옵션 두 개를 더한다.

- `NV_HEADLESS`: Linux에서 `window_linux.c` 대신 `window_headless.c`를 고른다(창 없이 화면 밖에 그린다). 기본은
  꺼짐이라 Linux 빌드는 Wayland 창으로 그린다. (처음에는 반대 뜻의 `NV_LINUX_WINDOWED`가 기본 꺼짐으로 있었다.)
  엔진 C 코드에는 `NV_HEADLESS`가 0이나 1로 정의되고, 웹과 Windows에서는 늘 0이다. 그 둘에서 켜면 구성 오류로 멈춘다.
- `NV_BUILD_VISUAL_TEST`: `tests/visual/`을 더한다. `NV_HEADLESS`가 켜져 있으면 구성 오류로 멈춘다.

### 창 백엔드

- `wl_display_connect` → 레지스트리에서 `wl_compositor`와 `xdg_wm_base`를 바인드 → `wl_surface`,
  `xdg_surface`, `xdg_toplevel`을 만든다. `app_id`는 `visual_test`가 정하는 제목과 함께 설정한다.
  첫 `xdg_surface.configure`를 ack 하기 전에는 버퍼를 붙이지 않는다 (프로토콜 규칙).
- 프레임버퍼 크기는 고정이다. 기본 1280×720이고 `NV_WINDOW_WIDTH`/`NV_WINDOW_HEIGHT`가 바꾸며,
  컴포지터의 configure 크기는 ack만 하고 따르지 않는다. NOTE: 캡처한 픽셀이 정확해야 해서다.
  타일링 컴포지터에서는 창이 늘어져 보일 수 있으나 그려진 픽셀은 고정 크기다.
- 이벤트는 프레임마다 논블로킹으로 비운다 (`prepare_read`/`poll(0)`/`dispatch_pending`).
  `xdg_toplevel.close`는 종료를 요청한다. 입력 장치는 바인드하지 않는다.
- `nv_window_run`은 `nv_quit` 뒤에 돌아온다. NOTE: 헤드리스 백엔드와 같은, 이 함수의 "돌아오지
  않는다"는 계약의 의도된 예외다. 테스트 앱이 종료 코드로 성공/실패를 알려야 해서다.
- `xdg-shell` 프로토콜 코드는 빌드 때 `wayland-scanner`로 생성해 빌드 디렉터리에 둔다. 소스에 넣지 않는다.

### GPU와 스크린샷

- 창 빌드의 `gpu_webgpu.c`는 `WGPUSurfaceSourceWaylandSurface`로 서피스를 만들고, 웹과 같은
  capability 경로로 포맷을 고른다. 프레임 끝에서는 제출 뒤 `wgpuSurfacePresent`를 부른다.
  IMPORTANT: 이는 네이티브 전용 분기이며, 브라우저 경로에서는 여전히 부르지 않는다.
- `nv_gpu_debug_screenshot`를 Linux Debug 빌드에서도 선언한다 (조건에 `NV_PLATFORM_LINUX`를 더한다).
  웹 빌드에는 없다. 의미는 D3D12의 것과 같다: 그 프레임의 캔버스를 PNG로 쓴다.
- 구현은 프레임 제출 뒤 present 전에 캔버스 텍스처를 리드백 버퍼로 복사하고(서피스 usage에
  Debug에서 CopySrc를 더한다), present 뒤 map을 기다려서(`wgpuInstanceWaitAny`) PNG를 쓴다.
  행 pitch는 256 바이트 정렬이고, BGRA 서피스는 RGBA로 채널을 바꾸며, 알파는 255로 쓴다.
  스크린샷은 느려도 되는 디버그 경로이므로 막혀서 기다리는 것이 맞다.

### 테스트 앱과 케이스

`visual_test <출력 디렉터리>`:

- 장면은 코드로 만든다. 파일도, 디코드할 이미지도, 저장소도 쓰지 않는다.
- 구성: 카메라 노드(원점, -Z를 봄, fov 45°), 방향광 노드(기본 회전, 세기 1.1),
  삼각형 노드(z = -3). 삼각형은 정점 3개(법선 +Z, UV 모서리)와 인덱스 3개의 `NvMeshData`이며,
  재질은 단색(선형 RGBA), 양면이다.
- 렌더러 설정은 `nv_renderer_init` 기본값을 그대로 쓴다: MSAA 1, Clamp 톤 매핑, 노출 1, 블룸 끔,
  그림자 끔. 씬 해상도는 Scale 1이라 캔버스와 같은 1280×720이다.
- 3프레임을 그리고 3번째 프레임에서 스크린샷을 요청한 뒤 종료한다. 앞의 두 프레임은 파이프라인과
  타깃 생성이 끝난 그림을 찍기 위한 것이다.
- 종료 코드: PNG가 쓰였으면 0, 아니면 1. 실패 이유는 로그에 남는다 (`nv_log`).

### 합격 규칙

- 크기가 다르면 무조건 실패다 (레이아웃 차이).
- odiff 임계값(`--threshold`)은 0.05, 다른 픽셀이 전체의 0.1% 이하이면 합격이다.
  odiff는 다른 픽셀 수와 비율을 보고만 하므로, 비율 규칙은 `compare.mjs`가 출력을 읽어 적용한다.
- diff 그림은 실제 그림 옆에 `<이름>.diff.png`로 남긴다. 같은 픽셀은 흰색, 다른 픽셀은 빨강이다(odiff `--diff-overlay 1`, `visual_report.md`). 실패 출력은 기준/실제/diff 세 경로를 말한다.
- 기준 그림이 없으면 실패하고 `just visual-update-ref`를 안내한다. 러너가 기준을 스스로 만들지 않는다.

### 결정성

- 씬은 정적이다. 시간으로 움직이는 것이 없고, 난수도 쓰지 않으며, dt 자체를 쓰지 않는다.
- 창의 프레임버퍼 크기가 고정이라 컴포지터의 타일링/크기 변경이 그림에 닿지 않는다.
- 남는 변수는 GPU와 드라이버의 래스터화뿐이다. 같은 기계의 두 실행은 픽셀 단위로 같아야 하며,
  그것을 이 작업의 검증으로 삼는다. 다른 기계에서는 합격 규칙의 허용치가 흡수하는 만큼만 보장한다.

### 검증

- `just visual-update-ref`로 기준을 만들고, `just visual`이 합격하는지 본다.
- 같은 캡처를 두 번 돌려 두 PNG가 바이트 단위로 같은지 본다 (결정성).
- 기준 그림을 한 픽셀 고친 복사본으로 비교해 러너가 실패를 보고하는지 본다 (음성 검사).
- 헤드리스 Linux 빌드와 웹 빌드가 여전히 빌드되는지 본다 (옵션 기본값이 꺼짐인 것의 확인).

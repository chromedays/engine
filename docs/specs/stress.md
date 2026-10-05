# Stress scene spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-09-27). 이 스펙의 변경은 먼저 합의한다.

### 목표

엔진에 많은 객체로 부하를 거는 샌드박스 안의 두 번째 씬. 수가 늘 때 프레임 시간이 어디로 가는지 재서, 다음에 무엇을 최적화할지
(컬링, 인스턴싱, 그리기 정렬) 알고 장치와 커밋을 비교할 수 있게 한다.

지금은 노드마다 draw call 하나이고, 아무것도 컬링하거나 인스턴싱하지 않으며, 매 프레임 객체 버퍼 전체를 올린다. 그중
어느 것도 아직 측정하지 않았다.

### 결정

| 주제 | 결정 |
|---|---|
| 어디 | 같은 샌드박스 안의 두 번째 씬. View 탭이 Showcase와 Stress 사이를 바꾼다. (URL의 `#stress`가 자동 저장 스펙이 그것을 없앨 때까지 그것을 바로 열었다; 샌드박스는 이제 항상 쇼케이스에서 시작한다) |
| 한도 | 모든 단계가 여전히 60 fps로 돈 첫 폰 결과 뒤에 올림: `NV_MAX_NODES` 16384 (전에 4096), `NV_MAX_ANIMATORS` 256 (전에 64), `NV_MAX_DEBUG_LINES` 16384 (전에 8192); `NV_MAX_MATERIALS`는 256으로 유지 |
| 벤치마크 | 버튼이 고정된 단계를 실행하고 복사할 수 있는 표를 보여 준다 |
| 시간 | 샌드박스에서 `emscripten_get_now`로 잰다; GPU 시간은 브라우저가 `timestamp-query`를 제공하는 곳에서만 |
| 서드파티 | 없음. Tracy (C++, BSD)를 프로파일러로 고려했지만 wasm 지원이 약하다 |

### 워크로드

각 워크로드에는 켜기/끄기 토글과 수 슬라이더가 있다. 슬라이더는 모든 워크로드를 합쳐 `NV_MAX_NODES` 아래에 머물도록
제한된다.

| 워크로드 | 만드는 것 | 최대 | 재는 것 |
|---|---|---|---|
| Cube grid | 정사각 그리드 위의 정적 큐브 N개 | 16000 | draw call, 씬 갱신, 객체 업로드 |
| Deep hierarchy | 각각 앞의 것의 자식인 작은 큐브 N개의 체인, 모두 회전 | 1000 | 트랜스폼 전파 |
| Crowd | 줄지어 선 캐릭터 N명, 각자의 클립과 시작 시간 | 200 | ozz 샘플링, 스킨 행렬 업로드, 스키닝 셰이더 |
| Material variety | 그리드가 머티리얼 하나 또는 최대 256개의 서로 다른 색을 쓴다 | 256 | bind group 전환 |
| Churn | 매 프레임 그리드 큐브 K개를 제거하고 다시 추가 | 256/프레임 | 프리 리스트, 세대 id |
| Bone overlay | 모든 crowd 캐릭터의 뼈 라인 | ~12800 라인 | 디버그 라인 |
| Fire effects | 살아 있는 파티클 N개 (0–4M; 작은 불씨 버스트로 유지), 초당 폭발 (0–200), 날아가는 미사일 (0–2,000; 궤적과 연기), 빔 (0–500), 초당 데칼 (0–2,000) | 위와 같음 | compute 파티클 패스, 선분과 데칼 그리기, bloom, fill rate (`vfx.md`) |

스트레스 씬에도 카메라, 태양, 지면이 있다. 큰 워크로드는 Scene 탭에서 접힌 채로 시작하는 그룹 노드 ("grid", "chain",
"crowd") 아래에 있다. 그렇지 않으면 트리가 매 프레임 수천 행을 그려 측정을 왜곡할 것이다. Scene 탭의 검색 상자는 그래도
그 안의 맞는 것을 나열한다 (최대 500행; `search.md`).

### 엔진 변경

- **애니메이터가 씬에 속한다.**
  - `nv_anim_create_animator(skeleton, scene, owner)`가 씬을 기록한다.
  - `nv_anim_update_scene(scene, dt)`가 그 씬의 애니메이터만 갱신하므로, 숨겨진 씬의 캐릭터는 다른 씬의 노드에서
    assert하지 않고 멈춘다.
- **불러온 모델을 인스턴스화.**
  - `nv_gltf_instantiate(const NvGltfModel* model, NvScene* scene, NvNodeId parent, NvGltfModel* out)`가 모델의 노드
    트리를 `parent` 아래 새 루트와 함께 `scene`에 복사한다.
  - 같은 메시, 머티리얼, 스켈레톤을 재사용하고 새 애니메이터를 만든다. 그것이 없으면 각 crowd 캐릭터가 자체 메시와
    텍스처를 불러와 `NV_MAX_MESHES`를 넘을 것이다.
- **그리기 통계.**
  - `NvRenderer.stats` (`NvRenderStats`: 그리기, 삼각형, skinned 그리기, 파이프라인과 bind group 전환)는 매 프레임
    `nv_renderer_draw`가 채운다.

### 샌드박스

- **씬:**
  - `Sandbox`이 쇼케이스와 스트레스 씬과 보이는 씬을 담는다.
  - Scene, Inspector, View 탭은 보이는 씬에서 동작한다. 각 씬은 자체 선택과 카메라를 유지한다.
  - 스트레스 씬은 처음 보일 때 만든다.
- **전환:**
  - View 탭의 콤보 상자가 씬을 바꾼다. (`save.md`가 없앨 때까지 시작할 때 읽는 `#stress` URL 해시도 설정했다.)
- **Stress 탭** (스트레스 씬이 보이는 동안 보임):
  - 워크로드 토글과 슬라이더;
  - 그 위의 검색 상자 (`search.md`): 통계, 워크로드 행, 벤치마크 그룹이 그것이 거르는 행이고, 팔레트도 그것을 찾는다;
  - 실시간 통계: FPS; 프레임 시간 (지난 1초의 평균과 최대); 애니메이션 갱신, 씬 갱신, 그리기 기록, ImGui의 CPU 시간;
    노드, 그리기, 삼각형, skinned 수; 가능하면 GPU 시간.
- **벤치마크:** "Run benchmark"가 고정된 구성을 차례로 돌며, 1초의 준비 뒤 각각 3초를 기록한다.
  - 큐브: 250, 1000, 4000, 8000, 16000, crowd 꺼짐.
  - 캐릭터: 8, 32, 60, 120, 200, 그리드 꺼짐.
  - 표는 부하도 보여 준다: CPU 단계 합계와 GPU 패스 중 큰 것을 프레임 시간에 대한 비율로. vsync가 프레임률을 붙잡는
    동안에도 계속 오른다.
  - 표는 단계마다 평균과 최악의 프레임 시간과 CPU 단계 시간을 보여 준다.
  - "Copy"는 브라우저의 user agent와 커밋 해시와 함께 표를 텍스트로 클립보드에 넣는다.
  - 워크로드 조작을 건드리면 실행이 멈춘다.

### 단계

1. **엔진:** 씬이 소유하는 애니메이터, `nv_gltf_instantiate`, 렌더 통계. 쇼케이스 씬이 전과 정확히 같게 동작하는지
   확인.
2. **씬:** 스트레스 씬, 그 워크로드, 씬 전환, `#stress`.
3. **측정:** Stress 탭의 통계, 벤치마크와 복사할 수 있는 표, 그다음 지원되는 곳에서 GPU 타임스탬프.

모든 단계는 헤드리스 Chromium에서 Release와 Debug로, 데스크톱과 폰 크기에서 확인한다:

- 모든 워크로드가 최대에서 assert 없음;
- 상태를 유지하며 씬을 왔다 갔다 전환;
- `#stress`가 바로 열림;
- 전체 벤치마크 실행이 끝남.

SwiftShader의 숫자는 의미가 없다; 실제 측정은 실제 장치에서 나온다.

## English

Status: implemented (2026-09-27). Changes to this spec are agreed first.

### Goal

A second scene in the sandbox that loads the engine with many objects. It measures where the frame
time goes as counts grow, so we know what to optimize next (culling, instancing, draw sorting)
and can compare devices and commits.

Today every node is one draw call, nothing is culled or instanced, and the whole object buffer is
uploaded every frame. None of that has been measured yet.

### Decisions

| Topic | Decision |
|---|---|
| Where | A second scene in the same sandbox. The View tab switches between Showcase and Stress. (`#stress` in the URL opened it directly until the autosave spec removed it; the sandbox now always starts on the showcase) |
| Limits | Raised after the first phone results, where every step still ran at 60 fps: `NV_MAX_NODES` 16384 (was 4096), `NV_MAX_ANIMATORS` 256 (was 64), `NV_MAX_DEBUG_LINES` 16384 (was 8192); `NV_MAX_MATERIALS` stays 256 |
| Benchmark | A button runs fixed steps and shows a table that can be copied |
| Timing | Measured in the sandbox with `emscripten_get_now`; GPU time only where the browser offers `timestamp-query` |
| Third-party | None. Tracy (C++, BSD) was considered as a profiler, but its wasm support is weak |

### Workloads

Each workload has an on/off toggle and a count slider. The sliders are clamped so that all
workloads together stay under `NV_MAX_NODES`.

| Workload | What it builds | Max | Measures |
|---|---|---|---|
| Cube grid | N static cubes on a square grid | 16000 | draw calls, scene update, object upload |
| Deep hierarchy | a chain of N small cubes, each the child of the last, all turning | 1000 | transform propagation |
| Crowd | N characters in rows, each on its own clip and start time | 200 | ozz sampling, skin matrix upload, skinning shader |
| Material variety | the grid uses one material, or up to 256 distinct colors | 256 | bind group switches |
| Churn | K grid cubes removed and added again every frame | 256/frame | free list, generation ids |
| Bone overlay | bone lines for every crowd character | ~12800 lines | debug lines |
| Fire effects | N live particles (0–4M, held by embers fired in bursts), explosions per second (0–200), missiles in flight (0–2,000; trails and smoke), beams (0–500), decals per second (0–2,000) | as listed | the compute particle passes, drawing segments and decals, bloom, fill rate (`vfx.md`) |

The stress scene also has a camera, a sun and a ground. Big workloads sit under group nodes
("grid", "chain", "crowd") that start collapsed in the Scene tab. Otherwise the tree would draw
thousands of rows every frame and distort the measurements. The Scene tab's search box lists a
match inside them anyway (at most 500 rows; `search.md`).

### Engine changes

- **Animators belong to a scene.**
  - `nv_anim_create_animator(skeleton, scene, owner)` records the scene.
  - `nv_anim_update_scene(scene, dt)` updates only that scene's animators, so the hidden scene's
    characters pause instead of asserting on a node from the other scene.
- **Instantiating a loaded model.**
  - `nv_gltf_instantiate(const NvGltfModel* model, NvScene* scene, NvNodeId parent, NvGltfModel* out)`
    copies the model's node tree into `scene` with a new root under `parent`.
  - It reuses the same meshes, materials and skeleton, and creates a new animator. Without it,
    each crowd character would load its own meshes and textures and run past `NV_MAX_MESHES`.
- **Draw statistics.**
  - `NvRenderer.stats` (`NvRenderStats`: draws, triangles, skinned draws, pipeline and bind group
    changes) is filled by `nv_renderer_draw` every frame.

### Sandbox

- **Scenes:**
  - `Sandbox` holds the showcase and stress scenes and the one that is shown.
  - The Scene, Inspector and View tabs work on the shown scene. Each scene keeps its own
    selection and camera.
  - The stress scene is built the first time it is shown.
- **Switching:**
  - A combo box in the View tab switches scenes. (It also set a `#stress` URL hash, read at
    startup, until `save.md` removed it.)
- **Stress tab** (shown while the stress scene is):
  - the workload toggles and sliders;
  - a search box above them (`search.md`): the stats, the workload rows and the benchmark group are
    rows it filters, and the palette finds them too;
  - live stats: FPS; frame time (average and max over the last second); CPU time for animation
    update, scene update, draw recording and ImGui; node, draw, triangle and skinned counts;
    GPU time when available.
- **Benchmark:** "Run benchmark" steps through fixed setups and records 3 seconds of each, after
  1 second of warm-up.
  - Cubes: 250, 1000, 4000, 8000 and 16000, the crowd off.
  - Characters: 8, 32, 60, 120 and 200, the grid off.
  - The table also shows the load: the larger of the CPU stage total and the GPU pass, as a
    share of the frame time. It keeps rising while vsync holds the frame rate.
  - The table shows average and worst frame time and the CPU stage times per step.
  - "Copy" puts the table on the clipboard as text, with the browser's user agent and the commit
    hash.
  - Touching the workload controls stops a run.

### Phases

1. **Engine:** scene-owned animators, `nv_gltf_instantiate` and render stats. Check that the
   showcase scene behaves exactly as before.
2. **Scene:** the stress scene, its workloads, scene switching and `#stress`.
3. **Measurement:** the stats in the Stress tab, the benchmark and the copyable table, then GPU
   timestamps where supported.

Every phase is checked in Release and Debug in headless Chromium at desktop and phone size:

- every workload at its maximum without asserts;
- switching scenes back and forth with state kept;
- `#stress` opening directly;
- a full benchmark run finishing.

The numbers from SwiftShader are not meaningful; real measurements come from real devices.

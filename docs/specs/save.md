# Autosave spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-09-28). 이 스펙의 변경은 먼저 합의한다.

### 목표

탭을 닫고 돌아와도 앱이 떠난 그대로 있게 한다. 앱은 자기 상태 (쇼케이스 씬과 에디터 설정)를 저장 하나로, 스스로,
Emscripten의 IDBFS를 통해 브라우저의 IndexedDB에 저장한다; 잊어버릴 저장 버튼이 없다.

스트레스 씬은 저장하지 않는다: 벤치마크이고, 매 방문마다 기본값으로 시작하므로 모든 실행이 같은 곳에서 시작한다.

지금은 범위 밖: 파일로의 수동 저장 (내보내기와 가져오기)과 서버 저장소 (Worker 뒤의 Cloudflare R2 같은 CDN). 둘 다
이 스펙의 저장 바이트를 바꾸지 않고 실어 나를 수 있다.

### IDBFS의 동작 방식

- Emscripten의 파일 시스템은 JavaScript 메모리 (MEMFS)에 산다. 앱은 이미 `fopen`으로 거기서 `/assets/...`를 읽고,
  페이지가 닫히면 사라진다.
- IDBFS는 디렉터리 하나가 IndexedDB 데이터베이스와 짝지어진 MEMFS다. 거기 파일도 여전히 메모리에서 읽고 쓰므로
  `fopen`과 `fwrite`가 그대로 동작한다.
- 앱이 요청하기 전까지는 아무것도 IndexedDB에 닿지 않는다. `FS.syncfs(true)`는 시작할 때 한 번 IndexedDB를 메모리로
  복사한다. `FS.syncfs(false)`는 저장 뒤 메모리를 IndexedDB로 복사한다 (타임스탬프가 바뀐 파일만). 둘 다 비동기이고
  끝나면 콜백한다.
- 데이터베이스는 페이지의 origin `chromedays.github.io`에 속하고, 마운트 경로를 따라 이름이 붙는다.

### 형식 후보

| 후보 | 라이브러리 (라이선스) | 장점 | 단점 |
|---|---|---|---|
| **자체 태그 바이너리** (선택) | 없음 | 의존성 없음. 모르는 태그는 건너뛰므로 옛 빌드와 새 빌드가 서로의 저장을 읽는다. float가 정확히 저장된다. 코딩 표준에 맞는다 | 사람이 읽을 수 없어서 뷰어가 필요하다 (아래) |
| JSON | jsmn (MIT)과 자체 writer | 사람이 읽을 수 있다 | 텍스트 파싱, 텍스트를 거치는 float |
| MessagePack, CBOR | mpack (MIT), tinycbor (MIT) | 바이너리 JSON, 표준 | 의존성이고, 도구 없이는 여전히 읽을 수 없다 |
| Protocol Buffers, FlatBuffers | nanopb (zlib), flatcc (Apache-2.0) | 스키마 | 빌드에 스키마 컴파일러; 몇 킬로바이트 저장에 필요한 것보다 훨씬 많다 |
| 구조체 그대로 덤프 | 없음 | 가장 단순 | 구조체가 바뀔 때마다 깨진다 |

### 저장 형식

저장은 청크의 트리다. 모든 청크는 태그, 크기, 페이로드다:

```
u32 tag     four ASCII characters, e.g. 'NODE' (NV_TAG('N','O','D','E'))
u32 size    payload bytes, not counting these 8
u8  payload[size]
```

- **정수와 float는 리틀 엔디언**이고, 바이트 단위로 쓰고 읽으므로 형식이 기계에 의존하지 않는다 (어차피 wasm은 리틀
  엔디언이다).
- **컨테이너와 필드.** 컨테이너의 페이로드는 더 많은 청크다 (`NODE`는 `NAME`, `POS `, ...를 담는다). 필드의 페이로드는
  값이다: `u32`나 `f32` 배열, 또는 문자열 (그 바이트, NUL 없음; 크기가 길이를 준다).
- **순서.** 필드는 어떤 순서로든 올 수 있고, 스펙이 그렇다고 하는 곳에서만 반복된다 (`SCNE` 안의 `NODE`). 리더는
  태그로 필드를 찾는다.
- **모르는 태그는** 크기만큼 **건너뛴다**, 그래서 옛 빌드가 새 저장을 읽고 모르는 것은 무시한다.
- **없는 필드는 앱이 시작할 때의 값을 유지한다**, 그래서 옛 빌드의 저장은 가진 것을 불러오고 나머지는 첫 방문처럼
  둔다 (아래 "없음" 열). 리더 자체는 없는 필드를 0으로 읽고 (ZII) 없다고 알린다.
- **타입에 맞지 않는 크기의 필드** (12바이트가 아닌 `POS `)는 저장을 나쁘게 만든다 (아래 참고): 손상이나 버그에서만
  생길 수 있다.
- **버전.** 헤더의 버전은 기존 태그의 뜻이 바뀔 때만 오른다. 태그를 추가하는 것은 버전을 올리지 않는다. 빌드는
  자신이 아는 것보다 새 버전의 저장을 거부한다.

#### 파일 배치

```
Header (16 bytes)
  u32 magic     'NVSV'
  u32 version   1
  u32 size      bytes after the header
  u32 checksum  CRC-32 of the bytes after the header
Chunks
  'EDIT'  editor settings
  'SCNE'  the scene (the app's showcase)
```

체크섬과 크기가 짧게 잘렸거나 손상된 저장을 잡는다. 모든 태그는 `app/save.c`의 한 곳에 있다.

### 저장되는 것

저장은 사용자가 바꿀 수 있고 다시 찾기를 기대할 모든 것을 담고, 다시 만들거나, 측정하거나, 순간적인 것은 담지 않는다.
표가 모든 태그를 나열한다. "없음"은 태그가 없을 때 (옛 저장) 불러오기가 하는 일이다: 지금 앱이 시작할 때의 값.

#### 쇼케이스 노드를 저장하는 방법

에디터는 노드를 편집할 수 있지만 추가하거나 제거할 수 없고, 쇼케이스는 항상 같은 방식으로 만들어진다. 그래서 불러오기는
**지금처럼 쇼케이스를 만든 다음 저장된 값을 그 노드에 적용한다**; 노드는 결코 저장에서 만들어지지 않는다.

- 노드는 **경로**로 식별한다: 맨 위 수준부터 아래로, 형제 중 각 노드의 인덱스 (`[3]`은 네 번째 최상위 노드, `[5, 0]`은
  여섯 번째의 첫 자식). Inspector가 노드 이름을 바꿀 수 있으므로 이름은 쓰지 않는다.
- `SCNE`는 **배치 번호**도 저장한다: 쇼케이스를 만든 직후 얻은 기본 트리의 이름과 모양의 해시. 나중 빌드가 쇼케이스를
  바꿔 번호가 달라지면, 저장된 노드는 건너뛰고 (경로가 다른 노드를 가리킬 수 있다) 나머지는 여전히 불러온다.
- 존재하지 않는 저장된 경로는 건너뛴다.

불러오기가 노드를 결코 대신하지 않으므로, 앱의 참조 (`app->planet`, `app->moon`, `app->sword`, 캐릭터)가 유효하게
남는다. 에디터가 노드를 추가하고 제거하게 되면 새 태그가 만들어진 노드를 설명할 것이다; 옛 빌드는 그것을 건너뛸
것이다.

#### `EDIT`: 에디터 설정

| 태그 | 타입 | 저장하는 값 | 없음 |
|---|---|---|---|
| `AUTO` | u32 | 자동 저장 켜짐 (View 탭 체크박스) | 1 |
| `GZOP` | u32 | `app->gizmo_operation`: 0 이동, 1 회전, 2 크기 | 0 |
| `GZLC` | u32 | `app->gizmo_local` | 0 |
| `GZSN` | u32 | `app->gizmo_snap` | 0 |
| `SHSZ` | u32 | 그림자 맵 크기: 512, 1024, 2048; 다른 값은 꺼짐 (0) | 시작 값 (2048, 터치 화면에서 1024) |
| `SHFM` | u32 | 그림자 맵 형식: 0 `depth32float`, 1 `depth16unorm` | 0 |
| `SHFL` | u32 | 그림자 필터: 0 Low, 1 High | 시작 값 (High, 터치 화면에서 Low) |
| `SHDS` | f32 | 그림자 거리, 미터, 불러올 때 5..100로 제한 | 30 |
| `SHBX` | u32 | 빛 상자 표시 | 0 |
| `MSAA` | u32 | 안티앨리어싱: 픽셀당 샘플, 1 (꺼짐) 또는 4 (`msaa.md`); 다른 값은 4로 불러옴 | 4 |
| `RSMD` | u32 | 해상도 모드: 0 scale, 1 fixed (`resolution.md`); 다른 값은 0으로 불러옴 | 0 |
| `RSCL` | u32 | scale 모드의 제수, 1–4; 다른 값은 장치 기본값으로 불러옴 | 데스크톱 UI에서 1, 폰 UI에서 2 |
| `RSFT` | u32 | fixed 모드의 맞춤: 0 정수 배, 1 뷰포트에 맞춤, 2 늘림; 다른 값은 0으로 불러옴 | 0 |
| `RSFW`, `RSFH` | u32 | fixed 모드의 폭과 높이, 각각 16–4096; 다른 값은 1280 × 720으로 불러옴 | 1280, 720 |
| `DKLW` | u32 | 데스크톱 UI: 왼쪽 도크의 원하는 폭, CSS 픽셀, 불러올 때 160..640로 제한 (`layout.md`) | 260 |
| `DKRW` | u32 | 오른쪽 도크의 원하는 폭, 220..640로 제한 | 340 |
| `DKBH` | u32 | 하단 도크의 원하는 높이, 120..600로 제한 | 220 |
| `DKBO` | u32 | 하단 도크가 내용을 보임 (1) 또는 탭 줄만 보임 (0) | 1 |
| `LANG` | u32 | UI 언어: 0 영어, 1 한국어 (`korean.md`); 다른 값은 0으로 불러옴 | 브라우저 언어 ("ko"로 시작하면 한국어) |

#### `VIEW`: 쇼케이스의 카메라와 선택

| 태그 | 타입 | 저장하는 값 | 없음 |
|---|---|---|---|
| `YAW ` | f32 | `SceneView.camera_yaw`, 라디안 | 씬의 시작 값 |
| `PTCH` | f32 | `camera_pitch`, 라디안, 불러올 때 -10°..80°로 제한 | 시작 값 |
| `DIST` | f32 | `camera_distance`, 미터, 불러올 때 1..100로 제한 | 시작 값 |
| `FOLW` | u32 | `follow_selection` | 0 |
| `ORBT` | f32[3] | `orbit_point` (따라가지 않는 동안 카메라가 보는 곳) | 시작 값 |
| `PAN ` | f32[3] | `pan` (따라가는 선택으로부터의 오프셋) | (0, 0, 0) |
| `SELN` | u32[] | 선택된 노드의 경로; 비어 있으면 선택 없음 | 시작 선택 |

`pan`은 복원된 선택에 속하는 것으로 복원된다 (`panned_for`). 뷰의 `focus`와 `camera`는 빌드가 정하므로 저장하지 않는다.

#### `SCNE`: 씬

저장은 쇼케이스를 단순히 "씬"이라고 부른다: 저장이 담는 유일한 씬이다.

| 태그 | 타입 | 저장하는 값 | 없음 |
|---|---|---|---|
| `LAYT` | u32 | 배치 번호 (위) | 노드를 건너뜀 |
| `VIEW` | container | 쇼케이스의 뷰 | 시작 뷰 |
| `PLNT` | f32[2] | `orbit_speed` (rad/s)와 `orbit_angle` (rad). Edit와 Play 모드 (`play.md`) 이후로 각도는 0으로 쓴다: 회전은 재생 중에만, 0부터, 행성의 지정한 회전 위에서 돈다 | 0.7, 0 |
| `BONE` | u32 | `app->show_bones` | 0 |
| `CHAR` | container | 캐릭터 (아래) | 만든 그대로 |
| `NODE` | container, 반복 | 노드마다 하나, 트리 순서로 (아래) | 만든 그대로 |

**`CHAR`: 캐릭터의 재생과 조작**

| 태그 | 타입 | 저장하는 값 | 없음 |
|---|---|---|---|
| `CLIP` | string | 레이어 0에서 재생 중인 클립의 이름, 루트 모션 사본 제외 (`app_regular_clip`). 점프 중에는 점프가 돌아갈 클립 | `Idle_Loop` |
| `CTIM` | f32 | 그 클립 안에서 레이어 0의 시간, 초. Edit와 Play 모드 (`play.md`) 이후로 더 이상 쓰지 않는다: 실행은 모든 클립을 처음부터 시작한다. 그것이 있는 옛 저장도 여전히 불러온다 | 0 |
| `SPED` | f32 | 레이어 0의 속도 | 1 |
| `FADE` | f32 | `fade_seconds` | 0.3 |
| `BLND` | string | 블렌드 클립의 이름 (`clips[blend_clip]`) | 첫 클립 |
| `BLDW` | f32 | `blend_weight` | 0 |
| `RMOT` | u32 | `root_motion` | 0 |
| `TURN` | f32 | `turn_rate`, rad/s | 0.6 |
| `LOOK` | u32 | `look_at` | 만든 그대로 |
| `SWRD` | u32 | `show_sword` | 1 |

불러올 때 클립은 페이드 없이 `CTIM`에서 `app_play`로 시작한다 (그래서 루트 모션이 그 사본을 고른다). 빌드에 없는 클립
이름은 `Idle_Loop`로 돌아간다.

**`NODE`: 노드 하나의 편집 가능한 값**

| 태그 | 타입 | 저장하는 값 | 없음 |
|---|---|---|---|
| `PATH` | u32[] | 노드의 경로 | 노드를 건너뜀 |
| `NAME` | string | `NvNode.name` (최대 31바이트) | 만든 그대로 |
| `POS ` | f32[3] | `position` | 만든 그대로 |
| `ROT ` | f32[4] | `rotation` (x, y, z, w), 불러올 때 정규화 | 만든 그대로 |
| `SCL ` | f32[3] | `scale` | 만든 그대로 |
| `COLR` | f32[4] | 메시가 있는 노드의 머티리얼 base color | 만든 그대로 |
| `ATCH` | string | 부착된 노드가 따라가는 관절, 이름으로 | 만든 그대로 |
| `CFOV` | f32 | 카메라 노드의 `camera.fov_y`, 라디안 | 만든 그대로 |
| `LCOL` | f32[3] | 빛 노드의 `light.color` | 만든 그대로 |
| `LINT` | f32 | 빛 노드의 `light.intensity` | 만든 그대로 |

캐릭터의 메시 노드와 look 타깃을 포함해 모든 노드를 저장한다. 저장은 항상 편집 상태를 담는다 (`play.md`): 재생 중에는
자동 저장이 Play에서 찍은 스냅숏을 쓴다. 공전 카메라의 트랜스폼은 저장되지만 남는 효과가 없다 (`VIEW`에서 온다).
스켈레톤에 없는 관절 이름은 부착을 만든 그대로 둔다.

#### 저장하지 않는 것

- 스트레스 씬: 그 설정, 카메라, 선택, 노드 편집, 벤치마크 결과. 앱은 항상 쇼케이스에서 시작한다.
- 프레임 시간과 통계.
- 진행 중인 점프 (돌아갈 클립은 저장된다), 진행 중인 크로스페이드, 블렌드 레이어 자체의 시간.
- 패널 상태: 열린 탭, 스크롤 위치, 어떤 트리 노드가 열려 있는지.
- 시작할 때 다시 만드는 모든 것: 메시, 색 외의 머티리얼, 스켈레톤, 클립, GPU와 ImGui 리소스.

### 결정

| 주제 | 결정 |
|---|---|
| 저장소 | `/nv-save`에 마운트한 IDBFS를 통한 IndexedDB. 파일 하나 `state.nvs`가 앱 상태 전체를 담는다 |
| 언제 저장하나 | 바이트가 마지막 저장과 다를 때 10초마다, 그리고 페이지가 숨겨질 때 (`visibilitychange`). 숨겨짐이 폰에서 마지막으로 믿을 만한 순간이다; `beforeunload`는 아니다. 쓰기는 먼저 임시 이름으로 간 다음 이름을 바꾸므로, 반쯤 쓴 저장이 결코 좋은 것을 대신하지 않는다 |
| 시작할 때 | 앱은 지금처럼 쇼케이스를 만들고, 저장이 있으면 첫 프레임 전에 불러온다: 쇼케이스와 에디터 설정. 항상 쇼케이스에서 시작한다 |
| 나쁜 저장 | 잘못된 magic, 크기, 체크섬, 잘못된 크기의 필드, 또는 새 버전: 파일 이름을 `state.nvs.bad`로 바꾸고 앱은 첫 방문처럼 시작한다. View 탭의 메시지가 그렇다고 말한다. 불러오기는 전부 아니면 전무다: 앱의 어떤 것이 바뀌기 전에 파일 전체를 확인한다 |
| `#stress` | 제거. 주소가 더 이상 씬을 가리키지 않고, 앱은 항상 쇼케이스에서 시작한다 |
| UI | View 탭의 **Autosave** 섹션: 켜기/끄기 체크박스 (기본 켜짐, 그 자체도 저장됨; 끄면 선택이 남도록 한 번 쓴다), 마지막으로 저장한 때, **Save now**, **Reset** (먼저 묻고, 저장과 `.bad` 파일을 지우고 IndexedDB가 따라잡으면 페이지를 새로고침해 첫 방문처럼 시작한다; 애니메이터는 제거할 수 없어서 제자리에서 다시 만들면 누수가 생긴다; 새로고침하는 동안 아무것도 저장하지 않는다), **Show save** (아래) |
| 저장 뷰어 | **Show save**는 저장의 청크 트리를 연다: 헤더 (magic, 버전, 크기, 체크섬과 그것이 맞는지), 그다음 태그, 크기, 값 (숫자, 문자열, 모르는 태그는 16진수; 노드는 이름으로 라벨). 로더처럼 파일을 읽고, 손상된 파일은 갈 수 있는 데까지 걸으므로, 나쁜 저장이 어디서 깨지는지 보여 준다. `.bad` 파일로 바꿀 수 있다 |
| 지속성 | 앱은 `navigator.storage.persist()`를 요청하므로, 브라우저가 저장 공간 압박에서 데이터를 지우지 않는다 |
| 서드파티 | 없음 |

### 엔진 변경

- **청크 (`nv/chunk.h`, `engine/src/chunk.c`).**
  - `NV_TAG(a, b, c, d)`가 태그를 만든다.
  - `NvChunkWriter`는 아레나 버퍼에 덧붙인다: `nv_chunk_begin(tag)`과 `nv_chunk_end` (크기를 채워 넣음), 그리고 필드:
    `nv_chunk_u32s`, `nv_chunk_f32s`, `nv_chunk_string`.
  - `NvChunkReader`는 경계 검사와 함께 바이트 범위를 걷는다: 다음 자식, 태그로 자식 찾기, 필드를 `u32`나 `f32` 배열
    또는 문자열로 읽기. 잘못된 크기의 필드는 리더를 실패로 표시한다; 실패 뒤에는 모든 읽기가 0을 돌려주므로, 로더는
    끝에서 한 번 확인한다.
  - `nv_crc32`, 그리고 헤더 도우미.
- **저장소 (`nv/storage.h`, `engine/src/storage.c`).**
  - `nv_storage_init`은 `/nv-save`에 IDBFS를 마운트하고, 영구 저장소를 요청하고, IndexedDB로부터의 첫 동기화를
    기다린다 (`wgpuInstanceWaitAny`처럼 Asyncify를 통해).
  - `nv_storage_write(name, bytes, size)`는 임시 파일과 이름 바꾸기를 거쳐 쓴다.
  - `nv_storage_flush`는 기다리지 않고 IndexedDB로의 동기화를 시작한다. 하나가 도는 동안 요청한 flush는 그것이 끝나면
    시작한다.
  - `NvStorage`는 저장소가 사용 가능한지 (비공개 창은 IndexedDB를 거부할 수 있다)와 마지막 동기화 오류를 알린다.
    저장소가 없으면 앱은 지금처럼 돌고 아무것도 저장되지 않는다고 말한다. `nv_storage_init`은 마운트 전에 IndexedDB를
    확인한다: Emscripten의 IDBFS는 그것이 없으면 런타임 전체를 중단시킨다.
  - `nv_storage_flush_then_reload`는 호출을 덮는 flush 뒤에 페이지를 새로고침한다 (Reset).
- **창 (`nv/window.h`).** `NvWindow`가 페이지가 숨겨질 때를 알리므로, 앱이 그때 저장할 수 있다.
- **링크 플래그.** `-lidbfs.js`.

### 앱 변경

- `app/save.c`가 청크 도우미로 앱 상태 전체를 쓰고 읽는다. 불러오기는 같은 코드로 파일 전체를 두 번 읽는다: 확인만
  하는 시험 실행, 그다음 통과하면 쇼케이스의 노드, 캐릭터, 쇼케이스의 뷰, 에디터 설정에 값을 적용하는 패스. 두 패스가
  같은 필드를 읽으므로 두 번째는 중간에 실패할 수 없다.
- 불러오기는 이미 단위 길이인 회전을 그대로 둔다 (다시 정규화하면 마지막 비트가 바뀐다), 그래서 저장, 불러오기, 다시
  저장이 같은 바이트를 준다. Debug 빌드는 시작할 때 그것을 확인하고, 테스트는 편집 뒤 `Module._app_debug_save_round_trip()`을
  부른다.
- 자동 저장 타이머, 페이지 숨김 저장, 시작할 때 불러오기, 저장 뷰어가 있는 View 탭의 Autosave 섹션.
- 쇼케이스를 만든 뒤 계산하는 배치 번호.
- `#stress`가 사라진다: `js_hash_is_stress`, `js_set_hash`, 그리고 `AGENTS.md`, README, `docs/specs/stress.md`의 언급.

### 한계

- origin은 공유된다. Release와 Debug 빌드는 같은 저장을 쓰고, `chromedays.github.io`의 다른 모든 프로젝트도 마찬가지라서
  마운트가 독특한 이름을 가진다. Release와 Debug는 같은 형식을 쓰므로 어느 쪽이든 다른 쪽의 저장을 읽는다.
- 브라우저가 여전히 지울 수 있다: iOS Safari는 방문 없이 7일 뒤에, 비공개 창은 닫힐 때. 자동 저장은 편의이지 보관소가
  아니다.
- 저장은 최대 256 KB다 (`SAVE_MAX_SIZE`, 쓸 때 확인); 쇼케이스는 약 2 KB다.

### 단계

1. **형식:** `nv/chunk.h`, 그리고 상태 전체를 메모리 안의 바이트로 저장하고 불러오기. 왕복을 확인한다: 저장, 불러오기,
   다시 저장, 바이트 비교. Node용으로 빌드해 `ctest` (CI에서도)가 실행하는 `tests/chunk_test.c`가 리더의 경계 검사를
   다룬다: 모든 잘림, 모든 뒤집힌 비트, 너무 많이 주장하는 청크, 잘못된 필드 크기, 공간이 떨어진 writer.
2. **자동 저장:** IDBFS, `nv_storage_*`, 타이머와 페이지 숨김 저장, 시작할 때 불러오기, View 탭의 Autosave 섹션,
   `#stress` 제거.
3. **실패 사례, 뷰어, 문서:** 나쁜, 손상된, 새 버전의 저장, 사용할 수 없는 저장소, Reset, 저장 뷰어, `AGENTS.md`,
   README, `docs/specs/stress.md`.

모든 단계는 헤드리스 Chromium에서 Release와 Debug로 확인한다. 2단계는 쇼케이스를 편집하고, 페이지를 새로고침하고,
편집이 돌아왔는지, 그리고 스트레스 씬이 여전히 기본값으로 열리는지 확인한다. 3단계는 잘린 저장, 바이트 하나가 뒤집힌
저장, 새 버전의 저장을 쓰고, 앱이 첫 방문처럼 시작하고 `.bad` 파일을 유지하는지 확인한다.

## English

Status: implemented (2026-09-28). Changes to this spec are agreed first.

### Goal

Close the tab, come back, and find the app as it was left. The app saves its state (the showcase
scene and the editor settings) as one save, by itself, into the browser's IndexedDB through
Emscripten's IDBFS; there is no save button to forget.

The stress scene is not saved: it is a benchmark, and it starts from its defaults on every visit,
so every run starts from the same place.

Out of scope for now: manual saves as files (export and import) and server storage (a CDN such as
Cloudflare R2 behind a Worker). Both could carry this spec's save bytes unchanged.

### How IDBFS works

- Emscripten's file system lives in JavaScript memory (MEMFS). The app already reads
  `/assets/...` from it with `fopen`, and it is gone when the page closes.
- IDBFS is MEMFS with one directory paired to an IndexedDB database. Files there are still read and
  written in memory, so `fopen` and `fwrite` work unchanged.
- Nothing reaches IndexedDB until the app asks. `FS.syncfs(true)` copies IndexedDB into memory, once
  at start. `FS.syncfs(false)` copies memory into IndexedDB (only files whose timestamps changed)
  after a save. Both are asynchronous and call back when done.
- The database belongs to the page's origin, `chromedays.github.io`, and is named after the mount
  path.

### Format candidates

| Candidate | Library (license) | Pros | Cons |
|---|---|---|---|
| **Our own tagged binary** (chosen) | None | No dependency. Unknown tags are skipped, so old and new builds read each other's saves. Floats are stored exactly. Fits the coding standard | Not human-readable, so it needs a viewer (below) |
| JSON | jsmn (MIT) and our own writer | Human-readable | Parsing text, and floats through text |
| MessagePack, CBOR | mpack (MIT), tinycbor (MIT) | Binary JSON, standard | A dependency, and still unreadable without a tool |
| Protocol Buffers, FlatBuffers | nanopb (zlib), flatcc (Apache-2.0) | Schemas | A schema compiler in the build; far more than a few-kilobyte save needs |
| A raw struct dump | None | Simplest | Breaks whenever a struct changes |

### The save format

The save is a tree of chunks. Every chunk is a tag, a size and a payload:

```
u32 tag     four ASCII characters, e.g. 'NODE' (NV_TAG('N','O','D','E'))
u32 size    payload bytes, not counting these 8
u8  payload[size]
```

- **Integers and floats are little-endian**, written and read byte by byte, so the format does not
  depend on the machine (wasm is little-endian anyway).
- **Containers and fields.** A container's payload is more chunks (`NODE` holds `NAME`, `POS `,
  ...). A field's payload is a value: `u32` or `f32` arrays, or a string (its bytes, no NUL; the
  size gives its length).
- **Order.** Fields can come in any order, and repeat only where the spec says so (`NODE` in
  `SCNE`). A reader finds fields by tag.
- **Unknown tags are skipped** by their size, so an older build reads a newer save and ignores what
  it does not know.
- **A missing field keeps the value the app starts with**, so a save from an older build loads
  what it has and leaves the rest as on a first visit (the "Missing" columns below). The reader
  itself reads a missing field as zero (ZII) and reports it as missing.
- **A field whose size is wrong** for its type (a `POS ` that is not 12 bytes) makes the save bad
  (see below): it can only come from corruption or a bug.
- **Version.** The header's version rises only when the meaning of an existing tag changes. Adding a
  tag does not raise it. A build refuses a save with a newer version than it knows.

#### File layout

```
Header (16 bytes)
  u32 magic     'NVSV'
  u32 version   1
  u32 size      bytes after the header
  u32 checksum  CRC-32 of the bytes after the header
Chunks
  'EDIT'  editor settings
  'SCNE'  the scene (the app's showcase)
```

The checksum and the size catch a save that was cut short or damaged. Every tag lives in one place
in `app/save.c`.

### What is saved

The save holds everything the user can change and would expect to find again, and nothing that
is rebuilt, measured or momentary. The tables list every tag. "Missing" is what a load does when
the tag is absent (an older save): the value the app starts with today.

#### How showcase nodes are saved

The editor can edit nodes but cannot add or remove them, and the showcase is always built the same
way. So a load **builds the showcase as today and then applies the saved values to its nodes**;
nodes are never created from the save.

- A node is identified by its **path**: the index of each node among its siblings, from the top
  level down (`[3]` is the fourth top-level node, `[5, 0]` the first child of the sixth). Names
  are not used because the Inspector can rename nodes.
- `SCNE` also saves a **layout number**: a hash of the default tree's names and shape, taken right
  after the showcase is built. If a later build changes the showcase so the number differs, the
  saved nodes are skipped (their paths might point at other nodes) and the rest still loads.
- A saved path that does not exist is skipped.

This keeps the app's references (`app->planet`, `app->moon`, `app->sword`, the character) valid,
since loading never replaces nodes. When the editor learns to add and remove nodes, new tags will
describe created nodes; older builds will skip them.

#### `EDIT`: editor settings

| Tag | Type | Saved from | Missing |
|---|---|---|---|
| `AUTO` | u32 | autosave on (the View tab checkbox) | 1 |
| `GZOP` | u32 | `app->gizmo_operation`: 0 move, 1 rotate, 2 scale | 0 |
| `GZLC` | u32 | `app->gizmo_local` | 0 |
| `GZSN` | u32 | `app->gizmo_snap` | 0 |
| `SHSZ` | u32 | shadow map size: 512, 1024, 2048; anything else is off (0) | the start value (2048, 1024 on touch screens) |
| `SHFM` | u32 | shadow map format: 0 `depth32float`, 1 `depth16unorm` | 0 |
| `SHFL` | u32 | shadow filter: 0 Low, 1 High | the start value (High, Low on touch screens) |
| `SHDS` | f32 | shadow distance, meters, clamped to 5..100 on load | 30 |
| `SHBX` | u32 | show the light box | 0 |
| `MSAA` | u32 | anti-aliasing: samples per pixel, 1 (off) or 4 (`msaa.md`); anything else loads as 4 | 4 |
| `RSMD` | u32 | resolution mode: 0 scale, 1 fixed (`resolution.md`); anything else loads as 0 | 0 |
| `RSCL` | u32 | scale mode's divisor, 1 to 4; anything else loads as the device's default | 1 on the desktop UI, 2 on the phone UI |
| `RSFT` | u32 | fixed mode's fit: 0 whole multiples, 1 fit to the viewport, 2 stretch; anything else loads as 0 | 0 |
| `RSFW`, `RSFH` | u32 | fixed mode's width and height, 16 to 4096 each; anything else loads as 1280 × 720 | 1280, 720 |
| `DKLW` | u32 | desktop UI: the left dock's wanted width, CSS pixels, clamped to 160..640 on load (`layout.md`) | 260 |
| `DKRW` | u32 | the right dock's wanted width, clamped to 220..640 | 340 |
| `DKBH` | u32 | the bottom dock's wanted height, clamped to 120..600 | 220 |
| `DKBO` | u32 | the bottom dock shows its contents (1) or only its strip (0) | 1 |
| `LANG` | u32 | the UI's language: 0 English, 1 Korean (`korean.md`); anything else loads as 0 | the browser's language (Korean if it starts with "ko") |

#### `VIEW`: the showcase's camera and selection

| Tag | Type | Saved from | Missing |
|---|---|---|---|
| `YAW ` | f32 | `SceneView.camera_yaw`, radians | the scene's start value |
| `PTCH` | f32 | `camera_pitch`, radians, clamped to -10°..80° on load | start value |
| `DIST` | f32 | `camera_distance`, meters, clamped to 1..100 on load | start value |
| `FOLW` | u32 | `follow_selection` | 0 |
| `ORBT` | f32[3] | `orbit_point` (where the camera looks while not following) | start value |
| `PAN ` | f32[3] | `pan` (offset from the followed selection) | (0, 0, 0) |
| `SELN` | u32[] | the selected node's path; empty = nothing selected | the start selection |

`pan` is restored as belonging to the restored selection (`panned_for`). The view's `focus` and
`camera` are fixed by the build and not saved.

#### `SCNE`: the scene

The save calls the showcase simply "the scene": it is the one scene the save holds.

| Tag | Type | Saved from | Missing |
|---|---|---|---|
| `LAYT` | u32 | the layout number (above) | nodes are skipped |
| `VIEW` | container | the showcase's view | start view |
| `PLNT` | f32[2] | `orbit_speed` (rad/s) and `orbit_angle` (rad). Since Edit and Play modes (`play.md`) the angle is written as 0: the spin only runs while playing, from 0, on top of the planet's authored rotation | 0.7, 0 |
| `BONE` | u32 | `app->show_bones` | 0 |
| `CHAR` | container | the character (below) | as built |
| `NODE` | container, repeated | one per node, in tree order (below) | as built |

**`CHAR`: the character's playback and controls**

| Tag | Type | Saved from | Missing |
|---|---|---|---|
| `CLIP` | string | the name of the clip playing on layer 0, without its root-motion copy (`app_regular_clip`). During a jump, the clip the jump returns to | `Idle_Loop` |
| `CTIM` | f32 | layer 0's time into that clip, seconds. No longer written since Edit and Play modes (`play.md`): a run starts every clip from its start. Older saves that have it still load | 0 |
| `SPED` | f32 | layer 0's speed | 1 |
| `FADE` | f32 | `fade_seconds` | 0.3 |
| `BLND` | string | the blend clip's name (`clips[blend_clip]`) | the first clip |
| `BLDW` | f32 | `blend_weight` | 0 |
| `RMOT` | u32 | `root_motion` | 0 |
| `TURN` | f32 | `turn_rate`, rad/s | 0.6 |
| `LOOK` | u32 | `look_at` | as built |
| `SWRD` | u32 | `show_sword` | 1 |

On load the clip starts with `app_play` (so root motion picks its copy) at `CTIM`, with no fade.
A clip name the build does not have falls back to `Idle_Loop`.

**`NODE`: one node's editable values**

| Tag | Type | Saved from | Missing |
|---|---|---|---|
| `PATH` | u32[] | the node's path | the node is skipped |
| `NAME` | string | `NvNode.name` (up to 31 bytes) | as built |
| `POS ` | f32[3] | `position` | as built |
| `ROT ` | f32[4] | `rotation` (x, y, z, w), normalized on load | as built |
| `SCL ` | f32[3] | `scale` | as built |
| `COLR` | f32[4] | the base color of the node's material, for nodes with a mesh | as built |
| `ATCH` | string | the joint the node follows, by name, for attached nodes | as built |
| `CFOV` | f32 | `camera.fov_y`, radians, for camera nodes | as built |
| `LCOL` | f32[3] | `light.color`, for light nodes | as built |
| `LINT` | f32 | `light.intensity`, for light nodes | as built |

Every node is saved, including the character's mesh nodes and the look target. The save always
holds the edit state (`play.md`): while playing, the autosave writes the snapshot taken at Play.
The orbit camera's transform is saved but has no lasting effect (it comes from `VIEW`). A joint
name the skeleton does not have leaves the attachment as built.

#### Not saved

- The stress scene: its settings, camera, selection, node edits and benchmark results. The app
  always starts on the showcase.
- Frame times and stats.
- A jump in progress (the returning clip is saved), crossfades in progress, and blend layers'
  own times.
- Panel state: the open tab, scroll positions, which tree nodes are open.
- Anything rebuilt at start: meshes, materials other than their colors, skeletons, clips, GPU and
  ImGui resources.

### Decisions

| Topic | Decision |
|---|---|
| Storage | IndexedDB through IDBFS, mounted at `/nv-save`. One file, `state.nvs`, holds the whole app state |
| When it saves | Every 10 seconds when the bytes would differ from the last save, and when the page is hidden (`visibilitychange`). Hidden is the last reliable moment on phones; `beforeunload` is not. A write goes to a temporary name first and is then renamed, so a half-written save never replaces a good one |
| On start | The app builds the showcase as today, then loads the save if there is one, before the first frame: the showcase and the editor settings. It always starts on the showcase |
| A bad save | A wrong magic, size or checksum, a field of the wrong size, or a newer version: the file is renamed to `state.nvs.bad` and the app starts as on a first visit. A message in the View tab says so. Loading is all or nothing: the whole file is checked before anything in the app changes |
| `#stress` | Removed. The address no longer names the scene, and the app always starts on the showcase |
| UI | An **Autosave** section in the View tab: an on/off checkbox (on by default, and itself saved; turning it off writes once so the choice sticks), when the state was last saved, **Save now**, **Reset** (asks first, then deletes the save and any `.bad` file and reloads the page once IndexedDB has caught up, which starts as on a first visit; animators cannot be removed, so rebuilding in place would leak them; nothing is saved while it reloads), and **Show save** (below) |
| Save viewer | **Show save** opens a tree of the save's chunks: the header (magic, version, size, checksum and whether it checks out), then tags, sizes and values (numbers, strings, hex for tags it does not know; nodes labeled by name). It reads the file as the loader does, and walks a damaged file as far as it goes, so it shows where a bad save breaks. It can switch to the `.bad` file |
| Durability | The app asks for `navigator.storage.persist()`, so the browser does not clear the data under storage pressure |
| Third-party | None |

### Engine changes

- **Chunks (`nv/chunk.h`, `engine/src/chunk.c`).**
  - `NV_TAG(a, b, c, d)` builds a tag.
  - `NvChunkWriter` appends to an arena buffer: `nv_chunk_begin(tag)` and `nv_chunk_end` (which
    patches the size), and fields: `nv_chunk_u32s`, `nv_chunk_f32s`, `nv_chunk_string`.
  - `NvChunkReader` walks a byte range with bounds checks: the next child, finding a child by tag,
    and reading a field as `u32` or `f32` arrays or a string. A field of the wrong size marks the
    reader as failed; after a failure, every read returns zero, so the loader checks once at the
    end.
  - `nv_crc32`, and the header helpers.
- **Storage (`nv/storage.h`, `engine/src/storage.c`).**
  - `nv_storage_init` mounts IDBFS at `/nv-save`, asks for persistent storage, and waits for the
    first sync from IndexedDB (through Asyncify, like `wgpuInstanceWaitAny`).
  - `nv_storage_write(name, bytes, size)` writes through a temporary file and a rename.
  - `nv_storage_flush` starts a sync to IndexedDB without waiting for it. A flush asked for while
    one is running starts when that one ends.
  - `NvStorage` reports whether storage is available (private windows may refuse IndexedDB) and
    the last sync error. Without storage the app runs as today and says nothing is saved.
    `nv_storage_init` checks for IndexedDB before mounting: Emscripten's IDBFS aborts the whole
    runtime when it is missing.
  - `nv_storage_flush_then_reload` reloads the page after the flush that covers the call (Reset).
- **Window (`nv/window.h`).** `NvWindow` reports when the page becomes hidden, so the app can save
  then.
- **Link flags.** `-lidbfs.js`.

### App changes

- `app/save.c` writes and reads the whole app state with the chunk helpers. Loading reads the
  whole file twice through the same code: a dry run that only checks it, then, if that passed, a
  pass that applies the values to the showcase's nodes, the character, the showcase's view and the
  editor settings. Both passes read the same fields, so the second cannot fail halfway.
- A load leaves a rotation that is already of unit length as it is (renormalizing would change its
  last bits), so saving, loading and saving again gives the same bytes. Debug builds check that at
  start, and tests call `Module._app_debug_save_round_trip()` after editing.
- The autosave timer, the page-hidden save, loading on start, and the View tab's Autosave section
  with the save viewer.
- The showcase's layout number, computed after it is built.
- `#stress` goes: `js_hash_is_stress`, `js_set_hash`, and its mentions in `AGENTS.md`, README and
  `docs/specs/stress.md`.

### Limits

- The origin is shared. The Release and Debug builds use the same save, and so does every other
  project on `chromedays.github.io`, which is why the mount has a distinctive name. Release and
  Debug write the same format, so either reads the other's save.
- Browsers may still clear it: iOS Safari after 7 days without a visit, and private windows when
  they close. Autosave is a convenience, not an archive.
- A save is at most 256 KB (`SAVE_MAX_SIZE`, checked when writing); the showcase is about 2 KB.

### Phases

1. **Format:** `nv/chunk.h`, and saving and loading the whole state to bytes in memory. A round
   trip is checked: save, load, save again, and compare the bytes. `tests/chunk_test.c`, built for
   Node and run by `ctest` (also in CI), covers the reader's bounds checks: every truncation, every
   flipped bit, chunks that claim too much, wrong field sizes, and a writer that runs out of room.
2. **Autosave:** IDBFS, `nv_storage_*`, the timer and the page-hidden save, loading on start, the
   View tab's Autosave section, and removing `#stress`.
3. **Failure cases, viewer and docs:** bad, damaged and newer-version saves, storage that is
   unavailable, Reset, the save viewer, `AGENTS.md`, README and `docs/specs/stress.md`.

Every phase is checked in Release and Debug in headless Chromium. Phase 2 edits the showcase,
reloads the page and checks that the edits came back, and that the stress scene still opens with
its defaults. Phase 3 writes a
truncated save, a save with a flipped byte and a save with a newer version, and checks that the
app starts as on a first visit and keeps the `.bad` file.

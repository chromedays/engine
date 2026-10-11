# Chess scene spec

상태: 제안 (2026-10-11). 구현 전이다. 이 스펙의 변경은 먼저 합의한다.

## 목표

조명과 그림자를 확인할 사실적인 레퍼런스 씬을 하나 더 둔다. Khronos 샘플 **A Beautiful Game**(체스 세트)을 세 번째 씬
"Chess"로 넣는다. 말들이 서로와 판 위에 그림자를 드리우므로 섀도맵, PCF, 바이어스, 톤 매핑을 한눈에 비교할 수 있다.

`scene_file.md`의 원칙을 그대로 따른다: 이 씬을 아는 코드는 없고, 다른 점은 씬 파일만 정한다. 이 씬이 요구하는 것은 모두 어느
씬이든 쓸 수 있는 일반 기능으로 넣는다.

## 에셋

| 항목 | 값 |
|---|---|
| 출처 | [KhronosGroup/glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets) `Models/ABeautifulGame/glTF-Binary/ABeautifulGame.glb` (커밋 해시를 고정해 받는다) |
| 라이선스 | CC-BY 4.0. 원본 MaterialX Project(ASWF, 2020; Moeen Sayed, Mujtaba Sayed가 SideFX를 위해 만듦), glTF 변환 Ed Mackey(2022) |
| 크기 | GLB 43 MB (지오메트리 10.8 MB, JPEG 33장) |
| 노드 | 49개, 모두 메시를 가짐. 폰의 윗부분(`Pawn_Top_*`)은 몸통(`Pawn_Body_*`)의 자식 |
| 메시 | 15개, 각각 프리미티브 하나. 같은 말은 메시를 공유한다(폰 몸통 하나를 16개 노드가 씀) |
| 삼각형 | 고유 57만, 그려지는 것 150만(공유를 펼치면) |
| 머티리얼 | 15개. 13개는 베이스 컬러 텍스처, 폰 윗부분 2개는 `KHR_materials_transmission`/`volume`(유리)이고 텍스처 없이 색만 있다 |
| 텍스처 | 머티리얼마다 base color, normal, ORM(JPEG, 2048×2048). 정점 속성은 POSITION, NORMAL, TEXCOORD_0. 스킨 없음 |
| 크기 단위 | 미터. 판이 0.705 m 정사각, 가장 큰 말(킹)이 0.15 m |

## 지금 코드로 넣으면 생기는 문제

1. **메시 노드 한도:** `NV_GLTF_MAX_MESH_NODES`가 32인데 메시 노드가 49개라 `NV_ASSERT`에 걸린다(`engine/gltf.c`).
2. **메시 공유가 사라진다:** 로더는 노드마다 프리미티브를 새로 올리고 노드의 월드 트랜스폼을 정점에 굽는다. 15개 메시가
   49개가 되어 GPU에 정점 약 100만 개가 올라간다.
3. **트리가 사라진다:** 모든 메시 노드가 루트의 바로 아래에 평평하게 놓이고 트랜스폼이 단위다. 말을 선택하면 기즈모가 원점에
   나타나고, 폰 몸통을 옮겨도 윗부분이 따라오지 않는다.
4. **너무 작다:** 판이 0.7 m인데 공전 카메라의 최소 거리는 1 m(`CAMERA_MIN_DISTANCE`)다.
5. **너무 어둡다:** 앰비언트가 0.15로 고정이라(`engine/renderer.c`) 그림자 쪽 말이 거의 검다.
6. **시작이 무거워진다:** 모든 씬을 시작할 때 만들고 `assets/` 전체가 `sandbox.data`에 들어가므로, 모두가 체스의 다운로드와
   텍스처 디코드를 시작할 때 치른다.
7. **유리 폰:** transmission과 volume은 지원하지 않는다. 불투명한 색으로 그려진다(이 스펙에서는 그대로 둔다).
8. **임시 메모리가 모자란다:** 로더는 glb 전체를 scratch 아레나에 읽는다(`cgltf_load_buffers`). 샌드박스의 scratch는 32 MB인데 glb가
   43 MB라 읽기가 실패한다.

## 정한 것

| 주제 | 결정 |
|---|---|
| 에셋 | A Beautiful Game의 GLB를 **그대로** `assets/chess/chess.glb`에 둔다(Git LFS). 줄이거나 다시 인코딩하는 과정은 이 스펙에 없다. CC-BY 4.0이므로 출처를 `assets/chess/LICENSE.txt`에 적는다 |
| 출처 표기 | `assets/chess/LICENSE.txt` 하나뿐이다. 앱 화면(View 탭 등)과 씬 파일에는 출처를 보이지 않는다 |
| 서드파티 | 새 라이브러리 없음 |
| 쓰지 않는 데이터 | 파일에 남는다. 로더는 지금처럼 베이스 컬러 텍스처만 디코드하고(`NvMaterialDesc`), normal과 ORM 이미지, `KHR_materials_transmission`/`volume`은 읽지 않는다(필수 확장이 아니므로 cgltf가 건너뛴다) |
| 임시 메모리 | 샌드박스의 scratch 아레나를 32 MB에서 128 MB로 늘린다(`sandbox/main.c`의 `scratch_memory`). glb 43 MB, 2048 텍스처 하나의 픽셀 16 MB와 밉, 정점 변환을 담는다 |
| 정적 모델의 트리 | 스킨이 없는 glb는 glTF의 노드 트리와 로컬 트랜스폼을 그대로 씬 노드로 만들고, 같은 프리미티브는 메시 하나를 공유한다. 스킨이 있는 glb(캐릭터)는 지금과 같다 |
| 크기 | 씬 파일에서 모델 노드에 `scale 10 10 10`을 준다. 판이 7 m가 된다. 공전 카메라의 한도는 바꾸지 않는다 |
| 앰비언트 | 씬마다의 값. 씬 파일의 새 `lighting` 블록이 기본값을 주고, View 탭에서 바꾸고, 저장되고, undo된다 |
| 시작 비용 | 씬은 처음 보일 때 만든다(`scene_file.md`의 원래 설계로 돌아간다). 다운로드를 나누는 것은 이 스펙에 없다 |

## 에셋 넣기

- 고정한 커밋의 `Models/ABeautifulGame/glTF-Binary/ABeautifulGame.glb`를 바이트 그대로 `assets/chess/chess.glb`로 둔다. glb는 이미
  `.gitattributes`의 LFS 패턴에 든다(`git lfs ls-files`로 확인).
- `assets/chess/LICENSE.txt`: 출처 URL과 커밋, CC-BY 4.0 링크, 저작자(MaterialX Project/ASWF, Moeen Sayed, Mujtaba Sayed, glTF 변환 Ed
  Mackey), "변경 없음".
- 샌드박스는 `assets/` 전체를 `sandbox.data`에 담으므로(`nv_setup_executable(sandbox ... ASSETS ...)`) 따로 할 일은 없다.

## 엔진 변경

### glTF: 정적 모델의 트리와 메시 공유 (`engine/gltf.c`, `engine/gltf.h`)

- **스킨이 없는 파일:** glTF 씬의 노드마다 씬 노드 하나를 만든다. 부모와 자식, 이름, 로컬 위치, 회전, 크기를 그대로 옮긴다(굽지 않음).
  - 노드의 메시에 프리미티브가 하나면 그 노드가 메시와 머티리얼을 갖는다. 둘 이상이면 프리미티브마다 자식 노드를 하나 더한다.
  - 프리미티브는 처음 만날 때 한 번만 올리고, 같은 `cgltf_primitive`를 쓰는 노드는 같은 `NvMeshId`를 쓴다.
  - 최상위 노드들은 지금처럼 파일 이름의 루트 아래에 놓인다.
- **스킨이 있는 파일:** 지금과 같다(루트 아래에 프리미티브마다 메시 노드 하나, 관절은 씬 노드가 아님).
- `NvGltfModel`은 만든 노드를 모두 기억한다: `nodes[NV_GLTF_MAX_NODES]`, 각 노드의 부모 인덱스(`parents`, 루트의 자식이면 -1).
  `mesh_nodes`는 메시를 가진 노드의 목록으로 남는다.
- `NV_GLTF_MAX_MESH_NODES` 32 → 64, 새 `NV_GLTF_MAX_NODES` 64. 체스는 49개다.
- `nv_gltf_instantiate`는 그 트리를 같은 모양으로 복사한다: 이름, 로컬 트랜스폼, 메시, 머티리얼, 애니메이터. 지금은 메시 노드의 로컬
  트랜스폼을 복사하지 않는다(`scene_file.md`의 "엔진 변경"이 남긴 일).
- 스킨이 있는 캐릭터는 바뀌지 않으므로 쇼케이스의 트리와 `LAYT`가 그대로다. 확인 항목이다.

### 렌더러

- 바뀌는 것 없음. 앰비언트는 이미 `NvRenderer.ambient`에 있고, 샌드박스가 매 프레임 보이는 씬의 값을 넣는다.

## 씬 파일 변경 (`sandbox/scene_file.c`, `scene_file.md`)

새 최상위 블록 하나. 선택이고, 없으면 지금과 같다.

| 문장 | 자리 | 값 | 기본 | 뜻 |
|---|---|---|---|---|
| `lighting` | `view` 뒤, 첫 `palette` 앞 | 블록 | | 씬의 조명 설정 |

`lighting` 블록:

| 키 | 값, 범위 | 필수 | 기본 | 뜻 |
|---|---|---|---|---|
| `ambient <r g b>` | 각 0–1, 선형 | 선택 | 0.15 0.15 0.15 | 그림자와 상관없이 모든 면에 더하는 빛 |

- 쇼케이스와 스트레스 파일은 바꾸지 않는다. 기본값이 지금의 상수와 같으므로 모습도 같다.
- 형식 버전(`nvscene_version 1`)은 그대로다. 새 블록은 선택이고, 옛 파일은 그대로 읽힌다.

## 샌드박스 변경

### 씬 파일 `sandbox/data/chess.nvscene`

```
nvscene_version 1
title "Chess"

view
    camera camera
    yaw 35
    pitch 30
    distance 9
    target 0 0.5 0
    focus board

lighting
    ambient 0.25 0.25 0.27

model chess
    file "/assets/chess/chess.glb"

node camera
    camera
        projection perspective
        fov 45
        near 0.05
        far 100

node sun
    euler -50 30 0
    light
        type directional
        color 1 0.96 0.9
        intensity 2

node board
    scale 10 10 10
    model chess
```

`scene_files[]`(`sandbox/scenes.c`)의 세 번째 항목이 된다. 값(시점, 태양 각도, 앰비언트)은 구현할 때 스크린샷으로 맞춘다.

### 앰비언트

- `SceneDoc`에 `ambient[3]`. 파일의 값으로 시작한다.
- 매 프레임 `nv_renderer_draw` 전에 보이는 씬의 값을 `renderer.ambient`에 넣는다.
- View 탭의 새 행 "Ambient"(색 편집, `search_row`, 키워드 "lighting light fill"). 문자열 표에 행을 더한다.
- 저장: `SCNE` 안의 새 태그 `AMBI`(f32[3]). 없으면 파일의 값. 기존 태그의 뜻은 그대로이므로 `SAVE_VERSION`은 그대로다.
- Undo: Scene 범위(`BONE`과 같은 곳)에 들어간다. 이름표 "Ambient".
- Play와 Stop: 씬 설정이므로 스냅숏이 되돌린다(규칙 그대로).

### 처음 보일 때 만들기

`scene_file.md`가 원래 정했던 동작으로 돌아간다(지금은 구현하며 "모두 시작할 때"로 바꿨다).

- 시작할 때는 첫 씬만 만든다. 다른 씬은 처음 보일 때 만든다. 첫 씬이 만들어지지 않으면 지금처럼 멈춘다.
- 만드는 프레임: 고른 프레임에는 뷰포트에 "Loading <title>..."을 그리고, 다음 프레임에 만든다(텍스처 디코드가 Asyncify로 기다리므로
  그 사이 화면이 멈춰 보이지 않게).
- 만들다 실패하면 지금처럼 그 씬은 거절되고 View 탭이 첫 오류를 보인다.
- **저장:** 아직 만들지 않은 씬의 `SCNE` 바이트는 불러올 때 `SceneDoc`에 지녀 둔다(영구 아레나, 그 크기만큼). 만든 직후 그 바이트를
  적용한다. 만들기 전에 저장하면 그 바이트를 그대로 다시 쓴다. 없으면 쓰지 않는다.
- **에셋:** `ASET`의 glb가 아직 읽히지 않았으면 그 바이트도 지녀 두었다가 읽을 때 적용하고, 그 전에 저장하면 그대로 쓴다.
- **Undo:** 단계는 만든 씬에만 생긴다. 변화 없음.
- 쇼케이스와 스트레스의 저장 바이트는 지금과 같아야 한다(두 씬이 모두 만들어졌을 때). 확인 항목이다.

## 사용자가 보는 변화

- View 탭의 Scene 콤보, 메뉴, 팔레트에 "Chess"가 생긴다. 처음 고르면 잠깐 "Loading Chess..."가 보인다.
- 말을 하나씩 선택하고 기즈모로 옮길 수 있다. 폰 몸통을 옮기면 윗부분이 따라온다. 옮긴 위치는 저장되고 undo된다.
- View 탭에 "Ambient"가 생긴다(모든 씬). 쇼케이스와 스트레스의 기본은 지금과 같다.
- 스트레스 씬도 처음 고를 때 만들어진다. 시작이 그만큼 가벼워진다.
- 다운로드가 체스 glb만큼, 약 43 MB 늘어난다(모든 방문자, 체스를 보지 않아도).

## 한계

- 유리 폰은 불투명한 색이다(transmission, volume 없음).
- normal, roughness, metallic이 없어서 나무 판과 반들한 말이 같은 무광으로 보인다. PBR은 별도 스펙이다.
- 다운로드를 씬마다 나누지 않는다. `sandbox.data`가 약 43 MB 커진다(지금 약 5 MB).
- 체스를 만들면 GPU 메모리를 많이 쓴다: 2048 베이스 컬러 텍스처 15장이 밉까지 약 320 MB, 정점 약 41만 개. 메모리가 적은 폰에서는
  만들기가 실패할 수 있다. 그때는 지금의 규칙대로 그 씬만 거절되고 View 탭이 이유를 보인다.
- 그려지는 삼각형이 약 150만 개라 SwiftShader에서는 매우 느리다(측정은 실제 장치에서).

## 이 스펙에 없는 것

- PBR 머티리얼, normal map, IBL, point/spot 조명.
- 씬마다 다운로드를 나누기(처음 보일 때 받기). `page_loading.md`가 바뀌는 일이라 따로 정한다.
- 체스 규칙이나 말의 상호작용.

## 단계

1. **에셋과 메모리:** `assets/chess/chess.glb`(원본 그대로), `LICENSE.txt`, scratch 128 MB. 확인: `git lfs ls-files`에 glb가 있다; 기존 두
   씬이 그대로 돈다.
2. **glTF 트리와 메시 공유:** 정적 모델의 노드 트리, 프리미티브 공유, 한도 64, `nv_gltf_instantiate`의 트리 복사. 확인: 쇼케이스와
   스트레스의 첫 방문 저장 바이트가 바뀌지 않음(`LAYT` 포함), ctest.
3. **씬 파일:** `lighting`/`ambient`의 읽기와 `tests/scene_file_test.c`의 시험, `scene_file.md`의 표.
4. **앰비언트:** `SceneDoc.ambient`, View 탭 행, `AMBI`, undo의 Scene 범위, 문자열 행.
5. **처음 보일 때 만들기:** 지연 생성, 로딩 프레임, 지녀 둔 `SCNE`/`ASET` 바이트. 확인: 시작 시간이 줄어든다; 체스를 보기 전후 저장이
   왕복한다; 만들기 전에 저장해도 그 씬의 편집이 사라지지 않는다.
6. **체스 씬:** `chess.nvscene`, 목록 항목, 시점과 조명 값. 확인: 스크린샷, 피킹, 기즈모(폰 몸통과 윗부분), Play/Stop 전후 CRC, 새로고침 뒤
   편집 유지.
7. **문서:** `AGENTS.md`, `save.md`(`AMBI`), `undo.md`, `scene_file.md`(새 문장, 지연 생성 복귀), README.

모든 단계는 헤드리스 Chromium에서 Release와 Debug 빌드로 확인한다.

## 열린 질문

1. **지연 생성을 이번에 할까?** 하지 않으면 체스의 텍스처 디코드와 GPU 메모리(약 320 MB)를 모든 방문자가 시작할 때 치른다. 추천:
   이번에.
2. **앰비언트를 편집 가능하게(View 탭, 저장, undo)?** 아니면 파일의 값만 쓴다. 추천: 편집 가능.
3. **다운로드 43 MB:** 원본을 그대로 넣으면 모든 방문자가 받는다. 씬마다 다운로드를 나누는 일(처음 보일 때 받기)을 이 스펙에 넣을까,
   다음 스펙으로 미룰까? 추천: 다음 스펙(바로 이어서).

# Unit editor spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 초안 (2026-10-05). 열린 질문이 정해지면 구현한다.

### 목표

오토배틀러 안에서 `units.txt`의 유닛 정의를 고치고, 바로 전투로 시험하고, 고친 내용을 저장소에 옮길 수 있게 한다.
지금은 값 하나를 바꾸려면 파일을 고치고 다시 패키징(`cmake --build`)해야 한다. 에디터는 지금 구현된 유닛 디자인(A의
`UnitDef`: 비용, 체력, 방어력, 속도, 크기, 포탄 무기 하나, Shield)만 다룬다. 위키 설계 문서의 필드(분대, 층, 피해 패턴
등)는 나중에 정의 파일에 키가 생길 때 에디터에도 더한다.

### 범위

| 포함 | 빠짐 (나중) |
|---|---|
| 지금의 모든 키 편집: 유닛, 무기, Shield | 위키 설계 문서의 새 필드 |
| 유닛 추가, 복제, 삭제, 이름 바꾸기 (최대 16) | `stage.txt` 편집 (적 배치, 공급, 시드) |
| 값의 범위 검사 (파서와 같은 표) | undo, redo |
| 파생 수치 표시 (DPS 등) | 3D 미리보기 창 (전장에서 바로 본다) |
| IndexedDB에 저장, 패키지 값으로 되돌리기 | 서버나 GitHub로 직접 커밋 |
| 내보내기: 클립보드 복사, 파일 내려받기 | 여러 판(버전) 관리 |
| 가져오기: 텍스트 붙여넣기 | |

### 화면

- Battle 패널 위쪽에 탭 둘: **Battle**(지금의 내용)과 **Units**(에디터). 패널 크기와 위치는 그대로다(데스크톱 오른쪽 300,
  폰 아래 42%). 뷰포트와 겹치지 않는다는 규칙도 그대로다.
- Units 탭, 위에서 아래로:
  1. 유닛 목록: 이름 버튼 한 줄씩(고른 것이 밝다). 그 아래 **Add**, **Duplicate**, **Delete**.
  2. 고른 유닛의 필드. 섹션 셋: **Unit**(이름, cost, health, armor, speed, radius, height), **Weapon**(이름, range, damage,
     cooldown, launch_angle, spread, muzzle), **Ability**(콤보 None / Shield; Shield면 radius, capacity, regen, regen_delay).
     필드 이름은 파일의 키와 같게 보인다(번역하지 않는다: 파일과 맞춰 보기 쉽게). 단위는 라벨 옆에 쓴다(m, s, °).
  3. 파생 수치(읽기 전용): DPS(`damage / cooldown`), 방어력 5·20·40 상대 한 발 피해(`max(d − a, d × 0.25)`), 사거리에서
     포탄 비행 시간, 공급 1000으로 살 수 있는 수.
  4. 상태 줄과 버튼: "Packaged" 또는 "Edited"(패키지 파일과 다르면), **Revert all**(패키지 값으로), **Copy units.txt**,
     **Download units.txt**, **Import...**(붙여넣기 창).
- 숫자는 `igDragFloat`/`igInputFloat`(폰에서는 탭하면 입력, 데스크톱에서는 드래그와 Ctrl+클릭 입력). 범위는 `defs.c`의
  `unit_fields`, `weapon_fields`, `shield_fields`에서 가져온다(열린 경계는 가장 가까운 허용값으로). 그래서 에디터가
  파서가 거부할 값을 만들 수 없다.
- 이름은 `igInputText`. 이름 규칙(식별자, 31바이트)이나 중복을 어기면 빨간 글로 알리고 그 이름은 적용하지 않는다.

### 언제 고칠 수 있나

- **배치 단계에서만** 고칠 수 있다. 전투와 결과 단계에서는 Units 탭이 읽기 전용이고 "Retry to edit"을 보인다.
  `Battle`은 `defs`가 쓰는 동안 바뀌지 않는다고 가정하고(`battle_init`), `battle_seek`의 다시 돌리기도 같은 정의에
  기대기 때문이다.
- 값이 바뀌면 같은 프레임에 적용된다:
  - 플레이어 배치를 같은 순서(행, 칸)로 다시 놓는다. 비용이 올라 공급을 넘으면 넘는 것부터 빠진다. 지운 유닛의 배치는
    빠진다.
  - `radius`나 `height`가 바뀐 유닛의 메시를 다시 만든다(`make_unit_mesh`).
  - 적 배치(`stage.txt`)는 이름으로 유닛을 찾으므로, **스테이지가 쓰는 유닛은 지우거나 이름을 바꿀 수 없다**(버튼이
    꺼지고 이유를 보인다). 스테이지가 쓰지 않는 유닛만 자유롭다.
- `selected_def`(배치할 유닛)는 목록이 바뀌면 범위 안으로 맞춘다.

### 저장과 내보내기

- **원본:** 패키지의 `/data/units.txt`(저장소의 `autobattler/data/units.txt`).
- **고친 판:** IndexedDB의 `/nv-battle/units.txt`(`engine/storage.h`의 `nv_storage_*`). 있으면 시작할 때 패키지 파일 대신
  읽는다. 읽다가 오류가 나면(예: 새 빌드가 키를 바꿈) 패키지 파일로 돌아가고 경고를 남긴다(`nv_log`). 고친 판은 지우지
  않는다(내려받아 살릴 수 있게).
- **언제 쓰나:** 위젯을 놓은 뒤(편집 중인 위젯이 없을 때) 내용이 바뀌었으면 쓰고 `nv_storage_flush`. 드래그하는 동안에는
  쓰지 않는다.
- **Revert all:** 고친 판을 지우고 패키지 파일을 다시 읽는다.
- **저장소로 옮기기:** **Copy units.txt**(클립보드, `NvImgui`의 기존 경로)나 **Download units.txt**(브라우저 다운로드: Blob과
  `<a download>`, 엔진에 `nv_window_download(name, bytes, size)`를 새로 둔다). 사람이 그 파일을 저장소에 넣고 커밋한다.
- **Import...:** 텍스트를 붙여 넣는 창. `defs_read_units`로 읽어 오류가 없을 때만 바꾼다. 오류는 줄 번호와 함께 창에 보인다.

### 쓰기 (직렬화)

- `defs.c`에 `umm defs_write_units(const BattleDefs* defs, char* buffer, umm size)`를 둔다. 같은 필드 표를 돌며 쓰므로 키가
  하나 늘어도 읽기와 쓰기가 함께 바뀐다.
- 형식: 들여쓰기 4칸, 키 순서는 필드 표 순서, 기본값과 같은 선택 키(`armor 0`, `spread 0`, `muzzle 0 0 0`)는 생략,
  각도는 도, 수는 `%.9g`를 뒤의 0 없이(f32를 정확히 되읽는 가장 짧은 꼴에 가깝게).
- **주석은 보존하지 않는다.** 첫 줄에 고정 머리 주석, 단위가 있는 키(`speed`, `launch_angle`, `regen`)에 고정 줄 끝 주석을
  쓴다. 저장소의 `units.txt`도 이 형식으로 한 번 맞춰 두어, 내려받은 파일과의 diff가 값만 보이게 한다.
- 결정론: 같은 `BattleDefs`는 언제나 같은 바이트를 쓴다.

### 코드 위치

| 파일 | 내용 |
|---|---|
| `autobattler/defs.c`, `battle.h` | `defs_write_units`; 필드 표를 에디터에 보이는 함수(`defs_unit_fields` 등: 키, 종류, 범위) |
| `autobattler/unit_editor.c` (새 파일) | Units 탭, 적용(배치 다시 놓기, 메시), 저장과 내보내기 |
| `autobattler/battle_view.c` | 탭 둘로 나누기, 메시 다시 만들기를 함수로 |
| `autobattler/main.c` | 시작할 때 IndexedDB의 고친 판을 먼저 읽기, 디버그 내보내기 |
| `autobattler/game.h` | 에디터 상태(고른 유닛, 고친 판 여부, 가져오기 창의 버퍼) |
| `engine/window.c`, `window.h` | `nv_window_download` (다른 앱도 쓸 수 있으므로 엔진) |
| `autobattler/strings.c` | 새 UI 글자의 한국어 행 |

### 서드파티 후보

| 후보 | 무엇 | 언어, 라이선스 | 판단 |
|---|---|---|---|
| **직접 작성** (추천) | ImGui 위젯으로 폼, 필드 표로 범위, 직렬화 약 80줄, 다운로드 `EM_JS` 약 10줄 | C17 | 이미 있는 것(필드 표, ImGui, storage)만 쓴다 |
| ImGui 속성 그리드 류 (예: imgui-knobs, 여러 property editor 예제) | 위젯 모음 | C++, 대개 MIT | 우리 필드는 숫자 몇 개라 이득이 작고, cimgui 바인딩이 없다 |
| FileSaver.js | 브라우저 다운로드 | JS, MIT | Blob과 `<a download>` 몇 줄과 같은 일. 페이지에 스크립트가 늘어난다 |

### 테스트

ctest(`tests/battle_test.c`):
- 쓰기와 읽기 왕복: 저장소의 `units.txt`를 읽고 쓰고 다시 읽으면 같은 `BattleDefs`(바이트 비교), 다시 쓰면 같은 텍스트.
- 쓰기의 형식: 기본값 키 생략, 각도가 도로 나감, 16종까지.
- 범위: 필드 표의 경계 값은 왕복하고, 경계 밖 값은 읽기가 거부한다(에디터가 막는 값과 같은지).

Playwright(Release, Debug, 데스크톱과 폰 크기; 손으로):
- Units 탭에서 health를 바꾸고 전투를 돌리면 해시가 바뀐다; 같은 값으로 되돌리면 원래 해시.
- 비용을 올리면 공급을 넘는 배치가 빠진다; radius를 바꾸면 메시 크기가 바뀐다.
- 새로 고침 뒤에도 고친 값이 남는다(IndexedDB); Revert all 뒤에는 패키지 값.
- 스테이지가 쓰는 유닛은 Delete와 이름 바꾸기가 꺼져 있다; 새로 더한 유닛은 지울 수 있다.
- Copy가 클립보드에 `units.txt`를 넣고, 그 텍스트를 Import하면 같은 해시.
- 전투 중에는 읽기 전용; 콘솔에 오류와 경고가 없다.

디버그 내보내기: `_battle_debug_unit_get(def, field)`, `_battle_debug_unit_set(def, field, value)`, `_battle_debug_units_text()`
(쓴 텍스트의 포인터), `_battle_debug_units_revert()`.

### 단계

1. `defs_write_units`, 필드 표 공개, 저장소 `units.txt`를 쓰기 형식으로 맞춤, ctest.
2. Units 탭(폼, 범위, 적용), 탭 나누기, 한국어 행.
3. IndexedDB 저장, Revert, Copy, Download(`nv_window_download`), Import.
4. Playwright 확인, `AGENTS.md`와 `battle.md` 갱신.

### 열린 질문

1. **위치:** Battle 패널의 탭(제안)인가, 패널과 따로인 창인가? 폰에서는 탭이 자연스럽다.
2. **스테이지가 쓰는 유닛:** 지우기와 이름 바꾸기를 막는 것(제안)으로 충분한가, 아니면 스테이지 편집까지 같이 할까?
3. **주석:** 쓰면 손으로 단 주석이 사라진다(제안: 고정 주석만). 주석을 보존해야 하는가?
4. **저장 시점:** 위젯을 놓을 때 자동 저장(제안)인가, Save 버튼인가?
5. **전투 중 편집:** 배치 단계에서만(제안)이면 충분한가?

## English

Status: draft (2026-10-05). Built once the open questions are settled.

### Goal

Edit the unit definitions of `units.txt` inside the auto-battler, try them in a battle at once, and carry the changes to the
repository. Today changing one value means editing the file and repackaging (`cmake --build`). The editor covers only the unit
design built now (A's `UnitDef`: cost, health, armor, speed, size, one shell weapon, Shield). The wiki design document's fields
(squads, layers, damage patterns and so on) join the editor when the definition files get their keys.

### Scope

| In | Out (later) |
|---|---|
| Editing every current key: unit, weapon, Shield | The wiki design document's new fields |
| Adding, duplicating, deleting and renaming units (at most 16) | Editing `stage.txt` (enemy deployment, supply, seed) |
| Range checks (the same tables as the parser) | Undo, redo |
| Derived numbers (DPS and so on) | A 3D preview window (the field shows it) |
| Saving to IndexedDB, reverting to the packaged values | Committing to a server or GitHub directly |
| Export: copy to the clipboard, download the file | Keeping several versions |
| Import: pasting text | |

### Screen

- Two tabs at the top of the Battle panel: **Battle** (what it has now) and **Units** (the editor). The panel keeps its size and
  place (right, 300 wide, on a desktop; below, 42% high, on a phone), and still does not overlap the viewport.
- The Units tab, top to bottom:
  1. The unit list: a button per name (the chosen one lit). Under it **Add**, **Duplicate**, **Delete**.
  2. The chosen unit's fields, in three sections: **Unit** (name, cost, health, armor, speed, radius, height), **Weapon** (name,
     range, damage, cooldown, launch_angle, spread, muzzle), **Ability** (a None / Shield combo; for Shield: radius, capacity,
     regen, regen_delay). Field labels are the file's keys (not translated, so they match the file). Units sit beside the label
     (m, s, °).
  3. Derived numbers (read only): DPS (`damage / cooldown`), one hit's damage against armor 5, 20 and 40
     (`max(d − a, d × 0.25)`), the shell's flight time at full range, how many 1000 supply buys.
  4. A status line and buttons: "Packaged" or "Edited" (when it differs from the packaged file), **Revert all** (to the packaged
     values), **Copy units.txt**, **Download units.txt**, **Import...** (a paste window).
- Numbers use `igDragFloat`/`igInputFloat` (a tap types on a phone; drag, or Ctrl+click to type, on a desktop). Ranges come from
  `defs.c`'s `unit_fields`, `weapon_fields` and `shield_fields` (an open bound becomes the nearest allowed value), so the editor
  cannot make a value the parser would refuse.
- Names use `igInputText`. A name that breaks the name rule (an identifier, 31 bytes) or repeats another is reported in red and
  not applied.

### When editing is allowed

- **Only while deploying.** In the fight and result phases the Units tab is read only and shows "Retry to edit". `Battle` assumes
  its `defs` do not change while it uses them (`battle_init`), and `battle_seek`'s replay relies on the same definitions.
- A change applies in the same frame:
  - The player's placement is placed again in the same order (row, cell). When a higher cost goes over the supply, the units
    beyond it drop out. A deleted unit's placements drop out.
  - A unit whose `radius` or `height` changed gets its mesh made again (`make_unit_mesh`).
  - The enemy deployment (`stage.txt`) finds units by name, so **a unit the stage uses cannot be deleted or renamed** (the
    buttons are off, with the reason shown). Units the stage does not use are free.
- `selected_def` (the unit the next tap places) is kept in range when the list changes.

### Saving and export

- **Original:** the package's `/data/units.txt` (the repository's `autobattler/data/units.txt`).
- **Edited copy:** `/nv-battle/units.txt` in IndexedDB (`nv_storage_*` from `engine/storage.h`). When it exists, it is read at
  start instead of the packaged file. When it fails to read (say a new build changed a key), the packaged file is used and a
  warning logged (`nv_log`). The edited copy is not deleted, so it can still be downloaded.
- **When it is written:** after a widget is released (no widget being edited) and the content changed, then `nv_storage_flush`.
  Not while dragging.
- **Revert all:** deletes the edited copy and reads the packaged file again.
- **To the repository:** **Copy units.txt** (the clipboard, `NvImgui`'s existing path) or **Download units.txt** (a browser
  download: a Blob and `<a download>`, through a new engine function `nv_window_download(name, bytes, size)`). A person puts the
  file in the repository and commits it.
- **Import...:** a window to paste text into. It is read with `defs_read_units` and replaces the units only when it has no
  errors; errors are shown in the window with their lines.

### Writing (serialization)

- `defs.c` gets `umm defs_write_units(const BattleDefs* defs, char* buffer, umm size)`. It walks the same field tables, so a new
  key changes reading and writing together.
- Format: 4-space indentation, keys in field-table order, optional keys equal to their default (`armor 0`, `spread 0`,
  `muzzle 0 0 0`) left out, angles in degrees, numbers as `%.9g` without trailing zeros (close to the shortest form that reads
  back as the same f32).
- **Comments are not kept.** A fixed header comment goes on the first line, and fixed end-of-line comments on the keys with
  units (`speed`, `launch_angle`, `regen`). The repository's `units.txt` is rewritten once in this format, so a downloaded file's
  diff shows only values.
- Determinism: the same `BattleDefs` always writes the same bytes.

### Where the code goes

| File | Contents |
|---|---|
| `autobattler/defs.c`, `battle.h` | `defs_write_units`; functions that show the field tables to the editor (`defs_unit_fields` and so on: key, kind, range) |
| `autobattler/unit_editor.c` (new) | The Units tab, applying changes (placing again, meshes), saving and export |
| `autobattler/battle_view.c` | Splitting the panel into two tabs, making a unit's mesh again as a function |
| `autobattler/main.c` | Reading the edited copy from IndexedDB first at start, debug exports |
| `autobattler/game.h` | The editor's state (the chosen unit, whether there is an edited copy, the import window's buffer) |
| `engine/window.c`, `window.h` | `nv_window_download` (other apps could use it, so the engine) |
| `autobattler/strings.c` | Korean rows for the new UI text |

### Third-party candidates

| Candidate | What it is | Language, license | Judgment |
|---|---|---|---|
| **Write it ourselves** (recommended) | A form of ImGui widgets, ranges from the field tables, a writer of about 80 lines, a download `EM_JS` of about 10 | C17 | Uses only what exists (field tables, ImGui, storage) |
| ImGui property-grid kinds (imgui-knobs, various property editor samples) | Widget sets | C++, mostly MIT | Our fields are a few numbers, so little gain, and there are no cimgui bindings |
| FileSaver.js | Browser downloads | JS, MIT | Does what a Blob and `<a download>` do in a few lines, and adds a script to the page |

### Tests

ctest (`tests/battle_test.c`):
- Write and read round trip: reading the repository's `units.txt`, writing it and reading it again gives the same `BattleDefs`
  (compared as bytes), and writing again gives the same text.
- The written format: default keys left out, angles written in degrees, up to 16 types.
- Ranges: the field tables' boundary values round-trip, and values beyond them are refused by the reader (the same values the
  editor blocks).

Playwright (Release and Debug, desktop and phone sizes; by hand):
- Changing health in the Units tab and running a battle changes the hash; setting it back gives the original hash.
- Raising the cost drops the placement beyond the supply; changing the radius changes the mesh's size.
- The edited values survive a reload (IndexedDB); after Revert all the packaged values are back.
- For a unit the stage uses, Delete and renaming are off; a newly added unit can be deleted.
- Copy puts `units.txt` in the clipboard, and importing that text gives the same hash.
- Read only during a fight; no console errors or warnings.

Debug exports: `_battle_debug_unit_get(def, field)`, `_battle_debug_unit_set(def, field, value)`, `_battle_debug_units_text()`
(a pointer to the written text), `_battle_debug_units_revert()`.

### Phases

1. `defs_write_units`, the field tables made visible, the repository's `units.txt` rewritten in the written format, ctest.
2. The Units tab (form, ranges, applying), the tab split, Korean rows.
3. Saving to IndexedDB, Revert, Copy, Download (`nv_window_download`), Import.
4. Playwright checks, updates to `AGENTS.md` and `battle.md`.

### Open questions

1. **Where:** a tab of the Battle panel (proposed), or a window apart from the panel? A tab suits the phone.
2. **Units the stage uses:** is blocking deletion and renaming (proposed) enough, or should stage editing come too?
3. **Comments:** writing drops hand-written comments (proposed: fixed comments only). Must comments be kept?
4. **When to save:** automatically when a widget is released (proposed), or a Save button?
5. **Editing during a fight:** is "only while deploying" (proposed) enough?

# Local files spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-10-05). 이 스펙의 변경은 먼저 합의한다.

### 목표

사용자 컴퓨터의 파일을 브라우저의 대화 상자로 열고 저장한다(`engine/local_file.h`). 처음 쓰는 곳은 오토배틀러의 프로젝트 파일이다
(`abproj.md`, "불러오기와 저장"). 다른 앱도 쓸 수 있으므로 엔진에 둔다.

### 브라우저마다

| 브라우저 | 열기 | 다시 읽기 | 저장(제자리) | 다른 이름으로 저장 |
|---|---|---|---|---|
| File System Access API가 있는 곳(데스크톱 Chrome, Edge) | `showOpenFilePicker` | 붙잡아 둔 파일을 대화 상자 없이 | 붙잡아 둔 파일에 바로 쓴다 | `showSaveFilePicker`, 그 파일을 붙잡는다 |
| 그 밖(Firefox, Safari, 폰) | 숨긴 `<input type="file">` | 없음 | 없음 | 내려받기(`<a download>`) |

- **붙잡아 둔 파일(kept):** 마지막으로 열거나 저장한 파일의 핸들. 페이지를 닫으면 사라진다.
- **바깥 수정 보호:** 제자리 저장은 쓰기 전에 파일의 수정 시각을 읽은 때(또는 쓴 때)와 비교한다. 다르면(텍스트 편집기가 고쳤다) 쓰지 않고
  실패한다: "the file changed on disk since it was read: reload it, or save it as another file".
- 대화 상자는 최근의 클릭이나 탭이 있어야 열린다. 버튼을 누른 프레임에서 부른다(rAF 안이라도 그 클릭의 활성 시간 안이다).

### API

```c
b32 nv_local_file_open(const char* extension);   // ".abproj"
b32 nv_local_file_reload(void);
b32 nv_local_file_save(const char* name, const void* bytes, umm size, b32 save_as);
void nv_local_file_poll(NvLocalFile* file);      // 매 프레임
NvFileData nv_local_file_take(NvArena* arena, umm max_size);
```

- 한 번에 하나만 돈다. 다른 것이 도는 동안의 요청은 거짓을 돌려준다.
- 결과는 나중 프레임에 `nv_local_file_poll`로 온다: `READ`(바이트를 `nv_local_file_take`로 가져간다), `WRITTEN`, `CANCELED`,
  `FAILED`(`error`에 영어로 까닭). 결과는 poll 한 번에만 알리고 그 뒤는 `IDLE`이다. 프레임은 대화 상자를 기다리지 않는다(Asyncify로
  멈추지 않는다).
- 상태는 `Module.nvLocalFile`에 있다. 프라미스는 그것만 바꾸고 WebAssembly를 부르지 않는다(로그 훅과 같은 규칙).
- `cancel` 이벤트가 없는 옛 브라우저에서 파일 입력을 닫으면 아무 소식이 없다. 그래서 파일 입력이 열려 있는 동안은 바쁘다고 알리지
  않고, 새 요청을 막지 않는다.
- `nv_local_file_take`는 `nv_file_read`처럼 뒤에 0 바이트를 붙인다. `max_size`나 아레나보다 크면 실패하고 바이트는 버린다.
- 엔진은 로그를 남기지 않는다. 무엇을 위한 파일이었는지는 부른 쪽이 말한다.

### 서드파티 후보

| 후보 | 무엇 | 언어, 라이선스 | 판단 |
|---|---|---|---|
| **직접 작성** (선택) | `EM_JS` 약 150줄 | C17, JS | 필요한 것(열기, 다시 읽기, 제자리 저장, 바깥 수정 보호, 대체 경로)만 있다 |
| browser-fs-access | File System Access API와 대체 경로를 감싼 함수 | JS, Apache-2.0 | 같은 일을 하지만 npm 패키지라 빌드에 번들 단계가 생긴다. 바깥 수정 보호는 없다 |

### 테스트

ctest로는 시험할 수 없다(브라우저 API). Playwright(손으로 돌리는 검사, 스크립트는 저장소에 없다):
- File System Access API를 메모리 속 가짜 파일로 바꾼 페이지: 열기, 제자리 저장(읽은 글 그대로), 바깥에서 고친 파일에 저장하면 실패하고
  파일은 그대로, 다시 읽기, 오류 있는 파일, 다른 이름으로 저장, 취소.
- API를 지운 페이지(대체 경로): 진짜 클릭으로 "Open..."이 파일 선택기를 열고, "Save as..."가 내려받는다.
- Release와 Debug 모두.

## English

Status: built (2026-10-05). Changes to this spec are agreed first.

### Goal

Open and save files on the user's computer through the browser's dialogs (`engine/local_file.h`). The first user is the
auto-battler's project file (`abproj.md`, "Loading and saving"). Another app could use it, so it is in the engine.

### Per browser

| Browser | Open | Reload | Save (in place) | Save as |
|---|---|---|---|---|
| With the File System Access API (desktop Chrome, Edge) | `showOpenFilePicker` | the kept file, without a dialog | writes the kept file | `showSaveFilePicker`, and that file is kept |
| Others (Firefox, Safari, phones) | a hidden `<input type="file">` | none | none | a download (`<a download>`) |

- **The kept file:** the handle of the file opened or saved last. It goes away when the page closes.
- **Outside edits are kept:** before writing in place, the file's modification time is compared with the one when it was read (or
  written). If it differs (a text editor changed it), nothing is written and the save fails: "the file changed on disk since it was
  read: reload it, or save it as another file".
- A dialog opens only after a recent click or tap. Call these in the frame the button was pressed (inside requestAnimationFrame is
  still within that click's activation).

### API

```c
b32 nv_local_file_open(const char* extension);   // ".abproj"
b32 nv_local_file_reload(void);
b32 nv_local_file_save(const char* name, const void* bytes, umm size, b32 save_as);
void nv_local_file_poll(NvLocalFile* file);      // every frame
NvFileData nv_local_file_take(NvArena* arena, umm max_size);
```

- One operation runs at a time. A request while another runs returns false.
- The result comes on a later frame through `nv_local_file_poll`: `READ` (take the bytes with `nv_local_file_take`), `WRITTEN`,
  `CANCELED`, `FAILED` (`error` says why, in English). A result is reported by one poll, and the status is `IDLE` after it. The frame
  never waits on a dialog (nothing suspends through Asyncify).
- The state lives in `Module.nvLocalFile`. Promises only change it and never call into WebAssembly (the same rule as the log hooks).
- In an older browser with no `cancel` event, a file input closed without a file says nothing. So while a file input is open it is
  not reported as busy, and it does not block a new request.
- `nv_local_file_take` puts a 0 byte after the bytes, as `nv_file_read` does. It fails, dropping the bytes, when they do not fit in
  `max_size` or the arena.
- The engine logs nothing: the caller says what the file was for.

### Third-party candidates

| Candidate | What | Language, license | Verdict |
|---|---|---|---|
| **Write it ourselves** (chosen) | about 150 lines of `EM_JS` | C17, JS | Has just what is needed (open, reload, save in place, the outside-edit guard, the fallbacks) |
| browser-fs-access | Functions over the File System Access API with fallbacks | JS, Apache-2.0 | Does the same, but as an npm package it adds a bundling step to the build. It has no outside-edit guard |

### Tests

ctest cannot try it (browser APIs). Playwright (run by hand; the script is not in the repository):
- A page whose File System Access API is replaced by fake files in memory: open, save in place (the text as read), a save to a
  file changed outside fails and leaves it as it was, reload, a file with errors, save as, cancel.
- A page with the API removed (the fallbacks): a real click on "Open..." shows a file chooser, and "Save as..." downloads.
- Both Release and Debug.

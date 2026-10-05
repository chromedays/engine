# Page loading spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-10-05). 이 스펙의 변경은 먼저 합의한다.

### 목표

페이지(`web/index.html.in`, 두 실행 파일 모두)가 엔진의 파일(`<target>.wasm`, `.data`, `.js`)을 내려받아 시작한다. 내려받은 파일은
언제나 같은 빌드의 것이어야 한다. 다른 빌드의 `.js`와 `.wasm`을 섞으면 시작하지 못한다(Debug 빌드: "missing Wasm export: ...").

### 문제

GitHub Pages는 모든 파일에 `Cache-Control: max-age=600`을 보낸다. 브라우저와 Pages의 CDN이 파일마다 따로 캐시하므로, 배포 직후
10분 안에는 한 파일은 새 빌드, 다른 파일은 옛 빌드일 수 있다.

### 매니페스트

`web/manifest.cmake`가 빌드마다(링크 뒤) `<target>.manifest.json`을 쓴다:

```json
{"version": "19a88f79ea4cc8ab", "files": {"autobattler.wasm": 717274, "autobattler.data": 2731408, "autobattler.js": 164567}}
```

- `version`: 세 파일의 MD5를 이은 것의 MD5, 앞 16자. 파일이 하나라도 바뀌면 바뀐다.
- `files`: 파일마다 바이트 수(압축 풀린 크기). 진행 막대와 크기 확인에 쓴다.

### 내려받기

1. 매니페스트는 `cache: "no-cache"`로 받는다(브라우저가 서버에 다시 확인한다).
2. 모든 파일을 `<name>?v=<version>`으로 받는다. 새 빌드는 새 주소이므로 옛 캐시 항목을 쓰지 않는다.
3. 받은 크기가 매니페스트와 다른 파일이 있으면(캐시가 다른 빌드의 파일을 주었다, 또는 매니페스트 자체가 옛것이었다) 경고를 남기고
   (`[page] files of another build (...)`), 모든 파일을 `?v=<version>-<시각>`과 `cache: "reload"`로 다시 받는다. 어떤 캐시도 본 적
   없는 주소이므로 모두 지금 서버에 있는 빌드의 것이다. 이 두 번째 받기는 크기를 확인하지 않는다(매니페스트가 옛것일 수 있다).
4. `.js`도 다른 파일과 함께 받아서, 그 글을 `<script>`로 실행한다(`//# sourceURL=<target>.js`를 붙인다). 그래서 실행되는 `.js`는 확인한
   그 바이트다. Emscripten은 스크립트 주소가 없으면 상대 경로를 쓰고, 페이지가 `.wasm`과 `.data`를 직접 넘기므로 영향이 없다.

빌드 라벨의 내려받은 양(`nv_window_download_text`)은 Resource Timing의 경로로 찾으므로 쿼리 문자열과 상관없다.

### 바꾼 때의 한 번

배포 직후에는 캐시에 남은 옛 `index.html`(최대 10분)이 새 형식의 매니페스트를 읽지 못해 실패할 수 있다. 다시 읽으면 된다. 형식을 바꾼
이번 한 번뿐이다.

### 테스트

Playwright(손으로 돌리는 검사, 스크립트는 저장소에 없다), Release와 Debug, 두 실행 파일:
- 보통: 요청이 매니페스트 하나와 파일마다 하나(`?v=<version>`)이고 시작한다.
- 버전 주소에서 다른 빌드의 `.wasm`(또는 `.js`)을 주는 서버: 경고 하나, 다시 받기 한 번, 그리고 시작한다.
- 옛 페이지로 같은 상황을 만들면 "missing Wasm export"로 실패한다(이 스펙 전의 문제 재현).

## English

Status: built (2026-10-05). Changes to this spec are agreed first.

### Goal

The page (`web/index.html.in`, for both executables) downloads the engine's files (`<target>.wasm`, `.data`, `.js`) and starts it.
The files must always be of one build. A `.js` and a `.wasm` of different builds do not start (Debug builds: "missing Wasm export:
...").

### The problem

GitHub Pages sends `Cache-Control: max-age=600` with every file. The browser and the Pages CDN cache each file on its own, so for 10
minutes after a deploy one file can be of the new build and another of the old one.

### The manifest

`web/manifest.cmake` writes `<target>.manifest.json` on every build (after the link):

```json
{"version": "19a88f79ea4cc8ab", "files": {"autobattler.wasm": 717274, "autobattler.data": 2731408, "autobattler.js": 164567}}
```

- `version`: the first 16 characters of the MD5 of the three files' MD5s together. It changes when any file changes.
- `files`: each file's byte count (uncompressed), for the progress bar and the size check.

### Downloading

1. The manifest is fetched with `cache: "no-cache"` (the browser checks with the server again).
2. Every file is fetched as `<name>?v=<version>`. A new build is a new address, so no old cache entry is used.
3. When a file's size differs from the manifest's (a cache gave another build's file, or the manifest itself was old), a warning is
   logged (`[page] files of another build (...)`) and every file is fetched again as `?v=<version>-<time>` with `cache: "reload"`.
   No cache has seen those addresses, so all of them are of the build on the server now. That second download checks no sizes (the
   manifest may be the old one).
4. The `.js` is downloaded with the others and its text run as a `<script>` (with `//# sourceURL=<target>.js`), so the `.js` that
   runs is the bytes that were checked. Without a script address Emscripten uses relative paths, which does not matter since the
   page hands it the `.wasm` and `.data` itself.

The build label's downloaded amount (`nv_window_download_text`) finds the files by their Resource Timing path, so the query string
does not matter to it.

### Once, at the change

Right after the deploy, an old `index.html` still cached (up to 10 minutes) cannot read the new manifest format and may fail. A
reload fixes it. This happens only this once, when the format changes.

### Tests

Playwright (run by hand; the script is not in the repository), Release and Debug, both executables:
- Normally: one request for the manifest and one per file (`?v=<version>`), and it starts.
- A server that gives another build's `.wasm` (or `.js`) at the version's address: one warning, one download again, and it starts.
- The old page in the same situation fails with "missing Wasm export" (the problem before this spec, reproduced).

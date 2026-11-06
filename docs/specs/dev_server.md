# 개발 서버 스펙 (웹 빌드의 라이브 리로드)

상태: 구현됨 (2026-11-04). 이 스펙의 변경은 먼저 합의한다.

### 목표

웹 빌드를 고치고 볼 때 손으로 하는 일(다시 빌드, 브라우저에서 F5)을 없앤다. 소스 파일을 저장하면 서버가 실행 파일의 타깃을 다시
빌드하고, 빌드가 바뀌었으면 열린 페이지가 스스로 다시 불러온다. `python3 -m http.server`를 대신한다.

```sh
node tools/dev_server.mjs --build build --root build/sandbox          # 엔진의 sandbox
node engine/tools/dev_server.mjs --build build                        # 엔진을 서브모듈로 쓰는 앱 (autobattler)
```

범위 밖:

- 상태를 지키는 리로드. 페이지를 다시 불러오는 것이라 F5와 같다: 앱은 처음부터 시작한다(저장된 것은 그대로 읽힌다).
- 실행 중인 WebAssembly의 코드 교체(핫 스왑). Emscripten은 메인 모듈을 바꿔 끼울 수 없고, `MAIN_MODULE`/`SIDE_MODULE` 동적 링크는
  Windows 빌드와 달라진다.
- 배포. CI와 빌드 대시보드는 이 서버를 쓰지 않는다. 개발할 때만 쓴다.
- HTTPS. 휴대폰에서 LAN 주소(`http://<ip>`)로 열면 보안 컨텍스트가 아니라 WebGPU가 없다.

### 의존성

없다. Node의 기본 모듈만 쓴다. Emscripten SDK가 Node를 함께 설치하므로(6.0.10은 Node 24), 웹용으로 빌드할 수 있는 컴퓨터에는 이미
있다. Python은 Windows에 없을 수 있어 쓰지 않는다.

### 동작

신호가 두 가지이고, 서로 따로 움직인다.

```
파일 저장 ──▶ 소스 감시 ──200 ms 디바운스──▶ cmake --build <build> --target <target>
                                                  │
<root>/<target>.manifest.json의 version이 바뀜 ◀────┘  (IDE나 다른 터미널의 빌드도 같다)
        │
        ▼
/__dev/events (Server-Sent Events) ──event: version──▶ 페이지에 넣은 스크립트 ──▶ location.reload()
```

- **소스가 바뀌면 다시 빌드한다.** 바뀐 파일의 종류는 보지 않는다. 빌드와 상관없는 파일이면 ninja가 아무것도 하지 않고 끝난다.
- **빌드가 바뀌면 다시 불러온다.** `web/manifest.cmake`가 링크 뒤에 쓰는 `version`(세 파일 MD5의 MD5, `page_loading.md`)이 신호다.
  그래서 주석만 고친 경우처럼 실행 파일이 같으면 다시 불러오지 않고, 실패한 빌드는 매니페스트를 쓰지 않으므로 페이지가 그대로 남는다.
  Debug 빌드는 주석만 고쳐도 `.wasm`이 바뀌므로(디버그 정보) 다시 불러온다.
  매니페스트는 `.wasm`, `.data`, `.js`가 다 쓰인 뒤에 쓰이므로, 신호를 받은 페이지는 한 빌드의 파일을 받는다.

### 명령줄

```
node tools/dev_server.mjs --build <dir> [--root <dir>] [--target <name>] [--port 8000] [--host 127.0.0.1] [--no-build]
```

| 옵션 | 뜻 |
|---|---|
| `--build` | CMake 빌드 디렉터리. `CMakeCache.txt`가 없으면 구성 명령을 보여 주고 1로 끝난다 |
| `--root` | 내보낼 디렉터리. 기본은 `--build`. sandbox는 `build/sandbox` |
| `--target` | 페이지의 타깃. 기본은 `--root`에 하나뿐인 `*.manifest.json`의 이름. 없거나 여럿이면 1로 끝난다 |
| `--port`, `--host` | 기본 `8000`, `127.0.0.1`. `--host 0.0.0.0`이면 LAN에서 열린다(위의 WebGPU 제약) |
| `--no-build` | 빌드하지 않는다. 빌드는 직접 하고, 서버는 내보내기와 다시 불러오기만 한다 |

감시할 소스 루트는 `CMakeCache.txt`의 `CMAKE_HOME_DIRECTORY`다. 앱의 빌드 디렉터리면 앱의 저장소이고, 그 안의 `engine/`도 함께 감시된다.

### 감시

- 디렉터리마다 재귀 없는 `fs.watch` 하나. Node의 재귀 감시는 Linux에서 빌드 디렉터리(`_deps`의 수천 개 디렉터리)까지 걸으므로 쓰지 않는다.
  새로 생긴 디렉터리는 그때 더한다.
- 건너뛰는 디렉터리: 점으로 시작하는 것(`.git`, `.vscode`), `node_modules`, `CMakeCache.txt`가 있는 것(빌드 디렉터리. 빌드의 출력이 다시
  빌드를 부르지 않게).
- 건너뛰는 파일: 점으로 시작하는 것, 편집기의 백업과 스왑 파일(`*~`, `*.swp`, vim의 `4913`, JetBrains의 `___jb_tmp___`).
- 빌드는 한 번에 하나다. 빌드 중의 변경은 표시만 하고, 빌드가 끝나면 한 번 더 빌드한다. 돌고 있는 ninja는 멈추지 않는다.
- 터미널에는 `[dev] build 3: src/battle.c`, 컴파일러 출력, `[dev] ok in 4.2 s` 또는 `[dev] failed (exit 1) in 0.2 s`가 나온다.
- 매니페스트는 300 ms마다 stat으로 본다(`fs.watchFile`). 다시 쓰이든, 바뀌어 놓이든, 새로 생기든 같게 보인다. 반쯤 쓰인 파일은
  JSON이 아니므로 다음 확인을 기다린다.

### HTTP

- `GET`/`HEAD`만 받는다(그 밖은 405). 쿼리 문자열은 무시한다(페이지가 `?v=<version>`을 붙인다).
- 경로는 디코드한 뒤 `--root` 안으로 풀리는 것만 내보낸다. 밖으로 나가는 경로(`..`, `%2e%2e`, NUL)와 디렉터리는 404다. `/`로 끝나면
  `index.html`이다.
- 모든 응답에 `Cache-Control: no-store`. 다른 파일은 이미 버전이 붙은 주소로 받지만(`page_loading.md`), 페이지와 매니페스트도 낡지 않게 한다.
- `index.html`은 내보낼 때 `</body>` 앞에 스크립트를 넣는다. 빌드 디렉터리의 파일은 바꾸지 않는다.

### 이벤트 (`/__dev/events`)

`text/event-stream`. 연결되면 `retry: 1000`, 매니페스트가 있으면 `version`, 그리고 `build`를 보낸다. 그 뒤로는 바뀔 때마다 보낸다.
15초마다 주석 줄(`: `)로 연결을 유지한다.

| 이벤트 | data | 언제 |
|---|---|---|
| `version` | 매니페스트의 `version` | 연결될 때, 그리고 버전이 바뀔 때 |
| `build` | `building`, `failed`, `ok` | 연결될 때, 그리고 서버가 빌드를 시작하고 끝낼 때 |

### 페이지에 넣는 스크립트

- 페이지를 내보낼 때의 버전을 품는다. `version` 이벤트의 버전이 그것과 다르면 `location.reload()`. 서버가 다시 시작되어도
  `EventSource`가 스스로 다시 연결하고, 연결 때 받는 버전이 놓친 이벤트를 메운다. 페이지를 내보낸 뒤 매니페스트를 받기 전에 빌드가
  끝나면 한 번 더 다시 불러올 뿐, 낡은 페이지가 남지는 않는다.
- 왼쪽 아래 구석의 배지: "Building…"(파랑), "Build failed: see the terminal"(빨강), 연결이 끊기면 "Dev server stopped"(회색).
  `pointer-events: none`이라 캔버스와 ImGui의 입력을 가로채지 않는다. 개발 도구의 글이라 `T()`를 거치지 않는다.
- `Module`과 WebAssembly를 건드리지 않는다. 그래서 앱이 멈춘 뒤에도(Debug 빌드의 `NV_ASSERT`) 고치고 저장하면 다시 불러온다.

### 테스트

`tests/dev_server_test.mjs`(ctest의 `dev_server_test`, 웹 빌드는 Emscripten의 Node, Windows는 찾은 Node). 브라우저 없이:

- 스크립트 넣기(`</body>`가 있을 때와 없을 때, 버전 문자열이 `</script>`를 닫지 못함), 경로 풀기(`..`, `%2e%2e`, NUL, 잘못된 인코딩),
  MIME, 매니페스트 읽기(반쯤 쓰인 것), 감시가 건너뛰는 것, 빌드 큐(빌드 중의 변경 여럿은 정확히 한 번 더).
- 임시 빌드 디렉터리에 `--no-build`로 띄운 서버: 페이지에 스크립트와 버전, `no-store`, `HEAD`, 404와 405, 이벤트 스트림의 첫 `version`과
  `build`, 반쯤 쓴 매니페스트는 무시되고 다 쓴 것이 새 `version`으로 오는 것.

실제 빌드와 브라우저는 손으로(또는 Playwright로) 확인한다: 데이터를 바꾸면 다시 빌드하고 다시 불러온다, 주석만 바꾸면 빌드하지만
(Release에서는) 다시 불러오지 않는다, 컴파일 오류는 배지에 보이고 페이지는 남는다, 고치면 배지가 사라진다. Release와 Debug 둘 다, 콘솔에
WebGPU 오류 없이.

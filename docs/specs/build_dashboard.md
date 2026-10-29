# 빌드 대시보드

상태: 구현됨, 배포 전 (2026-10-28). Cloudflare 계정과 비밀값(`tools/dashboard/README.md`)이 준비되면 쓴다. 이 스펙의 변경은 먼저 합의한다.

## 목표

master와 PR(풀 리퀘스트)이 열린 브랜치마다 오토배틀러의 Release·Debug 빌드를 한 페이지에서 열 수 있게 한다. PR이 열리고 닫히고
머지되거나, 커밋이 push되거나, CI가 끝나면 열려 있는 페이지에 몇 초 안에 나타난다. 새로고침은 필요 없다.

GitHub Pages로는 할 수 없다. Pages는 파일만 내주므로 웹훅을 받거나 열린 페이지로 변경을 보낼 수 없고, 사이트가 1 GB를 넘을 수
없다(2026-10-28에 983 MB). 그래서 대시보드와 빌드를 Cloudflare로 옮긴다. 빌드는 R2(객체 저장소)에, 페이지와 실시간 갱신은 Worker
하나와 Durable Object 하나가 맡는다. 무료 플랜 안에서 돈다(R2는 10 GB까지 무료).

## 구성

```
GitHub ──웹훅(push, pull_request, delete, workflow_run)──▶ Worker ──▶ Durable Object "main" ──WebSocket──▶ 열린 페이지들
CI(build.yml) ──R2에 업로드, 빌드 기록 POST──────────────▶ Worker ─┘          │ 5분마다 GitHub API로 재동기화
브라우저 ──GET /builds/...──▶ Worker ──▶ R2
```

- `tools/dashboard/wrangler.toml`: Worker `nv-builds`, R2 버킷 `nv-builds`(바인딩 `BUILDS`), Durable Object `Dashboard`(바인딩
  `DASHBOARD`, SQLite 기반: 무료 플랜은 이것만 받는다), 페이지 파일(`public/`). 비밀값은 여기에 쓰지 않는다.
- `src/worker.mjs`: 경로를 나누고, 서명과 토큰을 확인하고, R2의 파일을 내준다. Durable Object 클래스도 여기 있다.
- `src/state.mjs`: 상태와 그것을 바꾸는 규칙. Cloudflare API를 쓰지 않는 순수 함수라서 `tests/dashboard_test.mjs`가 Node로 시험한다.
- `public/index.html`: 페이지. 목업(https://claude.ai/artifact/9tJN1aV1pBnQUXBWFRt4mX)을 그대로 옮겼다.
- `publish.sh`: CI가 한 브랜치의 빌드를 R2에 올리고 기록을 등록한다.

## 경로

| 경로 | 하는 일 |
|---|---|
| `GET /` | 페이지(`public/index.html`, Workers 정적 파일) |
| `GET /ws` | WebSocket. `Origin`이 Worker 자신의 주소일 때만 받는다. 연결하면 상태 전체를, 그 뒤로는 바뀔 때마다 다시 전체를 보낸다 |
| `POST /webhook` | GitHub 웹훅. `X-Hub-Signature-256`(`GITHUB_WEBHOOK_SECRET`의 HMAC-SHA256)이 맞지 않으면 401 |
| `POST /api/builds` | CI의 빌드 기록. `Authorization: Bearer <DASHBOARD_TOKEN>`이 아니면 401, 형식이 틀리면 400 |
| `GET /api/state` | 페이지가 받는 상태(JSON). 확인용 |
| `GET /builds/<폴더>/<release\|debug>/<파일>` | R2의 `builds/...` 객체. `/`로 끝나면 `index.html`. 끝에 `/`가 없는 폴더는 301 |

빌드 파일 이름에는 해시가 없으므로 모든 응답이 `Cache-Control: no-cache`와 ETag를 보낸다. 바뀌지 않은 파일은 304로 끝난다.
페이지 로딩의 매니페스트(`docs/specs/page_loading.md`)는 그대로 동작한다(`?v=` 쿼리는 무시한다).

브랜치의 폴더는 브랜치 이름에서 `/`를 `-`로 바꾼 것이다(`claude/foo-bar` → `claude-foo-bar`). master는 `master`.

## 상태

Durable Object가 저장소(`state` 키 하나)에 둔다. 브랜치마다:

| 필드 | 뜻 | 바뀌는 때 |
|---|---|---|
| `head` | GitHub가 알려 준 가장 새 커밋(`sha`, 첫 줄 `subject`, `date`) | push, PR 열림(처음일 때), 재동기화 |
| `ci` | 그 커밋의 `build` 워크플로: `running`, `passed`, `failed`, 없음 | push(`running`), `workflow_run`, 빌드 기록(`passed`), 재동기화 |
| `pr` | 브랜치의 가장 새 PR(번호가 가장 큰 것): `number`, `title`, `state`(`open`, `merged`, `closed`) | `pull_request`, 재동기화 |
| `build` | CI가 올린 빌드: `sha`, `subject`, `date`, `release`·`debug`(내려받는 바이트, DWARF 제외), `stored`(폴더 전체 바이트), `ahead`(master에 없는 커밋 수) | 빌드 기록. PR이 머지되면 `ahead`가 0 |

- 페이지에는 master와 PR이 있는 브랜치만 보낸다. PR이 없는 브랜치도 따라가기는 한다(PR이 열리면 바로 보이도록).
- 포크의 PR은 무시한다. 포크의 코드는 비밀값과 함께 돌지 않으므로 빌드도 없다.
- `activity`: 보이는 브랜치의 변화(`push`, `opened`, `merged`, `closed`, `passed`, `failed`, `delete`)를 id가 올라가는 순서로 최근
  50개까지 둔다. 각 항목은 그때의 PR 번호와 제목을 담는다. 페이지는 열린 뒤에 생긴 항목만 보여 준다.
- 브랜치가 지워지면(`delete` 또는 `deleted: true`인 push) 상태에서 빼고 R2의 그 폴더를 지운다.
- 재동기화: 알람이 5분마다(새 객체는 1초 뒤에) GitHub API에서 브랜치(100개씩 5쪽까지), PR(최근 갱신 100개), `build.yml`의 push
  실행(최근 100개)을 읽고 놓친 웹훅의 차이를 고친다. 하나라도 실패하면 아무것도 바꾸지 않는다. 브랜치 목록이 다 오지 않았으면
  (500개 초과) 브랜치를 지우지 않는다. 재동기화는 활동 항목을 만들지 않는다. `GITHUB_TOKEN`(읽기 전용)이 없으면 인증 없이 읽는데,
  Cloudflare의 공유 IP라 한도에 걸릴 수 있다.

## 페이지

목업과 같다. 위에서부터: 헤더(실시간 표시, EN/한국어), 빌드 그리드(master를 맨 앞에 고정, 그다음 PR 카드), 활동, 저장 공간, 출처.

- 카드 제목은 PR 번호와 제목(PR로 가는 링크), 그 아래 작은 글씨로 브랜치 이름. master는 이름이 제목이고 아래에 "기본 브랜치".
- 표시: CI(통과, 실패, 빌드 중), PR(열림, 머지됨, 닫힘), master에 있음 또는 master에 없는 커밋 수.
- 새 커밋이 빌드 중이거나 실패하면 마지막 빌드를 그대로 두고 그 사실을 적는다. 빌드가 아직 없으면 "아직 빌드 없음".
- 바뀐 카드는 한 번 반짝인다(움직임 줄이기 설정에서는 테두리).
- 검색(PR 제목과 번호, 브랜치, 커밋), 필터(전체, 열린 PR, master에 없음, master에 있음), 정렬(최신순, 이름순은 PR 제목, 크기순).
- 연결: 30초마다 `ping`(Durable Object가 깨지 않고 `pong`으로 답한다). 끊기면 1, 2, 4 … 30초 간격으로 다시 연결하고, 새로 받은
  상태로 놓친 것을 따라잡는다. 세 번 실패하면 "실시간 갱신 중지"와 마지막 상태의 시각을 보여 준다.
- 저장 공간: 빌드 기록의 `stored` 합을 R2 무료 10 GB에 대어 보여 준다.

## CI

`.github/workflows/build.yml`:

- `publish`: 이 저장소의 push마다 `web` 뒤에 돈다. master이거나 열린 PR이 있는 브랜치면 그 실행의 `engine-web` 아티팩트에서
  `autobattler/release`와 `autobattler/debug`를 `publish.sh`로 올린다.
- `publish-opened`: PR이 열리거나 다시 열리면(이 저장소의 브랜치일 때만) 그 head 커밋의 push 실행을 찾아(아직 돌면 기다려서) 그
  아티팩트를 올린다. 다시 빌드하지 않는다. push 실행의 `publish`도 올릴 수 있지만 같은 파일이라 문제없다.
- 비밀값이 없으면 두 잡 모두 알림만 남기고 성공한다. 그래서 설정 전에도 CI는 초록이다.

`publish.sh`는 파일을 하나씩 `wrangler r2 object put`으로 올리고(종류에 맞는 Content-Type), 커밋 제목과 날짜, master 대비 앞선
커밋 수를 GitHub API에서 읽어 `/api/builds`에 등록한다. 업로드가 워크플로의 마지막 단계이므로 기록이 오면 그 커밋의 CI는 통과다.

`.github/workflows/dashboard.yml`: master에서 `tools/dashboard/`가 바뀌면 `wrangler deploy`로 Worker를 배포한다(수동 실행도 된다).

GitHub Pages(`stage` 잡, `pages.yml`)는 대시보드가 동작하는 것을 확인한 뒤 걷어 냈다(2026-10-29). 샌드박스 빌드는 이제 어디에도
올라가지 않고 `engine-web` 아티팩트에만 있다.

## 보안

저장소는 공개다. 저장소에는 비밀값이 하나도 없다.

| 비밀값 | 있는 곳 | 쓰임 |
|---|---|---|
| `CLOUDFLARE_API_TOKEN`, `CLOUDFLARE_ACCOUNT_ID` | GitHub Actions secrets | R2 업로드, Worker 배포 |
| `DASHBOARD_TOKEN` | GitHub Actions secrets와 Worker secret | CI의 빌드 등록 |
| `GITHUB_WEBHOOK_SECRET` | Worker secret과 GitHub 웹훅 설정 | 웹훅 서명 확인 |
| `GITHUB_TOKEN`(선택, 읽기 전용) | Worker secret | 재동기화 |

`DASHBOARD_URL`은 비밀이 아니라 Actions variable이다. `pull_request_target`은 쓰지 않는다. 비밀값을 쓰는 잡은 이 저장소로의 push와
이 저장소 브랜치의 PR에서만 돈다. 서명 비교는 `crypto.subtle.verify`, 토큰 비교는 `timingSafeEqual`로 한다.

## 도구

Cloudflare의 CLI인 Wrangler(`wrangler@4.150.0`, MIT/Apache 2.0)를 `npx`로 실행한다. 저장소에 설치하지 않고 어디에도 링크하지 않는다.
Worker 코드는 의존성이 없는 JavaScript다.

## 시험

- `tests/dashboard_test.mjs`(ctest): 웹훅, 빌드 기록, 재동기화의 규칙.
- 로컬: `tools/dashboard/README.md`의 "로컬에서 돌리기". `wrangler dev`가 Worker, Durable Object, R2를 내 컴퓨터에서 돌린다. 서명한
  웹훅과 빌드 기록을 보내고, 페이지를 열어 실시간 갱신을 본다.

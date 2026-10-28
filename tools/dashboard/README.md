# 빌드 대시보드 / Build dashboard

- [한국어](#한국어)
- [English](#english)

## 한국어

master와 PR이 열린 브랜치마다 오토배틀러 빌드를 보여 주고, push·PR·CI 결과를 몇 초 안에 반영하는 페이지다. Cloudflare Worker 하나,
Durable Object 하나, R2 버킷 하나로 돈다. 설계는 `docs/specs/build_dashboard.md`에 있다.

저장소에는 비밀값이 없다. 아래에서 만드는 토큰과 시크릿은 GitHub Actions secrets와 Worker secrets에만 넣는다. 어디에도 커밋하지 않는다.

### 처음 설정 (한 번)

준비물: 이 저장소의 관리자 권한, 이메일 주소. 내 컴퓨터에 아무것도 설치하지 않아도 된다.

**1. Cloudflare 계정**

1. https://dash.cloudflare.com/sign-up 에서 무료 계정을 만들고 이메일을 인증한다.
2. 왼쪽 메뉴 **Compute (Workers)** → **Workers & Pages**를 연다. 처음이면 `workers.dev` 하위 도메인을 정하라고 한다. 예: `chromedays`.
   그러면 대시보드 주소는 `https://nv-builds.chromedays.workers.dev`가 된다.
3. 같은 화면 오른쪽의 **Account ID**를 복사해 둔다.

**2. R2 버킷**

1. 왼쪽 메뉴 **Storage & Databases** → **R2 Object Storage**를 연다. 처음이면 R2를 켜라고 하는데, 결제 수단을 물을 수 있다. 무료
   한도(10 GB, 월 100만 쓰기) 안에서는 청구되지 않는다.
2. **Create bucket** → 이름 `nv-builds`(정확히 이 이름) → Location은 **Automatic** → **Create bucket**.

**3. Cloudflare API 토큰 (CI용)**

1. 오른쪽 위 프로필 → **My Profile** → **API Tokens** → **Create Token**.
2. **Edit Cloudflare Workers** 템플릿의 **Use template**.
3. **Permissions**에 **Add more** → `Account` / `Workers R2 Storage` / `Edit`를 더한다.
4. **Account Resources**: `Include` / 내 계정. **Zone Resources**: `Include` / `All zones`(템플릿이 요구한다; 도메인이 없어도 된다).
5. **Continue to summary** → **Create Token** → 토큰을 복사한다. 이 화면을 닫으면 다시 볼 수 없다.

**4. 무작위 값 두 개**

긴 무작위 문자열 두 개를 만든다. 하나는 `DASHBOARD_TOKEN`(CI가 빌드를 등록할 때), 하나는 `GITHUB_WEBHOOK_SECRET`(웹훅 서명)이다.
비밀번호 관리자의 생성기로 40자 이상을 만들거나, 터미널이 있으면 `openssl rand -hex 32`를 두 번 실행한다.

**5. GitHub Actions에 넣기**

https://github.com/chromedays/engine/settings/secrets/actions 에서:

1. **Secrets** 탭 → **New repository secret**를 세 번:
   - `CLOUDFLARE_API_TOKEN`: 3단계의 토큰
   - `CLOUDFLARE_ACCOUNT_ID`: 1단계의 Account ID
   - `DASHBOARD_TOKEN`: 4단계의 첫 값
2. **Variables** 탭 → **New repository variable**:
   - `DASHBOARD_URL`: `https://nv-builds.<하위 도메인>.workers.dev` (끝에 `/` 없이)

**6. Worker 배포**

1. 대시보드 PR을 master에 머지한다. `dashboard` 워크플로가 Worker를 배포한다. 이미 머지했다면
   https://github.com/chromedays/engine/actions/workflows/dashboard.yml → **Run workflow** → **Run workflow**.
2. 실행이 초록으로 끝나면 `DASHBOARD_URL`을 브라우저로 연다. 빈 대시보드가 보이면 된다.

**7. Worker에 비밀값 넣기**

Cloudflare 대시보드 → **Workers & Pages** → `nv-builds` → **Settings** → **Variables and Secrets** → **Add**에서 종류를 **Secret**으로
하고 차례로 넣은 뒤 **Deploy**:

- `DASHBOARD_TOKEN`: 4단계의 첫 값 (5단계와 같은 값)
- `GITHUB_WEBHOOK_SECRET`: 4단계의 두 번째 값
- `GITHUB_TOKEN` (권장): GitHub 읽기 전용 토큰. https://github.com/settings/personal-access-tokens/new 에서 **Fine-grained token**,
  Repository access는 **Only select repositories** → `chromedays/engine`, Permissions는 **Actions: Read-only**, **Contents: Read-only**,
  **Pull requests: Read-only**. 없어도 돌지만, 5분마다의 재동기화가 Cloudflare의 공유 IP 한도에 걸릴 수 있다.

**8. GitHub 웹훅**

https://github.com/chromedays/engine/settings/hooks → **Add webhook**:

- **Payload URL**: `https://nv-builds.<하위 도메인>.workers.dev/webhook`
- **Content type**: `application/json`
- **Secret**: 4단계의 두 번째 값
- **SSL verification**: Enable
- **Which events**: **Let me select individual events** → **Branch or tag deletion**, **Pull requests**, **Pushes**, **Workflow runs**만
  켠다(**Pushes**는 기본으로 켜져 있다).
- **Active** → **Add webhook**. **Recent Deliveries**의 첫 `ping`이 `204`면 된다.

**9. 첫 빌드 채우기**

열린 PR과 master는 5분 안에 재동기화로 대시보드에 나타난다. 빌드는 각 브랜치의 다음 push에 올라간다. 바로 채우려면
https://github.com/chromedays/engine/actions/workflows/build.yml 에서 master의 가장 최근 실행을 열고 **Re-run all jobs**를 누른다.
열린 PR 브랜치도 같은 식으로 다시 실행하면 된다.

### 로컬에서 돌리기

Node 20 이상이 필요하다. Wrangler는 `npx`가 받아 실행하고 저장소에는 설치하지 않는다.

```sh
cd tools/dashboard
printf 'GITHUB_WEBHOOK_SECRET=test-secret\nDASHBOARD_TOKEN=test-token\n' > .dev.vars   # 로컬 전용, Git이 무시한다
npx wrangler@4.150.0 dev                                                                # http://localhost:8787
```

`wrangler dev`는 Worker, Durable Object, R2를 내 컴퓨터에서 돌린다(`.wrangler/`에 저장, Git이 무시한다). `d`를 누르면 DevTools가
붙는다. 빌드를 올리려면 CI처럼 `publish.sh`를 `R2_TARGET=--local`과 함께 실행한다(`BRANCH`, `SHA`, `REPO`, `DEFAULT_BRANCH`,
`DASHBOARD_URL=http://localhost:8787`, `DASHBOARD_TOKEN=test-token`, `gh` 로그인 필요). 웹훅은 `.dev.vars`의 시크릿으로 서명해서
`POST /webhook`에 보낸다(`X-GitHub-Event`, `X-Hub-Signature-256`). 상태 규칙의 시험은 `node tests/dashboard_test.mjs`.

## English

A page that shows the auto-battler's builds of master and of every branch with an open pull request, and reflects pushes, pull
requests and CI results within seconds. It runs on one Cloudflare Worker, one Durable Object and one R2 bucket. The design is in
`docs/specs/build_dashboard.md`.

The repository holds no secret. The tokens and secrets made below go only into GitHub Actions secrets and Worker secrets; nothing
is committed.

### First setup (once)

You need admin rights on this repository and an email address. Nothing has to be installed on your computer.

**1. Cloudflare account**

1. Create a free account at https://dash.cloudflare.com/sign-up and verify the email.
2. Open **Compute (Workers)** → **Workers & Pages** in the left menu. The first time, it asks for a `workers.dev` subdomain, for
   example `chromedays`. The dashboard's address is then `https://nv-builds.chromedays.workers.dev`.
3. Copy the **Account ID** on the right of that screen.

**2. R2 bucket**

1. Open **Storage & Databases** → **R2 Object Storage** in the left menu. The first time, it asks to turn R2 on and may ask for a
   payment method. Nothing is charged within the free tier (10 GB, a million writes a month).
2. **Create bucket** → name `nv-builds` (exactly this) → Location **Automatic** → **Create bucket**.

**3. Cloudflare API token (for CI)**

1. Profile at the top right → **My Profile** → **API Tokens** → **Create Token**.
2. **Use template** on **Edit Cloudflare Workers**.
3. Under **Permissions**, **Add more** → `Account` / `Workers R2 Storage` / `Edit`.
4. **Account Resources**: `Include` / your account. **Zone Resources**: `Include` / `All zones` (the template needs it; no domain is
   needed).
5. **Continue to summary** → **Create Token** → copy the token. It is not shown again once the screen closes.

**4. Two random values**

Make two long random strings: one is `DASHBOARD_TOKEN` (CI registering builds), the other `GITHUB_WEBHOOK_SECRET` (webhook
signatures). Use a password manager's generator for 40 characters or more, or run `openssl rand -hex 32` twice in a terminal.

**5. Put them in GitHub Actions**

At https://github.com/chromedays/engine/settings/secrets/actions:

1. **Secrets** tab → **New repository secret**, three times:
   - `CLOUDFLARE_API_TOKEN`: the token from step 3
   - `CLOUDFLARE_ACCOUNT_ID`: the Account ID from step 1
   - `DASHBOARD_TOKEN`: the first value from step 4
2. **Variables** tab → **New repository variable**:
   - `DASHBOARD_URL`: `https://nv-builds.<subdomain>.workers.dev` (no trailing `/`)

**6. Deploy the Worker**

1. Merge the dashboard's pull request into master. The `dashboard` workflow deploys the Worker. If it is already merged, open
   https://github.com/chromedays/engine/actions/workflows/dashboard.yml → **Run workflow** → **Run workflow**.
2. When the run is green, open `DASHBOARD_URL` in a browser. An empty dashboard means it worked.

**7. Give the Worker its secrets**

Cloudflare dashboard → **Workers & Pages** → `nv-builds` → **Settings** → **Variables and Secrets** → **Add**, type **Secret**, one
at a time, then **Deploy**:

- `DASHBOARD_TOKEN`: the first value from step 4 (the same as in step 5)
- `GITHUB_WEBHOOK_SECRET`: the second value from step 4
- `GITHUB_TOKEN` (recommended): a read-only GitHub token. At https://github.com/settings/personal-access-tokens/new, a
  **Fine-grained token** with Repository access **Only select repositories** → `chromedays/engine`, and Permissions **Actions:
  Read-only**, **Contents: Read-only**, **Pull requests: Read-only**. It runs without one, but the resync every 5 minutes may hit
  the limit of Cloudflare's shared IP addresses.

**8. GitHub webhook**

https://github.com/chromedays/engine/settings/hooks → **Add webhook**:

- **Payload URL**: `https://nv-builds.<subdomain>.workers.dev/webhook`
- **Content type**: `application/json`
- **Secret**: the second value from step 4
- **SSL verification**: Enable
- **Which events**: **Let me select individual events** → only **Branch or tag deletion**, **Pull requests**, **Pushes** and
  **Workflow runs** (**Pushes** is on by default).
- **Active** → **Add webhook**. The first `ping` under **Recent Deliveries** should show `204`.

**9. Fill in the first builds**

Open pull requests and master appear on the dashboard within 5 minutes, from the resync. Their builds are uploaded at each branch's
next push. To fill them at once, open the newest master run at https://github.com/chromedays/engine/actions/workflows/build.yml and
press **Re-run all jobs**; do the same for open pull requests' branches.

### Running it locally

Needs Node 20 or later. `npx` fetches and runs Wrangler; nothing is installed into the repository.

```sh
cd tools/dashboard
printf 'GITHUB_WEBHOOK_SECRET=test-secret\nDASHBOARD_TOKEN=test-token\n' > .dev.vars   # local only; Git ignores it
npx wrangler@4.150.0 dev                                                                # http://localhost:8787
```

`wrangler dev` runs the Worker, the Durable Object and R2 on your computer (kept in `.wrangler/`, which Git ignores). Press `d` to
attach DevTools. To upload a build, run `publish.sh` as CI does, with `R2_TARGET=--local` (and `BRANCH`, `SHA`, `REPO`,
`DEFAULT_BRANCH`, `DASHBOARD_URL=http://localhost:8787`, `DASHBOARD_TOKEN=test-token`, and a signed-in `gh`). Send webhooks to
`POST /webhook`, signed with the secret in `.dev.vars` (`X-GitHub-Event`, `X-Hub-Signature-256`). The state rules' test is
`node tests/dashboard_test.mjs`.

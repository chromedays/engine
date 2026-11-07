# 시각 회귀 테스트 결과 페이지 스펙

상태: 구현됨 (2026-11-07). 이 스펙의 변경은 먼저 합의한다.

### 목표

`just visual`이 끝나면 결과를 터미널의 경로 세 줄이 아니라 웹 페이지 하나로 본다. 기준 그림, 실제 그림, diff 그림을
나란히 놓고, 겹쳐 보고, 확대해서 어느 픽셀이 얼마나 달라졌는지 바로 읽는 것이 목적이다. 지금은 실패하면 PNG 세 개를
따로 열어 눈으로 맞춰 보아야 하고, 임계값 아래의 작은 차이(톤 매핑, 반올림)는 그렇게 해서는 거의 보이지 않는다.

```sh
just visual
# ...
# compare: 1 of 1 case(s) differ. ...
# report: file:///home/.../build-visual/visual-out/report.html     (터미널에서 눌러 연다)
```

이 스펙은 `docs/specs/visual_regression.md`의 비교 단계(`tools/visual/compare.mjs`)에 출력 하나를 더한다. 캡처, 합격 규칙,
종료 코드, 기준 그림 갱신은 바뀌지 않는다.

합의된 결정:

- 실행이 끝나면 페이지의 링크(`file://` URL)를 터미널에 출력한다. 브라우저를 스스로 띄우지 않는다: 여는 것은 사람이 링크를
  눌러서 한다. NOTE: 플랫폼마다 다른 여는 명령, SSH와 그래픽 세션 판별, 여러 번 돌릴 때 쌓이는 창이 모두 없어진다.
- 보고서 쪽(비교 스크립트, 보고서 생성, 페이지)은 플랫폼을 가리지 않는다. Linux 전용인 것은 캡처뿐이다.
- 그림은 페이지 안에 담는다 (`data:` URI). 페이지는 HTML 파일 하나다.
- 페이지는 판정을 다시 하지 않는다. 임계값을 바꿔 다시 세는 기능은 없다. 판정은 odiff와 `compare.mjs`의 것 하나뿐이다.

범위 밖:

- 페이지에서 기준 그림을 받아들이기(Accept 버튼). 정적 페이지는 저장소에 쓸 수 없고, 서버를 띄우면 이 테스트에 없던
  상태가 생긴다. 페이지는 `just visual-update-ref`를 안내만 한다.
- 실행 사이의 기록(이전 실행과 비교, 추세). 페이지는 마지막 실행 하나만 보여 준다.
- CI 아티팩트로 올리기. 이 테스트에는 CI가 없다 (`visual_regression.md`의 결정).
- 캡처한 기계의 GPU/드라이버 이름. `visual_test`가 어댑터 정보를 남기게 되면 메타데이터 줄에 더한다.
- Windows/macOS에서의 캡처와 `justfile` 레시피. 레시피는 지금처럼 `[unix]`이고, Windows 캡처는 `visual_test`의 D3D12판이
  필요하므로 별도 스펙의 일이다. 이 스펙은 보고서 쪽만 플랫폼을 가리지 않게 만든다 ("링크").
- 브라우저를 스스로 여는 것 (합의된 결정).
- 페이지에서 임계값이나 허용 비율을 바꿔 다시 판정하기. 판정 규칙이 두 곳에 생긴다.

### 의존성

없다. Node의 기본 모듈만 쓰고(`compare.mjs`와 같다), 페이지는 CSS와 JavaScript를 안에 담은 HTML 파일 하나다.
CDN, 글꼴, 외부 스크립트를 쓰지 않는다. NOTE: 오프라인에서, `file://`로, 다른 컴퓨터로 옮겨서도 열려야 해서다.

### 출력

| 파일 | 내용 |
|---|---|
| `build-visual/visual-out/report.html` | 결과 페이지. 기준과 실제 그림을 `data:image/png;base64,...`로 안에 담는다 (diff는 페이지가 그린다) |
| `build-visual/visual-out/report.json` | 같은 결과를 기계가 읽는 형태로. 에이전트와 스크립트가 HTML을 파싱하지 않게 한다 |

- 그림을 안에 담는 이유: (1) 파일 하나라 옮기고 첨부할 수 있다. (2) `file://`의 그림을 캔버스에 그리면 Chrome은 캔버스를
  오염된 것(tainted)으로 보아 `getImageData`를 막는다. 픽셀 값 읽기("페이지" 절)는 `data:` URI여야 된다.
  (3) 나중에 `just visual-update-ref`가 기준 그림을 덮어써도 페이지는 비교한 그때의 기준을 보여 준다.
- 크기: 1280×720 PNG 하나는 단순한 씬에서 수십 KB, 복잡한 씬에서도 몇 MB다. 케이스당 두 장이므로 케이스가
  수십 개가 되기 전에는 문제가 없다. 케이스가 늘어 페이지가 무거워지면 그때 파일 복사 방식으로 바꾼다.
- 두 파일은 실행마다 새로 쓴다. 비교가 끝나기 전에 멈추는 실행 오류(종료 코드 2)에서도 그때까지의 결과와 오류 문장으로 쓴다.
  odiff가 없거나 기준 디렉터리가 없어 케이스 목록조차 없으면 쓰지 않는다 (지금처럼 터미널 문장만 남긴다).
- `compare.mjs`는 마지막 줄에 `report: file://<절대 경로>`를 출력한다 ("링크").
- 출력 디렉터리의 바로 아래에만 쓰고, `.png`가 아니므로 `visual-update-ref`의 복사(`visual-out/*.png`, `*.diff.png` 제외)에 걸리지 않는다.

#### 링크

보고서 쪽(`compare.mjs`, `report.mjs`, 페이지)은 플랫폼을 가리지 않는다. 지금 캡처는 Linux Wayland에서만 되지만
(`visual_regression.md`), 비교와 보고서는 Node와 브라우저만 있으면 Linux, Windows, macOS 어디서나 같게 돈다. NOTE: 나중에
Windows(D3D12) 캡처가 생기면 이 부분은 그대로 쓴다.

- 마지막 줄은 `report: <URL>` 하나다. 합격, 실패, 실행 오류(보고서를 쓴 경우) 모두 같다. 줄에는 URL 말고 다른 글자를 붙이지
  않는다 (마침표, 괄호, 따옴표). NOTE: 터미널의 URL 인식이 뒤에 붙은 글자까지 링크로 잡지 않게 한다.
- 대부분의 터미널(Windows Terminal, VS Code, GNOME Terminal, Konsole, kitty, iTerm2)은 `file://` URL을 스스로 링크로 만들어
  Ctrl+클릭(macOS는 Cmd+클릭)으로 연다. 그렇지 않은 터미널에서는 URL을 복사해 브라우저 주소창에 붙여 넣는다. 둘 다 이 스펙의 일이 아니다.
- OSC 8 하이퍼링크 이스케이프 시퀀스는 쓰지 않는다. URL 인식으로 충분하고, 지원하지 않는 터미널과 파일로 돌린 출력(로그,
  에이전트)에 제어 문자가 남는다.
- SSH로 돌리면 링크는 원격 기계의 파일을 가리켜 로컬 브라우저로는 열리지 않는다. 이때는 `report.html` 하나를 복사해 온다.
  그림이 안에 담겨 있어 파일 하나면 된다 ("출력").

#### 경로

- 출력하는 URL은 `url.pathToFileURL(경로).href`로 만든다. 손으로 `'file://' + 경로`를 붙이지 않는다: Windows의 드라이브 문자와
  `\`, 그리고 공백/`#`/`%`가 든 경로가 깨진다.
- `report.json`의 경로는 그 플랫폼의 절대 경로 그대로다 (`path.resolve`). 페이지는 그것을 글자로 보여 주기만 하고 열지 않는다.
- 줄바꿈과 경로 구분자에 기대는 코드를 쓰지 않는다 (`path.join`, `path.basename`만 쓴다).

#### 남은 diff 그림

odiff는 같을 때 diff 그림을 쓰지 않으므로, 앞 실행에서 실패했던 케이스가 이번에 합격해도 옛 `<이름>.diff.png`가 남는다.
`compare.mjs`는 케이스마다 odiff를 부르기 전에 그 diff 그림을 지운다. 그래야 페이지(와 사람)가 이번 실행의 diff만 본다.

#### report.json

```json
{
  "version": 1,
  "time": "2026-11-07T10:21:33Z",
  "commit": "d462f2b+",
  "subject": "Merge pull request #24 ...",
  "odiff": "4.5.0",
  "threshold": 0.05,
  "maxDiffPercent": 0.1,
  "passed": 0,
  "failed": 1,
  "error": null,
  "cases": [
    {
      "name": "triangle",
      "status": "fail",
      "reason": "pixels",
      "width": 1280, "height": 720,
      "diffPixels": 2210, "diffPercent": 0.24,
      "baseline": "/abs/tests/visual/baseline/triangle.png",
      "actual": "/abs/build-visual/visual-out/triangle.png",
      "diff": "/abs/build-visual/visual-out/triangle.diff.png"
    }
  ]
}
```

- `layout`이면 `width`/`height`는 기준 그림의 크기이고 `actualWidth`/`actualHeight`가 더해진다. `missing`이면 `actual`은
  `null`이고 찾은 경로가 `missing`에 있다. 그림이 없는 자리(`diff`가 없는 `identical` 등)는 `null`이다.
- `odiff`는 `odiff --version`의 출력에서 읽는다 (4.5.0은 stderr에 쓴다). 읽지 못하면 `null`이다.
- `status`는 `pass`, `fail`. `reason`은 `identical`(같음), `within`(허용치 안에서 다름), `pixels`(허용치를 넘음),
  `layout`(크기가 다름), `missing`(캡처 없음).
- `commit`과 `subject`는 `git rev-parse --short HEAD`, `git status --porcelain`(비어 있지 않으면 `+`), `git log -1 --format=%s`로
  얻는다. 빌드 라벨(`nv_imgui_draw_build_label`)과 같은 표기다. Git이 없으면 `null`이다.
- 필드를 바꾸거나 지우면 `version`을 올린다. 더하는 것은 올리지 않는다.

### 페이지

문구는 영어다 (`compare.mjs`의 출력, 로그 줄과 같다). `T()`를 거치지 않는다: 엔진의 실행 파일이 아니다.

#### 머리

- 결과 한 줄: 모두 합격이면 초록 "All N cases pass", 아니면 빨강 "M of N cases differ". 실행 오류가 있으면 그 문장을 맨 위에 둔다.
- 메타데이터 한 줄: 시각, 커밋(+), 커밋 제목, odiff 버전, 임계값, 허용 비율.
- 실패가 있으면 안내 한 줄: "If the change is intended, run `just visual-update-ref`." 명령은 눌러서 복사한다.
- 상태로 거르기: All / Failed / Passed. 기본은 실패가 있으면 Failed, 없으면 All.

#### 케이스

케이스마다 카드 하나. 실패한 케이스가 먼저 오고, 같은 상태 안에서는 이름순이다. 카드는 처음에 모두 펼쳐져 있다
(합격한 카드도). 머리줄을 누르면 접힌다.

- 머리줄: 상태 표시(색과 글자 둘 다: 색만으로 구분하지 않는다), 이름, 크기, 다른 픽셀 수와 비율, 허용 비율.
  `layout`은 두 크기를, `missing`은 찾은 경로를 적는다. 파일 경로 셋은 펼친 곳의 아래에 작게 둔다.
- 보기 방식 네 가지 (탭):
  - **Side by side**: 기준, 실제, diff 세 장. 한 장을 확대/이동하면 나머지 둘이 같이 따라간다.
  - **Swipe**: 기준과 실제를 겹치고, 세로 경계선을 끌어 왼쪽은 기준, 오른쪽은 실제를 본다.
  - **Toggle**: 기준과 실제를 같은 자리에서 번갈아 본다. 누르거나 Space로 바꾸고, Flicker를 켜면 0.5초마다 저절로 바뀐다.
    NOTE: 작은 차이는 나란히 볼 때보다 번갈아 볼 때 훨씬 잘 보인다.
  - **Diff**: 페이지가 기준과 실제의 바이트를 비교해 그리는 차이 지도(heatmap). 바이트가 같은 픽셀은 흰색, 다른 픽셀은
    빨강이고, 차이가 클수록 진하다. 픽셀의 차이는 네 채널 차이의 절댓값 중 가장 큰 것(0–255)이고, 빨강의 세기는
    `0.15 + 0.85 × √(차이 / 255)`다. NOTE: 모든 케이스에 같은 척도를 쓴다(그 그림의 최댓값에 맞추지 않는다): 작은 차이는 옅게
    보이고, 1의 차이도 보인다. 이름표는 그 그림의 가장 큰 차이를 적는다("Diff: redder is larger, up to 223 of 255 here").
    바이트가 모두 같으면 흰 칸이고 이름표는 "Diff: identical"다.
    - odiff의 임계값 아래의 차이도 칠한다 (합의, 2026-11-07). 목표의 "임계값 아래의 작은 차이"를 보이기 위해서다. 그래서
      odiff가 `identical`로 합격시킨 케이스에도 옅은 빨강이 있을 수 있고, 그때 이름표에 "all below the threshold"를 더한다.
    - 이 지도는 보여 주기만 한다: 수를 세지 않고 판정하지 않는다 (합의된 결정). 다른 픽셀 수와 판정은 odiff와
      `compare.mjs`의 것만 보인다.
    - odiff의 diff 그림은 디스크에만 남는다 (`<이름>.diff.png`, `compare.mjs`가 `--diff-overlay 1`을 주어 같은 픽셀은 흰색,
      다른 픽셀은 빨강인 두 색 그림; 경로는 카드에 적는다). 페이지에 담지 않는다. NOTE: odiff의 기본 diff 그림은 기준 그림 위에
      빨강을 칠한 것이라 원본과 거의 같아 보이고, `--diff-overlay`의 값은 기준 그림을 흰색으로 덮는 비율이라 1이면 기준 그림이
      남지 않는다. odiff는 세기를 주지 않는다 (픽셀마다 같다/다르다뿐이다).
    - 크기가 다르거나(`layout`) 캡처가 없으면(`missing`) 비교할 수 없으므로 diff 자리는 이유를 적는다.
- 확대: Fit, 1×, 2×, 4×, 8×와 휠. 확대한 그림은 `image-rendering: pixelated`로 픽셀이 뭉개지지 않는다. 드래그로 이동한다.
  확대 배율과 위치는 카드 안의 모든 보기 방식이 함께 쓴다.
  그림은 칸(pane)보다 작아지지 않는다: Fit이 가장 작은 배율이고, 휠로 그보다 줄이거나 Fit보다 작은 배율 버튼(넓은 칸의 1× 등)을
  누르면 Fit이 되어 가운데에 놓인다. Side by side의 칸과 단일 보기의 칸은 크기가 달라 Fit 배율도 다르므로, 이 규칙은 칸마다 따로 적용한다.
- 픽셀 값 읽기: 그림 위에 마우스를 두면 그 픽셀의 좌표와 기준/실제의 RGBA(0–255), 그리고 채널별 차이를 카드 아래 한 줄에
  보인다. 그림을 숨은 캔버스에 한 번 그려 `getImageData`로 읽는다.
- 다음 차이로(버튼 "Go to difference"): 차이 지도와 같은 바이트 비교로 다른 픽셀을 감싸는 사각형을 구하고, 버튼 하나로 그곳에 맞춰 확대한다.
  NOTE: 이 비교는 정확히 같은지(바이트)만 보고 임계값을 쓰지 않으며, 수를 세어 보이지도 않는다. 판정과 다른 픽셀 수는
  odiff와 `compare.mjs`의 것만 보인다 (합의된 결정). 페이지는 위치를 찾아 줄 뿐이다. 다른 픽셀이 없으면 버튼은 비활성이다.

#### 그 밖

- 밝은/어두운 테마는 `prefers-color-scheme`를 따른다. 그림 뒤 배경은 체크무늬가 아니라 중간 회색 하나다 (알파는 언제나 255다).
- 휴대폰 폭에서도 읽힌다: Side by side는 세 장이 세로로 쌓인다.
- 페이지는 아무것도 저장하지 않는다 (`localStorage`도 쓰지 않는다).

### 구현

| 파일 | 바뀌는 것 |
|---|---|
| `tools/visual/compare.mjs` | 케이스 결과를 배열에 모은다, diff 그림을 먼저 지운다, 끝에(오류에서도) 보고서를 쓰고 링크를 출력한다. 종료 코드와 기존 출력 줄은 그대로다 |
| `tools/visual/report.mjs` | 새 파일. `writeReport(outDir, run)`: `report.json`과 `report.html`을 쓴다. HTML은 템플릿 리터럴 하나에 CSS와 스크립트를 담고, 결과 JSON을 `<script type="application/json">`에 넣어 페이지의 스크립트가 읽는다 |
| `docs/specs/visual_regression.md` | "구성"의 그림과 파일 표에 보고서를 더한다 |
| `AGENTS.md` | `tests/` 줄의 visual 설명에 결과 페이지(`visual-out/report.html`, `report.json`)를 더한다 |

- HTML에 넣는 문자열(경로, 커밋 제목, 오류 문장)은 모두 이스케이프한다. JSON을 `<script>` 안에 넣을 때는 `<`를 `\u003c`로 바꿔
  `</script>`가 끼어들지 못하게 한다.
- `--report <dir>`로 쓰는 곳을 바꿀 수 있고, 기본은 `--actual` 디렉터리다. `--no-report`는 쓰지 않는다 (링크 줄도 없다).
- `justfile`은 바뀌지 않는다 (`visual`이 이미 `compare.mjs`를 부른다).

### 검증

- 합격하는 실행: 페이지가 "All 1 case passes"를 보이고, 카드가 펼쳐져 있고, Diff 자리가 "Diff: identical" 이름표를 단
  흰 칸이다.
- 차이 지도: 전체를 1% 밝게 한 캡처(임계값 아래, `identical`)는 옅은 분홍과 "all below the threshold"를, 0에서 64까지 커지는
  차이는 옅은 쪽에서 진한 쪽으로 가는 빨강을 보인다.
- 실패하는 실행: 기준 그림의 한 영역을 칠한 복사본을 `--baseline`으로 주고 돌린다. 카드가 펼쳐져 있고, 다른 픽셀 수가
  터미널 출력과 같고, "다음 차이로"가 칠한 영역으로 확대하고, 픽셀 값 읽기가 칠한 색을 기준 쪽에 보인다.
- 크기가 다른 기준과 캡처가 없는 케이스: 각각 `layout`, `missing`으로 보이고 그림이 없는 자리는 문장으로 채워진다.
- 앞 실행의 diff 그림이 남은 채 합격하는 실행: 그 diff 그림이 지워지고 페이지에 나오지 않는다.
- 페이지를 다른 디렉터리로 옮겨 `file://`로 열어도 모든 그림과 픽셀 값 읽기가 된다. 네트워크를 끈 채로도 된다.
- 경로에 `<`와 따옴표가 든 디렉터리로 돌려 페이지가 깨지지 않는지 본다 (이스케이프).
- `just visual`의 마지막 줄이 `report: file:///...report.html` 하나이고, 그 줄을 터미널에서 Ctrl+클릭하면 페이지가 열린다.
  출력을 파일로 돌렸을 때 그 줄에 제어 문자가 없다.
- Windows: Linux에서 만든 `visual-out`과 기준 디렉터리를 복사해 `node tools/visual/compare.mjs --odiff <odiff.exe> ...`로 돌린다.
  출력한 `file:///C:/...` URL을 Windows Terminal에서 Ctrl+클릭하거나 브라우저에 붙여 넣으면 페이지가 열린다. 공백과 `&`가 든 디렉터리에서도 같다.
  (Windows용 odiff는 `cmake/odiff.cmake`가 고정하지 않으므로 `NV_ODIFF`처럼 직접 받은 것을 준다.)
- `report.json`이 위 형식이고, `node -e`로 읽어 `failed`가 종료 코드와 맞는지 본다.
- Chrome과 Firefox에서 열어 본다. 휴대폰 폭(개발자 도구)에서 가로 스크롤이 없는지 본다.

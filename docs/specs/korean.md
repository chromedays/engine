# Korean support spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-10-01). 이 스펙의 변경은 먼저 합의한다.

### 목표

에디터를 한국어로 쓸 수 있게 한다:

1. **한글 표시.** 지금 UI 글꼴 (Inter, `fonts.md`)에는 한글이 없어서, 한국어로 이름을 바꾼 노드가 "?" 상자로
   보인다.
2. **데스크톱에서 한글 입력.** 데스크톱은 `keydown`마다 텍스트를 받는다 (`engine/src/imgui.c`의 `on_key`).
   한국어 입력기 (IME)는 여러 키에 걸쳐 음절을 조합하고 그것을 "Process" 키로 알리므로, 조합된 텍스트가 결코
   도착하지 않는다. 폰은 이미 숨겨진 `<input id="nv-text-agent">`로 입력하는데, 그 `compositionend`와
   `input` 이벤트가 완성된 텍스트를 실어 오지만, 음절을 조합하는 동안에는 아무것도 보여 주지 않는다.
3. **한국어 UI.** 메뉴, 탭, 버튼, 라벨, 팔레트의 이름, 도움말 창을 한국어로, View 탭에서 고른다.

범위 밖: 영어와 한국어 외의 언어 (구조는 나중에 허용한다), 글꼴에 있는 몇 개를 넘는 한자, 세로쓰기,
Console 로그 메시지 번역 (개발자 출력, 영어로 유지), 문서 번역, 초성 검색 ("ㄱㅈ"으로 "그림자" 찾기).

### 접근법

#### 한글 표시

| 접근법 | 무엇인가 | 맞음 | 장단점 |
|---|---|---|---|
| **Pretendard의 한글을 서브셋으로 Inter 뒤에 합침** (추천) | 오프라인 스크립트가 KS X 1001의 한글 음절 (일상에서 쓰는 2,350자), 호환용 자모 (ㄱ부터 ㅣ, 조합 중에 보임), 한국어 문자열 표가 쓰는 모든 문자만 남긴다; 결과를 포함하고 Inter에 폴백으로 합친다 (ImGui `MergeMode`) | 라틴은 Inter로 남는다; Pretendard의 한글은 Inter에 맞춰 그려졌다. 압축 전 0.4–0.7 MB로 추정 (1단계에서 측정). 오프라인에서 동작하고 시작할 때 기다림이 없다 | 2,350자 밖의 드문 음절 ("똠", "햏")은 상자로 보인다. 문자열 표에 문자가 늘면 서브셋을 다시 만든다 (스크립트가 확인한다) |
| Pretendard 전체 포함 | 11,172 음절 전부 | 빠진 것이 없다 | 대부분의 방문에서 보이지 않을 텍스트를 위해 모든 첫 로딩에 약 1.5–2.5 MB 추가 (`fonts.md`) |
| Pretendard 전체를 필요할 때 가져오기 | 아무것도 포함하지 않고, 처음 한국어가 필요할 때 가져와 IndexedDB에 둔다 | 영어 사용자에게 공짜 | 한국어 UI가 가져오기가 끝날 때까지 상자로 시작하고, 오프라인에서는 결코 보이지 않는다; 코드가 늘어난다 (가져오기, 캐시, 재시도) |
| Noto Sans KR, 나눔고딕, Spoqa Han Sans Neo (모두 OFL) | 다른 한국어 글꼴 | 좋은 글꼴 | Pretendard가 Inter의 라틴에 맞춰 설계된 것이라, 영어와 한국어가 섞여도 한 글꼴처럼 보인다 |

서브셋 도구 후보 (오프라인, 손으로 실행, 빌드나 저장소의 의존성에는 아무것도 없음):

| 도구 | 언어, 라이선스 | 맞음 |
|---|---|---|
| `npx`를 통한 **`subset-font`** (Node, MIT; WebAssembly로 컴파일한 HarfBuzz 서브셋터) | `tools/` 스크립트가 이미 도는 방식으로 돈다 (`npx`, 저장소에 아무것도 설치하지 않음) | 추천 |
| fontTools `pyftsubset` (Python, MIT) | 표준 서브셋터 | 저장소의 도구들이 쓰지 않는 Python 환경이 필요하다 |

#### 데스크톱에서 한글 입력

| 접근법 | 무엇인가 | 맞음 | 장단점 |
|---|---|---|---|
| **데스크톱에도 텍스트 에이전트, 조합 중에는 캐럿 위치에 보임** (추천) | ImGui가 텍스트를 편집하는 동안 폰처럼 데스크톱에서도 숨겨진 input이 포커스를 가진다; 텍스트는 그 `input`과 `compositionend` 이벤트로 도착한다. 조합 중에는 input이 텍스트 캐럿 위치에 보이게 되어 (ImGui가 `PlatformIO.Platform_SetImeDataFn`으로 캐럿을 준다), 브라우저가 조합 중인 음절을 그리고 IME의 후보 창을 거기 둔다 | 두 UI에 텍스트 경로 하나; 브라우저가 IME 일을 한다 | 조합 중인 음절은 ImGui 필드 안이 아니라 캔버스 위에 페이지 글꼴로 브라우저가 그린다; 끝나면 필드 안으로 사라진다 |
| `keydown`을 유지하고 캔버스에 조합 이벤트 추가 | 캔버스 요소에서 조합을 듣는다 | 변경이 적다 | `<canvas>`는 편집 가능하지 않아서 브라우저가 거기서 IME를 시작하지 않는다 |
| ImGui 안에 조합 그리기 | 조합 중 문자열을 필드에 넣는다 | 자연스러워 보인다 | Dear ImGui의 입력 필드에는 조합 텍스트 지원이 없다; ImGui를 패치해야 한다 |

#### 한국어 UI

| 접근법 | 무엇인가 | 맞음 | 장단점 |
|---|---|---|---|
| **영어 텍스트를 키로 하는 자체 문자열 표** (추천) | `app/strings.c`가 (영어, 한국어) 쌍을 담는다. UI 코드는 `T("Speed")`라고 쓴다; 사용 중인 언어에 맞는 영어나 한국어 텍스트를 돌려주며, 시작할 때 만든 해시 테이블로 찾는다. 위젯 라벨은 ImGui ID를 영어로 유지한다: `TL("Speed")`는 `"속도###Speed"`를 돌려준다 | 코드에서 영어가 원본으로 남아 코드가 지금처럼 읽힌다; 번역이 없으면 영어로 돌아간다. 고정 배열, 순수 C | 모든 UI 문자열을 감싸야 한다; 번역 없는 문자열은 테스트가 나열한다 |
| GNU gettext (C, LGPL, `.po` 파일) | 고전 | 표준 도구 | 두 언어를 위한 런타임 라이브러리와 파일 형식; 웹에서의 LGPL 링크 조건 |
| 문자열 id 열거형 | `T(STR_SPEED)` | 컴파일 시간 검사 | 코드가 더 이상 영어로 읽히지 않고, 모든 문자열에 id가 필요하다 |

추천: 추천한 세 줄. 새 서드파티 요소는 Pretendard 글꼴 (OFL, 에셋)과 `subset-font` 도구 (MIT, 손으로 실행)다;
확인될 때까지 아무것도 추가하지 않는다.

### 결정

#### 글꼴

| 주제 | 결정 |
|---|---|
| 파일 | `assets/fonts/Pretendard-Hangul-Subset.otf` (또는 `.ttf`), 옆에 OFL 라이선스, Pretendard Regular로부터 `tools/subset_hangul.sh`가 만듦. 스크립트의 입력: KS X 1001의 음절, U+3131–U+318E (호환용 자모), U+AC00–U+D7A3은 나열된 것만, 한국어 문자열의 모든 문자 |
| 합치기 | `nv_imgui_set_font`에 폴백이 생긴다: 한글 글꼴을 `MergeMode`로 Inter에 더하므로, 문자열은 Inter에 글리프가 있으면 Inter의 것을, 없으면 Pretendard의 것을 쓴다. 크기와 세로 오프셋을 조정해 (`ImFontConfig.SizePixels` 비율, `GlyphOffset`) 한글이 같은 시각적 높이로 Inter의 기준선에 앉게 한다; 스크린샷으로 확인 |
| 항상 불러옴 | 폴백은 모든 언어에서 시작할 때 불러오므로, 영어 UI에서도 한국어 이름이 보인다 |
| 이름 | 노드 이름은 `NV_NODE_NAME_MAX` (32) 바이트다: 한글 10음절 (UTF-8에서 각 3바이트). `nv_scene_add_node`와 Name 필드는 문자 경계에서 자르고, 문자 중간에서 자르지 않는다 (잘린 음절은 저장에서 잘못된 UTF-8이 된다). 한도 자체는 그대로다 |

#### 입력

| 주제 | 결정 |
|---|---|
| 포커스 | 데스크톱에서도 `io.WantTextInput`이 켜지면 텍스트 에이전트가 포커스를 가져가고, 꺼지면 돌려준다 (폰의 규칙, `imgui.c`). 포커스를 가진 동안 `on_key`는 키 (화살표, Enter, Escape, Backspace, 단축키)를 전달하지만 문자는 전달하지 않는다, 폰에서 이미 그러듯이 |
| 조합 표시 | `Platform_SetImeDataFn`이 매 프레임 캐럿 위치와 줄 높이를 알린다; 에이전트가 거기로 옮긴다. `compositionstart`에서 보이게 되고 (불투명 텍스트, 필드의 텍스트 색, 어두운 배경, CSS 픽셀로 ImGui와 같은 글꼴 크기) `compositionend`에서 다시 숨는다. IME의 후보 창이 그것을 따라간다 |
| 조합 중의 키 | IME가 소비하는 키 (`event.isComposing`, 또는 키 "Process")는 ImGui에도 단축키 표에도 주지 않으므로, 조합이 W, E, R, Space를 발동시키지 않는다 |
| 검색 상자와 팔레트 | ImGui 필드다: 거기 입력한 한글도 똑같이 동작하고, 매처 (`search.md`)는 UTF-8 바이트를 비교하므로 "그림"이 "그림자"를 찾는다 |

#### 한국어 UI

| 주제 | 결정 |
|---|---|
| 문자열 | 텍스트에는 `T(english)`, 위젯 라벨에는 `TL(english)` (`"번역###English"`, 그래서 위젯의 ID와 그 상태가 두 언어에서 같다). 번역 대상: 메뉴, 탭 이름, 버튼, 라벨, 툴팁, 섹션 제목, 팔레트의 액션 이름과 종류, 도움말 창, Play 안내, View 탭에 보이는 저장 메시지, 검색 상자 힌트. 번역하지 않음: Console 로그 행, 노드 이름, 에셋의 클립과 텍스처 이름, 빌드 라벨의 커밋 줄, 숫자와 단위 |
| 설정 | View 탭의 **Language** 콤보 (English, 한국어), `search_row`를 거침 (두 언어로 검색됨). 다음 프레임에 적용된다 |
| 기본값 | 첫 시작에는 브라우저 언어로 (`navigator.language`가 "ko"로 시작하면 한국어), 그 뒤에는 고른 것 |
| 저장 | 새 `EDIT` 태그 `LANG` (u32: 0 영어, 1 한국어; 다른 값은 영어로 불러옴). 그것이 없는 저장은 브라우저 언어를 쓴다. `SAVE_VERSION`은 그대로. 에디터 설정이고 undo되지 않는다 |
| 검색 | 맞춰 볼 행의 텍스트는 영어 라벨, 한국어 라벨, 키워드다, 그래서 "shadow"와 "그림자" 둘 다 어느 언어에서든 Shadows 섹션을 찾는다; 팔레트의 액션도 마찬가지 |
| 배치 | 한국어 라벨을 가장 넓은 문자열로 모든 도크, 폰 패널, 상단 바에서 확인한다; 그것을 자르는 고정 폭 (폰의 Find와 Undo 버튼, 데스크톱 Play 버튼)은 넓히거나 더 짧은 말을 쓴다 |
| 빠진 번역 | Debug 빌드는 한국어가 보이는 동안 한국어 항목이 없는 문자열을 각각 한 번 기록한다; ctest 테스트 (`tests/`)는 `app/`의 모든 `T`/`TL` 리터럴에 항목이 있는지 확인한다 |

### 변경

- **엔진 (`nv/imgui.h`, `engine/src/imgui.c`).** `nv_imgui_set_font`의 한글 폴백 (두 번째 글꼴 인자), 데스크톱의
  텍스트 에이전트, `Platform_SetImeDataFn`, 조합 오버레이, 조합 중 막는 키, 문자 경계에서 이름 자르기
  (`engine/src/scene.c`).
- **앱.** `app/strings.c`와 `app/strings.h` (표, `T`, `TL`, 언어), `app/*.c`의 모든 UI 문자열을 감싸는 `T`/`TL`,
  View 탭의 Language 행, `app/save.c`의 `LANG` 태그, 두 언어의 검색 텍스트 (`app/search.c`), `app/main.c`에서
  불러오는 글꼴 파일.
- **도구와 에셋.** `tools/subset_hangul.sh` (`npx subset-font`), `assets/fonts/`의 서브셋 글꼴과 그 라이선스.
- **테스트.** `tests/strings.c`: `T`/`TL`로 감싼 모든 리터럴에 한국어 항목이 있다.
- **문서.** `fonts.md`, `save.md` (태그), `search.md`, `layout.md`, `AGENTS.md` (UI 문자열은 `T`/`TL`을 거친다;
  표의 위치), README (Pretendard 크레딧).

### 단계

1. **한글 표시:** 서브셋 스크립트와 글꼴, 폴백 합치기, 문자 경계에서 자르는 이름. 확인: 한글로 이름을 바꾼
   노드 (데스크톱에서 붙여 넣기, 폰에서 입력)가 같은 기준선에서 라틴 옆에 음절을 보여 주고, 새로고침과 저장
   왕복을 견디며, 열 번째 음절은 들어가고 열한 번째는 통째로 거부된다; 패키지 크기 전후.
2. **데스크톱 입력:** 데스크톱의 텍스트 에이전트, 캐럿 위치의 조합 오버레이, 조합 중 막는 키. Playwright의
   IME 이벤트 (CDP `Input.imeSetComposition`과 `Input.insertText`)로 확인: Name 필드, 검색 상자, 팔레트에
   "그림자" 입력; 조합 중에 단축키가 발동하지 않음; Escape, Enter, Backspace가 여전히 동작; 폰은 바뀌지 않음.
3. **한국어 UI:** 문자열 표, 앱 전체의 `T`/`TL`, Language 행과 태그, 두 언어 검색, 빠진 문자열 테스트. 확인:
   모든 도크, 메뉴, 팝업, 팔레트, 도움말 창이 1280×800과 390×664에서 잘린 라벨 없이 한국어로, 새로고침이 언어를
   유지, "그림자"와 "shadow"가 같은 행을 찾음, 한국어로 설정된 새 브라우저가 한국어로 시작.
4. **문서.**

모든 단계는 헤드리스 Chromium에서 Release와 Debug로 확인한다.

만든 결과와 만들면서 알게 된 점:

- **한글 글꼴.** `assets/fonts/Hangul-Subset.ttf` (387 KB; `app/strings.c`의 모든 문자가 KS X 1001의 2,350자 안에
  있어서 스크립트가 더한 것은 없다), Pretendard Regular의 TrueType 파일로부터 `tools/subset_hangul.sh`가 만듦.
  OFL이 "Pretendard"라는 이름을 수정하지 않은 글꼴에 예약하므로 이름을 바꿨다 (라이선스의 예약 이름 조항은
  사용자에게 보이는 이름을 다루는데, 앱은 그것을 보여 주지 않는다); 글꼴 자체의 name 테이블은 저자가 공개한
  서브셋처럼 여전히 Pretendard의 것이다. `nv_imgui_set_font`는 폴백을 두 번째 글꼴로 받아 합친다
  (`MergeMode`); 한글은 조정 없이 같은 크기로 Inter의 기준선에 앉았다. 패키지가 0.39 MB 커졌다.
- **이름.** `nv_utf8_fit` (`nv/base.h`)은 문자 단위로 자른다: `nv_scene_add_node`와 저장에서 읽은 이름은 온전한
  음절을 유지한다 (32바이트에 10자가 들어간다). ImGui 자체 텍스트 필드는 이미 들어가지 않는 문자를 거부한다.
- **텍스트를 자르거나 비교하는 모든 곳에 UTF-8.** `nv_utf8_length`, `nv_utf8_fit`, `nv_utf8_trim` (`nv/base.h`,
  `tests/utf8_test.c`가 테스트): `snprintf`로 고정 버퍼에 복사한 문자열 (undo 단계 이름, 팔레트 행, 검색의 라벨,
  섹션, 키워드, 후보와 질의 단어, 저장 뷰어의 문자열, 번역된 위젯 라벨)은 어떤 문자도 반으로 잘리지 않게
  다듬는다. 검색은 바이트가 아닌 문자 단위로 대소문자를 접는다: ASCII, Latin-1과 Latin Extended-A, 그리스,
  키릴 (모두 바이트 길이를 유지하므로 일치의 오프셋이 원본 텍스트에서도 같고, 하이라이트가 그것을 쓴다), 그리고
  전각 공백 (U+3000)을 단어 구분자로 다룬다. 한글에는 대소문자가 없으므로 그대로 비교한다.
- **입력.** 데스크톱에서 텍스트 에이전트는 `WantTextInput`이 켜지면 포커스를 가져가고, ImGui가 알리는 캐럿
  위치 (`Platform_SetImeDataFn`)에 앉아, 조합 중에는 음절을 보여 준다 (밝은 상자 위의 어두운 텍스트: Chromium이
  조합 위에 자체 하이라이트를 칠한다). 에이전트의 키: 입력기가 가져가는 것 ("Process")은 그대로 둔다; 텍스트를
  입력하지 않는 키와, 붙여 넣기를 뺀 조합 키는 ImGui에서만 동작하고 에이전트에는 가지 않는다 (그 캐럿이
  움직이면 안 되고, Ctrl+Z가 거기서 undo하면 안 된다); 입력과 Backspace는 에이전트에 남는다. 그 결과 필드에
  입력한 글자는 더 이상 브라우저로부터 막히지 않는다 (그것에 대해 `defaultPrevented`가 false), 텍스트 필드에는
  원래 필요 없던 일이다. CDP `Input.imeSetComposition`과 `Input.insertText`로 확인.
- **문자열.** `T(english)`는 텍스트를, `TL(english)`는 위젯 라벨 `text###English`를 두 언어로 돌려주므로, 언어가
  바뀌어도 위젯이 id를 유지한다 (탭이 제자리를 지킨다). `app/strings.c`에 222행의 표, 처음 쓸 때 만든 해시
  테이블로 찾는다. `local_persist`였던 콤보 항목 배열은 호출마다 `T()`로 만든다. `tests/strings_test.mjs`
  (ctest `strings_test`)는 코드가 감싸거나, 검색에 넘기거나 (`search_row`, `search_group`, `search_section`),
  단축키 표에 넣거나 (그룹, 이름, 키 텍스트), 통계, 텍스처 메모, 패널 이름에 쓰는 문자열에 행이 없을 때, 그리고
  행의 printf 변환이 영어 텍스트와 다를 때 실패한다.
- **검색.** 행의 검색 텍스트는 영어 라벨, 그 한국어, 두 언어의 섹션, 키워드다, 그래서 "shadow"와 "그림자"가
  어느 언어가 보이든 같은 행을 찾는다; 팔레트는 사용 중인 언어로 이름을 나열한다.
- **Language 행.** View 탭의 **Language** 콤보 (English, 한국어, 항상 각자의 언어로 씀); `LANG` 태그; 첫 시작에는
  브라우저 언어가 정한다.
- **번역하지 않음**, 결정대로: Console의 로그 행, 노드, 클립, 텍스처 이름, 엔진의 텍스처 설명 ("1 mip, 5.3 MB,
  1 user"), 빌드 라벨, 스트레스 벤치마크의 복사되는 표, 저장 뷰어의 내부 (헤더와 청크 줄), undo 단계의 이름
  ("moon Position": 노드 이름과 필드 이름은 영어로 남고, "Undo"와 "Redo"만 번역된다).
- 가는 길에 발견한 것: `focus_panel`이 0 (패널 0)에서 시작해서 시작할 때 Scene 탭의 검색 상자가 포커스를 받았다;
  이제는 패널 번호에 1을 더해 담는다.
- Debug 빌드는 `_app_debug_language`, `_app_debug_set_language`, `_app_debug_selected_name(k)`,
  `_app_debug_rename_selected` (선택된 노드의 이름을 검색 버퍼의 텍스트로 바꿈), `_app_debug_search_query(panel, k)`를
  export한다.
- Debug (데스크톱 1280×800과 폰 390×664, 2×)와 Release에서 확인: 트리에서 라틴 옆의 한글 이름, 10음절 한도, 저장
  왕복과 새로고침; Name 필드, 검색 상자, 팔레트로의 IME 조합과 확정, 보통 입력, Backspace, 화살표가 여전히 동작;
  한국어 브라우저 (`locale: ko-KR`)가 한국어로 시작; 데스크톱과 폰 UI 전체가 잘린 라벨 없이 한국어로 (폰의 Undo
  버튼, 64 px에 "실행 취소"가 빠듯하게 들어감); 어느 언어가 보이든 두 언어로 검색; 새로고침이 어느 쪽이든 선택을
  유지; 검색과 단축키의 기존 확인.

### 범위 밖

- 영어와 한국어 외의 언어, 오른쪽에서 왼쪽 쓰기와 글자 모양이 바뀌는 문자 체계.
- KS X 1001의 2,350자 밖의 음절 (상자로 보인다), 한자.
- 초성 검색.
- 로그 메시지와 문서의 번역 (앱 안에서). 저장소의 문서 자체는 2026-10-01부터 `AGENTS.md`의 규칙에 따라
  한국어와 영어로 함께 쓴다.

## English

Status: implemented (2026-10-01). Changes to this spec are agreed first.

### Goal

Let the editor be used in Korean:

1. **Show Hangul.** Today the UI font (Inter, `fonts.md`) has no Hangul, so a node renamed in Korean
   shows as "?" boxes.
2. **Type Hangul on the desktop.** The desktop takes text from each `keydown` (`on_key` in
   `engine/src/imgui.c`). A Korean input method (IME) composes a syllable over several keys and
   reports them as "Process" keys, so composed text never arrives. The phone already types through
   the hidden `<input id="nv-text-agent">`, whose `compositionend` and `input` events carry the
   finished text, but it shows nothing while a syllable is being composed.
3. **A Korean UI.** Menus, tabs, buttons, labels, the palette's names and the help window in
   Korean, chosen in the View tab.

Out of scope: other languages beyond English and Korean (the mechanism allows them later),
Chinese characters (Hanja) beyond the few in the font, vertical text, translating the Console's
log messages (developer output, kept in English), translating the docs, and initial-consonant
search ("ㄱㅈ" finding "그림자").

### Approaches

#### Showing Hangul

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **Pretendard's Hangul, subset, merged behind Inter** (recommended) | An offline script keeps only the Hangul syllables of KS X 1001 (the 2,350 in everyday use), the compatibility jamo (ㄱ to ㅣ, shown while composing) and every character the Korean string table uses; the result is bundled and merged into Inter as a fallback (ImGui `MergeMode`) | Latin stays Inter; Pretendard's Hangul is drawn to match Inter. Estimated 0.4 to 0.7 MB before compression (measured in phase 1). Works offline, no wait at start | A rare syllable outside the 2,350 ("똠", "햏") shows as a box. The subset is regenerated when the string table gains characters (the script checks) |
| Full Pretendard bundled | All 11,172 syllables | Nothing missing | About 1.5 to 2.5 MB more on every first load (`fonts.md`), for text most visits never show |
| Full Pretendard fetched on demand | Bundle nothing; fetch when Korean is first needed and keep it in IndexedDB | Free for English users | The Korean UI would start in boxes until the fetch ends, and offline it never shows; more code (fetch, cache, retry) |
| Noto Sans KR, Nanum Gothic, Spoqa Han Sans Neo (all OFL) | Other Korean faces | Fine faces | Pretendard is the one designed alongside Inter's Latin, so mixed English and Korean look like one font |

Subsetting tool candidates (offline, run by hand, nothing in the build or the repo's dependencies):

| Tool | Language, license | Fit |
|---|---|---|
| **`subset-font`** through `npx` (Node, MIT; HarfBuzz's subsetter compiled to WebAssembly) | Runs the way `tools/` scripts already run (`npx`, nothing installed into the repo) | Recommended |
| fontTools `pyftsubset` (Python, MIT) | The standard subsetter | Needs a Python environment, which the repo's tools do not use |

#### Typing Hangul on the desktop

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **The text agent on the desktop too, shown at the caret while composing** (recommended) | While ImGui edits text, the hidden input has focus on the desktop as it does on the phone; text arrives through its `input` and `compositionend` events. During a composition the input becomes visible at the text caret (ImGui gives the caret through `PlatformIO.Platform_SetImeDataFn`), so the browser draws the syllable being composed and places the IME's candidate window there | One text path for both UIs; the browser does the IME work | The composing syllable is drawn by the browser over the canvas, in the page's font, not inside the ImGui field; it disappears into the field when finished |
| Keep `keydown` and add composition events on the canvas | Listen for composition on the canvas element | Less change | A `<canvas>` is not editable, so browsers do not start an IME on it |
| Draw the composition inside ImGui | Feed the pre-edit string into the field | Looks native | Dear ImGui has no pre-edit (composition) text support in its input field; we would patch ImGui |

#### A Korean UI

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **Our own string table, keyed by the English text** (recommended) | `app/strings.c` holds pairs (English, Korean). UI code writes `T("Speed")`; it returns the English or the Korean text for the language in use, found through a hash table built at start. Widget labels keep their ImGui ID in English: `TL("Speed")` returns `"속도###Speed"` | English stays the source in the code, so code reads as today; a missing translation falls back to English. Fixed arrays, plain C | Every UI string has to be wrapped; a test lists strings that have no translation |
| GNU gettext (C, LGPL, `.po` files) | The classic | Standard tooling | A runtime library and a file format for two languages; LGPL linking terms on the web |
| An enum of string ids | `T(STR_SPEED)` | Compile-time checked | The code no longer reads in English, and every string needs an id |

Recommendation: the three recommended rows. The new third-party pieces are the Pretendard face
(OFL, an asset) and the `subset-font` tool (MIT, run by hand); nothing is added until they are
confirmed.

### Decisions

#### Font

| Topic | Decision |
|---|---|
| File | `assets/fonts/Pretendard-Hangul-Subset.otf` (or `.ttf`), its OFL license beside it, made by `tools/subset_hangul.sh` from Pretendard Regular. The script's inputs: KS X 1001's syllables, U+3131 to U+318E (compatibility jamo), U+AC00 to U+D7A3 only as listed, and every character of the Korean strings |
| Merge | `nv_imgui_set_font` gains a fallback: the Hangul face is added with `MergeMode` into Inter, so a string uses Inter's glyph where Inter has one and Pretendard's otherwise. Its size and vertical offset are tuned (`ImFontConfig.SizePixels` ratio, `GlyphOffset`) so Hangul sits on Inter's baseline with the same visual height; checked by screenshots |
| Always loaded | The fallback is loaded at start for every language, so Korean names show in the English UI too |
| Names | Node names are `NV_NODE_NAME_MAX` (32) bytes: 10 Hangul syllables (3 bytes each in UTF-8). `nv_scene_add_node` and the Name field cut at a character boundary, never inside one (a cut syllable would be invalid UTF-8 in the save). The limit itself stays |

#### Typing

| Topic | Decision |
|---|---|
| Focus | On the desktop too, the text agent takes focus whenever `io.WantTextInput` turns on, and gives it back when it turns off (the phone's rule, `imgui.c`). While it has focus, `on_key` forwards keys (arrows, Enter, Escape, Backspace, shortcuts) but not characters, as it already does on the phone |
| Composition shown | `Platform_SetImeDataFn` reports the caret position and line height each frame; the agent moves there. On `compositionstart` it becomes visible (opaque text, the field's text color, a dark background, a font size equal to ImGui's in CSS pixels) and on `compositionend` it hides again. The IME's candidate window follows it |
| Keys during composition | A key the IME consumes (`event.isComposing`, or key "Process") is not given to ImGui or to the shortcut table, so composing never fires W, E, R or Space |
| Search boxes and the palette | They are ImGui fields: Hangul typed there works the same, and the matcher (`search.md`) compares UTF-8 bytes, so "그림" finds "그림자" |

#### Korean UI

| Topic | Decision |
|---|---|
| Strings | `T(english)` for text and `TL(english)` for widget labels (`"번역###English"`, so a widget's ID, and so its state, is the same in both languages). Translated: menus, tab names, buttons, labels, tooltips, section headings, the palette's action names and kinds, the help window, the Play note, the save messages shown in the View tab, the search box hints. Not translated: Console log rows, node names, clip and texture names from the assets, the build label's commit line, numbers and units |
| Setting | A **Language** combo (English, 한국어) in the View tab, through `search_row` (searchable in both languages). It takes effect on the next frame |
| Default | From the browser's language at first start (`navigator.language` starting with "ko" gives Korean), then whatever was chosen |
| Saved | A new `EDIT` tag `LANG` (u32: 0 English, 1 Korean; anything else loads as English). A save without it uses the browser's language. `SAVE_VERSION` stays. An editor setting, not undoable |
| Search | A row's text for matching is its English label, its Korean label and its keywords, so "shadow" and "그림자" both find the Shadows section in either language; the palette's actions likewise |
| Layout | Korean labels are checked in every dock, the phone panel and the top bars at the widest strings; fixed widths that cut them (the phone's Find and Undo buttons, the desktop Play button) grow or use shorter words |
| Missing translations | A Debug build logs, once each, a string that has no Korean entry while Korean is shown; a ctest test (`tests/`) checks that every `T`/`TL` literal in `app/` has an entry |

### Changes

- **Engine (`nv/imgui.h`, `engine/src/imgui.c`).** The Hangul fallback in `nv_imgui_set_font` (a
  second face argument), the text agent on the desktop, `Platform_SetImeDataFn`, the composing
  overlay, keys held back during composition, cutting names at character boundaries
  (`engine/src/scene.c`).
- **App.** `app/strings.c` and `app/strings.h` (the table, `T`, `TL`, the language), `T`/`TL`
  around every UI string in `app/*.c`, the Language row in the View tab, the `LANG` tag in
  `app/save.c`, the search texts in both languages (`app/search.c`), the font file loaded in
  `app/main.c`.
- **Tools and assets.** `tools/subset_hangul.sh` (`npx subset-font`), the subset font and its
  license in `assets/fonts/`.
- **Tests.** `tests/strings.c`: every literal wrapped in `T`/`TL` has a Korean entry.
- **Docs.** `fonts.md`, `save.md` (the tag), `search.md`, `layout.md`, `AGENTS.md` (UI strings go
  through `T`/`TL`; the table's place), README (Pretendard's credit).

### Phases

1. **Hangul shows:** the subset script and font, the fallback merge, names cut at character
   boundaries. Checked: a node renamed in Hangul (pasted on the desktop, typed on the phone) shows
   its syllables beside Latin at the same baseline, survives a reload and the save round trip, a
   tenth syllable fits and an eleventh is refused whole; the package's size before and after.
2. **Typing on the desktop:** the text agent on the desktop, the composing overlay at the caret,
   keys held back while composing. Checked with Playwright's IME events (CDP `Input.imeSetComposition`
   and `Input.insertText`): "그림자" typed into the Name field, a search box and the palette;
   no shortcut fires during composition; Escape, Enter and Backspace still work; the phone is
   unchanged.
3. **Korean UI:** the string table, `T`/`TL` through the app, the Language row and tag, search in
   both languages, the missing-string test. Checked: every dock, menu, popup, the palette and the
   help window in Korean at 1280×800 and 390×664 with no cut labels, a reload keeps the language,
   "그림자" and "shadow" find the same rows, a fresh browser in Korean starts in Korean.
4. **Docs.**

Every phase is checked in Release and Debug in headless Chromium.

As built, with the notes the build taught:

- **Hangul font.** `assets/fonts/Hangul-Subset.ttf` (387 KB; every character of `app/strings.c` is
  inside KS X 1001's 2,350, so the script added none), made by `tools/subset_hangul.sh` from
  Pretendard Regular's TrueType file. It is renamed because the OFL reserves the name "Pretendard" for
  unmodified fonts (the license's reserved-name clause covers the name shown to users, which the app
  never shows); the font's own name table is still Pretendard's, as in the author's published
  subsets. `nv_imgui_set_font` takes the fallback as a second face and merges it (`MergeMode`);
  Hangul sat on Inter's baseline at the same size with no tuning. The package grew by 0.39 MB.
- **Names.** `nv_utf8_fit` (`nv/base.h`) cuts at a character: `nv_scene_add_node` and a name read from a
  save keep whole syllables (10 fit in 32 bytes). ImGui's own text field already refuses a character
  that does not fit.
- **UTF-8 everywhere text is cut or compared.** `nv_utf8_length`, `nv_utf8_fit` and `nv_utf8_trim`
  (`nv/base.h`, tested by `tests/utf8_test.c`): a string copied into a fixed buffer with `snprintf`
  (undo step names, palette rows, the search's labels, sections, keywords, candidates and query
  words, the save viewer's strings, the translated widget labels) is trimmed so no character is cut
  in half. The search folds case by character, not byte: ASCII, Latin-1 and Latin Extended-A, Greek and
  Cyrillic (all keep the byte length, so a match's offsets are the same in the original text, which the
  highlight uses), and treats the ideographic space (U+3000) as a word separator. Hangul has no case
  and is compared as it is.
- **Typing.** On the desktop the text agent takes focus when `WantTextInput` turns on, sits at the
  caret ImGui reports (`Platform_SetImeDataFn`) and, while composing, shows the syllable (dark text on a
  light box: Chromium paints its own highlight over a composition). The agent's keys: the ones the
  input method takes ("Process") are left alone; keys that type no text, and chords except paste,
  act in ImGui only and are kept from the agent (its caret must not move, Ctrl+Z must not undo in
  it); typing and Backspace stay with the agent. As a consequence a letter typed in a field is no
  longer kept from the browser (`defaultPrevented` is false for it), which a text field never needed.
  Checked with CDP `Input.imeSetComposition` and `Input.insertText`.
- **Strings.** `T(english)` returns the text and `TL(english)` a widget label, `text###English`, in
  both languages, so a widget keeps its id when the language changes (a tab keeps its place). A table of
  222 rows in `app/strings.c`, found through a hash table built on first use. Combo item arrays that
  were `local_persist` are built each call with `T()`. `tests/strings_test.mjs` (ctest `strings_test`)
  fails when a string the code wraps, passes to the search (`search_row`, `search_group`,
  `search_section`), puts in the shortcut table (group, name, keys text), the stats, a texture note or
  a panel name has no row, and when a row's printf conversions differ from the English text's.
- **Search.** A row's search text is its English label, the Korean of it, its section in both
  languages and its keywords, so "shadow" and "그림자" find the same rows whichever language is
  shown; the palette lists names in the language in use.
- **Language row.** A **Language** combo (English, 한국어, always written in their own language) in
  the View tab; `LANG` tag; the browser's language decides at the first start.
- **Not translated**, as decided: the Console's log rows, node, clip and texture names, the engine's
  texture descriptions ("1 mip, 5.3 MB, 1 user"), the build label, the stress benchmark's copied
  table, the save viewer's internals (header and chunk lines), and the undo steps' names ("moon
  Position": the node's name and the field name stay English; only "Undo" and "Redo" are
  translated).
- Found on the way: the Scene tab's search box was focused at start, because `focus_panel` began at 0
  (panel 0); it now holds the panel plus one.
- Debug builds export `_app_debug_language`, `_app_debug_set_language`, `_app_debug_selected_name(k)`,
  `_app_debug_rename_selected` (renames the selected node to the search buffer's text) and
  `_app_debug_search_query(panel, k)`.
- Checked in Debug (desktop 1280×800 and phone 390×664 at 2×) and Release: Hangul names in the tree
  beside Latin, the 10 syllable limit, the save round trip and a reload; IME composition and commit into
  the Name field, a search box and the palette, with ordinary typing, Backspace and arrows still
  working; a Korean browser (`locale: ko-KR`) starts in Korean; the whole desktop and phone UI in Korean
  without cut labels (the phone's Undo button, 64 px, fits "실행 취소" tightly); searches in either language
  with either language shown; a reload keeps the choice either way; the old checks of search and shortcuts.

### Out of scope

- Languages other than English and Korean, right-to-left and shaped scripts.
- Syllables outside KS X 1001's 2,350 (they show as boxes), Hanja.
- Initial-consonant (chosung) search.
- Translating log messages and documents (inside the app). The repository's documents themselves are
  written in both Korean and English since 2026-10-01, by the rule in `AGENTS.md`.

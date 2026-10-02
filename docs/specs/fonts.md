# Fonts spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 더 작은 범위로 구현됨 (2026-10-01). 이 스펙의 변경은 먼저 합의한다.

**결정된 범위:** Inter, 고정. 에디터는 Inter Regular를 14 CSS 픽셀 (곱하기 터치 배율)로만 쓴다: Font 섹션,
글꼴이나 크기 설정, 저장 태그, 한글 폴백, 가져오기는 없다. 아래 후보 표, 결정, 변경, 단계는 이것이 대신한
더 큰 설계다; 나중에 글꼴 선택이 필요해지면 계획으로 남는다. 실제로 만든 것은 "만든 결과" 아래에 있다.

### 목표

에디터의 기본 내장 글꼴을 더 보기 좋은 것으로 바꾸고, 사용자가 앱에서 글꼴 (과 크기)을 고를 수 있게 한다.
지금 모든 ImGui 창은 Dear ImGui의 내장 글꼴 (ProggyClean, 디버그 도구용으로 만든 13 px 고정폭 글꼴)을
쓴다: 각지고, 문자 집합이 좁고 (한글이 없어 한국어로 이름 지은 노드는 "?"로 보인다), 바꿀 수 없다.

범위 밖: 한 번에 둘 이상의 글꼴 사용 (Console용 별도 고정폭 글꼴, 굵게나 기울임), 3D 씬 안의 텍스트
렌더링, 오른쪽에서 왼쪽 쓰기와 복잡한 문자 체계, 사용자 파일에서 불러온 글꼴.

### 엔진이 이미 주는 것

Dear ImGui 1.92 (우리가 빌드하는 버전)에는 동적 글꼴이 있다: TrueType (TTF)이나 OpenType 파일을 고정 크기
없이 한 번 불러오면, 글리프는 처음 그려질 때 그려지는 크기와 device pixel ratio로 래스터화되고, 글꼴은
언제든 크기를 바꿀 수 있다 (`style.FontSizeBase`, `PushFont(font, size)`). 글리프 비트맵은
`engine/imgui.c`가 이미 처리하는 텍스처 요청 (`ImGuiBackendFlags_RendererHasTextures`)으로 GPU에 가므로,
수천 개 글리프가 있는 글꼴 (한글)도 쓴 글리프만큼만 든다. 글꼴은 합칠 수 있다: 라틴 문자용 글꼴 하나와 한글
폴백용 글꼴 하나. 글꼴 파일의 바이트는 쓰는 동안 메모리에 남아 있어야 한다.

### 후보

모두 자유롭게 재배포할 수 있다. 파일 크기는 대략이며 파일을 가져올 때 확인한다. "글리프"는 글꼴이 다루는
범위다.

| 글꼴 | 라이선스 | 모양 | 크기 (굵기 하나) | 글리프 | 메모 |
|---|---|---|---|---|---|
| ProggyClean (현재) | MIT | 픽셀 격자 고정폭 | 내장 | 라틴 | 13 px에서만 선명; 배율을 바꾸면 흐려진다 |
| **Inter** | SIL Open Font License (OFL) 1.1 | 화면용으로 설계된 중립적인 UI 산세리프, 높은 x-height, 고정폭 숫자 | 약 300–400 KB (Regular, TTF) | 라틴, 그리스, 키릴 | 도구 UI의 흔한 선택 |
| Roboto | Apache-2.0 | 익숙한 Android / Material 산세리프 | 약 170 KB | 라틴, 그리스, 키릴 | Inter보다 약간 좁음; 작은 크기에서 괜찮다 |
| IBM Plex Sans | OFL | 약간 기술적인 산세리프 | 약 200 KB | 라틴, 그리스, 키릴 | 아래 Plex Mono와 어울린다 |
| **JetBrains Mono** | OFL | 프로그래머용 고정폭, 0/O와 1/l/I가 분명 | 약 270 KB | 라틴, 그리스, 키릴 | 지금 UI의 "도구" 느낌을 유지하면서 읽기 쉽다 |
| **Pretendard** | OFL | Inter 같은 라틴과 어울리는 한글, UI용 | 약 1.5–2.5 MB (Regular) | 라틴, 한글 (11 172 음절 전부), 일부 한자 | 영어와 한국어에 글꼴 하나 |
| Noto Sans KR | OFL | Google의 한국어 산세리프 | 약 4–6 MB (static) | 라틴, 한글, 한자 | 크다; 라틴 부분이 평범하다 |
| D2Coding | OFL | 코드용 한국어 고정폭 | 약 3–4 MB | 라틴, 한글 | 나중에 한국어 고정폭이 필요하면 |

추천:

- **포함** (패키지 안, 기다림 없음): ProggyClean ("Classic"으로 유지), **Inter**, **JetBrains Mono**, 그리고
  견본 (아래)에서 이유가 보이면 Roboto. 다운로드에 1 MB 미만을 더한다.
- **필요할 때 가져오기**: 사용자가 "Korean text"를 켤 때만, 어떤 라틴 글꼴을 고르든 그 뒤의 한글 폴백으로
  **Pretendard**. 포함하면 모든 첫 로딩에 약 2 MB가 들고, 에디터 자체의 텍스트는 영어다: 한글은 사용자가
  입력한 이름에만 보인다. 한 번 가져오면 브라우저 저장소에 둔다 (`engine/storage.h`, 저장처럼) 그래서 한 번만
  다운로드된다.
- **기본값**: 견본에서 고른 크기의 Inter. 모두가 보는 하나의 변화이므로 지금이 아니라 스크린샷을 보고
  정한다.
- **지금은 안 함**: FreeType 글꼴 로더 (`imgui_freetype`; FreeType은 FTL 또는 GPL 이중 라이선스이고 새
  의존성이 된다). Dear ImGui 자체의 stb_truetype 래스터라이저는 힌팅 없는 윤곽선을 그리는데, 1× 디스플레이의
  작은 크기에서는 부드럽고 2×에서는 괜찮다. 견본이 1×에서 너무 흐리다고 보여 주면, 가벼운 힌팅의 FreeType을
  별도 단계로 제안하고 모든 라이브러리처럼 먼저 승인을 받는다.

### 결정

| 주제 | 결정 |
|---|---|
| 선택지 | 앱의 고정 목록: Classic (ProggyClean), Inter, JetBrains Mono, 그 밖에 포함된 글꼴. 파일 선택기가 아니다 |
| 설정 | View 탭의 **Font** 섹션 (행은 `search_row`를 거쳐 팔레트가 찾는다): **Face** 콤보, **Size** 슬라이더 (10–24 CSS 픽셀, 기본 크기: 터치 배율 1.3이 여전히 곱해진다), Pretendard를 가져와 합치는 **Korean text** 체크박스. **Reset** 버튼이 기본값으로 되돌린다. 두 UI 모두 |
| 적용 범위 | 모든 ImGui 창 (도크, 메뉴, 팔레트, 팝업, Console). foreground 목록에 그리는 빌드 라벨도 쓴다 |
| 방법 | `NvImgui`가 글꼴의 작은 테이블 (`NvImguiFont`: 이름, 바이트, 불러온 글꼴)을 가진다. `nv_imgui_set_font(imgui, face, size)`는 처음 쓸 때 글꼴을 만들고 (`ImFontAtlas_AddFontFromMemoryTTF`, 크기 0 = 동적), 한글 폴백이 불러와져 있으면 합치고, `io->FontDefault`와 `style.FontSizeBase`를 설정한다. 엔진은 "글꼴 하나와 크기"만 안다; 어떤 글꼴이 있는지는 앱의 목록이다 |
| 크기와 배치 | 창, 버튼, 행은 이미 크기를 글꼴에서 가져온다 (`FontSize`, `FramePadding`). 앱의 고정 픽셀 크기 (도크 너비, `DESKTOP_TOP_BAR` 28, 폰의 48과 버튼 크기, Console의 좁은 기준)를 가장 큰 크기와 새 글꼴에 대해 확인하고, 따라가지 못하는 것은 `igGetFontSize()`의 배수가 된다 |
| 저장 | 새 `EDIT` 태그 두 개: `FONT` (글꼴 이름을 텍스트로, 그래서 저장된 값의 뜻을 바꾸지 않고 글꼴을 추가하거나 순서를 바꿀 수 있다; 모르는 이름은 기본값으로 불러온다)와 `FSIZ` (f32, 10..24로 제한; 없으면 기본값). `SAVE_VERSION`은 그대로; 옛 저장은 기본값으로 불러온다. 크기와 글꼴은 에디터 설정이고, undo되지 않으며 Play의 스냅샷 (`SAVE_PART_EDITOR`)에 들지 않는다 |
| Korean text | `KRFN` 태그 (u32 0 또는 1). 불러올 때 1이면 즉시 가져오기를 시작하고 도착하면 폴백을 합친다; 그때까지 한글은 빠진 글리프 상자로 보인다. 가져오기가 실패하면 (오프라인) Console에 경고로 기록하고 체크박스를 끈다 |
| 가져오기 | 페이지에서 `app.js` 옆의 `fonts/Pretendard-Regular.otf` (Pages 패키지에 있지만 앱 자체 패키지가 미리 불러오지 않는 파일)를 `fetch()`하고, 바이트를 앱에 넘기는 `EM_JS` 도우미를 거친다; 진행률은 보이지 않고 (약 2 MB), 그동안 체크박스가 "loading..."이라고 말한다. IndexedDB의 `/nv-save` 아래 저장 옆에 두고, 이후 방문에서는 거기서 읽는다 |
| 시작 | 고른 글꼴은 첫 프레임 전에 패키지에서 불러오므로 잘못된 글꼴의 프레임이 없다. 저장을 먼저 읽는다 (지금 `save_init`은 `nv_imgui_init` 뒤에 돈다), 그래서 첫 프레임은 기본값을 쓰고 저장이 불러와지면 바뀐다: 불러오기가 이미 첫 그리기 전에 있지 않을 때만 스펙이 받아들이는 깜박임이다; 아니면 `save_init`을 앞으로 옮긴다 |
| Textures 탭 | 글꼴 아틀라스는 이미 "UI" 아래 나열된다; 그 크기가 보이므로 글리프 캐시의 증가를 지켜볼 수 있다 |
| 메모리 | 글꼴의 바이트는 상주한다: 포함된 것은 약 0.4 MB, Pretendard는 불러오면 약 2 MB. 글꼴은 처음 쓸 때 만들므로 안 쓰는 것은 비용이 없다 |
| 라이선스 | 각 글꼴의 라이선스 텍스트를 옆에 둔다 (`assets/fonts/<name>/OFL.txt`, `LICENSE.txt`), 크레딧은 README에 나열한다. `.gitattributes`는 이미 `*.ttf`와 `*.otf`를 Git LFS로 추적한다 |
| 서드파티 | 글꼴은 라이브러리가 아니라 에셋이지만, AGENTS.md가 모든 서드파티 추가에 요구하듯 확인을 위해 위에 나열한다: 확인될 때까지 아무것도 추가하지 않는다 |

### 변경

- **엔진 (`engine/imgui.h`, `engine/imgui.c`).** 글꼴 테이블, `nv_imgui_set_font`, 한글 폴백 합치기, 메모리에서
  불러오기, 저장소 캐시가 있는 가져오기 도우미 (`EM_JS`).
- **앱.** `app/ui.c`의 Font 섹션 (View 탭), `app/save.c`의 `FONT`, `FSIZ`, `KRFN` 태그, `app/ui_desktop.c`,
  `app/ui_phone.c`, `app/console.c`의 고정 크기 중 글꼴을 따라가야 하는 것은 글꼴 높이로 표현,
  `app/main.c`의 포함 글꼴 목록.
- **에셋.** 글꼴과 라이선스가 있는 `assets/fonts/`; `web/manifest.cmake`와 CI 패키지에 필요할 때 가져오는 한글
  파일이 더해진다.
- **문서.** `save.md` (태그), `layout.md` (글꼴 높이 단위 크기), `search.md` (행), `AGENTS.md`, README 크레딧.

### 단계

1. **견본과 엔진의 글꼴 테이블:** 포함 글꼴이 든 테이블과 `nv_imgui_set_font`, 임시 Debug 전용 스위치,
   기본값을 고르기 위한 모든 글꼴의 12, 14, 16, 18 px 스크린샷 (데스크톱 1×와 2× pixel ratio, 폰 2×의
   390×664). 확인: 바꾸는 동안 WebGPU 오류 없음, 아틀라스가 끝없이 자라지 않음, Console과 Textures 탭이 여전히
   그려짐.
2. **설정:** View 탭 섹션, 저장 태그, 스크린샷에서 고른 기본값, 글꼴을 따라가게 만든 고정 픽셀 크기. 확인:
   가장 작은 크기와 가장 큰 크기에서 두 UI, 메뉴, 도크, 상단 바, 팔레트, Console에 잘린 라벨 없음, 새로고침이
   글꼴과 크기를 유지, 옛 저장은 기본값으로 불러옴.
3. **Korean text:** 필요할 때 가져오기, 저장소 캐시, 체크박스, 합치기. 확인: 한글로 이름을 바꾼 노드가
   체크박스를 켜면 텍스트로, 끄면 빠진 글리프 상자로 보임, 두 번째 방문은 네트워크 요청 없이 저장소에서 파일을
   읽음, 오프라인 가져오기는 Console 경고이고 체크박스가 스스로 꺼짐.
4. **문서와 라이선스.**

모든 단계는 헤드리스 Chromium에서 Release와 Debug로 확인한다. 데스크톱 UI는 1280×800에서 마우스로, 폰 UI는
390×664에서 터치로.

### 만든 결과

- `assets/fonts/Inter-Regular.ttf` (Inter 4.1, static, 411 KB, SIL Open Font License 1.1)와
  `assets/fonts/Inter-LICENSE.txt`, Git LFS에 저장 (`*.ttf`).
- 패키지는 이제 `assets/` 디렉터리 전체를 `/assets`로 미리 불러온다 (전에는 `assets/quaternius`), 그래서
  glTF 파일은 `/assets/quaternius/*.glb`에, 글꼴은 `/assets/fonts/`에 있다.
- `nv_imgui_set_font(imgui, ttf, size, pixel_size)` (`engine/imgui.h`)는 메모리에서 글꼴을 동적 글꼴 (크기 0)로
  추가하고, `io->FontDefault`로 만들고, `style.FontSizeBase`를 설정한다; 바이트는 앱의 영구 아레나에 남는다.
  `app/main.c` (`load_font`)는 `nv_imgui_init` 뒤, 첫 프레임 전에 파일을 읽고, 파일이 없으면 Console 경고를
  기록하고 내장 글꼴을 유지한다.
- Inter 14 px는 옛 13 px 글꼴과 줄 높이와 평균 너비가 거의 같아서 배치를 바꿀 필요가 없었다: 도크, 상단 바,
  탭이 데스크톱과 폰 (390×664)에서 여전히 들어맞고, 폰에서는 Inspector 탭의 라벨을 더 이상 자를 필요가 없다.
- 패키지가 0.4 MB 커진다 (`app.data` 1.7 MB); `korean.md`가 그 뒤에 한글 폴백 글꼴을 더한다 (0.39 MB).
- 옛 글꼴의 픽셀 위치에 맞춰 쓴 Playwright 스크립트 (메뉴 항목, Reset 대화 상자의 버튼, Scene 트리의 행)는
  엉뚱한 곳을 클릭하므로 새 좌표가 필요하다; 앱의 동작은 바뀌지 않았다.

### 범위 밖

- 역할별 별도 글꼴 (나머지는 Inter인데 Console은 고정폭), 굵게와 기울임.
- 사용자가 준 글꼴 파일, 아이콘 글꼴, 이모지.
- FreeType 힌팅 (위, 견본이 요구할 때까지).

## English

Status: implemented in a smaller scope (2026-10-01). Changes to this spec are agreed first.

**Scope as decided:** Inter, fixed. The editor uses Inter Regular at 14 CSS pixels (times the touch
scale) and nothing else: no Font section, no face or size setting, no saved tags, no Korean
fallback and no fetching. The candidate table, Decisions, Changes and Phases below are the larger
design this replaced; they stay as the plan if choosing fonts is wanted later. What was built is
under "As built".

### Goal

Replace the editor's built-in font with a better-looking one, and let the user pick the font (and
its size) in the app. Today every ImGui window uses Dear ImGui's built-in font (ProggyClean, a
13 px monospaced face made for debugging tools): it is blocky, has a narrow character set (no
Hangul, so a node named in Korean shows as "?"), and cannot be changed.

Out of scope: more than one font in use at once (a separate monospaced font for the Console, bold
or italic text), text rendering in the 3D scene, right-to-left and complex scripts, and fonts
loaded from the user's own files.

### What the engine already gives us

Dear ImGui 1.92 (the version we build) has dynamic fonts: a TrueType (TTF) or OpenType file is
loaded once with no fixed size, glyphs are rasterized when first drawn, at the size and the device
pixel ratio they are drawn at, and the font can change size at any time (`style.FontSizeBase`,
`PushFont(font, size)`). Glyph bitmaps reach the GPU through the texture requests that
`engine/imgui.c` already serves (`ImGuiBackendFlags_RendererHasTextures`), so a font with
thousands of glyphs (Hangul) costs only the glyphs used. Fonts can be merged: one face for Latin
and another as the fallback for Hangul. The font file's bytes must stay in memory while it is used.

### Candidates

All are free to redistribute. File sizes are approximate and are checked when a file is fetched.
"Glyphs" is what the face covers.

| Font | License | Look | Size (one weight) | Glyphs | Notes |
|---|---|---|---|---|---|
| ProggyClean (today) | MIT | Pixel-grid monospaced | built in | Latin | Crisp at 13 px only; scaling it blurs |
| **Inter** | SIL Open Font License (OFL) 1.1 | Neutral UI sans, designed for screens, tall x-height, tabular digits | about 300 to 400 KB (Regular, TTF) | Latin, Greek, Cyrillic | The usual choice for tool UIs |
| Roboto | Apache-2.0 | Familiar Android / Material sans | about 170 KB | Latin, Greek, Cyrillic | Slightly narrower than Inter; fine at small sizes |
| IBM Plex Sans | OFL | Slightly technical sans | about 200 KB | Latin, Greek, Cyrillic | Matches the Plex Mono below |
| **JetBrains Mono** | OFL | Programmer's monospace, clear 0/O and 1/l/I | about 270 KB | Latin, Greek, Cyrillic | Keeps the "tool" feel of today's UI, readable |
| **Pretendard** | OFL | Inter-like Latin with matching Hangul, made for UI | about 1.5 to 2.5 MB (Regular) | Latin, Hangul (all 11 172 syllables), some Hanja | One face for English and Korean |
| Noto Sans KR | OFL | Google's Korean sans | about 4 to 6 MB (static) | Latin, Hangul, Hanja | Large; the Latin part is plain |
| D2Coding | OFL | Korean monospace for code | about 3 to 4 MB | Latin, Hangul | If a Korean monospace is wanted later |

Recommendation:

- **Bundled** (in the package, no wait): ProggyClean (kept as "Classic"), **Inter**, **JetBrains
  Mono**, and Roboto if the specimen (below) shows a reason to. They add under 1 MB to the download.
- **Fetched on demand**: **Pretendard** as the Hangul fallback behind whichever Latin face is
  chosen, only when the user turns "Korean text" on. It would cost about 2 MB of every first load
  if it were bundled, and the editor's own text is English: Hangul only shows in names the user
  types. Once fetched it is kept in browser storage (`engine/storage.h`, as the save is) so it is
  downloaded once.
- **Default**: Inter, at a size picked from the specimen. This is the one change everyone sees, so
  it is decided by looking at screenshots, not now.
- **Not now**: the FreeType font loader (`imgui_freetype`; FreeType is dual-licensed FTL or GPL and
  would be a new dependency). Dear ImGui's own stb_truetype rasterizer draws unhinted outlines,
  which are soft at small sizes on a 1× display and fine at 2×. If the specimen shows it is too
  soft at 1×, FreeType with light hinting is proposed as its own step and needs a yes first, as
  every library does.

### Decisions

| Topic | Decision |
|---|---|
| Choices | A fixed list in the app: Classic (ProggyClean), Inter, JetBrains Mono, plus any other bundled face. Not a file picker |
| Setting | A **Font** section in the View tab (rows go through `search_row`, so the palette finds them): a **Face** combo, a **Size** slider (10 to 24 CSS pixels, a base size: the touch scale 1.3 still multiplies it), and a **Korean text** checkbox that fetches and merges Pretendard. A **Reset** button puts the default back. Both UIs |
| Where it applies | Every ImGui window (docks, menus, palette, popups, Console). The build label, drawn on the foreground list, uses it too |
| How | `NvImgui` keeps a small table of faces (`NvImguiFont`: name, bytes, loaded font). `nv_imgui_set_font(imgui, face, size)` makes the font on first use (`ImFontAtlas_AddFontFromMemoryTTF`, size 0 = dynamic), merges the Hangul fallback when it is loaded, sets `io->FontDefault` and `style.FontSizeBase`. The engine only knows "a face and a size"; which faces exist is the app's list |
| Sizes and layout | Windows, buttons and rows already take their sizes from the font (`FontSize`, `FramePadding`). The fixed pixel sizes in the app (the dock widths, `DESKTOP_TOP_BAR` 28, the phone's 48 and button sizes, the Console's compact limit) are checked against the largest size and the new faces; those that cannot follow become multiples of `igGetFontSize()` |
| Saved | Two new `EDIT` tags: `FONT` (the face's name as text, so a face can be added or reordered without changing what a saved value means; an unknown name loads as the default) and `FSIZ` (f32, clamped to 10..24; missing = the default). `SAVE_VERSION` stays; old saves load with the default. The size and face are editor settings, not undoable and not part of Play's snapshot (`SAVE_PART_EDITOR`) |
| Korean text | `KRFN` tag (u32 0 or 1). When 1 at load, the fetch starts at once and the fallback is merged when it arrives; until then Hangul shows as the missing-glyph box. A failed fetch (offline) is logged as a warning in the Console and the box is turned off |
| Fetching | `fetch()` from the page of `fonts/Pretendard-Regular.otf` next to `app.js` (a file in the Pages package that the app's own package does not preload), through an `EM_JS` helper that hands the bytes to the app; progress is not shown (about 2 MB), the checkbox says "loading..." meanwhile. Stored in IndexedDB under `/nv-save` beside the save, read from there on later visits |
| Startup | The chosen face is loaded before the first frame, from the package, so there is no frame in the wrong font. The save is read first (`save_init` runs after `nv_imgui_init` today), so the first frame uses the default and switches when the save is loaded: a flicker the spec accepts only if the load is not already before the first draw; otherwise `save_init` is moved earlier |
| Textures tab | The font atlas is already listed under "UI"; its size is shown, which is how the glyph cache's growth is watched |
| Memory | A face's bytes stay resident: about 0.4 MB for the bundled ones, about 2 MB for Pretendard once loaded. Faces are made on first use, so unused ones cost nothing |
| Licenses | Each face's license text is stored next to it (`assets/fonts/<name>/OFL.txt`, `LICENSE.txt`), and the credits are listed in the README. `.gitattributes` already tracks `*.ttf` and `*.otf` in Git LFS |
| Third-party | Fonts are assets, not libraries, but the faces are listed above for confirmation, as AGENTS.md asks of any third-party addition: nothing is added until they are confirmed |

### Changes

- **Engine (`engine/imgui.h`, `engine/imgui.c`).** The face table, `nv_imgui_set_font`, the Hangul
  fallback merge, loading from memory, the fetch helper (`EM_JS`) with its storage cache.
- **App.** A Font section in `app/ui.c` (View tab), the `FONT`, `FSIZ` and `KRFN` tags in
  `app/save.c`, fixed sizes in `app/ui_desktop.c`, `app/ui_phone.c` and `app/console.c` expressed
  in font heights where they must follow the font, the list of bundled faces in `app/main.c`.
- **Assets.** `assets/fonts/` with the faces and their licenses; `web/manifest.cmake` and the
  CI package gain the on-demand Hangul file.
- **Docs.** `save.md` (the tags), `layout.md` (sizes in font heights), `search.md` (the rows),
  `AGENTS.md` and the README credits.

### Phases

1. **Specimen and the engine's font table:** the table and `nv_imgui_set_font` with the bundled
   faces, a temporary Debug-only switch, and screenshots of every face at 12, 14, 16 and 18 px on
   the desktop (1× and 2× pixel ratio) and the phone (390×664 at 2×) for choosing the default.
   Checked: no WebGPU errors while switching, the atlas does not grow without bound, the Console
   and Textures tabs still draw.
2. **The setting:** the View tab section, the saved tags, the default chosen from the screenshots,
   and the fixed pixel sizes made to follow the font. Checked: both UIs at the smallest and the
   largest size, no clipped labels in the menus, docks, top bars, palette and Console, a reload
   keeps the face and size, an old save loads with the default.
3. **Korean text:** the on-demand fetch, the storage cache, the checkbox, the merge. Checked: a node
   renamed in Hangul shows its text with the box on and the missing-glyph box with it off, the
   second visit reads the file from storage with no network request, an offline fetch is a Console
   warning and the box turns itself off.
4. **Docs and the licenses.**

Every phase is checked in Release and Debug in headless Chromium, the desktop UI with the mouse at
1280×800, the phone UI with touch at 390×664.

### As built

- `assets/fonts/Inter-Regular.ttf` (Inter 4.1, static, 411 KB, SIL Open Font License 1.1) and
  `assets/fonts/Inter-LICENSE.txt`, stored in Git LFS (`*.ttf`).
- The package now preloads the whole `assets/` directory as `/assets` (it was `assets/quaternius`),
  so the glTF files are at `/assets/quaternius/*.glb` and the font at `/assets/fonts/`.
- `nv_imgui_set_font(imgui, ttf, size, pixel_size)` (`engine/imgui.h`) adds the font from memory as a
  dynamic font (size 0), makes it `io->FontDefault` and sets `style.FontSizeBase`; the bytes stay
  in the app's permanent arena. `app/main.c` (`load_font`) reads the file after `nv_imgui_init` and
  before the first frame, and logs a Console warning and keeps the built-in font if it is missing.
- Inter 14 px has about the same line height and average width as the old 13 px face, so the
  layout needed no changes: the docks, top bars and tabs still fit on desktop and on the phone
  (390×664), where the Inspector tab's label no longer needs to be cut.
- The package grows by 0.4 MB (`app.data` 1.7 MB); `korean.md` adds a Hangul fallback face behind it (0.39 MB).
- Playwright scripts written against the old font's pixel positions (menu items, the Reset
  dialog's buttons, rows of the Scene tree) click the wrong places and need new coordinates; the
  app's behavior is unchanged.

### Out of scope

- Separate fonts per role (a monospaced Console while the rest is Inter), bold and italic.
- User-supplied font files, an icon font, and emoji.
- FreeType hinting (above, until the specimen asks for it).

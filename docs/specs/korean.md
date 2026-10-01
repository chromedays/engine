# Korean support spec

Status: proposed (2026-10-01). Changes to this spec are agreed first.

## Goal

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

## Approaches

### Showing Hangul

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

### Typing Hangul on the desktop

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **The text agent on the desktop too, shown at the caret while composing** (recommended) | While ImGui edits text, the hidden input has focus on the desktop as it does on the phone; text arrives through its `input` and `compositionend` events. During a composition the input becomes visible at the text caret (ImGui gives the caret through `PlatformIO.Platform_SetImeDataFn`), so the browser draws the syllable being composed and places the IME's candidate window there | One text path for both UIs; the browser does the IME work | The composing syllable is drawn by the browser over the canvas, in the page's font, not inside the ImGui field; it disappears into the field when finished |
| Keep `keydown` and add composition events on the canvas | Listen for composition on the canvas element | Less change | A `<canvas>` is not editable, so browsers do not start an IME on it |
| Draw the composition inside ImGui | Feed the pre-edit string into the field | Looks native | Dear ImGui has no pre-edit (composition) text support in its input field; we would patch ImGui |

### A Korean UI

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **Our own string table, keyed by the English text** (recommended) | `app/strings.c` holds pairs (English, Korean). UI code writes `T("Speed")`; it returns the English or the Korean text for the language in use, found through a hash table built at start. Widget labels keep their ImGui ID in English: `TL("Speed")` returns `"속도###Speed"` | English stays the source in the code, so code reads as today; a missing translation falls back to English. Fixed arrays, plain C | Every UI string has to be wrapped; a test lists strings that have no translation |
| GNU gettext (C, LGPL, `.po` files) | The classic | Standard tooling | A runtime library and a file format for two languages; LGPL linking terms on the web |
| An enum of string ids | `T(STR_SPEED)` | Compile-time checked | The code no longer reads in English, and every string needs an id |

Recommendation: the three recommended rows. The new third-party pieces are the Pretendard face
(OFL, an asset) and the `subset-font` tool (MIT, run by hand); nothing is added until they are
confirmed.

## Decisions

### Font

| Topic | Decision |
|---|---|
| File | `assets/fonts/Pretendard-Hangul-Subset.otf` (or `.ttf`), its OFL license beside it, made by `tools/subset_hangul.sh` from Pretendard Regular. The script's inputs: KS X 1001's syllables, U+3131 to U+318E (compatibility jamo), U+AC00 to U+D7A3 only as listed, and every character of the Korean strings |
| Merge | `nv_imgui_set_font` gains a fallback: the Hangul face is added with `MergeMode` into Inter, so a string uses Inter's glyph where Inter has one and Pretendard's otherwise. Its size and vertical offset are tuned (`ImFontConfig.SizePixels` ratio, `GlyphOffset`) so Hangul sits on Inter's baseline with the same visual height; checked by screenshots |
| Always loaded | The fallback is loaded at start for every language, so Korean names show in the English UI too |
| Names | Node names are `NV_NODE_NAME_MAX` (32) bytes: 10 Hangul syllables (3 bytes each in UTF-8). `nv_scene_add_node` and the Name field cut at a character boundary, never inside one (a cut syllable would be invalid UTF-8 in the save). The limit itself stays |

### Typing

| Topic | Decision |
|---|---|
| Focus | On the desktop too, the text agent takes focus whenever `io.WantTextInput` turns on, and gives it back when it turns off (the phone's rule, `imgui.c`). While it has focus, `on_key` forwards keys (arrows, Enter, Escape, Backspace, shortcuts) but not characters, as it already does on the phone |
| Composition shown | `Platform_SetImeDataFn` reports the caret position and line height each frame; the agent moves there. On `compositionstart` it becomes visible (opaque text, the field's text color, a dark background, a font size equal to ImGui's in CSS pixels) and on `compositionend` it hides again. The IME's candidate window follows it |
| Keys during composition | A key the IME consumes (`event.isComposing`, or key "Process") is not given to ImGui or to the shortcut table, so composing never fires W, E, R or Space |
| Search boxes and the palette | They are ImGui fields: Hangul typed there works the same, and the matcher (`search.md`) compares UTF-8 bytes, so "그림" finds "그림자" |

### Korean UI

| Topic | Decision |
|---|---|
| Strings | `T(english)` for text and `TL(english)` for widget labels (`"번역###English"`, so a widget's ID, and so its state, is the same in both languages). Translated: menus, tab names, buttons, labels, tooltips, section headings, the palette's action names and kinds, the help window, the Play note, the save messages shown in the View tab, the search box hints. Not translated: Console log rows, node names, clip and texture names from the assets, the build label's commit line, numbers and units |
| Setting | A **Language** combo (English, 한국어) in the View tab, through `search_row` (searchable in both languages). It takes effect on the next frame |
| Default | From the browser's language at first start (`navigator.language` starting with "ko" gives Korean), then whatever was chosen |
| Saved | A new `EDIT` tag `LANG` (u32: 0 English, 1 Korean; anything else loads as English). A save without it uses the browser's language. `SAVE_VERSION` stays. An editor setting, not undoable |
| Search | A row's text for matching is its English label, its Korean label and its keywords, so "shadow" and "그림자" both find the Shadows section in either language; the palette's actions likewise |
| Layout | Korean labels are checked in every dock, the phone panel and the top bars at the widest strings; fixed widths that cut them (the phone's Find and Undo buttons, the desktop Play button) grow or use shorter words |
| Missing translations | A Debug build logs, once each, a string that has no Korean entry while Korean is shown; a ctest test (`tests/`) checks that every `T`/`TL` literal in `app/` has an entry |

## Changes

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

## Phases

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

## Out of scope

- Languages other than English and Korean, right-to-left and shaped scripts.
- Syllables outside KS X 1001's 2,350 (they show as boxes), Hanja.
- Initial-consonant (chosung) search.
- Translating log messages and documents.

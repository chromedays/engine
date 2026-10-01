# Fonts spec

Status: implemented in a smaller scope (2026-10-01). Changes to this spec are agreed first.

**Scope as decided:** Inter, fixed. The editor uses Inter Regular at 14 CSS pixels (times the touch
scale) and nothing else: no Font section, no face or size setting, no saved tags, no Korean
fallback and no fetching. The candidate table, Decisions, Changes and Phases below are the larger
design this replaced; they stay as the plan if choosing fonts is wanted later. What was built is
under "As built".

## Goal

Replace the editor's built-in font with a better-looking one, and let the user pick the font (and
its size) in the app. Today every ImGui window uses Dear ImGui's built-in font (ProggyClean, a
13 px monospaced face made for debugging tools): it is blocky, has a narrow character set (no
Hangul, so a node named in Korean shows as "?"), and cannot be changed.

Out of scope: more than one font in use at once (a separate monospaced font for the Console, bold
or italic text), text rendering in the 3D scene, right-to-left and complex scripts, and fonts
loaded from the user's own files.

## What the engine already gives us

Dear ImGui 1.92 (the version we build) has dynamic fonts: a TrueType (TTF) or OpenType file is
loaded once with no fixed size, glyphs are rasterized when first drawn, at the size and the device
pixel ratio they are drawn at, and the font can change size at any time (`style.FontSizeBase`,
`PushFont(font, size)`). Glyph bitmaps reach the GPU through the texture requests that
`engine/src/imgui.c` already serves (`ImGuiBackendFlags_RendererHasTextures`), so a font with
thousands of glyphs (Hangul) costs only the glyphs used. Fonts can be merged: one face for Latin
and another as the fallback for Hangul. The font file's bytes must stay in memory while it is used.

## Candidates

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
  types. Once fetched it is kept in browser storage (`nv/storage.h`, as the save is) so it is
  downloaded once.
- **Default**: Inter, at a size picked from the specimen. This is the one change everyone sees, so
  it is decided by looking at screenshots, not now.
- **Not now**: the FreeType font loader (`imgui_freetype`; FreeType is dual-licensed FTL or GPL and
  would be a new dependency). Dear ImGui's own stb_truetype rasterizer draws unhinted outlines,
  which are soft at small sizes on a 1× display and fine at 2×. If the specimen shows it is too
  soft at 1×, FreeType with light hinting is proposed as its own step and needs a yes first, as
  every library does.

## Decisions

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

## Changes

- **Engine (`nv/imgui.h`, `engine/src/imgui.c`).** The face table, `nv_imgui_set_font`, the Hangul
  fallback merge, loading from memory, the fetch helper (`EM_JS`) with its storage cache.
- **App.** A Font section in `app/ui.c` (View tab), the `FONT`, `FSIZ` and `KRFN` tags in
  `app/save.c`, fixed sizes in `app/ui_desktop.c`, `app/ui_phone.c` and `app/console.c` expressed
  in font heights where they must follow the font, the list of bundled faces in `app/main.c`.
- **Assets.** `assets/fonts/` with the faces and their licenses; `web/manifest.cmake` and the
  CI package gain the on-demand Hangul file.
- **Docs.** `save.md` (the tags), `layout.md` (sizes in font heights), `search.md` (the rows),
  `AGENTS.md` and the README credits.

## Phases

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

## As built

- `assets/fonts/Inter-Regular.ttf` (Inter 4.1, static, 411 KB, SIL Open Font License 1.1) and
  `assets/fonts/Inter-LICENSE.txt`, stored in Git LFS (`*.ttf`).
- The package now preloads the whole `assets/` directory as `/assets` (it was `assets/quaternius`),
  so the glTF files are at `/assets/quaternius/*.glb` and the font at `/assets/fonts/`.
- `nv_imgui_set_font(imgui, ttf, size, pixel_size)` (`nv/imgui.h`) adds the font from memory as a
  dynamic font (size 0), makes it `io->FontDefault` and sets `style.FontSizeBase`; the bytes stay
  in the app's permanent arena. `app/main.c` (`load_font`) reads the file after `nv_imgui_init` and
  before the first frame, and logs a Console warning and keeps the built-in font if it is missing.
- Inter 14 px has about the same line height and average width as the old 13 px face, so the
  layout needed no changes: the docks, top bars and tabs still fit on desktop and on the phone
  (390×664), where the Inspector tab's label no longer needs to be cut.
- The package grows by 0.4 MB (`app.data` 1.7 MB).
- Playwright scripts written against the old font's pixel positions (menu items, the Reset
  dialog's buttons, rows of the Scene tree) click the wrong places and need new coordinates; the
  app's behavior is unchanged.

## Out of scope

- Separate fonts per role (a monospaced Console while the rest is Inter), bold and italic.
- User-supplied font files, an icon font, and emoji.
- FreeType hinting (above, until the specimen asks for it).

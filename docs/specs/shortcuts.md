# Desktop keyboard shortcuts spec

Status: implemented (2026-09-30). Changes to this spec are agreed first.

## Goal

Give the desktop UI (`layout.md`) a full set of keyboard shortcuts, kept in one table that drives
the keys, the menus' shortcut labels and a help window, so a shortcut is defined once and shown
wherever its action appears.

Today a few keys are handled where their action lives, each with its own checks: Ctrl+Z,
Ctrl+Shift+Z and Ctrl+Y in `app/undo.c`, W, E and R in `draw_gizmo` (only while the pointer is over
the viewport), Space and F in `app/ui_desktop.c`. The Edit menu writes "Ctrl+Z" as a literal. And
a key the page does not consume goes on to the browser: Ctrl+S opens the browser's "Save page"
dialog, because the engine consumes a key only while ImGui wants the keyboard (a text field).

## Approaches

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **Our table, fired through Dear ImGui's `Shortcut()`** (recommended) | `app/shortcuts.c` holds one row per shortcut: its key chord, its name, when it applies and the function it calls. Each frame the desktop UI asks ImGui's `igShortcut_Nil(chord, RouteGlobal)` for every row | ImGui 1.92 already routes chords: a text field that is being edited owns the keys it uses (Ctrl+Z undoes the field's text, a typed W stays a W), and exact modifiers are matched (Ctrl+Z is not Ctrl+Shift+Z). The table gives the menus and the help window the same labels. Plain C, fixed arrays | Our own code for the table, the help window and the browser hook (below) |
| Keep checking keys at each action | As today | Nothing to write | Checks drift apart (W/E/R need the pointer in the viewport, Space does not), labels are typed twice, and nothing lists the keys |
| A web hotkey library (hotkeys-js, Mousetrap; JavaScript, MIT) | Binds keys in the page | Handles the browser side | Keys would reach the app outside ImGui's input, so a text field could not keep its keys; a second input path beside `imgui.c`. Not worth a dependency |
| ImGui's keyboard navigation (`NavEnableKeyboard`) | Arrow keys and Enter move through widgets | Built in | Not shortcuts; it also makes ImGui want the keyboard whenever a window is focused, which would take keys from the viewport. Stays off |

Recommendation: our table, fired through `igShortcut_Nil`. No third-party library.

## Decisions

| Topic | Decision |
|---|---|
| Where | Desktop UI only. The phone UI has no shortcuts, as `layout.md` says: Ctrl+Z, Ctrl+Shift+Z and Ctrl+Y move from `undo_update` into the table, so a phone (or a tablet with a keyboard, which gets the phone UI) no longer has them |
| One table | `app/shortcuts.c`: `Shortcut {ShortcutId id; ImGuiKeyChord chord; const char* name; u32 when; void (*run)(App*); b32 repeat;}`. A row may have a second chord (Ctrl+Y beside Ctrl+Shift+Z). Menus call `shortcut_label(id)` for their right-hand text; the help window lists the rows |
| When one fires | Through `igShortcut_Nil(chord, ImGuiInputFlags_RouteGlobal)`, once per frame, after the docks are built. Never while a text field is being edited (`io.WantTextInput`), a popup or modal is open (Escape is theirs), or the gizmo is being dragged. Rows can further require the showcase (`WHEN_SHOWCASE`), Edit mode (`WHEN_EDITING`) or a selection (`WHEN_SELECTION`). Unlike today, W, E and R no longer need the pointer over the viewport: the text-field rule is what keeps typing safe |
| Repeat | Only Undo and Redo repeat while held; everything else fires once per press |
| The browser's own keys | The engine gains a hook, `NvImgui.claims_key(data, key, mods)`, asked on every keydown: when it says yes, `on_key` consumes the event (`preventDefault`) even though ImGui does not want the keyboard. The app answers from the table, so Ctrl+S saves the app and not the web page. Keys a browser never gives to a page are never bound: Ctrl+W, Ctrl+T, Ctrl+N, Ctrl+Tab, Ctrl+Shift+W/T/N, Ctrl+1 to Ctrl+9, Ctrl+Q, F11. Reload (F5, Ctrl+R) and developer tools (F12, Ctrl+Shift+I) are left to the browser on purpose |
| Keys by position | Letters are matched by physical position (`KeyboardEvent.code`), as `imgui.c` already reads them, so W, E and R sit together on any layout. Labels name the US-layout key. `?` is Shift+Slash by position too |
| macOS | Cmd already arrives as Ctrl (`imgui.c`). Labels say "Cmd" instead of "Ctrl" on macOS (from `navigator.platform`), spelled out since the default font has no ⌘ glyph |
| Help | **Help > Keyboard shortcuts** and the `?` key open a window listing every row by group, with its keys, greyed while it does not apply. Escape or the window's close button closes it |
| Tests | Debug builds export `Module._app_debug_gizmo(n)` (0 operation, 1 local, 2 snap), `_app_debug_docks_shown(n)` and `_app_debug_selected()`, beside the existing `_app_debug_playing`, `_app_debug_view` and `_app_debug_undo_done`. Playwright presses the keys and checks those, and checks `defaultPrevented` on a claimed key with a listener of its own |
| Third-party | None |

## The shortcuts

Existing ones are marked; the rest are new.

| Group | Keys | Does | When |
|---|---|---|---|
| File | Ctrl+S | Save now (the File menu's item) | Storage available |
| Edit | Ctrl+Z (existing) | Undo | Showcase, Edit mode; repeats |
| Edit | Ctrl+Shift+Z, Ctrl+Y (existing) | Redo | Showcase, Edit mode; repeats |
| Edit | Escape | Clear the selection | A selection; no popup open |
| Play | Space (existing) | Play or Stop | Showcase |
| Gizmo | W, E, R (existing, now global) | Move, Rotate, Scale | |
| Gizmo | X | Switch Local and World axes (Unity's key) | |
| Gizmo | Ctrl, held during a drag | Snap for this drag (the opposite of the Snap box while held) | Dragging the gizmo |
| View | F (existing) | Move the orbit point to the selection | A selection |
| View | Shift+F | Turn "Camera follows selection" on or off | |
| View | Home | Back to the scene's start view (yaw, pitch, distance, orbit point) | |
| Docks | Ctrl+B | Show or hide the Scene dock (left) | |
| Docks | Ctrl+I | Show or hide the Inspector dock (right) | |
| Docks | \` (backquote) | Show or hide the Console dock; opens it if it is collapsed | |
| Help | ? (Shift+/) | Open the Keyboard shortcuts window | |

Kept free for later work: Delete, Ctrl+D (duplicate) and F2 (rename) for scene editing;
Ctrl+Shift+P for a command palette. Ctrl+D is the browser's bookmark key: the claim hook will take
it when duplicate exists.

## Changes

- **Engine (`nv/imgui.h`, `engine/src/imgui.c`).** `NvImgui.claims_key` and `claims_key_data`; `on_key`
  returns true when the hook claims the key.
- **App.**
  - `app/shortcuts.c` (new): the table, `shortcuts_update` (fires rows, desktop only),
    `shortcut_label`, `shortcuts_claim` (the hook), `shortcuts_help` (the window).
  - `app/ui_desktop.c`: the Help menu, menu labels from the table, and its Space and F code moves
    into the table.
  - `app/undo.c`: the keys leave `undo_update`; `request` stays how the table asks for a step.
  - `app/main.c`: W, E, R leave `draw_gizmo`; Ctrl-held snap reads the key there; Home's start
    view is kept per scene when it is built.
- **Docs.** `layout.md` (its shortcut table moves here), `gizmo.md`, `undo.md`, `play.md`,
  `AGENTS.md` and README.

## Phases

1. **The table:** `shortcuts.c` with the existing shortcuts moved into it (Ctrl+Z, Ctrl+Shift+Z,
   Ctrl+Y, W, E, R, Space, F), the firing rules, the engine's claim hook, and the Edit menu's labels
   from the table. Checked: each one still works; none fires while typing in a field (and Ctrl+Z
   there undoes the field's text, not the scene); W, E and R work with the pointer over a dock; the
   phone UI has none.
2. **New shortcuts:** Ctrl+S (checked: `defaultPrevented`, so no Save page dialog), Escape, X,
   Ctrl-held snap, Shift+F, Home, Ctrl+B, Ctrl+I and backquote. Checked: each one's effect through
   the debug exports, and that popups keep Escape.
3. **Help and docs:** the Help menu and `?` window, Cmd labels on macOS (checked by overriding
   `navigator.platform`), and the documents above.

Every phase is checked in Release and Debug in headless Chromium at 1280×800 with a US keyboard.

As built, phases 1 and 2 arrived together (the whole table and the claim hook are one piece of
code) and phase 3's help window and Cmd labels with them; the phases then checked them. Notes:

- The help window is a modal popup, not a plain window: input that starts over a plain window
  on the viewport would become a camera drag, and the viewport already leaves open popups alone.
  Escape and `?` close it, but not in the frame that opened it (the opening press is still that
  frame's press).
- Escape is never claimed from the browser (it leaves full screen); every other bound chord is, unless
  a text field wants the keyboard.
- Ctrl and Shift+F use the chords through ImGui's `Shortcut()` with `RouteGlobal`; Ctrl held during
  a gizmo drag reads `io.KeyCtrl` in `draw_gizmo`, since it is a held modifier, not a chord.
- Debug builds export `_app_debug_gizmo`, `_app_debug_state` (selection, follow, help open, docks
  shown) and `_app_debug_selected_position`, which the tests read.

## Out of scope

- Remapping keys, and saving custom bindings.
- Shortcuts on the phone UI, and for a tablet with a keyboard (it gets the phone UI).
- Actions that do not exist yet (duplicate, delete, rename, a command palette); their keys are
  kept free above.

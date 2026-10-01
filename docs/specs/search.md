# Search spec

Status: proposed (2026-10-01). Changes to this spec are agreed first.

## Goal

Find things by typing, as in Unreal Editor, in two ways:

1. **Panel search boxes.** A box at the top of every editor panel except the Console (the Scene
   tree, Inspector, View, Textures and Stress tabs) filters that panel as you type: the Scene tree
   keeps the matching nodes and their parents, the other panels keep the matching rows under their
   section headings. This works like the World Outliner and the Details panel's "Search Details".
2. **A command palette.** One box (Ctrl+Shift+P on the desktop) searches everything at once:
   actions (the shortcut table and a few more), the nodes of the shown scene, and every setting in
   the panels. Enter runs an action, selects a node, or jumps to a setting.

The Console keeps its own level filters (`console.md`) and gets no box.

## Approaches

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **Our matcher and our widgets** (recommended) | One matching function (words, case-insensitive substrings, below) used by every box; panel rows go through a small helper that decides whether to draw them; the palette is a modal popup listing results | Plain C, a few hundred lines in a new `app/search.c`. Same rules everywhere | Every panel row has to go through the helper (a rule for new UI code) |
| Dear ImGui's `ImGuiTextFilter` | ImGui's built-in filter: a text box and `PassFilter(text)` | Already in the build | Its syntax is comma-separated with `-` exclusions, not words separated by spaces as Unreal and most search boxes behave. It filters strings, not panels: the deferred headings, the tree's parents and the palette are ours anyway. Not used |
| Fuzzy matching: fts_fuzzy_match (C/C++ single header, public domain or MIT, Forrest Smith) or fzy's algorithm (C, MIT) | Scores letters in order with gaps ("tgsh" finds "Toggle shadows") | Good for a palette with hundreds of entries | Rankings are hard to predict; long settings lists match too much. Not needed at our size; it can replace the palette's scoring later without changing anything else |
| A web library (Fuse.js, JavaScript, Apache-2.0) | Fuzzy search in the page | | Results would cross the JavaScript boundary every keystroke, for strings that live in WebAssembly. No |

Recommendation: our matcher and widgets. No third-party library.

## Matching

- The query is split at spaces into words; a candidate matches when **every** word is a substring
  of it, ignoring ASCII case (`shad res` matches "Shadow map resolution"). Bytes outside ASCII
  compare as they are, so a UTF-8 node name matches its own spelling.
- What a candidate's text is: a node's name; a panel row's label plus its section's heading plus
  any extra keywords the row gives (so `msaa` finds the Anti-aliasing combo, and `shadows` shows
  the whole Shadows section); an action's name plus its group.
- Ranking (the palette only; panels keep their order): a word matching the start of a word in the
  candidate ranks above one in its middle; then shorter candidates first; then the order they were
  listed in.
- An empty query matches everything.

## Panel search boxes

| Topic | Decision |
|---|---|
| Panels | Scene, Inspector, View, Textures and Stress, on both UIs. Not the Console |
| The box | An input with the hint "Search" across the panel's width, above its content, with a clear button (×) while it has text. It does not scroll away: the content below scrolls in a child window, which keeps the touch-drag scroll (`nv_imgui_touch_scroll`) |
| Keys in the box | Escape clears it (and leaves it); Enter leaves it. While the box is being edited shortcuts do not fire, as for every text field |
| One query per panel | Each panel keeps its own text, kept when switching tabs, scenes or selections (as Unreal's Details search stays when selecting another actor). Not saved, not undoable |
| Scene tree | Matching nodes are shown with all their parents; parents that do not match are greyed. Parents of matches are opened while the filter is on (their open state without a filter is kept). The depth limit (24) and the closed big groups do not apply while filtering: a match deep in the stress chain is shown under its parents, cut to the 24 nearest with a "..." row. Matches are found once when the query or the scene changes (a bit per node in the app arena), not every frame, and the tree shows at most 500 matching rows, then "and N more" |
| Other panels | A row is drawn when it matches. A section heading (`igSeparatorText`, a collapsing header) is drawn only when one of its rows is, and every row of a section shows when the heading itself matches. Text that is not a setting (help lines, the FPS line) is shown only with an empty query |
| No match | The panel shows "No match for '<query>'" |
| Highlight | The matched part of each shown label gets a translucent accent box (Scene tree names, row labels, palette rows) |
| Ctrl+F (desktop) | Focuses the search box of the panel under the mouse pointer, or else of the right dock's current tab. It is a row in the shortcut table and claimed from the browser (its own Find) |
| Phone | Same boxes; typing goes through the text agent (`imgui.c`), so the phone keyboard opens on tap |
| Tests | Debug builds export `_app_debug_search_rows(panel)` (rows drawn last frame) and `_app_debug_search_set(panel)`, which reads the query from a fixed buffer the test writes with `stringToUTF8` |

### How a panel row is written

Panels draw through two calls, so headings can wait for their first row:

```c
search_section(&app->search, "Shadows");                      // remembered, not drawn yet
if (search_row(&app->search, "Shadow map resolution", NULL))  // draws the heading if pending
    igCombo_Str_arr("Shadow map resolution", ...);
if (search_row(&app->search, "Anti-aliasing", "msaa samples"))
    igCombo_Str_arr("Anti-aliasing", ...);
```

A group of widgets on one line (Move, Rotate, Scale, Local, Snap) is one row whose keywords name
them all. `search_row` also records the row for the palette (below). This becomes a rule in
`AGENTS.md`: every widget in a searchable panel goes through `search_row`.

## Command palette

| Topic | Decision |
|---|---|
| Opening | Desktop: Ctrl+Shift+P (kept free for it in `shortcuts.md`, and claimed from the browser), F1, and **Edit > Command palette**. Phone: a **Find** button in the top bar between Undo and Play (the phone top bar in `layout.md` gains it) |
| Window | A modal popup centered under the top bar: 560 px wide on the desktop, the canvas's width less the margins on the phone; a box focused on open and up to 12 result rows, scrolling beyond that. It never covers the top bar |
| Keys | Up and Down move the highlighted row (wrapping), Page Up and Page Down by a page, Enter runs it, Escape closes. A mouse click or a tap runs a row. Typing anywhere goes to the box |
| Rows | Each row: its kind (Action, Node, Setting) greyed on the left, its name with the match highlighted, and on the right the shortcut keys (actions), the parent's name (nodes) or the panel and section (settings) |
| Actions | Every row of the shortcut table that has something to run, plus rows that only the palette offers (no keys): Show save, Reset..., Show the Showcase / Stress scene, Open the Textures tab, Clear the console. They are one table: `Shortcut` becomes `Command`, and a command without a chord is palette-only. Actions that do not apply now (`when`) are listed last, greyed, and cannot run |
| Nodes | Every node of the shown scene. Enter selects it and opens the Inspector, as a click in the tree does; Shift+Enter also moves the orbit point to it (F). In the stress scene at most 200 node results are listed, with "and N more" |
| Settings | Every row the searchable panels have, collected by drawing them in a collect mode where `search_row` records the row and returns false, so nothing is drawn. The Inspector's rows are those of the current selection. Enter shows the row's dock and tab (opening the dock if hidden) and puts the row's label into that panel's search box, so the setting is the first thing there |
| Empty query | The last 8 actions run from the palette (not saved), then every action in table order |
| While playing, popups, gizmo | The palette opens under the same rules as a shortcut (not while a field is edited, a popup is open or the gizmo is dragged). It does not open over the stress scene's running benchmark |
| Tests | `_app_debug_palette(n)` (0 open, 1 result count, 2 highlighted index, 3 kind of the highlighted row) and the same query buffer as the panels |

```
+----------------------------- top bar ------------------------------+
|        +--------------------------------------------------+        |
|        | shad                                          [x] |        |
|        | Setting  Shadows                    View > Shadows |        |
|        | Setting  Shadow map resolution      View > Shadows |        |
|        | Action   Show save                                 |        |
|        +--------------------------------------------------+        |
```

## Changes

- **App.**
  - `app/search.c` (new): the matcher, the per-panel boxes and queries, `search_section` and
    `search_row`, the collect mode, the Scene tree's match bits, the palette.
  - `app/ui.c`, `app/stress.c`, `app/textures.c`, `app/save.c` (its section in the View tab): rows
    through `search_row`, boxes at the top of the sections; the tree filtered.
  - `app/shortcuts.c`: `Shortcut` becomes `Command`, with palette-only rows; Ctrl+F, Ctrl+Shift+P
    and F1.
  - `app/ui_desktop.c`: Edit > Command palette, the right dock's tab chosen by a setting result.
    `app/ui_phone.c`: the Find button, the panel tab chosen by a setting result.
  - `app/app.h`: `Search` state in `App`.
- **Engine.** Nothing expected; the highlight uses the window draw list.
- **Docs.** `layout.md` (the phone Find button), `shortcuts.md` (Ctrl+F, Ctrl+Shift+P, F1 and the
  palette-only commands), `textures.md`, `stress.md`, `AGENTS.md` (the `search_row` rule) and
  README.

## Phases

1. **Panel search:** the matcher, the boxes on the five panels in both UIs, the deferred headings,
   the Scene tree with parents (and the stress scene's deep chain and big groups). Checked:
   matching rows and headings for several queries in each panel, no-match text, Escape clears,
   shortcuts do not fire while typing, touch scroll under the box on the phone, the stress scene's
   tree finds a node at the chain's end without a slow frame.
2. **Command palette:** the command table, nodes, settings by collect mode, the keys, Edit menu and
   the phone's Find button. Checked: each kind of result runs (an action, a node selected, a
   setting shown in its tab with the filter set), greyed actions do not run, Escape closes,
   recent actions come first, Ctrl+Shift+P does not reach the browser.
3. **Polish and docs:** the highlight, Ctrl+F, F1, the documents above.

Every phase is checked in Release and Debug in headless Chromium, desktop at 1280×800 with the
mouse and keyboard, phone at 390×664 with touch.

## Out of scope

- Fuzzy matching (above), searching inside values (a node's position, a material's color) and
  regular expressions.
- Searching the Console's messages.
- Remembering queries or recent commands across reloads.
- Commands that do not exist yet (add, delete, duplicate, rename nodes).

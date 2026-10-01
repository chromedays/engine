# Desktop and phone layout spec

Status: implemented (2026-09-30). Changes to this spec are agreed first.

## Goal

Give the desktop and the phone two separate editor UIs, each built for its input device, and put
the Play / Stop button at the top center of the screen in both.

Today one layout serves both: the scene viewport on the top 60% of the canvas and one tabbed panel
on the bottom 40% (`nv_editor_layout`, `app_build_ui`). That is right for a phone held upright. On
a wide screen it leaves the scene a thin strip, forces tab switches between the Scene tree and the
Inspector, and keeps Play, Undo and Redo in a row inside the panel.

## Approaches

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **Two UIs on our own rectangles** (recommended) | The app computes every region's rectangle itself and opens one ImGui window per region, as `nv_imgui_begin_panel` does today. Each device has its own layout function and its own file | The viewport is already "a rectangle" everywhere (`NvImgui.view_rect`, the renderer's viewport, picking, the gizmo), so the viewport being in the middle of the canvas changes only the numbers. No docking state to keep in step with the save | Splitters and collapsing are ours to write (phase 3) |
| Dear ImGui docking | The cimgui build already has `ImGuiConfigFlags_DockingEnable`. Users drag windows into any arrangement | Free drag-and-dock behavior | Window positions live in ImGui's own `.ini` text, apart from our save format and its versioning; the viewport is then a dock node whose rectangle we read back each frame, and a stray drop can hide the viewport or the Play button. Docking is a mouse feature, which this desktop UI would use and the phone UI must not |
| One adaptive layout | Switch arrangement by window size inside the same code | One code path | The two devices differ in controls, sizes and gestures, not only in arrangement; one function ends up full of `if (phone)`. Rejected: the UIs are to be fully separate |
| A library | None fits: the layout is the app's own windows | | |

Recommendation: two UIs on our own rectangles. Docking stays available to switch to later; nothing
here rules it out.

## Decisions

| Topic | Decision |
|---|---|
| Choosing the UI | Once at start, from the primary pointer: a coarse pointer (touch) gives the **phone UI**, anything else the **desktop UI** (`js_touch_is_primary`, which already sets `NvImgui.ui_scale`). The choice follows the input device, not the window size: a narrow desktop window keeps the desktop UI. There is no switch for now |
| Separation | `app/ui_desktop.c` and `app/ui_phone.c` each own their layout, windows, toolbar, sizes and gestures. `app_build_ui` becomes a call to one of them. Neither file branches on the other's device. They share only the **sections**: functions that draw the content of one panel into the current window (`scene_tab`, `inspector_tab`, `view_tab`, `stress_ui`, `console_tab`, `undo_ui` and the save and shadow sections inside them), so editing logic exists once. A section may ask `app->ui_mode` for a size, never to change what it edits |
| Play / Stop | Top center of the screen in both UIs: a button whose center is the canvas's horizontal center, in a **top bar** across the full width above the scene viewport. It shows Play in Edit mode and Stop while playing, as today, and is hidden while the stress scene is shown (`play.md`) |
| Top bar, desktop | 32 px high. Left: the menu bar (File, Edit, View). Center: Play / Stop, and right after it, while playing, the note "Playing: edits are lost on Stop". Right: frame time and the shown scene's name |
| Top bar, phone | 48 CSS pixels high (hit areas a finger can reach). Left: Undo, then Find (opens the command palette, `search.md`). Center: Play / Stop. Right: Redo. Nothing else. The row of Play, Undo and Redo leaves the panel, which gets that line back |
| Desktop regions | Below the top bar: **left dock** (Scene tree, 260 px wide), **right dock** (tabs Inspector and View, 340 px wide), **bottom dock** (tabs Console and, while the stress scene is shown, Stress; 220 px high, collapsible to its tab strip), and the **scene viewport** filling what is left in the middle |
| Phone regions | Below the top bar, as today: the viewport on the top 60% of the rest and one tabbed panel on the bottom 40% (Scene, Inspector, View, Console, and Stress while shown). Picking a node still jumps to Inspector |
| Build label | Stays in the viewport's top-left corner in both UIs, with its badge; a tap or click on it opens the Console (the bottom dock tab on desktop, the Console tab on phone). On phone it sits just below the top bar, so it never meets the Play button |
| Play tint | The panels (docks on desktop, the panel on phone) and the top bar are tinted while the showcase plays, as the panel is today |
| Units | Sizes in this spec are CSS pixels times `ui_scale` (1.0 on desktop, 1.3 on phone); rectangles handed to the renderer, picking and `view_rect` are in framebuffer pixels, converted by the pixel ratio as now |
| Keeping the viewport | The docks clamp so the viewport is never smaller than 40% of the canvas width or 40% of the height under the top bar. Below that (a very small desktop window) the docks stop at their minimums (left 160, right 220, bottom 120) and the viewport takes what is left |
| Splitters | The borders between the docks and the viewport can be dragged (mouse only, 8 px hit area, resize cursor), and a click on the bottom dock's tab strip collapses or opens it. The sizes are saved |
| Saved state | Editor settings gain `EDIT` tags for the left width, the right width, the bottom height and whether the bottom dock is open (`save.md`), clamped on load. These are desktop settings; the phone UI has no saved layout. The UI choice itself is not saved: it comes from the device every start |
| Third-party | None |

## Desktop UI

```
+----------------------------------------------------------------------+
| File  Edit  View |        [ Play ]  (note while playing) |  16.7 ms  |   top bar
+----------+--------------------------------------+--------------------+
| Scene    |                                      | Inspector | View   |
| (tree)   |           scene viewport             |                    |
|          |   build label + badge                | selected node's    |
|          |                                      | components         |
+----------+--------------------------------------+--------------------+
| Console | Stress                                                ▾    |   bottom dock
+----------------------------------------------------------------------+
```

- **Menu bar.**
  - File: Save now, Show save, Reset (with its confirmation), Export and Import when they exist.
  - Edit: Undo, Redo, with their labels and shortcuts.
  - View: show or hide the left dock, the right dock and the bottom dock, and the scene picker
    (Showcase, Stress) that the View tab holds today.
- **Right dock tabs.** Inspector selects itself when a node is picked, as today. View keeps its
  current contents (camera, shadows, save).
- **Bottom dock.** The Console, and the Stress tab while the stress scene is shown (it selects
  itself when the stress scene is opened, as today). The Console tab's label and color still show
  unseen warnings and errors; on desktop the label has room for the count.
- **Shortcuts** (desktop only): see `shortcuts.md`, which holds the table (Space plays and stops,
  F moves the orbit point to the selection, and the rest).

- **Mouse habits.** Hover tooltips name what a control does; Ctrl+click on a slider types a
  number. Both are ImGui's own behavior, so they need no code beyond a few tooltips.

## Phone UI

```
+------------------------------+
| Undo Find [ Play ]     Redo  |   top bar, 48 px
+------------------------------+
|  build label + badge         |
|        scene viewport        |   60% of the rest
+------------------------------+
| Scene | Inspector | View | Console
| (the tab's content)          |   40% of the rest
+------------------------------+
```

The phone UI keeps what it does today (one panel of tabs, touch scrolling, the compact Console
rows, the sizes in `gizmo.md` and `console.md`), with these changes: the Play / Undo / Redo row
moves into the top bar, the "Playing: edits are lost on Stop." line stays at the top of the panel
(wrapped), and the build label moves just below the top bar. It has no menu bar, no shortcuts,
and no splitters.

## Changes

- **Engine (`nv/imgui.h`, `engine/src/imgui.c`).**
  - `nv_editor_layout` is replaced by rectangle helpers the two UIs call: a top bar rectangle, and
    a split of the rest. The viewport no longer has to start at (0, 0).
  - `in_view` already tests `view_rect` as a rectangle, so input routing needs no change. A press
    that starts on a splitter or a dock belongs to ImGui (docks are windows, so `WantCaptureMouse`
    covers them).
  - The page's canvas cursor follows ImGui's mouse cursor (for the splitters' resize cursor).
- **App.**
  - `app/ui_desktop.c` and `app/ui_phone.c`; `app/ui.c` keeps the sections and the shared
    helpers; `App.ui_mode`; the docks' sizes in `App`.
  - `app/main.c` asks the active UI for the viewport rectangle each frame. The gizmo, picking,
    `badge_box` and the Play button read it instead of assuming a top-left viewport.
  - `app/save.c`: the four `EDIT` tags and a row each in the spec's table.
- **Tests.** Debug builds export the viewport and dock rectangles and the Play button's rectangle
  (`Module._app_debug_layout_*`) so tests can click them and check the Play button's center against
  the canvas center.
- **Docs.** `AGENTS.md` (the editor-layout rule), README, `viewport.md`, `play.md`, `console.md`,
  `undo.md` and `save.md` where they name the panel, the Play row, or the layout.

## Phases

1. **Two UIs:** the UI choice, the top bar with the centered Play / Stop (both UIs), the desktop
   docks at fixed sizes with the sections in them, the phone layout moved to its own file with
   the Play row in the top bar, and the viewport as a general rectangle. Checked in Release and
   Debug: the desktop viewport's size and position, picking and the gizmo at several points of
   the viewport (including near each dock), the Play button's center equal to the canvas center at
   several window sizes, and the phone UI unchanged apart from the top bar (Playwright with touch).
2. **Desktop habits:** the menu bar, Space and F, the bottom dock's collapse, tooltips. Checked
   that shortcuts do not fire while typing in a field, and that F moves the orbit point only.
3. **Splitters and docs:** dragging the docks' borders with the clamps, the resize cursor, the
   saved sizes (a reload keeps them, an old save without the tags loads with the defaults), a
   very small desktop window, and the documents above.

Every phase is checked in Release and Debug in headless Chromium, the desktop UI with the mouse
at 1280×800 and 1920×1080, the phone UI with touch at 390×664.

As built, the menu bar, Space and F, the collapsing bottom dock, the splitters and the saved sizes
all arrived with phase 1, since they are part of the same window code; phases 2 and 3 then
checked them. Notes on the build:

- File has Save now, Show save (it opens the save viewer in the View tab) and Reset...; Export and
  Import are not there yet.
- The phone's Undo and Redo buttons are 64 px wide, and Find 52 px (`search.md`), so all three fit
  beside the centered Play button on a 360 px screen.
- The Play button is 84 px wide on the desktop (104 on the phone); the desktop top bar is 28 px
  high, not 32, because it is the menu bar's own height.
- A splitter is a transparent window over the 6 px edge strip inside the dock, not inside the
  viewport, so a press on it never becomes a camera drag. A drag starts from the size shown, which
  the window may have cut below the wanted one.
- Dock sizes clamp to 160..640 (left), 220..640 (right) and 120..600 (bottom), then shrink so the
  viewport keeps 40% of the canvas width and of the height under the top bar (the right dock gives
  way first), and never drops below 64 px. The wanted sizes are what is saved.
- The canvas's pointer follows ImGui's cursor (`js_set_cursor` in `imgui.c`): a resize cursor over
  a splitter, a text cursor over a field.

## Out of scope

- Adding, deleting, duplicating and renaming nodes (a right-click menu in the tree needs them): a
  separate scene-editing spec.
- Switching UIs at runtime, and a tablet-specific layout (a tablet with a coarse pointer gets the
  phone UI for now).
- Dragging panels into other arrangements (docking), multiple viewports.

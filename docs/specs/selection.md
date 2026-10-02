# Multiple selection spec

Status: implemented (2026-10-01). Changes to this spec are agreed first.

## Goal

Select several nodes at once in the editor and move, rotate or scale them together with the
transform gizmo, as one undo step. Today `SceneView.selected` holds one node, and everything that
shows or edits the selection (the Scene tab, picking, the outline, the gizmo, the Inspector, undo,
the save) works on that one node.

Out of scope: box (marquee) selection in the viewport, editing a field of every selected node at
once in the Inspector, and actions on the selection beyond the gizmo (the editor has no delete or
duplicate yet).

## Approaches

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **A primary node plus a list of others** (recommended) | `SceneView.selected` stays the primary node (the last one picked); a fixed array holds the other selected nodes. Clicks with Ctrl or Shift, a Shift range in the Scene tab, and a Multi toggle on the phone change it | Everything that only needs one node (the Inspector, the camera's follow, the palette's node jump, the save's `SELN`) keeps working on the primary. Plain C, fixed arrays | Our own click and range rules |
| Dear ImGui's multi-select (`BeginMultiSelect`, 1.92) | ImGui's own selection requests for lists and trees: Ctrl, Shift ranges, Ctrl+A, box select in a list | Built in, for the Scene tab | Covers the Scene tab only (not the viewport); its tree support needs every row submitted in order, which the clipped, filtered tree does not do; touch has no Ctrl, so the phone's toggle would need to fake modifier keys |
| A library | None: selection is about the app's own data | | |

Recommendation: a primary node plus a list of others. No third-party library.

## Decisions

| Topic | Decision |
|---|---|
| State | `SceneView.selected` is the primary node: shown in the Inspector, followed by the camera, the start of a Shift range. `SceneView.others` holds up to 255 more (`SELECTION_MAX` 256 in all), without duplicates and never the primary. Nothing selected means no primary and no others |
| Primary | The node picked last. Adding a node makes it the primary; removing the primary makes the most recently added other the primary |
| Viewport, desktop | A click selects only the mesh under it, or clears the selection on empty space, as today. Ctrl+click or Shift+click (Cmd counts as Ctrl on macOS) adds the node under the pointer, or removes it if it is selected; on empty space it does nothing. A character's meshes stand for its root, as today. The keys count as they were when the button went down: a tap is decided a few frames later (`gizmo.md`), by which time they may be up |
| Scene tab, desktop | A click selects only that node. Ctrl+click adds or removes it. Shift+click selects the rows from the range's anchor (the node last clicked without Shift) to the clicked one, in the order the tree shows them, and makes the clicked one the primary. Rows hidden in a closed group are not part of a range |
| Phone | The phone has no modifier keys. A **Multi** toggle in the top bar, between Find and Redo, makes every tap add or remove, in the viewport and in the Scene tab; a tap on empty space then does nothing. It stays on until tapped again, and is not saved |
| Limit | Adding a 257th node does nothing and logs a warning once. The limit comes from fixed arrays, not from the scene (`NV_MAX_NODES` is 16384): the undo steps (128 of them, each holding the nodes before and after) and the stack buffers of the save's selection tag grow with it, and the stack is 64 KB. Raising it further needs a smaller undo record (only the changed nodes and fields) first |
| Inspector | Shows and edits the primary node only. With more than one node selected, a line above it says "N selected" with a **Keep one** button, which keeps only the primary |
| Clearing | Esc (desktop) clears everything, as today. The Inspector's Keep one keeps the primary |
| Outline | Every selected node gets its mesh boxes. The primary's are cyan, as today; the others' are a darker blue |
| Gizmo, one node | As today (`gizmo.md`) |
| Gizmo, several nodes | The gizmo sits at the average of the selected nodes' world positions. Move: every node moves by the drag. Rotate and scale: around that point. Axes: world, or the primary's rotation when Local is on; scale uses the primary's rotation. The active camera is left out (the orbit camera owns it), and so is a node whose parent or other ancestor is selected, since it already moves with that ancestor |
| Applying a drag | At the first frame of a drag, each moving node's world matrix and the gizmo's matrix are kept. Each frame the gizmo's change since then (current × inverse of the start) is applied to every kept world matrix, which then goes back to the node's position, rotation and scale through its parent (`gizmo.md`'s write-back). So snapping, and rotation and scale around the center, do not drift over a long drag |
| Camera follow | Follows the primary, as today; still holds still while the gizmo drags |
| Undo | The Node scope (`undo.md`) covers every selected node: its bytes are each node's fields in a container, in selection order. A multi-node gizmo drag is one step. The label is "name Field" for one node changed, "N nodes Field" for several. Undoing or redoing a step selects the nodes it holds, the first as the primary. Selecting or deselecting a node still commits without a step. The step size grows to 64 KB (`SELECTION_MAX` nodes at `UNDO_NODE_BYTES` 256 each: a node writes about 150 bytes, 220 at most) |
| Save | `SELN` stays the primary's path. A new `VIEW` tag, `SELO` (u32[]), holds the others' paths, each as its length followed by its indices; missing or broken: no others. No `SAVE_VERSION` change: older saves have no `SELO` |
| Stale nodes | Each frame, selected nodes that no longer exist (the stress scene removes nodes when its counts drop) leave the selection; a missing primary is replaced as above. (This also fixes an assert when the selected stress node was removed.) |
| Play / Stop | As today: the selection is not scene content and is kept |
| Third-party | None |

## Engine changes

- `nv/scene.h`: `nv_scene_alive(scene, id)`, whether an id refers to a live node, for the stale-node
  check (`nv_scene_get` asserts instead).
- `nv/imgui.h`: `NvViewInput.tap_mods`, the modifier keys (`ImGuiMod_Ctrl`, `Shift`, `Alt`) held when
  a tap's press went down. A left press in the viewport waits two frames for the gizmo's hit test,
  so reading the keys when the tap arrives missed a quick Ctrl+click on a slow frame.

## App changes

- `app/selection.c` (new): selecting only, adding or removing, a range, clearing, keeping only the
  primary, the stale-node check, and whether a node is selected.
- `app/main.c`: picking with Ctrl / Shift and the Multi toggle; the outline for every node; the
  multi-node gizmo; the stale-node check at the start of the frame.
- `app/ui.c`: the Scene tab's clicks and ranges (rows recorded in the order drawn; a click ranges over
  last frame's rows, since this frame's are still being drawn); the Inspector's "N selected" line.
- `app/ui_phone.c`: the Multi toggle.
- `app/undo.c`, `app/save.c`: the Node scope over several nodes; `SELO`.
- `app/search.c`, `app/shortcuts.c`: picking a node in the palette selects only it; Esc clears all.
- Debug builds export `_app_debug_selection_count()`, `_app_debug_selection(i)` (the node index,
  0 = the primary), `_app_debug_select(index, mode)` (0 only, 1 add or remove),
  `_app_debug_set_multi(on)`, `_app_debug_tree_row(k, component)` (the Scene tab's k-th row: its
  node, then its rect), `_app_debug_find_node()` (the node named as the search buffer's text),
  `_app_debug_node(index, which)` (world and local position) and `_app_debug_set_grid(cubes)` (the
  stress scene's grid) for tests.

## Tests

Playwright, Release and Debug, desktop mouse and phone touch sizes:

- Ctrl+click and Shift+click in the viewport add and remove; a plain click selects one; a plain
  click on empty space clears; a modified click on empty space keeps the selection.
- Ctrl+click and Shift+click ranges in the Scene tab; the range follows the tree's order.
- The phone's Multi toggle: taps add and remove in both places; empty space keeps the selection.
- A move drag with three nodes moves all three by the same offset; a rotate drag keeps their center;
  a parent and its child selected together move once. One undo puts all three back; redo again.
- The Inspector shows the primary and "3 selected"; Keep one keeps the primary.
- The save round trip keeps the others (`_app_debug_save_round_trip`); a reload restores them.
- The stress scene: selecting grid cubes, then lowering the grid count, drops them without an assert.
- No WebGPU errors, nothing logged at warning level except the 257th-node warning when tested.

## Phases

1. **Selection:** the state, the clicks, ranges and the Multi toggle, the outline, the Inspector
   line, the stale-node check, the save tag.
2. **Gizmo and undo:** the multi-node drag and its single undo step.
3. **Docs:** `AGENTS.md`, `undo.md`, `save.md`, `gizmo.md`, `viewport.md`.

Every phase is checked in Release and Debug in headless Chromium, with the mouse at desktop size
and with touch at phone size.

As built, every phase was checked in Release and Debug at desktop and phone size:

- Desktop, in the viewport: a click selects one; Ctrl+click adds (the new node becomes the primary)
  and removes; Shift+click adds; Ctrl+click on empty space keeps the selection; a click on empty
  space clears it. In the Scene tab: a click selects one; Shift+click selects the four rows between
  in the tree's order; Ctrl+click removes one; Shift+click ranges again from the anchor; Esc clears.
- Phone: taps select one; with Multi on, taps add and remove and empty space keeps the selection;
  with it off again a tap selects one.
- A move drag with the planet, its moon and the look target selected moved all three by the same
  offset (the moon once, with the planet) and was one undo step; undo put all three back and
  selected them again, redo moved them again. A rotate drag kept their center and each node's
  distance from it.
- The save round trip with others selected gives the same bytes, and a reload restores the
  selection. In the stress scene, removed cubes leave the selection, and a removed primary with no
  others left clears it, without an assert.
- No WebGPU errors and nothing in the browser console. The new Korean strings are in the Hangul font.

# Undo and redo spec

Status: draft (2026-09-28). Changes to this spec are agreed first.

## Goal

Take back an edit in the showcase, and redo it: a gizmo drag, an Inspector field, a checkbox, a
clip picked for the character. One user action is one step, however many frames it lasted.

## Approaches

| Approach | How | Pros | Cons |
|---|---|---|---|
| **Scoped snapshots, found by comparing when idle** (chosen) | Each frame no widget or gizmo is in use, the app writes the undoable state of a few scopes with the save code (`app/save.c`) and compares it to the last committed bytes. A difference becomes a step holding the bytes before and after | No code per widget: every edit path, present and future, is caught. One drag is one step. Reuses the save format's writer and reader | Values the app drives every frame must be left out of the snapshots, or every frame would look like an edit |
| Commands, one per kind of edit | Every widget and the gizmo record what they change | Exact | Code at every edit site, and a missed site is a silent gap |
| Whole-state snapshots | The whole save before and after each step | Simplest | Undoing one edit would also put back everything that moved since (a walking character, the planet's spin) |
| A library | None found that fits: undo is about the app's own data | | |

## What is undoable

Only the showcase, and only its content. The stress scene is rebuilt from its settings and is not
undoable (Undo and Redo are off while it is shown).

| Scope | Holds (the save's fields) | Left out, because the app drives them |
|---|---|---|
| **Node** (the selected node) | `NAME`, `POS`, `ROT`, `SCL`, `COLR`, `ATCH`, `CFOV`, `LCOL`, `LINT` | the orbit camera's transform; the look target's transform; the planet's and the moon's rotations (they spin); the character root's position and rotation while root motion is on |
| **Character** | `CLIP`, `SPED`, `FADE`, `BLND`, `BLDW`, `RMOT`, `TURN`, `LOOK`, `SWRD` | `CTIM` (the clip's time); mid-jump, `CLIP` already names the clip the jump returns to |
| **Scene settings** | orbit speed (`ORBS`, a tag only undo uses: the save's `PLNT` holds the angle too), show bones | the orbit angle |

Not undoable: the camera view (yaw, pitch, distance, pan, follow), the selection, the editor
settings (gizmo mode, autosave), anything in the stress scene. Text fields keep their own undo
while they are being typed in (Dear ImGui's); the finished edit is then one step.

## Decisions

| Topic | Decision |
|---|---|
| When a step is taken | At the first frame that is idle after a change: no ImGui item active, no gizmo in use, no popup open. The scopes' bytes are compared with the committed ones; each scope that differs becomes a step (committed bytes before, current bytes after), then becomes the committed bytes |
| Selection | Selecting another node commits its bytes as they are, without a step. Only the selected node is compared, since only it can be edited. So does a change in which of its fields are driven (root motion turned on or off), so that change is not mistaken for an edit |
| Undo / redo | Undo applies the step's before-bytes through the save reader, which only touches the fields present; redo applies the after-bytes. The applied bytes become the committed ones, so an undo is not recorded as a new edit. A new step after an undo drops the steps that could have been redone |
| Undo shows what it changed | A node step selects its node, so the Inspector (and a following camera) show the change |
| History | In memory, 128 steps; the oldest is dropped when full. It is not saved: a reload starts with no history. The state after an undo is autosaved as usual |
| Step size | Before and after are at most 1 KB each (a node is about 120 bytes); a scope that does not fit is not recorded and asserts in Debug |
| Keys | Ctrl+Z undo; Ctrl+Shift+Z and Ctrl+Y redo (Cmd counts as Ctrl on macOS). Not while a text field is being edited |
| UI | A row above the panel's tabs: **Undo** and **Redo**, each labeled with what it would change ("Undo: character Position"), disabled when there is nothing to do or the stress scene is shown |
| Labels | Taken from the first field that differs: Position, Rotation, Scale, Name, Color, Joint, Field of view, Light color, Intensity, Clip, Speed, Fade, Blend, Root motion, Turn, Look at, Sword, Planet orbit, Show bones |
| Third-party | None |

## App changes

- `app/save.c` gains scope writers and readers shared with the autosave: a node's fields, the
  character's, the scene settings', each with a flag that leaves out driven values. The autosave
  keeps writing everything it writes today.
- `app/undo.c`: the step ring (fixed slots from the permanent arena), the idle comparison, undo,
  redo, labels, and the Undo/Redo row.
- `app/ui.c`: the row above the tabs, and the keys.

Nodes are identified by their path in the tree, as in the save. The editor cannot add or remove
nodes yet, so paths stay valid; when it can, steps will need stable node ids and steps for
creating and removing nodes.

## Phases

1. **Steps:** the scope writers, the idle comparison, the ring, undo and redo with the keys. Checked:
   a gizmo drag is one step, undo puts the node back and redo moves it again; a slider drag is one
   step; a new edit after an undo drops the redo steps.
2. **UI:** the Undo/Redo row with labels, selecting the changed node, off in the stress scene, the
   touch layout at phone width.
3. **Edge cases and docs:** a walking character (root motion on) makes no steps by itself; typing a
   name is one step after the field is left; a jump or crossfade in progress; 130 edits keep the
   last 128; undo followed by a reload keeps the undone state; `AGENTS.md` and README.

Every phase is checked in Release and Debug in headless Chromium, with the mouse at desktop size
and with touch at phone size.

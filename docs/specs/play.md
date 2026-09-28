# Edit and Play modes spec

Status: implemented (2026-09-28). Changes to this spec are agreed first.

## Goal

Separate editing the showcase from running it, as Unity, Unreal and Godot do.

- **Edit mode:** nothing moves the scene by itself. What is on screen is what gets saved and undone.
- **Play mode:** the scene runs (the planet spins, the character walks and jumps, the head
  follows the target). Pressing Stop puts the scene back exactly as it was when Play was pressed.

Today the showcase always runs while it is edited, so undo and the autosave have to leave out the
values the app changes every frame, and an edit to something moving (a walking character) cannot
be undone. With the modes apart, both only ever see the edit state.

## How other engines do it

| Engine | Play | Stop | Edits while playing |
|---|---|---|---|
| Unity | Runs the open scene | Restores the scene from before Play (it was serialized) | Allowed, visibly tinted, and lost on Stop |
| Unreal (Play In Editor) | Runs a copy of the world | Throws the copy away | Allowed on the copy, lost on Stop |
| Godot | Runs the scene in a separate process | Ends that process | In the running game only |

We follow Unity: the running scene is the edited one, and Stop restores it from a snapshot. The
snapshot is the save format we already have (`docs/specs/save.md`), so what Stop restores is
exactly what the autosave keeps. Copying the whole world (Unreal) would also have to copy
animators, materials and app state, which live outside `NvScene`.

No third-party library applies.

## What runs in each mode

| | Edit | Play |
|---|---|---|
| Planet and moon spin | Still, at their authored rotation | Spin on top of their authored rotation (the spin no longer replaces it) |
| Character animation | Plays in place as a preview: the pose moves, the node does not | Plays with root motion when it is on |
| Root motion, turning | Off (the in-place clip plays) | On when the Root motion box is ticked |
| Look target | Stays where it is placed, and can be moved like any node | Sweeps in front of the head, as today |
| Jump | Off | On |
| Crossfades and blends | On (they are part of the preview) | On |
| Orbit camera, picking, gizmo | On | On |
| Stress scene | Not affected: a benchmark always runs, and the Play button is hidden while it is shown | |

Play time starts at 0 on Play, so a run always starts the same way: clips from their start, the
planet from its authored rotation.

## Decisions

| Topic | Decision |
|---|---|
| Controls | A **Play** / **Stop** button at the start of the row above the panel's tabs, before Undo and Redo. No keyboard shortcut for now (Ctrl+P prints in browsers) |
| Showing the mode | While playing, the panel is tinted and the build label in the viewport reads "... · Playing"; a line under the row ("Playing: edits are lost on Stop.", wrapped on phones) says edits are lost on Stop. The line pushes the tabs down one line while playing |
| Tests | Debug builds expose `Module._app_debug_save_crc()`, a CRC-32 of the save the state would write now; equal values before Play and after Stop mean Stop restored the scene |
| Play | Takes a snapshot: the save bytes of the scene (`save_write`). Play time starts at 0 |
| Stop | Restores the scene part of the snapshot (nodes, character settings, scene settings) with the save reader, so a jump, a crossfade or a walk in progress ends with the scene as it was. The camera view and the selection are not restored: they are not scene content |
| Edits while playing | Allowed (the gizmo and the Inspector work), and lost on Stop, as in Unity |
| Undo while playing | Off: its history belongs to the edit state. After Stop the scene equals the last commit again, so no step appears |
| Autosave while playing | Writes the snapshot, never the running scene; a reload always starts in Edit mode with the edit state |
| Leaving the showcase while playing | The showcase stays in Play mode but pauses, as it does today while the stress scene is shown |
| Save format | The edit state has no running time, so the save stops writing the clip time (`CTIM`) and the planet's orbit angle (`PLNT` keeps its layout with the angle written as 0). Older saves still load; the values have no lasting effect, since Play starts both from 0. This moved into phase 1: before-Play and after-Stop saves can only be compared once the preview's clip time is out of them |
| Undo | Only the orbit camera stays a driven node (it follows the view). The planet, the moon, the look target and a root-motion character become undoable like any node |
| Third-party | None |

## Changes

- **App (`app/main.c`).** `App.playing`, `App.play_time`, and the snapshot buffer (`SAVE_MAX_SIZE`).
  `update_showcase` splits into what always runs (the animation preview, blends) and what only
  runs while playing (spin, jump, look target sweep, root motion and turning). `app_play` picks the
  root-motion clip only while playing. The look target gets a start position in front of the
  head, since it no longer sweeps in Edit mode.
- **Save (`app/save.c`).** `save_load` gains a parts mask, so Stop restores the scene without the
  view or the editor settings. `CTIM` and the orbit angle are no longer written. `driven_fields`
  keeps only the orbit camera.
- **Undo (`app/undo.c`).** Off while playing; buttons disabled.
- **UI (`app/ui.c`).** The Play/Stop button, the tint, the label, the line about lost edits; Jump
  disabled in Edit mode.
- **Specs.** `save.md` and `undo.md` updated for the fields above.

## Phases

1. **Modes:** the Play/Stop button, the snapshot and restore, splitting the showcase update, play
   time, spin on top of the authored rotation, the look target's start position, Jump only in Play.
   Checked: Play then Stop after the character has walked and jumped puts every node back (the
   save bytes before Play and after Stop are equal).
2. **Editor:** undo off while playing and the driven list cut to the camera; the autosave keeping
   the snapshot while playing; the tint and the label; the save format changes.
3. **Edge cases and docs:** Stop during a jump and during a crossfade; switching to the stress scene
   while playing and back; reloading while playing; editing while playing (lost on Stop); the phone
   layout; `AGENTS.md`, README and the other specs.

Every phase is checked in Release and Debug in headless Chromium, with the mouse at desktop size
and with touch at phone size.

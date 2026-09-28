# Autosave spec

Status: draft (2026-09-28). Changes to this spec are agreed first.

## Goal

Close the tab, come back, and find the app as it was left. The app saves its whole state (every
scene, the scene on screen, and the editor settings) as one save, by itself, into the browser's
IndexedDB through Emscripten's IDBFS; there is no save button to forget.

Out of scope for now: manual saves as files (export and import) and server storage (a CDN such as
Cloudflare R2 behind a Worker). Both could carry this spec's save bytes unchanged.

## How IDBFS works

- Emscripten's file system lives in JavaScript memory (MEMFS). The app already reads
  `/assets/...` from it with `fopen`, and it is gone when the page closes.
- IDBFS is MEMFS with one directory paired to an IndexedDB database. Files there are still read and
  written in memory, so `fopen` and `fwrite` work unchanged.
- Nothing reaches IndexedDB until the app asks. `FS.syncfs(true)` copies IndexedDB into memory, once
  at start. `FS.syncfs(false)` copies memory into IndexedDB (only files whose timestamps changed)
  after a save. Both are asynchronous and call back when done.
- The database belongs to the page's origin, `chromedays.github.io`, and is named after the mount
  path.

## Format candidates

| Candidate | Library (license) | Pros | Cons |
|---|---|---|---|
| **Our own tagged binary** (chosen) | None | No dependency. Unknown tags are skipped, so old and new builds read each other's saves. Floats are stored exactly. Fits the coding standard | Not human-readable, so it needs a viewer (below) |
| JSON | jsmn (MIT) and our own writer | Human-readable | Parsing text, and floats through text |
| MessagePack, CBOR | mpack (MIT), tinycbor (MIT) | Binary JSON, standard | A dependency, and still unreadable without a tool |
| Protocol Buffers, FlatBuffers | nanopb (zlib), flatcc (Apache-2.0) | Schemas | A schema compiler in the build; far more than a few-kilobyte save needs |
| A raw struct dump | None | Simplest | Breaks whenever a struct changes |

## The save format

The save is a tree of chunks. Every chunk is a tag, a size and a payload:

```
u32 tag     four ASCII characters, e.g. 'NODE' (NV_TAG('N','O','D','E'))
u32 size    payload bytes, not counting these 8
u8  payload[size]
```

- **Integers and floats are little-endian**, written and read byte by byte, so the format does not
  depend on the machine (wasm is little-endian anyway).
- **Containers and fields.** A container's payload is more chunks (`NODE` holds `NAME`, `POS `,
  ...). A field's payload is a value: `u32` or `f32` arrays, or a string (its bytes, no NUL; the
  size gives its length).
- **Order.** Fields can come in any order, and repeat only where the spec says so (`NODE` in
  `SHOW`). A reader finds fields by tag.
- **Unknown tags are skipped** by their size, so an older build reads a newer save and ignores what
  it does not know.
- **A missing field keeps the value the app starts with**, so a save from an older build loads
  what it has and leaves the rest as on a first visit (the "Missing" columns below). The reader
  itself reads a missing field as zero (ZII) and reports it as missing.
- **A field whose size is wrong** for its type (a `POS ` that is not 12 bytes) makes the save bad
  (see below): it can only come from corruption or a bug.
- **Version.** The header's version rises only when the meaning of an existing tag changes. Adding a
  tag does not raise it. A build refuses a save with a newer version than it knows.

### File layout

```
Header (16 bytes)
  u32 magic     'NVSV'
  u32 version   1
  u32 size      bytes after the header
  u32 checksum  CRC-32 of the bytes after the header
Chunks
  'EDIT'  editor settings
  'SHOW'  the showcase scene
  'STRS'  the stress scene
```

The checksum and the size catch a save that was cut short or damaged. Every tag lives in one place
in `app/save.c`.

## What is saved

The save holds everything the user can change and would expect to find again, and nothing that
is rebuilt, measured or momentary. The tables list every tag. "Missing" is what a load does when
the tag is absent (an older save): the value the app starts with today.

### How showcase nodes are saved

The editor can edit nodes but cannot add or remove them, and the showcase is always built the same
way. So a load **builds the showcase as today and then applies the saved values to its nodes**;
nodes are never created from the save.

- A node is identified by its **path**: the index of each node among its siblings, from the top
  level down (`[3]` is the fourth top-level node, `[5, 0]` the first child of the sixth). Names
  are not used because the Inspector can rename nodes.
- `SHOW` also saves a **layout number**: a hash of the default tree's names and shape, taken right
  after the showcase is built. If a later build changes the showcase so the number differs, the
  saved nodes are skipped (their paths might point at other nodes) and the rest still loads.
- A saved path that does not exist is skipped.

This keeps the app's references (`app->planet`, `app->moon`, `app->sword`, the character) valid,
since loading never replaces nodes. When the editor learns to add and remove nodes, new tags will
describe created nodes; older builds will skip them.

### `EDIT`: editor settings

| Tag | Type | Saved from | Missing |
|---|---|---|---|
| `SHWN` | u32 | `app->shown`: 0 showcase, 1 stress | 0 |
| `AUTO` | u32 | autosave on (the View tab checkbox) | 1 |
| `GZOP` | u32 | `app->gizmo_operation`: 0 move, 1 rotate, 2 scale | 0 |
| `GZLC` | u32 | `app->gizmo_local` | 0 |
| `GZSN` | u32 | `app->gizmo_snap` | 0 |

### `VIEW`: a scene's camera and selection (in `SHOW` and in `STRS`)

| Tag | Type | Saved from | Missing |
|---|---|---|---|
| `YAW ` | f32 | `SceneView.camera_yaw`, radians | the scene's start value |
| `PTCH` | f32 | `camera_pitch`, radians, clamped to -10°..80° on load | start value |
| `DIST` | f32 | `camera_distance`, meters, clamped to 1..100 on load | start value |
| `FOLW` | u32 | `follow_selection` | 1 |
| `ORBT` | f32[3] | `orbit_point` (where the camera looks while not following) | (0, 0, 0) |
| `PAN ` | f32[3] | `pan` (offset from the followed selection) | (0, 0, 0) |
| `SELN` | u32[] | the selected node's path; empty = nothing selected | the start selection |

`pan` is restored as belonging to the restored selection (`panned_for`). The view's `focus` and
`camera` are fixed by the build and not saved. In the stress scene, `SELN` is not saved (its nodes
are rebuilt, so a path would not last).

### `SHOW`: the showcase

| Tag | Type | Saved from | Missing |
|---|---|---|---|
| `LAYT` | u32 | the layout number (above) | nodes are skipped |
| `VIEW` | container | the showcase's view | start view |
| `PLNT` | f32[2] | `orbit_speed` (rad/s) and `orbit_angle` (rad): the planet's spin, which rewrites the planet's rotation every frame | 0.7, 0 |
| `BONE` | u32 | `app->show_bones` | 0 |
| `CHAR` | container | the character (below) | as built |
| `NODE` | container, repeated | one per node, in tree order (below) | as built |

**`CHAR`: the character's playback and controls**

| Tag | Type | Saved from | Missing |
|---|---|---|---|
| `CLIP` | string | the name of the clip playing on layer 0, without its root-motion copy (`app_regular_clip`). During a jump, the clip the jump returns to | `Idle_Loop` |
| `CTIM` | f32 | layer 0's time into that clip, seconds | 0 |
| `SPED` | f32 | layer 0's speed | 1 |
| `FADE` | f32 | `fade_seconds` | 0.3 |
| `BLND` | string | the blend clip's name (`clips[blend_clip]`) | the first clip |
| `BLDW` | f32 | `blend_weight` | 0 |
| `RMOT` | u32 | `root_motion` | 0 |
| `TURN` | f32 | `turn_rate`, rad/s | 0.6 |
| `LOOK` | u32 | `look_at` | as built |
| `SWRD` | u32 | `show_sword` | 1 |

On load the clip starts with `app_play` (so root motion picks its copy) at `CTIM`, with no fade.
A clip name the build does not have falls back to `Idle_Loop`.

**`NODE`: one node's editable values**

| Tag | Type | Saved from | Missing |
|---|---|---|---|
| `PATH` | u32[] | the node's path | the node is skipped |
| `NAME` | string | `NvNode.name` (up to 31 bytes) | as built |
| `POS ` | f32[3] | `position` | as built |
| `ROT ` | f32[4] | `rotation` (x, y, z, w), normalized on load | as built |
| `SCL ` | f32[3] | `scale` | as built |
| `COLR` | f32[4] | the base color of the node's material, for nodes with a mesh | as built |
| `ATCH` | string | the joint the node follows, by name, for attached nodes | as built |
| `CFOV` | f32 | `camera.fov_y`, radians, for camera nodes | as built |
| `LCOL` | f32[3] | `light.color`, for light nodes | as built |
| `LINT` | f32 | `light.intensity`, for light nodes | as built |

Every node is saved, including the character's mesh nodes and the look target. Values the app
rewrites every frame are saved but have no lasting effect: the look target's transform (it sweeps
around the head), the planet's rotation (from `PLNT`), and the orbit camera's transform (from
`VIEW`). A joint name the skeleton does not have leaves the attachment as built.

### `STRS`: the stress scene

| Tag | Type | Saved from | Missing |
|---|---|---|---|
| `VIEW` | container | the stress scene's view (no `SELN`) | start view |
| `GRID` | u32[2] | `want.grid_on`, `want.grid_count` (clamped to the slider's range on load, as are all counts) | as today |
| `COLS` | u32[2] | `colors_on`, `color_count` | as today |
| `CHAN` | u32[2] | `chain_on`, `chain_count` | as today |
| `CRWD` | u32[2] | `crowd_on`, `crowd_count` | as today |
| `CHRN` | u32[2] | `churn_on`, `churn_count` | as today |
| `BONE` | u32 | `want.show_bones` | 0 |

While a benchmark runs, the settings from before it started (`before_benchmark`) are saved, not
the benchmark's own steps. The stress scene is built from these settings the first time it is
shown, as today; a scene that was never opened is still saved, so opening it later starts where it
was left.

### Not saved

- Edits to single nodes in the stress scene: its nodes are generated from the settings.
- Benchmark results and a running benchmark; frame times and stats.
- A jump in progress (the returning clip is saved), crossfades in progress, and blend layers'
  own times.
- The node selected in the stress scene.
- Panel state: the open tab, scroll positions, which tree nodes are open.
- Anything rebuilt at start: meshes, materials other than their colors, skeletons, clips, GPU and
  ImGui resources.

## Decisions

| Topic | Decision |
|---|---|
| Storage | IndexedDB through IDBFS, mounted at `/nv-save`. One file, `state.nvs`, holds the whole app state |
| When it saves | Every 10 seconds when the bytes would differ from the last save, and when the page is hidden (`visibilitychange`). Hidden is the last reliable moment on phones; `beforeunload` is not. A write goes to a temporary name first and is then renamed, so a half-written save never replaces a good one |
| On start | The app builds the showcase as today, then loads the save if there is one, before the first frame: both scenes, the scene that was on screen, and the editor settings. Otherwise it starts as today, on the showcase |
| A bad save | A wrong magic, size or checksum, a field of the wrong size, or a newer version: the file is renamed to `state.nvs.bad` and the app starts as on a first visit. A message in the View tab says so. Loading is all or nothing: the whole file is checked before anything in the app changes |
| `#stress` | Removed. The address no longer names the scene; the save remembers it |
| UI | An **Autosave** section in the View tab: an on/off checkbox (on by default, and itself saved), when the state was last saved, **Save now**, **Reset** (deletes the save and reloads the page, which starts as on a first visit; animators cannot be removed, so rebuilding in place would leak them), and **Show save** (below) |
| Save viewer | **Show save** opens a tree of the save's chunks: tags, sizes, and values (numbers, strings, hex for tags it does not know). It reads the file as the loader does, so it also shows where a bad save goes wrong |
| Durability | The app asks for `navigator.storage.persist()`, so the browser does not clear the data under storage pressure |
| Third-party | None |

## Engine changes

- **Chunks (`nv/chunk.h`, `engine/src/chunk.c`).**
  - `NV_TAG(a, b, c, d)` builds a tag.
  - `NvChunkWriter` appends to an arena buffer: `nv_chunk_begin(tag)` and `nv_chunk_end` (which
    patches the size), and fields: `nv_chunk_u32s`, `nv_chunk_f32s`, `nv_chunk_string`.
  - `NvChunkReader` walks a byte range with bounds checks: the next child, finding a child by tag,
    and reading a field as `u32` or `f32` arrays or a string. A field of the wrong size marks the
    reader as failed; after a failure, every read returns zero, so the loader checks once at the
    end.
  - `nv_crc32`, and the header helpers.
- **Storage (`nv/storage.h`, `engine/src/storage.c`).**
  - `nv_storage_init` mounts IDBFS at `/nv-save`, asks for persistent storage, and waits for the
    first sync from IndexedDB (through Asyncify, like `wgpuInstanceWaitAny`).
  - `nv_storage_write(name, bytes, size)` writes through a temporary file and a rename.
  - `nv_storage_flush` starts a sync to IndexedDB without waiting for it. A flush asked for while
    one is running starts when that one ends.
  - `NvStorage` reports whether storage is available (private windows may refuse IndexedDB) and
    the last sync error. Without storage the app runs as today and says autosave is off.
- **Window (`nv/window.h`).** `NvWindow` reports when the page becomes hidden, so the app can save
  then.
- **Link flags.** `-lidbfs.js`.

## App changes

- `app/save.c` writes and reads the whole app state with the chunk helpers. Loading reads the
  whole file into a staging struct first; only if that succeeds does it apply the values to the
  showcase's nodes, the character, the views, the stress settings and the editor settings, and
  show the saved scene.
- The autosave timer, the page-hidden save, loading on start, and the View tab's Autosave section
  with the save viewer.
- The showcase's layout number, computed after it is built.
- `#stress` goes: `js_hash_is_stress`, `js_set_hash`, and its mentions in `AGENTS.md`, README and
  `docs/specs/stress.md`.

## Limits

- The origin is shared. The Release and Debug builds use the same save, and so does every other
  project on `chromedays.github.io`, which is why the mount has a distinctive name. Release and
  Debug write the same format, so either reads the other's save.
- Browsers may still clear it: iOS Safari after 7 days without a visit, and private windows when
  they close. Autosave is a convenience, not an archive.
- A save is at most 4 MB (checked when writing); the showcase is a few kilobytes.

## Phases

1. **Format:** `nv/chunk.h`, and saving and loading the whole state to bytes in memory. A round
   trip is checked: save, load, save again, and compare the bytes. A native test also covers the
   reader's bounds checks: truncated chunks, sizes past the end, wrong field sizes.
2. **Autosave:** IDBFS, `nv_storage_*`, the timer and the page-hidden save, loading on start, the
   View tab's Autosave section, and removing `#stress`.
3. **Failure cases, viewer and docs:** bad, damaged and newer-version saves, storage that is
   unavailable, Reset, the save viewer, `AGENTS.md`, README and `docs/specs/stress.md`.

Every phase is checked in Release and Debug in headless Chromium. Phase 2 edits both scenes,
reloads the page and checks that the edits and the scene on screen came back. Phase 3 writes a
truncated save, a save with a flipped byte and a save with a newer version, and checks that the
app starts as on a first visit and keeps the `.bad` file.

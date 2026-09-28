# Autosave spec

Status: draft (2026-09-28). Changes to this spec are agreed first.

## Goal

Close the tab, come back, and find each scene as it was left. The app saves by itself into the
browser's IndexedDB through Emscripten's IDBFS; there is no save button to forget.

Out of scope for now: manual saves as files (export and import) and server storage (a CDN such as
Cloudflare R2 behind a Worker). Both would reuse this spec's format, and the save text is kept
self-contained so they can be added later.

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

## Third-party candidates

| Candidate | What it is | Language, license | Fit | Trade-offs |
|---|---|---|---|---|
| **jsmn** (recommended) | A JSON tokenizer: it fills a caller-owned array of tokens (type, start, end, child count) | C, one header, MIT | No allocation: the token array comes from our arena. About 400 lines | Only tokenizes; turning tokens into numbers and strings is ours (small) |
| cJSON | A JSON parser and writer that builds a tree | C, MIT | Complete, and it writes JSON too | Allocates every node with malloc, which goes against the arena rule |
| json.h (sheredom) | A parser that builds a tree in one allocation | C, one header, Unlicense | One allocation, which we can hand from an arena | Larger, and we would still walk its tree |
| Our own line-based format | For example `node "moon" parent=planet position=2.4,0,0` | C | No dependency | We would design, parse and escape it ourselves, and other tools could not read it |
| A binary dump of the structs | `fwrite` of the scene | C | Fastest, no parsing | Breaks whenever a struct changes, and ids and pointers would need fixing up |

Recommendation: JSON, written by our own small writer and read through jsmn. It survives struct
changes, it can be read when debugging, and a later export is just this text as a file.

## Decisions

| Topic | Decision |
|---|---|
| Storage | IndexedDB through IDBFS, mounted at `/nv-save`. One file per scene kind: `showcase.json`, `stress.json` |
| Format | JSON, `"format": "nv-scene"` with a `"version"` number. Unknown fields are skipped; missing fields read as zero (ZII), with the node defaults for scale and rotation |
| When it saves | Every 10 seconds when the text would differ from the last save, and when the page is hidden (`visibilitychange`). Hidden is the last reliable moment on phones; `beforeunload` is not. A write goes to a temporary name first and is then renamed, so a half-written save never replaces a good one |
| On start | Each scene kind loads its save if there is one; otherwise it is built as today |
| A bad save | A file that does not parse, or has a newer `version`, is renamed to `<name>.bad` and the scene is built as today. A message in the View tab says so |
| UI | An **Autosave** section in the View tab: an on/off checkbox (on by default), when the shown scene was last saved, **Save now**, and **Reset scene** (rebuilds the built-in scene and overwrites its save) |
| Stress scene | Only its settings (workloads and counts) and its camera view; the scene is rebuilt from them. A running benchmark is not saved |
| Durability | The app asks for `navigator.storage.persist()`, so the browser does not clear the data under storage pressure |
| Third-party | jsmn, pinned to a tagged release |

## What a save holds

```json
{
  "format": "nv-scene", "version": 1, "scene": "showcase",
  "saved": "2026-09-28T04:10:00Z", "build": "Release 6422430",
  "view": {"yaw": 0.35, "pitch": 0.12, "distance": 5.0, "follow": true, "selected": "moon"},
  "app": {"orbit_speed": 0.7, "clip": "Idle_Loop", "root_motion": false, "look_at": true, "sword": true},
  "nodes": [
    {"name": "planet", "position": [-4.5, 1.6, -4.0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1],
     "mesh": "cube", "color": [0.95, 0.55, 0.25, 1]},
    {"name": "moon", "parent": 1, "position": [2.4, 0, 0], "mesh": "cube", "color": [0.55, 0.7, 0.95, 1]},
    {"name": "character", "model": "/assets/character.glb", "animator": true},
    {"name": "sword", "parent": 2, "mesh": "sword", "attach": "hand_r"},
    {"name": "sun", "light": {"type": "directional", "color": [1, 0.96, 0.9], "intensity": 1.1}}
  ]
}
```

- **Nodes** are saved in tree order. `parent` is an index into `nodes`, so a parent always comes
  first.
- **Meshes** are saved as names, never as geometry: a built-in mesh (`cube`, `sword`, `ground`,
  `target`), or the node's glTF model (`model`: an asset path). A model's own child nodes are
  created again when it is loaded, so they are not saved one by one.
- **Colors** are saved per node. Loading creates one material per distinct color, up to
  `NV_MAX_MATERIALS`.
- **Joint attachments** are saved by joint name (joint indices depend on the asset).
- **Cameras and lights** are saved with their fields. The orbit camera is saved through `view`, so
  its transform is not saved.
- **App references.** The showcase's own nodes (`planet`, `moon`, `sword`, `character`,
  `look target`) are found again by name after a load. A node that is missing turns off what it
  drives (the moon's orbit, the look target).

## Engine changes

- **JSON (`nv/json.h`, `engine/src/json.c`).**
  - `NvJsonWriter` appends to an arena buffer: objects, arrays, keys, strings (escaped), numbers
    (`%.9g`, which round-trips a float), booleans.
  - The reader wraps jsmn: it parses into an arena token array (sized by a counting pass) and
    offers `nv_json_find` (a key in an object), `nv_json_f32`, `nv_json_string` (unescaped into a
    buffer), `nv_json_array_count` and `nv_json_array_at`.
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

- `app/save.c` writes and reads a scene kind with the JSON helpers. Loading clears that scene,
  rebuilds it from the text and finds the app's nodes again by name.
- The autosave timer, the page-hidden save, loading on start, and the View tab's Autosave section.
- Built-in meshes get names in the app (`cube`, `sword`, `ground`, `target`), so they can be
  saved as names.

## Limits

- The origin is shared. The Release and Debug builds use the same saves, and so does every other
  project on `chromedays.github.io`, which is why the mount has a distinctive name. A Debug build
  reading a Release save (or the reverse) is fine because both write the same format.
- Browsers may still clear it: iOS Safari after 7 days without a visit, and private windows when
  they close. Autosave is a convenience, not an archive.
- A save is at most 4 MB (checked when writing); the showcase is a few kilobytes.

## Phases

1. **Format:** jsmn, the JSON writer and reader, and save/load of both scene kinds to a string.
   A round trip is checked in memory: save, load, save again, and compare the two texts.
2. **Autosave:** IDBFS, `nv_storage_*`, the timer and the page-hidden save, loading on start, and
   the View tab's Autosave section.
3. **Failure cases and docs:** bad and newer-version saves, storage that is unavailable, Reset
   scene, `AGENTS.md`, README, and the Dependencies section of `docs/CODING_STANDARD.md`.

Every phase is checked in Release and Debug in headless Chromium. Phase 2 edits a scene, reloads
the page and checks that the edit came back. Phase 3 writes a broken save and a save with a newer
version, and checks that the app starts from the built-in scene and keeps the `.bad` file.

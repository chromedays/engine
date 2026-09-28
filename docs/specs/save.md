# Save and load spec

Status: draft (2026-09-28); server storage (a CDN with Cloudflare R2) is under discussion. Changes to
this spec are agreed first.

## Goal

Keep the edits made in the editor, in two ways:

- **Autosave:** close the tab and find the scene as it was left. It lives in the browser
  (IndexedDB) and is written without being asked.
- **Manual saves are files:** Export downloads the scene as a file and Import opens one. Files are
  what the user keeps, names, moves to another device or sends to someone.

The app is served from GitHub Pages, a static host, so nothing can be stored on a server.

## Third-party candidates

| Candidate | What it is | Language, license | Fit | Trade-offs |
|---|---|---|---|---|
| **jsmn** (recommended) | A JSON tokenizer: it fills a caller-owned array of tokens (type, start, end, child count) | C, one header, MIT | No allocation: the token array comes from our arena. About 400 lines | Only tokenizes; turning tokens into numbers and strings is ours (small) |
| cJSON | A JSON parser and writer that builds a tree | C, MIT | Complete, and it writes JSON too | Allocates every node with malloc, which goes against the arena rule |
| json.h (sheredom) | A parser that builds a tree in one allocation | C, one header, Unlicense | One allocation, which we can hand from an arena | Larger, and we would still walk its tree |
| Our own line-based format | For example `node "moon" parent=planet position=2.4,0,0` | C | No dependency | We would design, parse and escape it ourselves, and other tools could not read it |
| glTF through `cgltf_write` | The standard scene format, already a dependency | C, MIT | Opens in Blender | App state must go in `extras`, and writing geometry back is heavy. It fits a later "Export glTF" better than save and load |

Recommendation: JSON, written by our own small writer and read through jsmn.

## Decisions

| Topic | Decision |
|---|---|
| Storage | The autosave in IndexedDB, through Emscripten's IDBFS mounted at `/nv-save`. Manual saves are files (export and import); there are no save slots in the browser |
| Format | JSON, `"format": "nv-scene"` with a `"version"` number. Unknown fields are skipped; missing fields read as zero (ZII), with the node defaults for scale and rotation |
| Autosave files | One per scene kind: `/nv-save/showcase.json`, `/nv-save/stress.json` |
| Autosave | Every 10 seconds when the saved text would differ from the last one, and when the page is hidden (`visibilitychange`). Hidden is the last reliable moment on phones; `beforeunload` is not |
| On start | The autosave loads if there is one. **Reset scene** rebuilds the built-in scene |
| UI | A new **File** tab: Export, Import, Reset scene, and when the autosave was last written |
| Import | A file picker, or a `.json` dropped on the canvas. It replaces the scene kind the file names (switching to it), and the next autosave keeps it |
| Export | Downloads `nv-<scene>-<date>-<time>.json` |
| Stress scene | Only its settings (workloads and counts) and its camera; the scene is rebuilt from them |
| Third-party | jsmn, pinned to a tagged release |
| Not chosen: commits to this repository | Saving through the GitHub API as a git commit was considered. A static page would need a personal access token pasted into the browser, every commit would run CI, it suits neither autosave nor binary assets (the API bypasses Git LFS), and the public repository would make every save public |

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
  - `nv_storage_init` mounts IDBFS at `/nv-save` and waits for the first sync from IndexedDB
    (through Asyncify, like `wgpuInstanceWaitAny`).
  - `nv_storage_flush` starts a sync to IndexedDB without waiting for it; a flush asked for while
    one is running starts when it ends.
  - `nv_storage_export(name, bytes, size)` downloads a file.
  - Imported files are written to `/nv-import/`, and `nv_storage_take_import` hands the next
    path to the app.
  - `NvWindow` reports when the page is hidden, so the app can autosave then.
- **Link flags.** `-lidbfs.js`, and `FS` exported to the page glue.

## App changes

- `app/save.c` writes and reads a scene kind with the JSON helpers. Loading clears that scene,
  rebuilds it from the file and finds the app's nodes again by name.
- The File tab, the autosave timer, and loading the autosave on start.
- Built-in meshes get names in the app (`cube`, `sword`, `ground`, `target`), so they can be
  saved as names.

## Limits

- Browser storage belongs to the site's origin, `chromedays.github.io`. The Release and Debug
  builds share it, and so saves made in one open in the other. Every other project on that origin
  shares it too, which is why the mount has a distinctive name.
- Browsers may clear it: iOS Safari after 7 days without a visit, private windows when they close.
  The app asks for `navigator.storage.persist()`, and Export is the durable copy.
- Saves are at most 4 MB (checked when writing).

## Phases

1. **Format:** jsmn, the JSON writer and reader, and save/load of both scene kinds to a string.
   A round trip is checked in memory: save, load, save again, and compare the two texts.
2. **Autosave:** IDBFS, the autosave timer and page-hidden save, loading it on start, and the File
   tab with Reset scene.
3. **Files and docs:** export, import (picker and drop), `AGENTS.md`, README, and the Dependencies
   section of `docs/CODING_STANDARD.md`.

Every phase is checked in Release and Debug in headless Chromium. Phase 2 reloads the page to see
the autosave come back, and phase 3 downloads a file and imports it again.

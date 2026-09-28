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
- **Missing fields read as zero (ZII)**, except the node defaults for scale and rotation.
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
  'EDIT'  editor
          'SHWN' u32 scene on screen (0 showcase, 1 stress)
          'AUTO' u32 autosave on
          'GZOP' u32 gizmo operation   'GZLC' u32 local   'GZSN' u32 snap
  'SHOW'  showcase
          'VIEW' camera: 'YAW ' f32  'PTCH' f32  'DIST' f32  'FOLW' u32  'ORBT' f32[3]  'PAN ' f32[3]
                 'SELN' string (the selected node's name)
          'APP ' 'ORBS' f32 orbit speed  'CLIP' string  'RMOT' u32  'LOOK' u32  'SWRD' u32
          'NODE' (repeated, in tree order)
                 'NAME' string        'PRNT' u32 (1-based index of an earlier NODE; 0 = top level)
                 'POS ' f32[3]  'ROT ' f32[4]  'SCL ' f32[3]
                 'MESH' string (built-in mesh)   'MODL' string (glTF asset path)   'COLR' f32[4]
                 'ATCH' string (joint name)
                 'LGHT' 'TYPE' u32  'COLR' f32[3]  'INTS' f32  'RANG' f32  'CONE' f32[2]
                 'CAMR' 'PROJ' u32  'FOV ' f32  'ORTH' f32  'NEAR' f32  'FAR ' f32
  'STRS'  stress
          'VIEW' as above
          'WORK' 'GRID' u32[2] (on, count)  'COLS' u32[2]  'CHAN' u32[2]  'CRWD' u32[2]
                 'CHRN' u32[2]  'BONE' u32
```

The checksum and the size catch a save that was cut short or damaged. The tags above are the
starting set; each lives in one place in `app/save.c`.

### What the save holds

- **Nodes** are saved in tree order, so a parent always comes before its children.
- **Meshes** are saved as names, never as geometry: a built-in mesh (`cube`, `sword`, `ground`,
  `target`), or the node's glTF model (`MODL`, an asset path). A model's own child nodes are
  created again when it is loaded, so they are not saved one by one.
- **Colors** are saved per node. Loading creates one material per distinct color, up to
  `NV_MAX_MATERIALS`.
- **Joint attachments** are saved by joint name (joint indices depend on the asset).
- **Cameras and lights** are saved with their fields. The orbit camera is saved through `VIEW`, so
  its transform is not saved.
- **App references.** The showcase's own nodes (`planet`, `moon`, `sword`, `character`,
  `look target`) are found again by name after a load. A node that is missing turns off what it
  drives (the moon's orbit, the look target).
- **The stress scene** is saved as its settings and camera; it is rebuilt from them. A scene that
  was never opened is still saved from its settings, so opening it later starts where it was left.
  A running benchmark is not saved.

## Decisions

| Topic | Decision |
|---|---|
| Storage | IndexedDB through IDBFS, mounted at `/nv-save`. One file, `state.nvs`, holds the whole app state |
| When it saves | Every 10 seconds when the bytes would differ from the last save, and when the page is hidden (`visibilitychange`). Hidden is the last reliable moment on phones; `beforeunload` is not. A write goes to a temporary name first and is then renamed, so a half-written save never replaces a good one |
| On start | The save loads if there is one: both scenes, the scene that was on screen, and the editor settings. Otherwise the app starts as today, on the showcase |
| A bad save | A wrong magic, size or checksum, a field of the wrong size, or a newer version: the file is renamed to `state.nvs.bad` and the app starts as on a first visit. A message in the View tab says so. Loading is all or nothing: the whole file is checked before anything in the app changes |
| `#stress` | Removed. The address no longer names the scene; the save remembers it |
| UI | An **Autosave** section in the View tab: an on/off checkbox (on by default, and itself saved), when the state was last saved, **Save now**, **Reset** (rebuilds everything as on a first visit and overwrites the save), and **Show save** (below) |
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

- `app/save.c` writes and reads the whole app state with the chunk helpers. Loading checks the
  whole file first (a dry run through the reader), then rebuilds the showcase from its nodes,
  applies the stress settings, restores the editor settings, and shows the saved scene.
- The autosave timer, the page-hidden save, loading on start, and the View tab's Autosave section
  with the save viewer.
- Built-in meshes get names in the app (`cube`, `sword`, `ground`, `target`), so they can be saved
  as names.
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

# Coding standard

nv follows the Handmade Hero philosophy: write the code that does the work, keep data plain and
visible, own your memory, and add abstraction only when repetition asks for it.

## Principles

- **Write the usage code first.** Pull code out into a function the second time it is needed,
  not before. No speculative layers, interfaces or options "for later".
- **Plain data.** Structs with public fields. No getters/setters, opaque types, vtables or
  callbacks where a direct call works.
- **Own the memory.** Reserve memory once, then hand it out from arenas. Group allocations by
  lifetime and drop each group at once.
- **Know what the machine does.** Prefer flat arrays and straight loops over clever structures.
  Fixed capacities are fine; exceeding one is a bug that asserts.

## Language and files

- The C17 rule covers only the code we write. External libraries may be written in C or C++
  (Dear ImGui and ozz-animation are C++); they are compiled as they come, with their warnings off.
- Our code is C17 and compiles warning-free with `-Wall -Wextra`. The one exception is the wrapper
  around a C++ library that has no C API: that file is C++, exposes only `extern "C"` functions
  through a C header, and stays as thin as possible (currently the ozz-animation wrapper).
- Public headers live in `engine/include/nv/`, one per module; sources in `engine/src/`.
- `nv/base.h` is included (directly or through another nv header) by every file.

## Naming

| Kind | Style | Example |
|---|---|---|
| Public function | `nv_module_verb` | `nv_gpu_begin_frame` |
| Public type | `NvPascalCase` | `NvGpu`, `NvArena` |
| Public macro / constant | `NV_UPPER_CASE` | `NV_ASSERT`, `NV_PUSH_ARRAY` |
| File-local function / type | `snake_case` / `PascalCase`, no prefix | `configure_surface`, `AdapterRequest` |
| Variables and fields | `snake_case`, full words | `surface_format`, `new_width` |

## Types and keywords

- Use the aliases from `nv/base.h`: `u8`–`u64`, `s8`–`s64`, `f32`, `f64`, `b32` (true/false),
  `umm` (memory sizes and indices into memory). Use `int` or `bool` only where an external API
  demands it (Emscripten callback signatures, ImGui's `bool*` parameters).
- `static` is spelled by intent:
  - `internal` for file-local functions,
  - `global` for file-scope variables,
  - `local_persist` for static variables inside a function.

## Memory

- The engine does not call `malloc`/`free`. Memory comes from an `NvArena` over a block reserved
  once at startup, via `NV_PUSH_STRUCT` / `NV_PUSH_ARRAY`.
- Arenas are reset, never freed piece by piece. Per-frame scratch data lives in its own arena that
  is reset every frame.
- Arrays have fixed capacities chosen up front. Growth is not the default.

## Initialization

- **Zero is initialization.** Design every struct so that `{0}` is a valid state, and prefer that
  over init functions. Arena pushes return zeroed memory.
- Handles and indices reserve 0 for "none", so a zeroed handle is a null handle.

## Errors

- Programmer mistakes (bad handle, capacity exceeded, broken invariant) hit `NV_ASSERT` and stop.
  They are not turned into error codes. `NV_INVALID_CODE_PATH` marks branches that must not run.
- Failures that really happen at runtime (no WebGPU in the browser, a missing file) return `b32`
  and are handled by the caller that can do something about them.

## Dependencies

- What the platform requires: Emscripten and its WebGPU port (`emdawnwebgpu`).
- Dear ImGui, through cimgui (its C API), for debug and tool UI: writing an immediate-mode UI
  library is not the point of this project. Its platform and renderer backends are ours
  (`engine/src/imgui.c`), in C.
- cgltf, a single-header C glTF parser, for loading models and animations: glTF is the asset
  format, and parsing it (JSON included) is not the point of this project.
- ozz-animation for skeletal animation at runtime (sampling, blending, skinning matrices): a
  mature, data-oriented library that covers what we would otherwise rebuild. Its API is C++, so
  the engine talks to it only through our own C wrapper.
- Anything else (even a single-header library) needs a written reason and agreement first. Math,
  containers and strings are written here.

## Comments

- Comments explain why; the code says what. Use `//` comments.
- Tags for things a reader must not miss: `// TODO:` (known missing work), `// NOTE:` (a
  non-obvious fact), `// IMPORTANT:` (breaks things if ignored).

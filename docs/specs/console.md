# Console spec

Status: draft (2026-09-29). Changes to this spec are agreed first.

## Goal

See what the engine and the page report, inside the app: WebGPU validation errors, a glTF that
failed to load, a save that could not be written, a warning from ozz-animation. Today these go to
`fprintf(stderr, ...)` or `console.warn`, so they are only seen with the browser's developer tools
open, which phones do not have. A **Console** tab in the editor panel lists them, with levels,
filters and copy, and a badge on the viewport's build label says when a warning or error arrived
while another tab is shown.

Out of scope: typing commands (a REPL), logging to a file or a server, and messages after a crash
(the page's crash screen already shows the stack).

## Approaches

| Approach | What it is | Fit | Trade-offs |
|---|---|---|---|
| **Our own log ring and Console tab** (recommended) | `nv/log.h`: one fixed ring of messages in static memory, written by `nv_log`; `app/console.c` draws it with ImGui, following the pattern of Dear ImGui's own `ExampleAppLog` demo | Plain C17, fixed capacities, no allocation; about 300 lines. The ImGui demo shows the UI parts (`ImGuiListClipper`, `ImGuiTextFilter`, stick-to-bottom scrolling) | Ours to write and test |
| Dear ImGui's `ExampleAppLog` / `ExampleAppConsole` (`imgui_demo.cpp`, MIT) | Demo code, not a library | A reference for the UI, which we follow | C++, and its `ImGuiTextBuffer` grows on the heap without limit. Not usable as it is |
| rxi/log.c (C99, MIT) | A tiny logging library: levels, `stderr` output, callbacks | C, small | Keeps no history, which is the whole point here; would add a callback layer over a ring we still write. Not worth a dependency |
| spdlog with its `ringbuffer_sink` (C++, MIT) | A full logging library | Has a ring of messages | C++ in the engine's core, heap allocation, a large dependency for a small job |
| ImGui console widgets on GitHub (e.g. `imgui-console`, C++) | A console window with commands and history | Looks close | C++, heap, commands we do not want, and a window instead of a panel tab |

Recommendation: write it ourselves. No third-party library.

## Decisions

| Topic | Decision |
|---|---|
| Levels | Info, Warning, Error |
| Source | A short tag kept with each message: `nv` (engine), `wgpu` (WebGPU callbacks), `app`, `stdout`, `stderr`, `js` (page errors). Shown in its own column and filterable by the text filter |
| Storage | One engine-wide ring in `engine/src/log.c`, in static memory: up to 1024 messages whose text shares a 128 KB text ring. When either is full, the oldest messages are dropped. A message longer than 4 KB is cut and ends in `...` |
| Why one global | Messages come from places with no app at hand (WebGPU callbacks, the glTF loader, `anim.cpp`); there is one page and one log |
| Repeats | A message equal to the newest one (same level, source and text) raises that message's repeat count and time instead of adding a new one, so an error raised every frame takes one row ("x240") |
| Browser console | `nv_log` also writes to `console.log` / `console.warn` / `console.error` by level, as `[source] text`, so developer tools and Playwright's `page.on("console")` keep seeing everything. It does not go through `stderr`, so it is not captured twice |
| Existing output | Every `fprintf(stderr, ...)` in `engine/` and `app/` becomes `nv_log`. `console.warn` / `console.error` in our `EM_JS` code become `Module.nvLog` (below) |
| Output we do not write | The page's `Module.print` and `Module.printErr` (Emscripten's `stdout` and `stderr`: ozz-animation's logs, Emscripten's warnings) keep writing to the browser console and also add an Info (`stdout`) or Error (`stderr`) message. So do the page's `error` and `unhandledrejection` handlers (`js`), before the crash screen |
| JS messages | The page defines `Module.nvLog(level, source, text)`, which writes to the console and queues the message with its time. `nv_log_pump()`, called at the start of each frame, moves the queue into the ring. Nothing calls into WebAssembly from those hooks: `printErr` runs inside a WebAssembly call, and a re-entrant call could land in an Asyncify wait |
| Time | Seconds since the page started (`performance.now`, the clock `nv_time_seconds` uses), shown as `12.345` |
| Saved | No. The log is not in the save, and a reload starts empty. The Console tab's settings (level filters, auto-scroll) are not saved either: they are view state, like the text filter |
| Undo | Not undoable, and touching nothing undo compares: clearing the log is not a step |
| Tab | A **Console** tab after View in the panel's tab bar. While it is not shown, its label counts the warnings and errors that arrived since it was last shown, repeats included: `Console (3)`, red when one is an error, yellow for warnings only. Info does not count. The label's ImGui id stays fixed (`###console`), so the tab keeps its place |
| Badge | The same count on the build label in the viewport's top-left corner (`ui.c`'s `build_label`), after its text: a dot and the number, `Release build 1a2b3c · ● 3`, red when one is an error, yellow for warnings only. No count, no badge. It is drawn on the foreground draw list like the label, so it is not an ImGui item. The build label is already an exception to "editor UI stays in the panel"; the badge adds no new one |
| Badge tap | A tap or click that lands on the build label's box while the badge shows opens the Console tab (which clears the count) instead of picking. Input in the viewport skips ImGui (`NvImgui.view`), so the app tests `view.tap_x` / `tap_y` (CSS pixels, the coordinates the label is drawn in) against the box before `pick` runs. The box is grown to at least 32 × 32 CSS pixels times `NvImgui.ui_scale`, so a finger can hit it. Drags that start on it still orbit the camera |
| Toolbar | Info, Warning and Error checkboxes, each with the count the ring holds ("Error 2"); a text filter (`ImGuiTextFilter`, matching the source and the text); **Clear**; **Copy** (the shown messages as text, through ImGui's clipboard, which reaches the browser's); **Auto-scroll**. Items wrap to the next line on a narrow panel (`same_line_if_fits` in `ui.c`) |
| List | A child window filling the tab above the detail box. One row per message, one line each: time, level (colored), source, text up to its first line break, and the repeat count. Longer text ends in `...`. Rows are drawn with `ImGuiListClipper` over the indices that pass the filters (built in the scratch arena each frame), so 1024 messages cost only the visible rows |
| Auto-scroll | On by default. The list follows new messages while it is scrolled to the bottom; scrolling up stops following until it is back at the bottom |
| Detail | Tapping a row selects it; the box under the list shows its full text, wrapped, in a read-only multi-line text field, so it can be selected and copied on phones too. Nothing selected: the box is hidden. A selected message that is dropped from the ring clears the selection |
| Touch | A vertical drag scrolls the list like the panel. `nv_imgui_begin_panel` scrolls only a hovered panel, not its child windows, so the scroll it applies moves into `nv_imgui_touch_scroll`, which the panel and the list's child window both call |
| Tests | A Node test for the ring; Debug exports for Playwright (below) |
| Third-party | None |

## Engine API

**`nv/log.h`** (new; usable from `anim.cpp` inside its `extern "C"` block):

```c
typedef enum NvLogLevel {
    NV_LOG_INFO,
    NV_LOG_WARNING,
    NV_LOG_ERROR,
    NV_LOG_LEVEL_COUNT,
} NvLogLevel;

#define NV_LOG_MAX_MESSAGES     1024
#define NV_LOG_TEXT_SIZE        NV_KILOBYTES(128)
#define NV_LOG_MAX_MESSAGE_SIZE NV_KILOBYTES(4)
#define NV_LOG_SOURCE_SIZE      8

typedef struct NvLogMessage {
    NvLogLevel level;
    char source[NV_LOG_SOURCE_SIZE]; // "wgpu"
    u32 repeat;      // times it arrived in a row; 1 = once
    f64 time;        // seconds since the page started, of the latest repeat
    u32 text_offset; // into NvLog.text; a message's text never wraps around the ring's end
    u32 text_size;   // without a terminator
} NvLogMessage;

typedef struct NvLog {
    NvLogMessage messages[NV_LOG_MAX_MESSAGES]; // a ring
    u32 first;                                  // the oldest message's slot
    u32 count;
    u32 level_counts[NV_LOG_LEVEL_COUNT]; // messages held, by level
    u64 arrived[NV_LOG_LEVEL_COUNT];      // messages and repeats ever added, by level; never cleared
    u64 added; // messages ever added (not repeats): the i-th held is number added - count + i
    char text[NV_LOG_TEXT_SIZE];
    u32 text_end; // where the next message's text goes
} NvLog;

// The page's one log. Zero is an empty log, so it is valid before anything runs.
extern NvLog nv_log_ring;

// Adds a message and writes it to the browser console. `source` is cut to 7 characters.
void nv_log(NvLogLevel level, const char* source, const char* format, ...)
    __attribute__((format(printf, 3, 4)));

// Moves messages queued by the page (Module.nvLog) into the ring. Call once per frame.
void nv_log_pump(void);

// Drops every message held; `arrived` and `added` keep counting.
void nv_log_clear(void);

// The i-th message held, 0 = the oldest.
NvLogMessage* nv_log_message(u32 i);
```

A message's text is `nv_log_ring.text + message->text_offset`, `text_size` bytes long.

**`nv/imgui.h`**: `void nv_imgui_touch_scroll(NvImgui* imgui);` applies this frame's touch scroll to
the current window if it is hovered. `nv_imgui_begin_panel` calls it instead of doing it inline.

## App changes

- `app/console.c` (new): the Console tab (toolbar, list, detail) and its state, a `Console` struct
  in `App`: level filters, auto-scroll, the `ImGuiTextFilter`, the selected message (by its
  number, which stays valid as the ring moves), and the `arrived` counts when the tab was
  last shown.
- `app/ui.c`: the tab and its label; `build_label` draws the badge and keeps the label's box
  (`App.build_label_box`, CSS pixels; zero while no badge shows).
- `app/main.c`: `nv_log_pump()` at the start of `frame`; `pick` first checks the tap against
  `build_label_box` and, on a hit, opens the Console tab (`app->open_console`, like
  `open_inspector`) and returns without changing the selection.
- `fprintf(stderr, ...)` in `main.c` becomes `nv_log(NV_LOG_ERROR, "app", ...)`. The save's
  notices (`app->save_notice`) stay where they are in the View tab and are also logged as warnings.
- `web/index.html.in`: `Module.nvLog`, its queue, and the `print`, `printErr`, `error` and
  `unhandledrejection` hooks feeding it.

## Tests

- **Node** (`tests/log_test.c`, run by ctest): the ring wraps at 1024 messages; long texts drop
  older messages when the text ring fills; a text that does not fit before the ring's end starts at
  0; a 5 KB message is cut to 4 KB with `...`; repeats collapse; level counts follow drops and
  clears; zero is a valid empty log.
- **Playwright**, Release and Debug, desktop mouse and phone touch sizes. Debug builds export
  `Module._app_debug_log(level, text)` (adds an `app` message) and `Module._app_debug_log_count()`.
  - A real WebGPU error (a Debug export that makes an invalid buffer) shows as a `wgpu` Error; one
    per frame for a second stays one row with a repeat count.
  - `Module.printErr("x")` and a thrown error in a `setTimeout` show as `stderr` and `js` Errors.
  - The tab label and the build-label badge count while another tab is shown, and clear when the
    Console tab is opened. Info messages show no badge.
  - A click (desktop) and a tap (phone) on the badge open the Console tab and keep the selection; a
    click on the label without a badge picks as before; a drag starting on the badge orbits.
  - Level checkboxes and the text filter hide rows; Copy puts the shown rows on the clipboard.
  - 2000 messages keep the newest 1024; the list stays at the bottom while auto-scrolling, and stays
    put after scrolling up. The UI time in the Stress tab stays about the same with a full log.
  - Tapping a row shows its full text; a touch drag scrolls the list.
  - The save round trip (`_app_debug_save_round_trip`) and the undo step count are unchanged by
    anything done in the tab.

## Phases

1. **Log:** `nv/log.h`, `log.c`, the Node test, and every existing `fprintf(stderr, ...)` and
   `console.*` call moved to it. Checked: the browser console shows the same messages as before.
2. **Console tab:** the tab, toolbar, list with the clipper, detail box, tab count, the build-label
   badge and its tap, `nv_imgui_touch_scroll`, and the page's hooks.
3. **Edge cases and docs:** floods (repeats, a full ring), the phone layout, the UI cost with a full
   log; `AGENTS.md` (the log module, the Console tab, and
   the badge in the build-label exception) and README.

Every phase is checked in Release and Debug in headless Chromium, with the mouse at desktop size
and with touch at phone size.

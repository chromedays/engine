#pragma once

// The nv app: a showcase scene with a planet and moon and an animated character, a stress scene
// that loads the engine with many objects (docs/specs/stress.md), and an editor panel to inspect
// and change them. main.c owns the showcase and the frame, stress.c the stress scene and its
// benchmark, ui.c the panel.

#include <nv/anim.h>
#include <nv/chunk.h>
#include <nv/gltf.h>
#include <nv/gpu.h>
#include <nv/imgui.h>
#include <nv/log.h>
#include <nv/renderer.h>
#include <nv/scene.h>
#include <nv/storage.h>
#include <nv/window.h>

// Set by app/CMakeLists.txt; shown in the View tab and in benchmark reports.
#ifndef NV_GIT_COMMIT
#define NV_GIT_COMMIT "unknown"
#endif
#ifndef NV_BUILD_NAME
#define NV_BUILD_NAME "unknown"
#endif

#define APP_MAX_CLIPS 16

#define STRESS_MAX_GRID     16000
#define STRESS_MAX_CHAIN    1000
#define STRESS_MAX_CROWD    200
#define STRESS_MAX_COLORS   200
#define STRESS_MAX_CHURN    256
#define STRESS_MAX_STEPS    16

// Which editor UI runs: chosen once at start from the primary pointer (docs/specs/layout.md).
typedef enum UiMode {
    UI_DESKTOP,
    UI_PHONE,
} UiMode;

// Where the editor's regions are, in framebuffer pixels (app_layout, every frame). A region the
// shown UI does not have is empty. The phone's tabbed panel is `panel`.
typedef struct Layout {
    NvRect viewport;
    NvRect top_bar;
    NvRect left, right, bottom; // desktop docks
    NvRect panel;               // phone
} Layout;

// The desktop docks (app/ui_desktop.c). Sizes are the wanted ones, in CSS pixels; the layout
// clamps them to the window each frame. They are saved with the editor settings.
typedef struct Docks {
    f32 left_width, right_width, bottom_height;
    f32 drag_start_mouse, drag_start_size; // while a splitter is dragged
    b32 bottom_open;                   // the bottom dock shows its contents, not just its strip
    b32 show_left, show_right, show_bottom; // View menu; not saved
    s32 dragging;                      // the splitter being dragged (DockSplitter), 0 = none
} Docks;

// What the desktop splitters allow, in CSS pixels (ui_desktop.c clamps again to the window).
#define DOCK_LEFT_MIN   160.0f
#define DOCK_RIGHT_MIN  220.0f
#define DOCK_BOTTOM_MIN 120.0f
#define DOCK_SIDE_MAX   640.0f
#define DOCK_BOTTOM_MAX 600.0f

typedef enum SceneKind {
    SCENE_SHOWCASE,
    SCENE_STRESS,
    SCENE_COUNT,
} SceneKind;

// The orbit camera's limits.
#define CAMERA_MIN_PITCH (-10.0f * NV_PI / 180.0f)
#define CAMERA_MAX_PITCH (80.0f * NV_PI / 180.0f)
#define CAMERA_MIN_DISTANCE 1.0f
#define CAMERA_MAX_DISTANCE 100.0f

// The autosave (docs/specs/save.md).
#define SAVE_MAGIC NV_TAG('N', 'V', 'S', 'V')
#define SAVE_VERSION 1
#define SAVE_MAX_SIZE NV_KILOBYTES(256)
#define SAVE_DIR "/nv-save"
#define SAVE_FILE "state.nvs"
#define SAVE_BAD_FILE "state.nvs.bad" // a save that could not be loaded, kept for a look
#define AUTOSAVE_SECONDS 10.0

// Undo and redo (docs/specs/undo.md): steps hold a scope's bytes before and after one edit.
#define UNDO_MAX_STEPS 128
#define UNDO_MAX_BYTES 1024
#define UNDO_LABEL_MAX 64

// Parts of the showcase that undo snapshots separately (docs/specs/undo.md).
typedef enum SaveScope {
    SAVE_SCOPE_NODE,      // one node
    SAVE_SCOPE_CHARACTER, // the character's playback and controls
    SAVE_SCOPE_SCENE,     // planet orbit speed, show bones
    SAVE_SCOPE_COUNT,
} SaveScope;

typedef struct UndoStep {
    SaveScope scope;
    NvNodeId node; // SAVE_SCOPE_NODE
    u32 before_size;
    u32 after_size;
    u8 before[UNDO_MAX_BYTES];
    u8 after[UNDO_MAX_BYTES];
    char label[UNDO_LABEL_MAX]; // "moon Position"
} UndoStep;

typedef struct Undo {
    UndoStep* steps; // a ring of UNDO_MAX_STEPS
    u32 first;       // the oldest step's slot
    u32 count;       // steps held
    u32 done;        // how many of them are applied: undo takes step done - 1, redo step done
    s32 request;     // -1 undo, +1 redo, asked by the buttons; done at the frame's end
    // Each scope as last committed; an idle frame that finds it changed takes a step.
    u8 committed[SAVE_SCOPE_COUNT][UNDO_MAX_BYTES];
    u32 committed_size[SAVE_SCOPE_COUNT];
    NvNodeId committed_node; // the node SAVE_SCOPE_NODE's bytes are of
    u32 committed_driven;    // its driven fields then
    u8 current[UNDO_MAX_BYTES];
} Undo;

// The Console tab (docs/specs/console.md): what it shows of the log, and what it has seen of it.
typedef enum ConsoleRect {
    CONSOLE_RECT_TAB, // the tab's label, in the panel's tab bar
    CONSOLE_RECT_LEVEL, // three: info, warning, error
    CONSOLE_RECT_CLEAR = CONSOLE_RECT_LEVEL + NV_LOG_LEVEL_COUNT,
    CONSOLE_RECT_COPY,
    CONSOLE_RECT_AUTO_SCROLL,
    CONSOLE_RECT_FILTER,
    CONSOLE_RECT_LIST,
    CONSOLE_RECT_FIRST_ROW, // the first row drawn
    CONSOLE_RECT_COPY_MESSAGE,
    CONSOLE_RECT_DETAIL,
    CONSOLE_RECT_COUNT,
} ConsoleRect;

typedef struct Console {
    bool hidden[NV_LOG_LEVEL_COUNT]; // levels the checkboxes hide; zero = shown
    bool auto_scroll;                // follow new messages while at the bottom
    ImGuiTextFilter filter;          // matches "source text"
    u64 selected;                    // number (nv_log_number) of the selected message + 1; 0 = none
    u64 detail_number;               // the number + 1 of the message `detail` holds
    char detail[NV_LOG_MAX_MESSAGE_SIZE + 1]; // its text, for the read-only field
    u64 seen[NV_LOG_LEVEL_COUNT];    // NvLog.arrived when the tab was last shown
    b32 shown_now;                   // the tab was drawn this frame
    b32 shown_last;                  // ... and last frame: then nothing counts as unseen
    // Where things were drawn last frame, in CSS pixels (x0, y0, x1, y1), for tests to click.
    f32 rects[CONSOLE_RECT_COUNT][4];
    u32 rows;         // rows the filters let through
    f32 scroll_y;     // the list's scroll, and its end
    f32 scroll_max;
} Console;

// The Textures tab (docs/specs/textures.md).
typedef enum TextureKind {
    TEXTURE_MATERIAL, // NvRenderer.textures
    TEXTURE_SHADOW,
    TEXTURE_DEPTH,
    TEXTURE_SWAPCHAIN,
    TEXTURE_UI,       // NvImgui.textures
} TextureKind;

typedef enum TexturesRect {
    TEXTURES_RECT_TAB,
    TEXTURES_RECT_IN_USE,
    TEXTURES_RECT_FIRST_ROW,
    TEXTURES_RECT_ZOOM,
    TEXTURES_RECT_MIP,
    TEXTURES_RECT_CHANNELS, // six: RGBA, RGB, R, G, B, A
    TEXTURES_RECT_RANGE = TEXTURES_RECT_CHANNELS + 6,
    TEXTURES_RECT_IMAGE,
    TEXTURES_RECT_FIRST_USER,
    TEXTURES_RECT_INSPECTOR,
    TEXTURES_RECT_BACK,
    TEXTURES_RECT_COUNT,
} TexturesRect;

#define TEXTURES_MAX_ROW_RECTS 32

// What the Textures tab shows. Zero is valid: nothing picked, only textures in use, RGBA, zoom 1.
// View state only: not saved, not undoable.
typedef struct TextureViewer {
    bool show_unused;     // the "In use only" checkbox, off
    b32 has_selection;
    b32 detail_open;      // a narrow panel shows the picked texture instead of the list
    TextureKind selected_kind;
    u32 selected_index;
    u32 mip;
    s32 channels;         // NvImguiPreviewMode, RGBA to A
    bool no_checkerboard;
    f32 zoom;             // 1 to 16; 0 = 1
    f32 center[2];        // the zoomed window's center, 0..1 of the shown part
    f32 depth_range;      // depth target: the distance shown white, meters; 0 = twice the camera distance
    f32 shadow_range[2];  // shadow map: the stored depth shown black and white; equal = 0..1
    b32 shown_now;        // the tab was drawn this frame
    b32 shown_last;       // ... and last frame: the depth target is samplable while it is
    u32 listed;           // rows drawn
    // Where things were drawn last frame, in CSS pixels (x0, y0, x1, y1), for tests to click.
    f32 rects[TEXTURES_RECT_COUNT][4];
    f32 row_rects[TEXTURES_MAX_ROW_RECTS][4]; // the listed rows, in order; zero when not drawn
} TextureViewer;

// What the transform gizmo on the selection does (docs/specs/gizmo.md).
typedef enum GizmoOperation {
    GIZMO_MOVE,
    GIZMO_ROTATE,
    GIZMO_SCALE,
} GizmoOperation;

// What the editor shows of one scene: its camera orbit and its selection.
typedef struct SceneView {
    NvScene* scene;
    NvNodeId camera;
    NvNodeId focus;    // orbited while nothing is selected
    NvNodeId selected; // shown in the inspector; the camera orbits it
    f32 camera_yaw;      // radians
    f32 camera_pitch;    // radians, looking down
    f32 camera_distance; // meters
    NvVec3 pan;          // added to the orbit point by panning; cleared when the selection changes
    NvNodeId panned_for; // the selection `pan` belongs to
    bool follow_selection; // orbit the selection; otherwise stay at orbit_point
    NvVec3 orbit_point;    // where the camera looked last frame; panned directly while not following
} SceneView;

// CPU time of the frame's stages, in milliseconds.
typedef struct FrameTimes {
    f64 frame; // from one frame to the next
    f64 anim;  // nv_anim_update_scene
    f64 scene; // nv_scene_update
    f64 draw;  // nv_renderer_draw
    f64 ui;    // building and recording ImGui
    f64 gpu;   // the scene pass on the GPU; 0 where the browser has no timestamps
    f64 gpu_shadow; // the shadow pass on the GPU; 0 without timestamps or shadows
} FrameTimes;

typedef struct BenchmarkStep {
    const char* name;
    s32 grid;
    s32 crowd;
} BenchmarkStep;

typedef struct BenchmarkResult {
    u32 frames;
    FrameTimes average;
    f64 worst_frame;
} BenchmarkResult;

// How busy the frame is: the larger of the CPU stages and the GPU pass, as a share of the frame
// time. Unlike the frame time it keeps rising while vsync holds the frame rate at the display's.
f64 app_load(const FrameTimes* t);

// What the stress scene is asked to hold. bools because ImGui writes them through bool*.
typedef struct StressWorkloads {
    bool grid_on;
    s32 grid_count;
    bool colors_on;
    s32 color_count;
    bool chain_on;
    s32 chain_count;
    bool crowd_on;
    s32 crowd_count;
    bool churn_on;
    s32 churn_count;
    bool show_bones;
} StressWorkloads;

typedef struct Stress {
    b32 built;
    NvScene* scene;
    NvMeshId cube;
    NvMeshId small_cube;
    NvMaterialId colors[STRESS_MAX_COLORS]; // [0] is the grid's color while colors are off

    StressWorkloads want;

    // What is built. Crowd characters are never removed, since animators cannot be; the ones not
    // asked for are hidden and paused.
    NvNodeId grid_group, chain_group, crowd_group;
    NvNodeId grid[STRESS_MAX_GRID];
    u32 grid_built;
    u32 grid_colors; // colors the grid was last painted with; 0 = one material
    NvNodeId chain[STRESS_MAX_CHAIN];
    u32 chain_built;
    NvGltfModel crowd[STRESS_MAX_CROWD];
    u32 crowd_created;
    u32 crowd_active;
    u32 churn_cursor;
    f32 chain_angle;

    // Benchmark
    b32 benchmark_running;
    u32 benchmark_step;
    f64 benchmark_step_start; // seconds
    StressWorkloads before_benchmark; // put back when a run finishes
    NvShadowSettings benchmark_shadows; // the shadow settings the last run measured with
    BenchmarkStep steps[STRESS_MAX_STEPS];
    BenchmarkResult results[STRESS_MAX_STEPS];
    u32 step_count;
    u32 result_count;
} Stress;

typedef enum JumpPhase {
    JUMP_NONE,
    JUMP_START,
    JUMP_AIR,
    JUMP_LAND,
} JumpPhase;

typedef struct App {
    NvWindow window;
    NvGpu gpu;
    NvArena permanent;
    NvArena scratch;
    NvArena anim_memory;
    NvScene* scene;
    NvRenderer renderer;
    NvImgui imgui;
    f64 time;
    f64 last_time;

    SceneKind shown;
    SceneView views[SCENE_COUNT];

    // Frame timing: the last frame, and averages and worst case over about a second.
    FrameTimes times;
    FrameTimes shown_average;
    f64 shown_worst_frame;
    FrameTimes window_sum;
    f64 window_worst_frame;
    u32 window_frames;
    f64 window_start;

    Stress stress;

    // Planet and moon: the moon is the planet's child, so spinning the planet carries it around.
    NvNodeId planet;
    NvNodeId moon;
    f32 orbit_angle;
    f32 orbit_speed; // radians per second

    // Character
    NvGltfModel character;
    NvAnimatorId animator; // character.animator
    NvClipId clips[APP_MAX_CLIPS]; // in place
    u32 clip_count;
    NvClipId root_motion_clips[APP_MAX_CLIPS]; // same names, root motion taken out
    u32 root_motion_clip_count;

    // Playback
    f32 fade_seconds;
    s32 blend_clip; // index into clips; blended with the current clip by blend_weight
    f32 blend_weight;
    JumpPhase jump;
    f32 jump_air_time;
    NvClipId jump_return; // loop to go back to after landing

    // A sword attached to the right hand.
    bool show_sword; // bool because ImGui writes it through a bool*
    NvNodeId sword;
    NvMeshId sword_mesh;

    // Root motion: the character walks by what the clip's root joint does, turning as it goes.
    bool root_motion;
    f32 turn_rate; // radians per second

    // Aim IK: the head follows a target that sweeps in front of the character.
    bool look_at;
    NvNodeId target;
    NvMeshId target_mesh;

    Undo undo;
    Console console;
    TextureViewer textures;
    NvRect logged_viewport; // the scene viewport last logged

    // Edit and Play modes (docs/specs/play.md). In Edit mode nothing moves the showcase by itself;
    // Play runs it, and Stop restores it from the snapshot taken at Play.
    b32 playing;
    f32 play_time;          // seconds since Play
    u8* play_snapshot;      // save bytes of the showcase at Play [SAVE_MAX_SIZE]
    u32 play_snapshot_size;
    NvQuat planet_rotation; // the authored rotations the spin turns from, taken at Play
    NvQuat moon_rotation;

    // Autosave (save.c)
    bool autosave;
    u32 scene_layout; // save_scene_layout of the showcase as built
    NvStorage storage;
    u8* saved;       // the bytes last written [SAVE_MAX_SIZE], to skip writing an unchanged save
    u32 saved_size;
    u8* next_save;   // [SAVE_MAX_SIZE]
    f64 last_save_check; // nv_time_seconds
    f64 saved_at;        // nv_time_seconds of the last write this visit; 0 = none yet
    char save_notice[192]; // shown in the View tab: why the save was not loaded or written
    b32 save_stopped;      // Reset is reloading the page; nothing may be saved meanwhile
    bool show_save;        // the save viewer is open
    u8* viewed;            // the file the viewer shows [SAVE_MAX_SIZE]
    u32 viewed_size;
    b32 viewed_bad;        // it is SAVE_BAD_FILE

    // Editor
    GizmoOperation gizmo_operation;
    bool gizmo_local; // local axes for Move and Rotate (Scale always uses them)
    bool gizmo_snap;
    b32 gizmo_shown;  // drawn last frame, so ImGuizmo's hit test is current
    b32 open_inspector; // switch to the Inspector tab on the next frame
    b32 open_stress;    // switch to the Stress tab on the next frame
    b32 open_console;   // switch to the Console tab on the next frame
    b32 open_textures;  // switch to the Textures tab on the next frame
    // The build label's box while it carries a badge, in CSS pixels (x0, y0, x1, y1), grown to a
    // size a finger can hit; a tap inside opens the Console tab. Zero width = no badge.
    f32 badge_box[4];
    bool show_bones;

    // Layout (ui.c, ui_desktop.c, ui_phone.c)
    UiMode ui_mode;
    Layout layout;
    Docks docks;
    b32 open_view;    // switch to the View tab on the next frame
    f32 play_box[4];  // the Play / Stop button, CSS pixels (x0, y0, x1, y1); zero width = hidden
} App;

// main.c
NvClipId app_find_clip(App* app, const char* name);
NvClipId app_regular_clip(App* app, NvClipId clip); // the in-place clip with the same name
void app_play(App* app, NvClipId clip);             // crossfades, with root motion while it is on
void app_jump(App* app);
void app_back_to_center(App* app);
void app_start_playing(App* app); // Play: snapshot the showcase and run it
void app_stop_playing(App* app);  // Stop: restore the showcase from the snapshot
// The animator a node has, or that its first animated child has (a character root); 0 = none.
NvAnimatorId app_node_animator(NvScene* scene, NvNodeId id);
void app_show_scene(App* app, SceneKind kind);
void app_focus_selection(App* app); // F: the orbit point moves to the selected node
NvMeshId app_box_mesh(App* app, NvVec3 half);
SceneView* app_view(App* app); // the shown scene's view

// stress.c
void stress_build(App* app);
void stress_update(App* app, f32 dt);  // before nv_anim_update_scene
void stress_after_frame(App* app);     // after the frame's times are known: runs the benchmark
void stress_draw_bones(App* app);
u32 stress_live_nodes(NvScene* scene);
void stress_ui(App* app);

// ui.c: the UI chosen at start, and the sections both UIs draw into their own windows.
void app_layout(App* app); // fills app->layout from the canvas size; before nv_imgui_new_frame
void app_build_ui(App* app);
void ui_scene_tab(App* app);
void ui_inspector_tab(App* app);
void ui_view_tab(App* app);
// The Play / Stop button, `size` wide and high (0 = natural), at the cursor; nothing in the stress
// scene. Records its box in app->play_box.
void ui_play_button(App* app, ImVec2_c size);
// The line telling that edits are lost on Stop, while the showcase plays.
void ui_playing_note(App* app);
// Begins the Console tab item with its unseen count and color; if true, draw console_tab and
// igEndTabItem.
b32 ui_begin_console_tab(App* app);
b32 ui_begin_textures_tab(App* app); // the Textures tab item; jumps to it on app->open_textures
// The build label in the viewport's top-left corner, with its badge.
void ui_build_label(App* app);
// A tint for the panels while the showcase plays: push before igBegin, pop after.
b32 ui_push_play_tint(App* app);
void ui_pop_play_tint(b32 pushed);

// A rectangle given in CSS pixels as framebuffer pixels, rounded so neighbors share their edges.
NvRect ui_rect(f32 x0, f32 y0, f32 x1, f32 y1, f32 ratio);

// ui_desktop.c and ui_phone.c
void desktop_layout(App* app, f32 width, f32 height, f32 ratio);
void desktop_build_ui(App* app);
void phone_layout(App* app, f32 width, f32 height, f32 ratio);
void phone_build_ui(App* app);
// Keeps the next checkbox or button on this line when `width` fits, else starts a new one.
void ui_same_line_if_fits(f32 width);

// console.c
// Warnings and errors that arrived since the Console tab was last shown (0 while it is shown), and
// the worst level among them.
u32 console_unseen(App* app, NvLogLevel* worst);
void console_tab(App* app); // the Console tab's contents
ImU32 console_level_color(NvLogLevel level);
b32 console_is_compact(void); // the panel is too narrow (a phone) for the full layout
void console_record(Console* console, ConsoleRect id); // the last item's rect, for tests

// textures.c
void textures_tab(App* app);
void textures_inspector_thumbnail(App* app, NvTextureId texture); // the Inspector's Mesh section
void textures_record(TextureViewer* viewer, TexturesRect id);     // the last item's rect, for tests

// save.c
u32 save_scene_layout(NvScene* scene); // a hash of the tree's shape and names
// Writes the app state; returns its size, or 0 if it did not fit.
u32 save_write(App* app, void* buffer, u32 capacity);
// Loads a save into the app. It is checked whole first, and nothing changes unless it is good.
// Returns NULL, or what is wrong with it.
const char* save_load(App* app, const void* bytes, u32 size);
// Loads only some parts of a save (SAVE_PART_*), e.g. the scene without the view for Stop.
#define SAVE_PART_EDITOR (1u << 0) // gizmo and autosave settings
#define SAVE_PART_VIEW   (1u << 1) // the showcase's camera view and selection
#define SAVE_PART_SCENE  (1u << 2) // nodes, character and scene settings
#define SAVE_PART_ALL    (SAVE_PART_EDITOR | SAVE_PART_VIEW | SAVE_PART_SCENE)
const char* save_load_parts(App* app, const void* bytes, u32 size, u32 parts);
// Undo scopes: the undoable fields of a part of the showcase, written and read with the save's code
// (docs/specs/undo.md). Values the app drives every frame are left out.
// Returns the size written, or 0 if it did not fit.
u32 save_write_scope(App* app, SaveScope scope, u32 node, void* buffer, u32 capacity);
// Applies the fields present. Returns 0, changing nothing, if the bytes are malformed.
b32 save_apply_scope(App* app, SaveScope scope, u32 node, const void* bytes, u32 size);
// Which parts of a node's transform the app drives (bit 0 position, bit 1 rotation).
u32 save_driven_fields(App* app, u32 node);
const char* save_field_label(u32 tag); // "Position", "Clip", ...

// undo.c
void undo_init(App* app);   // after the save is loaded
void undo_update(App* app); // every frame, after every edit (the gizmo's included)
// The Undo (direction -1) or Redo (+1) button in the current window; `labels` adds the step's name.
void undo_button(App* app, s32 direction, b32 labels, ImVec2_c size);
// Undo and Redo as menu items with their shortcuts.
void undo_menu_items(App* app);

// Mounts browser storage and loads the save, if there is one. Call once the showcase is built.
void save_init(App* app);
// Autosaves every AUTOSAVE_SECONDS while autosave is on. Call every frame.
void save_update(App* app);
// Writes the save now if it changed since the last write, or even if not when `force` is set.
void save_now(App* app, b32 force);
void save_ui(App* app); // the View tab's Autosave section
void save_reset_popup(App* app); // the modal Reset asks in; call where igOpenPopup_Str("Reset everything?") ran
void save_show_viewer(App* app); // opens the save viewer (in the View tab)
// Whether saving, loading that save and saving again gives the same bytes. It loads, so it may
// end a jump or a crossfade; Debug builds check it at start and tests call it.
b32 save_round_trip_matches(App* app);

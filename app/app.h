#pragma once

// The nv app: a showcase scene with a planet and moon and an animated character, a stress scene
// that loads the engine with many objects (docs/specs/stress.md), and an editor panel to inspect
// and change them. main.c owns the showcase and the frame, stress.c the stress scene and its
// benchmark, ui.c the panel.

#include <engine/anim.h>
#include <engine/chunk.h>
#include <engine/gltf.h>
#include <engine/gpu.h>
#include <engine/imgui.h>
#include <engine/log.h>
#include <engine/renderer.h>
#include <engine/scene.h>
#include <engine/storage.h>
#include <engine/vfx.h>
#include <engine/window.h>

#include "strings.h"

// The commit's hash and subject line (nv_version.h, written by cmake/version.cmake on every
// build) and the build type (set by app/CMakeLists.txt); shown on the build label, in the View tab
// and in benchmark reports.
#include "nv_version.h"
#ifndef NV_BUILD_NAME
#define NV_BUILD_NAME "unknown"
#endif

#define APP_MAX_CLIPS 16
#define SELECTION_MAX 256 // selected nodes at once, the primary included (docs/specs/selection.md)
#define TREE_ROWS_MAX 4096 // Scene tab rows a Shift+click range can reach

#define STRESS_MAX_GRID     16000
#define STRESS_MAX_CHAIN    1000
#define STRESS_MAX_CROWD    200
#define STRESS_MAX_COLORS   200
#define STRESS_MAX_CHURN    256
#define STRESS_MAX_STEPS    16
#define STRESS_MAX_PARTICLES 4000000 // the Effects workload's targets
#define STRESS_MAX_EXPLOSIONS 200
#define STRESS_MAX_MISSILES 2000
#define STRESS_MAX_BEAMS    500
#define STRESS_MAX_DECALS   2000

// Which editor UI runs: chosen once at start from the primary pointer (docs/specs/layout.md).
typedef enum UiMode {
    UI_DESKTOP,
    UI_PHONE,
} UiMode;

// How the scene is rendered and shown in the viewport (docs/specs/resolution.md): at a whole
// fraction of the viewport's pixels filling it, or at a fixed size centered in it with black bars.
typedef enum ResolutionMode {
    RESOLUTION_SCALE,
    RESOLUTION_FIXED,
} ResolutionMode;

// How a fixed size is fitted to the viewport (docs/specs/resolution.md).
typedef enum FixedFit {
    FIT_WHOLE,    // the largest whole multiple that fits, centered, black bars around
    FIT_VIEWPORT, // the largest scale that fits keeping the aspect ratio (pixels of uneven width)
    FIT_STRETCH,  // stretched to fill the viewport, aspect ratio and all
} FixedFit;

typedef struct Resolution {
    ResolutionMode mode;
    FixedFit fixed_fit;
    u32 divisor;                    // SCALE: 1, 2, 3 or 4
    u32 fixed_width, fixed_height;  // FIXED: RESOLUTION_MIN..RESOLUTION_MAX each
} Resolution;

#define RESOLUTION_MIN 16
#define RESOLUTION_MAX 4096

// Where the editor's regions are, in framebuffer pixels (app_layout, every frame). A region the
// shown UI does not have is empty. The phone's tabbed panel is `panel`.
typedef struct Layout {
    NvRect viewport;
    NvSceneOutput scene; // the scene's resolution and where its image goes inside the viewport
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

// The desktop shortcuts (app/shortcuts.c); the menus ask for their labels.
typedef enum ShortcutId {
    SC_SAVE,
    SC_UNDO,
    SC_REDO,
    SC_DESELECT,
    SC_PLAY,
    SC_MOVE,
    SC_ROTATE,
    SC_SCALE,
    SC_AXES,
    SC_SNAP_HELD,
    SC_FOCUS,
    SC_FOLLOW,
    SC_HOME,
    SC_DOCK_LEFT,
    SC_DOCK_RIGHT,
    SC_DOCK_BOTTOM,
    SC_PALETTE,
    SC_FIND,
    SC_HELP,
    // Palette only: no keys (docs/specs/search.md).
    SC_SHOW_SAVE,
    SC_RESET,
    SC_SCENE_SHOWCASE,
    SC_SCENE_STRESS,
    SC_TEXTURES,
    SC_CLEAR_CONSOLE,
    SHORTCUT_COUNT,
} ShortcutId;

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
// The Node scope holds every selected node: a node writes at most about 220 bytes (its name, transform,
// color, joint, camera and light fields with their headers), so a step is sized for SELECTION_MAX nodes.
#define UNDO_NODE_BYTES 256
#define UNDO_MAX_BYTES (SELECTION_MAX * UNDO_NODE_BYTES)
#define UNDO_LABEL_MAX 64

// Parts of the showcase that undo snapshots separately (docs/specs/undo.md).
typedef enum SaveScope {
    SAVE_SCOPE_NODE,      // the selected nodes
    SAVE_SCOPE_CHARACTER, // the character's playback and controls
    SAVE_SCOPE_SCENE,     // planet orbit speed, show bones
    SAVE_SCOPE_COUNT,
} SaveScope;

typedef struct UndoStep {
    SaveScope scope;
    NvNodeId nodes[SELECTION_MAX]; // SAVE_SCOPE_NODE, in selection order (the primary first)
    u32 node_count;
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
    NvNodeId committed_nodes[SELECTION_MAX]; // the nodes SAVE_SCOPE_NODE's bytes are of
    u32 committed_node_count;
    u32 committed_driven[SELECTION_MAX];      // their driven fields then
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
    TEXTURE_SCENE,    // the scene color target, the scene's resolution (allocated a little larger)
    TEXTURE_BLOOM,    // the bloom chain (docs/specs/vfx.md), while bloom is on
    TEXTURE_MSAA,     // the multisampled color target the scene pass resolves into the swapchain
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
    // The selection (docs/specs/selection.md): the primary node, picked last, is shown in the
    // inspector and the camera orbits it; `others` are the rest, oldest first, never the primary.
    NvNodeId selected;
    NvNodeId others[SELECTION_MAX - 1];
    u32 other_count;
    NvNodeId range_anchor; // where a Shift+click range in the Scene tab starts
    f32 camera_yaw;      // radians
    f32 camera_pitch;    // radians, looking down
    f32 camera_distance; // meters
    NvVec3 pan;          // added to the orbit point by panning; cleared when the selection changes
    NvNodeId panned_for; // the selection `pan` belongs to
    bool follow_selection; // orbit the selection; otherwise stay at orbit_point
    NvVec3 orbit_point;    // where the camera looked last frame; panned directly while not following
    // The view the scene starts with, for Home (app_set_home).
    f32 home_yaw, home_pitch, home_distance;
    NvVec3 home_orbit;
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
    f64 gpu_upscale; // the upscale pass on the GPU; 0 without timestamps
    f64 gpu_bloom;   // the bloom passes on the GPU; 0 without timestamps or bloom
    f64 gpu_particles; // the particle compute passes on the GPU; 0 without timestamps or effects
} FrameTimes;

typedef struct BenchmarkStep {
    const char* name;
    s32 grid;
    s32 crowd;
    s32 particles; // the Effects workload: live particles kept, missiles flying, beams held
    s32 missiles;
    s32 beams;
} BenchmarkStep;

typedef struct BenchmarkResult {
    u32 frames;
    FrameTimes average;
    f64 worst_frame;
    f64 particles; // live particles, averaged over the step (a sum until it ends)
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
    bool effects_on;
    s32 effect_particles;  // live particles kept (swarm bursts fired to hold the number)
    s32 effect_explosions; // explosions per second
    s32 effect_missiles;   // missiles in flight, each with a trail and smoke
    s32 effect_beams;      // beams held
    s32 effect_decals;     // decals per second
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
    u32 benchmark_msaa;                 // the sample count it measured with
    char benchmark_resolution[64];      // the scene's resolution it measured at ("585 x 497, scale 1/2")
    BenchmarkStep steps[STRESS_MAX_STEPS];
    BenchmarkResult results[STRESS_MAX_STEPS];
    u32 step_count;
    u32 result_count;
} Stress;

// Search (docs/specs/search.md): the boxes at the top of the panels and the command palette.
typedef enum SearchPanel {
    SEARCH_SCENE,
    SEARCH_INSPECTOR,
    SEARCH_VIEW,
    SEARCH_TEXTURES,
    SEARCH_STRESS,
    SEARCH_PANEL_COUNT,
} SearchPanel;

#define SEARCH_QUERY_MAX     96
#define SEARCH_MAX_WORDS     6
#define SEARCH_WORD_MAX      32
#define SEARCH_MAX_SETTINGS  160
#define SEARCH_MAX_NODES     200 // node results the palette lists
#define SEARCH_MAX_RESULTS   512
#define SEARCH_RECENT        8
#define SEARCH_TREE_MAX_ROWS 500

// A query split into lowercase words; a candidate matches when it has every one of them.
typedef struct SearchQuery {
    u32 count;
    char words[SEARCH_MAX_WORDS][SEARCH_WORD_MAX];
} SearchQuery;

// A row of a panel, noted while the palette collects settings (search_row in collect mode).
typedef struct SearchSetting {
    u8 panel;
    char label[48];
    char section[40];
    char keywords[64];
} SearchSetting;

typedef enum PaletteKind {
    PALETTE_ACTION,
    PALETTE_SETTING,
    PALETTE_NODE,
    PALETTE_MORE, // "and N more", not selectable
} PaletteKind;

typedef struct PaletteResult {
    u8 kind;
    b32 enabled;
    u32 index; // command id, setting index or node index
    u64 key;   // sort key
} PaletteResult;

typedef struct Search {
    char queries[SEARCH_PANEL_COUNT][SEARCH_QUERY_MAX]; // one per panel; not saved
    SearchQuery query; // of the panel being drawn
    SearchPanel panel;
    b32 collecting; // search_row only notes rows (the palette's settings), draws nothing
    char section[40];
    b32 section_pending; // the heading waits for its first row
    char pending_label[48];
    b32 pending_highlight; // the last row's label gets its match marked after its widget
    u32 rows[SEARCH_PANEL_COUNT];     // rows drawn last frame
    u32 rows_now[SEARCH_PANEL_COUNT];
    s32 focus_panel;   // the box to focus next frame, as panel + 1; 0 = none
    s32 hover_panel;   // the panel under the pointer this frame; -1 = none
    s32 right_panel;   // the right dock's (or the phone panel's) panel this frame; -1 = none
    // The Scene tree's matches: a byte per node (bit 0 matches, bit 1 is on the way to a match).
    u8 tree[NV_MAX_NODES];
    u32 tree_key;
    s32 tree_frame;
    u32 tree_matches;
    u32 tree_drawn;
    b32 tree_filtering;
    // Settings the palette can jump to.
    SearchSetting settings[SEARCH_MAX_SETTINGS];
    u32 setting_count;
    // The command palette.
    b32 palette_request;
    b32 palette_open;
    b32 palette_focus;
    s32 palette_opened_frame;
    char palette_query[SEARCH_QUERY_MAX];
    s32 highlight;
    b32 scroll_to_highlight;
    b32 scroll_to_top;
    s32 recent[SEARCH_RECENT]; // command ids, most recent first
    u32 recent_count;
    PaletteResult results[SEARCH_MAX_RESULTS];
    u32 result_count;
    char debug_buffer[SEARCH_QUERY_MAX]; // tests write a query here (_app_debug_search_buffer)
} Search;

typedef enum JumpPhase {
    JUMP_NONE,
    JUMP_START,
    JUMP_AIR,
    JUMP_LAND,
} JumpPhase;

// A test missile in flight: it leaves a trail and explodes where its life ends.
typedef struct Flight {
    NvVec3 pos, vel;
    f32 life;
} Flight;

// The effects the app fires (effects.c).
typedef struct Effects {
    NvVfxEffectId explosion, sparks, smoke, missile, swarm;
    NvVfxLineStyle missile_trail, laser;
    NvVfxDecalStyle scorch;
    Flight flights[8];
    u32 flight_count;
    // The stress scene's Effects workload (effects_stress_update).
    Flight stress_flights[STRESS_MAX_MISSILES];
    u32 stress_flight_count;
    f32 swarm_carry, explosion_carry, decal_carry; // fractions of a burst not yet fired
    NvRandom rng;
} Effects;

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
    // A gizmo drag over several nodes (docs/specs/selection.md): the nodes it moves, their world
    // matrices and the gizmo's matrix when it started, and the gizmo's matrix now.
    NvNodeId gizmo_nodes[SELECTION_MAX];
    NvMat4 gizmo_start_world[SELECTION_MAX];
    u32 gizmo_node_count;
    NvMat4 gizmo_start;
    NvMat4 gizmo_matrix;
    bool multi_select;  // the phone's Multi toggle: taps add and remove (docs/specs/selection.md)
    // The Scene tab's rows in the order drawn, this frame's and last frame's, for Shift+click ranges.
    u32 tree_rows[2][TREE_ROWS_MAX];
    u32 tree_row_count[2];
#if !defined(NDEBUG)
    f32 tree_row_rects[64][4]; // the first rows' rects last frame, CSS pixels (x0, y0, x1, y1), for tests
#endif
    b32 selection_full_warned;
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
    Resolution resolution;
    Docks docks;
    b32 open_view;    // switch to the View tab on the next frame
    f32 play_box[4];  // the Play / Stop button, CSS pixels (x0, y0, x1, y1); zero width = hidden
    b32 show_shortcuts; // the Keyboard shortcuts window is open
    b32 request_reset;  // open the Reset confirmation (the palette asks; the top bar draws it)
    char download_text[64]; // what the page downloaded, for the build label ("0.9 MB downloaded"); empty = unknown
    Search search;

    // Effects (effects.c, docs/specs/vfx.md).
    NvVfx vfx;
    Effects effects;
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
void app_set_home(SceneView* view); // remembers the view as it is now as the one Home goes back to
NvMeshId app_box_mesh(App* app, NvVec3 half);
SceneView* app_view(App* app); // the shown scene's view

// effects.c
void effects_init(App* app);               // makes the particle system and registers the effects
void effects_update(App* app, f32 dt);     // each frame, before drawing
void effects_clear(App* app);              // Play and Stop: every live effect goes
void effects_fire(App* app, u32 which);    // 0 explosion, 1 sparks, 2 smoke, 3 missile, beside the orbit point
NvVec3 effects_test_point(App* app);
void effects_ui(App* app);
// The Effects workload of the stress scene: keeps the asked numbers of particles, explosions,
// missiles, beams and decals going. Call each frame while the stress scene is shown.
void effects_stress_update(App* app, const StressWorkloads* want, f32 dt);

// selection.c (docs/specs/selection.md)
u32 selection_count(SceneView* view);
NvNodeId selection_get(SceneView* view, u32 i); // 0 is the primary, then the others
b32 selection_has(SceneView* view, NvNodeId id);
void selection_set(SceneView* view, NvNodeId id);              // only this node; 0 clears
void selection_add(App* app, SceneView* view, NvNodeId id);    // adds it as the primary
void selection_toggle(App* app, SceneView* view, NvNodeId id); // adds it, or removes it if selected
void selection_keep_primary(SceneView* view);
void selection_prune(SceneView* view); // drops nodes that no longer exist

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

// shortcuts.c
void shortcuts_update(App* app); // fires the shortcuts pressed this frame; desktop UI, after the docks
b32 shortcuts_claim(void* data, ImGuiKeyChord chord); // NvImgui.claims_key
const char* shortcut_label(ShortcutId id);            // "Ctrl+Shift+Z / Ctrl+Y"
void shortcuts_help(App* app);                        // the Keyboard shortcuts window

// search.c
void search_frame(App* app); // start of the frame's UI
// The panel's search box and, under it, the child window its content is drawn in. In collect mode
// they draw nothing. Every widget of a searchable panel goes through search_row or search_group.
void search_panel_begin(App* app, SearchPanel panel);
void search_panel_end(App* app);
void search_section(App* app, const char* heading); // the heading draws with the section's first row; NULL = no heading
b32 search_row(App* app, const char* label, const char* keywords);   // one widget carrying `label`: draw it if true
b32 search_group(App* app, const char* label, const char* keywords); // several widgets, or a button: same, no highlight
b32 search_plain(App* app); // text that is no setting: only with an empty query
b32 search_active(App* app); // the current panel has a query
b32 search_match(App* app, const char* text);                  // whether `text` matches the current query
void search_mark(App* app, const char* text, ImVec2_c origin, f32 height); // marks the matched parts of text drawn at origin
void search_set_query(App* app, SearchPanel panel, const char* text);
void search_palette(App* app); // the palette window, the top level of the frame's UI
void search_open_palette(App* app);
void search_focus_box(App* app); // Ctrl+F

// shortcuts.c: the table's rows are the palette's actions.
b32 command_listed(u32 id);
const char* command_name(u32 id);
const char* command_group(u32 id);
b32 command_enabled(App* app, u32 id);
void command_run(App* app, u32 id);

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
// The Node scope holds each of `nodes` in a container, in order; the other scopes ignore them.
// Returns the size written, or 0 if it did not fit.
u32 save_write_scope(App* app, SaveScope scope, const NvNodeId* nodes, u32 node_count, void* buffer, u32 capacity);
// Applies the fields present. Returns 0, changing nothing, if the bytes are malformed.
b32 save_apply_scope(App* app, SaveScope scope, const NvNodeId* nodes, u32 node_count, const void* bytes, u32 size);
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

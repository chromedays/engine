#pragma once

// The nv app: a showcase scene with a planet and moon and an animated character, a stress scene
// that loads the engine with many objects (docs/specs/stress.md), and an editor panel to inspect
// and change them. main.c owns the showcase and the frame, stress.c the stress scene and its
// benchmark, ui.c the panel.

#include <nv/anim.h>
#include <nv/gltf.h>
#include <nv/gpu.h>
#include <nv/imgui.h>
#include <nv/renderer.h>
#include <nv/scene.h>
#include <nv/window.h>

#define APP_MAX_CLIPS 16

#define STRESS_MAX_GRID     4000
#define STRESS_MAX_CHAIN    1000
#define STRESS_MAX_CROWD    60
#define STRESS_MAX_COLORS   200
#define STRESS_MAX_CHURN    256
#define STRESS_MAX_STEPS    16

typedef enum SceneKind {
    SCENE_SHOWCASE,
    SCENE_STRESS,
    SCENE_COUNT,
} SceneKind;

// What the editor shows of one scene: its camera orbit and its selection.
typedef struct SceneView {
    NvScene* scene;
    NvNodeId camera;
    NvNodeId focus;    // orbited while nothing is selected
    NvNodeId selected; // shown in the inspector; the camera orbits it
    f32 camera_yaw;      // radians
    f32 camera_pitch;    // radians, looking down
    f32 camera_distance; // meters
} SceneView;

// CPU time of the frame's stages, in milliseconds.
typedef struct FrameTimes {
    f64 frame; // from one frame to the next
    f64 anim;  // nv_anim_update_scene
    f64 scene; // nv_scene_update
    f64 draw;  // nv_renderer_draw
    f64 ui;    // building and recording ImGui
    f64 gpu;   // the scene pass on the GPU; 0 where the browser has no timestamps
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

    // Editor
    b32 open_inspector; // switch to the Inspector tab on the next frame
    b32 open_stress;    // switch to the Stress tab on the next frame
    bool show_bones;
} App;

// main.c
NvClipId app_find_clip(App* app, const char* name);
NvClipId app_regular_clip(App* app, NvClipId clip); // the in-place clip with the same name
void app_play(App* app, NvClipId clip);             // crossfades, with root motion while it is on
void app_jump(App* app);
void app_back_to_center(App* app);
// The animator a node has, or that its first animated child has (a character root); 0 = none.
NvAnimatorId app_node_animator(NvScene* scene, NvNodeId id);
void app_show_scene(App* app, SceneKind kind);
NvMeshId app_box_mesh(App* app, NvVec3 half);
SceneView* app_view(App* app); // the shown scene's view

// stress.c
void stress_build(App* app);
void stress_update(App* app, f32 dt);  // before nv_anim_update_scene
void stress_after_frame(App* app);     // after the frame's times are known: runs the benchmark
void stress_draw_bones(App* app);
u32 stress_live_nodes(NvScene* scene);
void stress_ui(App* app);

// ui.c
void app_build_ui(App* app, NvRect panel);

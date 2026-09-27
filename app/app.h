#pragma once

// The nv app: one scene with a planet and moon and an animated character, and an editor panel
// to inspect and change it. main.c owns the scene and the frame; ui.c draws the panel.

#include <nv/anim.h>
#include <nv/gltf.h>
#include <nv/gpu.h>
#include <nv/imgui.h>
#include <nv/renderer.h>
#include <nv/scene.h>
#include <nv/window.h>

#define APP_MAX_CLIPS 16

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

    NvNodeId camera;

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
    NvNodeId selected;   // shown in the inspector; the camera orbits it
    b32 open_inspector;  // switch to the Inspector tab on the next frame
    bool show_bones;
    f32 camera_yaw;      // radians
    f32 camera_pitch;    // radians, looking down
    f32 camera_distance; // meters
} App;

// main.c
NvClipId app_find_clip(App* app, const char* name);
NvClipId app_regular_clip(App* app, NvClipId clip); // the in-place clip with the same name
void app_play(App* app, NvClipId clip);             // crossfades, with root motion while it is on
void app_jump(App* app);
void app_back_to_center(App* app);
NvAnimatorId app_node_animator(App* app, NvNodeId id); // the animator a node has or owns; 0 = none

// ui.c
void app_build_ui(App* app, NvRect panel);

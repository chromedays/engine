#pragma once

// The auto-battler executable's state (docs/specs/battle.md, "Executable"): the window, GPU and renderer, the battle, and
// what the view keeps to draw it. Nothing here is the editor app's (app/): this executable links only the engine.

#include "battle.h"

#include <engine/camera.h>
#include <engine/imgui.h>
#include <engine/log.h>
#include <engine/renderer.h>
#include <engine/strings.h>
#include <engine/window.h>

// Where the panel goes (CSS pixels): beside the viewport on a wide screen, under it on a tall one.
#define PANEL_WIDTH  300.0f
#define PANEL_HEIGHT_SHARE 0.42f

#define CAMERA_MIN_PITCH    (20.0f * NV_PI / 180.0f)
#define CAMERA_MAX_PITCH    (85.0f * NV_PI / 180.0f)
#define CAMERA_MIN_DISTANCE 15.0f
#define CAMERA_MAX_DISTANCE 140.0f
#define CAMERA_RADIANS_PER_PIXEL 0.008f

// The field, in meters.
#define FIELD_WIDTH  (BATTLE_GRID_WIDTH * BATTLE_CELL_SIZE)
#define FIELD_LENGTH (BATTLE_GRID_LENGTH * BATTLE_CELL_SIZE)

typedef struct Layout {
    NvRect viewport; // framebuffer pixels: where the field is drawn and the camera and taps are read
    NvRect panel;
    NvSceneOutput scene;
} Layout;

typedef struct Effects {
    NvVfxEffectId explosion, shield_hit, muzzle;
    NvVfxLineStyle trail;
    NvVfxDecalStyle scorch;
} Effects;

typedef struct Game {
    NvArena permanent, scratch;
    NvWindow window;
    NvGpu gpu;
    NvRenderer renderer;
    NvImgui imgui;
    NvVfx vfx;

    NvScene* scene;
    NvNodeId camera;
    NvOrbitCamera orbit;
    NvResolution resolution;
    Layout layout;

    BattleDefs defs;
    b32 defs_ok;
    Battle battle;
    f32 accumulator; // seconds of game time not yet run as ticks
    f32 speed;       // game time per real second: 0.5, 1, 2 or 4 (docs/specs/battle.md, "Time controls")
    b32 paused;      // the game clock stops; the speed is kept for when it goes on
    s32 step;        // asked by the panel for this frame: +1 one tick forward, -1 one tick back (both pause)
    f64 last_time;
    char download_text[48];

    // The view (battle_view.c)
    Effects effects;
    NvMeshId unit_meshes[BATTLE_MAX_UNIT_DEFS];
    NvMaterialId team_materials[2];
    NvMeshId shell_mesh;
    NvMaterialId shell_material;
    NvNodeId unit_nodes[BATTLE_MAX_UNITS];       // by slot; made when first needed
    u32 unit_nodes_made;                         // slots 1 to this have a node
    NvNodeId shell_nodes[BATTLE_MAX_PROJECTILES]; // by index in Battle.projectiles
    u32 shell_nodes_made;
    u32 selected_def; // the unit type the next tap places
    s32 hover_cell_x, hover_cell_row; // under the mouse pointer, in the player's zone; -1 = none
} Game;

// main.c
void game_layout(Game* game);

// battle_view.c
void view_build(Game* game); // meshes, materials, the field, the camera and the effects, once
void view_input(Game* game); // the camera from drags and the wheel; taps place and remove units
void view_on_tick(Game* game); // after each battle_tick: effects for its events and shells; clears the events
void view_update(Game* game, f32 game_dt); // every frame: nodes follow the units and shells, lines, effects' clock
void view_panel(Game* game);  // the Battle panel
void view_draw_build_label(Game* game);

// strings.c
void game_strings_init(void);

#pragma once

// The auto-battler executable's state (docs/specs/battle.md, "Executable"): the window, GPU and renderer, the battle, and
// what the view keeps to draw it. Nothing here is the sandbox's (sandbox/): this executable links only the engine.

#include "battle.h"

#include <engine/camera.h>
#include <engine/imgui.h>
#include <engine/local_file.h>
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
// The shadow distance is the camera's distance times this. From the starting camera the field's far corners are about 1.4
// times its distance away, before the fade that starts at 0.9 of the shadow distance, so every unit is shadowed; and the
// map's texels keep about the same size on screen as the camera zooms.
#define SHADOW_DISTANCE_PER_CAMERA_DISTANCE 1.6f

// The camera starts CAMERA_START_DISTANCE away from a field CAMERA_REFERENCE_LENGTH meters long; a longer or shorter field
// (the rules' `grid` and `cell_size`) scales that, and the far limits with it.
#define CAMERA_START_DISTANCE   104.0f
#define CAMERA_REFERENCE_LENGTH 96.0f
#define CAMERA_FAR_Z            400.0f

// The project file (docs/specs/abproj.md, "Loading and saving"): the one packed with the game, and the most a local one may be.
#define PROJECT_BUILT_IN  "default.abproj"
#define PROJECT_EXTENSION ".abproj"
#define PROJECT_MAX_SIZE  NV_KILOBYTES(256)

// What the panel's project buttons do (game_project_action).
typedef enum ProjectAction { PROJECT_OPEN, PROJECT_RELOAD, PROJECT_SAVE, PROJECT_SAVE_AS } ProjectAction;

// The field's planes.
enum { FIELD_GROUND, FIELD_PLAYER_ZONE, FIELD_ENEMY_ZONE, FIELD_PLANE_COUNT };

// What the viewport and the panel show (docs/specs/abproj.md, "Editing units"): the battle, or the unit editor's one unit.
typedef enum GameMode { MODE_BATTLE, MODE_UNITS } GameMode;

// The unit editor's camera and ground.
#define UNIT_CAMERA_MIN_DISTANCE 1.0f
#define UNIT_CAMERA_MAX_DISTANCE 150.0f
#define UNIT_GROUND_HALF         100.0f // meters from the unit to the ground's edge

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
    GameMode mode;
    f32 field_width, field_length; // meters, from the rules

    // The project the defs were read from. Its text is kept, so Save writes it back with its comments; the Rules section
    // changes only the values it edits in it.
    char* project_text; // PROJECT_MAX_SIZE bytes of permanent memory and a NUL after the text
    umm project_size;   // 0 while no good project is in place
    b32 project_edited; // the panel (Rules, Units mode) changed the text since it was read or saved
    char project_name[128];
    b32 kept_is_project; // the local file the browser keeps (NvLocalFile.kept) is this project's, so Save may write it
    NvLocalFile local_file;
    char project_message[320]; // what the last open, reload or save did
    b32 project_message_bad;
    u32 project_loads;          // projects put in place, the packed one included
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
    u32 unit_meshes_made;
    NvNodeId field_nodes[FIELD_PLANE_COUNT];
    NvMeshId field_meshes[FIELD_PLANE_COUNT]; // kept here, since Units mode takes them off the nodes
    NvNodeId preview_node, preview_ground;    // Units mode's unit and ground
    NvMeshId preview_ground_mesh;
    NvOrbitCamera unit_orbit;                 // Units mode's camera
    b32 unit_orbit_set;
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
// Reads `text` as a project. A good one is put in place (the battle back in deployment, the view remade for it) and its text
// kept; a bad one is reported in the panel and changes nothing, unless no good project is in place yet. True when it was good.
b32 game_load_project(Game* game, const char* name, const char* text, umm size);
// The panel's Rules section and Units mode (docs/specs/abproj.md, "Editing the rules", "Editing units"): whether values can
// be changed now, and changing the value at `where` to `values` in the project's text. A good result is put in place at once,
// keeping the player's deployment where it still fits; a bad one changes nothing and says why in the panel. True when the
// change was made.
b32 game_values_editable(const Game* game);
b32 game_set_value(Game* game, DefsKey where, const f64* values, u32 count);
// Switches what the viewport and panel show; Units only in deployment. True when it switched.
b32 game_set_mode(Game* game, GameMode mode);
// Whether a project button can be pressed now, and pressing it (it starts a dialog, read or write; false when it cannot).
b32 game_project_action_allowed(const Game* game, ProjectAction action);
b32 game_project_action(Game* game, ProjectAction action);

// battle_view.c
void view_build(Game* game); // materials, the shell mesh, the field's nodes, the camera and the effects, once
void view_apply_project(Game* game); // the field, the unit meshes and the camera for the project in place
void view_apply_edit(Game* game);    // the field and unit meshes after a value changed; the camera starts over only if the field's size did
void view_input(Game* game); // the camera from drags and the wheel; taps place and remove units
void view_on_tick(Game* game); // after each battle_tick: effects for its events and shells; clears the events
void view_update(Game* game, f32 game_dt); // every frame: nodes follow the units and shells, lines, effects' clock
void view_panel(Game* game);  // the Battle panel
void view_draw_build_label(Game* game);

// strings.c
void game_strings_init(void);

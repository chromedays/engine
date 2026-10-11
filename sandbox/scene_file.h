#pragma once

// The scene file (docs/specs/scene_file.md): the text of a .nvscene read into plain data. No GPU, ImGui or scene is touched, so
// tests run it under Node; scenes.c builds the scene from what is read here.
//
// A bad line never stops a read: every error is reported (nv_log, source "scene", with the file name and line) and the read
// fails at its end. Ids (`<id>` in the spec) are names used only inside the file; the references between things are resolved
// at the end of the read, into indices.

#include <engine/base.h>
#include <engine/math.h>
#include <engine/scene.h>

#define NVSCENE_VERSION 1

#define SCENE_ID_SIZE 32            // an id or a name, with its NUL
#define SCENE_PATH_SIZE 96          // a file path, with its NUL
#define SCENE_MAX_MODELS 32
#define SCENE_MAX_SHAPES 8          // shape lines in a model
#define SCENE_MAX_PALETTES 4
#define SCENE_MAX_PALETTE_COLORS 256
#define SCENE_MAX_NODES 256         // file nodes (not the ones generators make)
#define SCENE_MAX_DEPTH 8           // nested nodes; the save's node paths are longer than this
#define SCENE_MAX_BEHAVIORS 64
#define SCENE_MAX_STEPS 16
#define SCENE_MAX_STEP_LINES 4
#define SCENE_MAX_VERTICES 4096     // of a shape model

//
// The keys of a block
//

typedef enum SceneFieldKind {
    FIELD_NUMBER,      // `count` f32
    FIELD_INTEGER,     // `count` u32
    FIELD_BOOL,        // one u32, 0 or 1
    FIELD_DEGREES,     // `count` f32: degrees (or degrees a second) in the file, radians once read; the range is in degrees
    FIELD_NAME,        // char[SCENE_ID_SIZE]
    FIELD_NAME_PAIR,   // char[2][SCENE_ID_SIZE]
    FIELD_TEXT,        // quoted text, char[size]
    FIELD_WORD,        // one of `words`, as a u32 (the word's place + `first_word`)
    FIELD_TEXT_NAME,   // a RootMotionDesc: quoted text, then a name
    FIELD_JUMP,        // a JumpDesc: three names, then a number (seconds)
} SceneFieldKind;

// One key of a block: what a line `key value...` reads into, at `offset` in the block's struct. The same table makes the reading,
// and (scenes.c, behaviors.c) the UI, the search, the save and the undo labels.
typedef struct SceneField {
    const char* key;
    SceneFieldKind kind;
    u32 offset;
    u32 count;      // numbers on the line, for the number kinds
    f64 min, max;   // each number's range, in the file's units; 1e30 for none
    b32 min_open;   // `min` itself is not allowed
    b32 required;   // otherwise the key may be left out
    b32 parameter;  // a behavior's key that the UI changes, the save keeps and undo takes back; the others are structure
    f64 fallback;   // an optional number's value when it is left out, in run-time units
    const char* const* words;
    u32 word_count;
    u32 first_word;
    u32 size;       // FIELD_TEXT's bytes
    // A parameter's face in the UI, the search and the undo labels:
    const char* label;    // "Speed"; the English text, through T()
    const char* keywords; // search words
    const char* format;   // the slider's printf format ("%.2f rad/s")
    f64 ui_min, ui_max;   // the slider's range, in run-time units; both 0: the file's range
    b32 logarithmic;
} SceneField;

typedef struct RootMotionDesc {
    char path[SCENE_PATH_SIZE];
    char joint[SCENE_ID_SIZE];
} RootMotionDesc;

typedef struct JumpDesc {
    char clip[3][SCENE_ID_SIZE]; // start, air, land; [0] is empty without a jump
    f32 air_time;                // seconds
} JumpDesc;

//
// Behaviors
//

typedef enum BehaviorKind {
    BEHAVIOR_SPIN,
    BEHAVIOR_CHARACTER,
    BEHAVIOR_GRID,
    BEHAVIOR_CHAIN,
    BEHAVIOR_CROWD,
    BEHAVIOR_EFFECTS,
    BEHAVIOR_COUNT,
} BehaviorKind;

// Speeds are radians a second; angles radians. A reference that is empty is not given.
typedef struct SpinDesc {
    NvVec3 axis; // unit length
    f32 speed;
} SpinDesc;

typedef struct CharacterDesc {
    char look_target[2][SCENE_ID_SIZE]; // the joint, then the target node's id; [0] is empty without a look target
    JumpDesc jump;
    char clip[SCENE_ID_SIZE]; // empty: the model's first `_Loop` clip
    f32 speed;
    f32 fade; // seconds
    char blend[SCENE_ID_SIZE]; // empty: the model's first clip
    f32 blend_weight;
    u32 root_motion;
    f32 turn;
    u32 look_at;
    s32 look_node_index; // into SceneDesc.nodes, -1 without a look target
} CharacterDesc;

typedef struct GridDesc {
    char model[SCENE_ID_SIZE];
    char palette[SCENE_ID_SIZE]; // empty for a file model
    u32 max;
    u32 columns;
    f32 spacing, height, scale;
    u32 on;
    u32 count;
    u32 colors_on;
    u32 colors;
    u32 churn_on;
    u32 churn;
    s32 model_index, palette_index; // -1: none
} GridDesc;

typedef struct ChainDesc {
    char model[SCENE_ID_SIZE];
    char palette[SCENE_ID_SIZE];
    u32 stride;
    u32 max;
    NvVec3 link;
    f32 twist, wobble;
    u32 on;
    u32 count;
    s32 model_index, palette_index;
} ChainDesc;

typedef struct CrowdDesc {
    char model[SCENE_ID_SIZE];
    u32 max;
    u32 columns;
    f32 spacing;
    f32 offset; // seconds
    u32 on;
    u32 count;
    s32 model_index;
} CrowdDesc;

typedef struct EffectsDesc {
    f32 area[2]; // x and z, m
    u32 on;
    u32 particles, explosions, missiles, beams, decals;
} EffectsDesc;

typedef struct BehaviorDesc {
    BehaviorKind kind;
    s32 node; // index into SceneDesc.nodes
    u32 line;
    union {
        SpinDesc spin;
        CharacterDesc character;
        GridDesc grid;
        ChainDesc chain;
        CrowdDesc crowd;
        EffectsDesc effects;
    };
} BehaviorDesc;

typedef struct BehaviorDef {
    const char* name; // the block's key in the file
    const SceneField* fields;
    u32 field_count;
    b32 generator; // makes nodes (at most one per node)
} BehaviorDef;

const BehaviorDef* scene_behavior_def(BehaviorKind kind);

//
// The rest of the file
//

typedef enum ShapeKind {
    SHAPE_BOX,
    SHAPE_PLANE,
    SHAPE_SPHERE,
    SHAPE_CYLINDER,
    SHAPE_CONE,
    SHAPE_CAPSULE,
    SHAPE_TORUS,
    SHAPE_COUNT,
} ShapeKind;

typedef struct ShapeDesc {
    ShapeKind kind;
    f32 value[7]; // the line's numbers, in the order of the nv_mesh_append_* function's arguments
} ShapeDesc;

typedef struct ModelDesc {
    char id[SCENE_ID_SIZE];
    u32 line;
    b32 is_file;
    ShapeDesc shapes[SCENE_MAX_SHAPES]; // a shape model
    u32 shape_count;
    char file[SCENE_PATH_SIZE]; // a file model
    char clips[SCENE_PATH_SIZE];
    RootMotionDesc root_motion;
} ModelDesc;

typedef struct PaletteDesc {
    char id[SCENE_ID_SIZE];
    u32 line;
    f32 base[3];
    u32 wheel; // colors 1 to wheel - 1
} PaletteDesc;

typedef struct ViewDesc {
    char camera[SCENE_ID_SIZE];
    f32 yaw, pitch, distance;
    NvVec3 target;
    char focus[SCENE_ID_SIZE];  // empty: none
    char select[SCENE_ID_SIZE];
    s32 camera_index, focus_index, select_index; // into SceneDesc.nodes, -1: none
} ViewDesc;

typedef struct NodeDesc {
    char id[SCENE_ID_SIZE];
    char name[NV_NODE_NAME_MAX];
    u32 line;
    s32 parent; // into SceneDesc.nodes, -1 for a top-level node; a parent comes before its children
    NvVec3 position;
    NvQuat rotation;
    NvVec3 scale;
    NvVec3 euler; // the `euler` line's angles, until they become `rotation`
    char model[SCENE_ID_SIZE]; // empty: no model
    s32 model_index;           // into SceneDesc.models, -1: none
    b32 has_color;
    f32 color[3];
    b32 double_sided;
    b32 visible;
    b32 has_camera;
    NvCamera camera;
    b32 has_light;
    NvLight light;
    char attach[SCENE_ID_SIZE]; // empty: not attached
} NodeDesc;

// One line of a benchmark step: `<node id> <key> <value>`.
typedef struct StepLine {
    s32 behavior; // into SceneDesc.behaviors
    u32 field;    // into the behavior's SceneField table: a parameter with one number
    u32 value;    // the number's bits (an f32 or a u32, as the field's kind says)
} StepLine;

typedef struct StepDesc {
    char name[SCENE_ID_SIZE];
    StepLine lines[SCENE_MAX_STEP_LINES];
    u32 line_count;
} StepDesc;

typedef struct SceneDesc {
    char title[SCENE_ID_SIZE];
    ViewDesc view;
    PaletteDesc palettes[SCENE_MAX_PALETTES];
    u32 palette_count;
    ModelDesc models[SCENE_MAX_MODELS];
    u32 model_count;
    NodeDesc nodes[SCENE_MAX_NODES];
    u32 node_count;
    BehaviorDesc behaviors[SCENE_MAX_BEHAVIORS];
    u32 behavior_count;
    b32 has_benchmark;
    f32 warmup, measure; // seconds
    StepDesc steps[SCENE_MAX_STEPS];
    u32 step_count;
} SceneDesc;

typedef struct SceneRead {
    b32 ok;
    u32 error_count;       // all of them; only the first few are logged
    char first_error[160]; // "showcase.nvscene:12: unknown key 'healt'"
} SceneRead;

// Reads `text` (a .nvscene file) into `desc`, which is cleared first (it is large: do not put it on the stack).
SceneRead scene_read(SceneDesc* desc, const char* file_name, const char* text, umm size);

// The index of the node, model or palette with that id, or -1.
s32 scene_find_node(const SceneDesc* desc, const char* id);
s32 scene_find_model(const SceneDesc* desc, const char* id);
s32 scene_find_palette(const SceneDesc* desc, const char* id);

// The generator (a behavior that makes nodes) of a node, or -1.
s32 scene_find_generator(const SceneDesc* desc, s32 node);

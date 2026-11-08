// Reads a scene file (docs/specs/scene_file.md): line-based text, a block per indentation level, every key of a block in a table
// (SceneField) that says what it reads into, its range and whether it may be left out. No GPU or scene is used, so tests give it
// strings. A bad line never stops the read: every error is reported and the read fails at the end.

#include "sandbox.h"

#include <engine/log.h>
#include <engine/mesh.h>
#include <engine/text.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define NO_LIMIT 1e30
#define ANY      .min = -NO_LIMIT, .max = NO_LIMIT
#define PI_F64 3.14159265358979323846

// A row of a table: `base`'s `member` is what a line `key value...` reads into.
#define ROW(key_, kind_, base, member, ...) {.key = (key_), .kind = (kind_), .offset = offsetof(base, member), __VA_ARGS__}

//
// The keys of every block
//

global const SceneField view_fields[] = {
    ROW("camera", FIELD_NAME, ViewDesc, camera, .required = 1),
    ROW("yaw", FIELD_DEGREES, ViewDesc, yaw, .count = 1, .min = -360, .max = 360, .required = 1),
    ROW("pitch", FIELD_DEGREES, ViewDesc, pitch, .count = 1, .min = -90, .max = 90, .required = 1),
    ROW("distance", FIELD_NUMBER, ViewDesc, distance, .count = 1, .min = 0, .max = 1000, .min_open = 1, .required = 1),
    ROW("target", FIELD_NUMBER, ViewDesc, target, .count = 3, ANY),
    ROW("focus", FIELD_NAME, ViewDesc, focus),
    ROW("select", FIELD_NAME, ViewDesc, select),
};

global const SceneField lighting_fields[] = {
    ROW("ambient", FIELD_NUMBER, LightingDesc, ambient, .count = 3, .min = 0, .max = 1),
};

enum { MODEL_FILE, MODEL_CLIPS, MODEL_ROOT_MOTION };
global const SceneField model_fields[] = {
    ROW("file", FIELD_TEXT, ModelDesc, file, .size = SCENE_PATH_SIZE),
    ROW("clips", FIELD_TEXT, ModelDesc, clips, .size = SCENE_PATH_SIZE),
    ROW("root_motion_clips", FIELD_TEXT_NAME, ModelDesc, root_motion),
};

enum { NODE_NAME, NODE_POSITION, NODE_ROTATION, NODE_EULER, NODE_SCALE };
global const SceneField node_fields[] = {
    ROW("name", FIELD_TEXT, NodeDesc, name, .size = NV_NODE_NAME_MAX),
    ROW("position", FIELD_NUMBER, NodeDesc, position, .count = 3, ANY),
    ROW("rotation", FIELD_NUMBER, NodeDesc, rotation, .count = 4, ANY),
    ROW("euler", FIELD_DEGREES, NodeDesc, euler, .count = 3, .min = -3600, .max = 3600),
    ROW("scale", FIELD_NUMBER, NodeDesc, scale, .count = 3, .min = 0, .max = NO_LIMIT, .min_open = 1),
};

enum { USE_COLOR, USE_DOUBLE_SIDED, USE_VISIBLE };
global const SceneField model_use_fields[] = {
    ROW("color", FIELD_NUMBER, NodeDesc, color, .count = 3, .min = 0, .max = 1),
    ROW("double_sided", FIELD_BOOL, NodeDesc, double_sided, .count = 1),
    ROW("visible", FIELD_BOOL, NodeDesc, visible, .count = 1),
};

global const char* const projection_words[] = {"perspective", "orthographic"};
global const char* const light_words[] = {"directional", "point", "spot"};

enum { CAMERA_PROJECTION, CAMERA_FOV, CAMERA_HEIGHT, CAMERA_NEAR, CAMERA_FAR };
global const SceneField camera_fields[] = {
    ROW("projection", FIELD_WORD, NodeDesc, camera.projection, .words = projection_words, .word_count = 2,
        .first_word = NV_PROJECTION_PERSPECTIVE, .required = 1),
    ROW("fov", FIELD_DEGREES, NodeDesc, camera.fov_y, .count = 1, .min = 1, .max = 179),
    ROW("height", FIELD_NUMBER, NodeDesc, camera.ortho_height, .count = 1, .min = 0, .max = NO_LIMIT, .min_open = 1),
    ROW("near", FIELD_NUMBER, NodeDesc, camera.near_z, .count = 1, .min = 0, .max = NO_LIMIT, .min_open = 1, .required = 1),
    ROW("far", FIELD_NUMBER, NodeDesc, camera.far_z, .count = 1, .min = 0, .max = NO_LIMIT, .min_open = 1, .required = 1),
};

enum { LIGHT_TYPE, LIGHT_COLOR, LIGHT_INTENSITY, LIGHT_RANGE, LIGHT_CONE };
global const SceneField light_fields[] = {
    ROW("type", FIELD_WORD, NodeDesc, light.type, .words = light_words, .word_count = 3, .first_word = NV_LIGHT_DIRECTIONAL,
        .required = 1),
    ROW("color", FIELD_NUMBER, NodeDesc, light.color, .count = 3, .min = 0, .max = NO_LIMIT),
    ROW("intensity", FIELD_NUMBER, NodeDesc, light.intensity, .count = 1, .min = 0, .max = NO_LIMIT),
    ROW("range", FIELD_NUMBER, NodeDesc, light.range, .count = 1, .min = 0, .max = NO_LIMIT, .min_open = 1),
    ROW("cone", FIELD_DEGREES, NodeDesc, light.inner_cone, .count = 2, .min = 0, .max = 180),
};

// Behaviors: a parameter is changed by the UI, kept by the save and taken back by undo; the other keys are structure.
global const SceneField spin_fields[] = {
    ROW("axis", FIELD_NUMBER, SpinDesc, axis, .count = 3, ANY, .required = 1),
    ROW("speed", FIELD_DEGREES, SpinDesc, speed, .count = 1, .min = -171.9, .max = 171.9, .required = 1, .parameter = 1,
        .label = "Speed", .keywords = "spin orbit rotate planet moon", .format = "%.2f rad/s", .ui_min = -3, .ui_max = 3),
};

global const SceneField character_fields[] = {
    ROW("look_target", FIELD_NAME_PAIR, CharacterDesc, look_target),
    ROW("jump", FIELD_JUMP, CharacterDesc, jump, .min = 0, .max = 10, .min_open = 1),
    ROW("clip", FIELD_NAME, CharacterDesc, clip, .parameter = 1, .label = "Clip", .keywords = "animation play"),
    ROW("speed", FIELD_NUMBER, CharacterDesc, speed, .count = 1, .min = 0, .max = 2, .parameter = 1, .fallback = 1,
        .label = "Speed", .keywords = "playback", .format = "%.2fx"),
    ROW("fade", FIELD_NUMBER, CharacterDesc, fade, .count = 1, .min = 0, .max = 1, .parameter = 1, .fallback = 0.3,
        .label = "Fade", .keywords = "crossfade blend", .format = "%.2f s"),
    ROW("blend", FIELD_NAME, CharacterDesc, blend, .parameter = 1, .label = "Blend", .keywords = "clip"),
    ROW("blend_weight", FIELD_NUMBER, CharacterDesc, blend_weight, .count = 1, .min = 0, .max = 1, .parameter = 1,
        .label = "Weight", .keywords = "blend", .format = "%.2f"),
    ROW("root_motion", FIELD_BOOL, CharacterDesc, root_motion, .count = 1, .parameter = 1, .label = "Root motion", .keywords = "walk"),
    ROW("turn", FIELD_DEGREES, CharacterDesc, turn, .count = 1, .min = -85.95, .max = 85.95, .parameter = 1, .fallback = 0.6,
        .label = "Turn", .keywords = "root motion", .format = "%.2f rad/s", .ui_min = -1.5, .ui_max = 1.5),
    ROW("look_at", FIELD_BOOL, CharacterDesc, look_at, .count = 1, .parameter = 1, .label = "Look at target", .keywords = "head ik aim"),
};

#define TABLE(fields) fields, (u32)NV_ARRAY_COUNT(fields)

global const BehaviorDef behavior_defs[BEHAVIOR_COUNT] = {
    [BEHAVIOR_SPIN] = {"spin", TABLE(spin_fields)},
    [BEHAVIOR_CHARACTER] = {"character", TABLE(character_fields)},
};

const BehaviorDef* scene_behavior_def(BehaviorKind kind)
{
    NV_ASSERT(kind < BEHAVIOR_COUNT);
    return &behavior_defs[kind];
}

// A union member's address: every member of a BehaviorDesc starts at the same place.
internal void* behavior_data(BehaviorDesc* behavior)
{
    return &behavior->spin;
}

//
// Reading a line into a field
//

typedef enum BlockKind {
    BLOCK_ROOT,
    BLOCK_SKIP, // the inside of a block that was already reported
    BLOCK_VIEW,
    BLOCK_LIGHTING,
    BLOCK_MODEL,
    BLOCK_NODE,
    BLOCK_MODEL_USE, // the `model <id>` line of a node and the keys under it
    BLOCK_CAMERA,
    BLOCK_LIGHT,
    BLOCK_BEHAVIOR,
} BlockKind;

// An open block. Its lines are indented deeper than its header, all by the same amount.
typedef struct Block {
    BlockKind kind;
    s32 header_indent;
    s32 child_indent; // -1 until the first line inside it
    u32 header_line;
    u32 seen; // a bit per key of its table that was given
    s32 index; // the model, node or behavior being read
} Block;

// The top-level statements, in the order they must come in.
typedef enum Top { TOP_NONE, TOP_VERSION, TOP_TITLE, TOP_VIEW, TOP_LIGHTING, TOP_MODEL, TOP_NODE } Top;

typedef struct SceneReader {
    NvTextReader text;
    SceneDesc* desc;
    Block stack[SCENE_MAX_DEPTH + 4]; // root, the nested nodes, and a node's component
    u32 depth;
    Top last;
    u32 seen_top; // a bit per Top given
    b32 first_statement;
    b32 stop; // nothing more can be read (a newer format)
} SceneReader;

NV_PRINTF_FORMAT(3, 4) internal void fail(SceneReader* reader, u32 line, const char* format, ...)
{
    char message[128];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    nv_text_report(&reader->text, line, "%s", message);
}

internal u32 field_values(const SceneField* field)
{
    switch (field->kind) {
    case FIELD_NUMBER:
    case FIELD_INTEGER:
    case FIELD_DEGREES:
        return field->count;
    case FIELD_BOOL:
    case FIELD_NAME:
    case FIELD_TEXT:
    case FIELD_WORD:
        return 1;
    case FIELD_NAME_PAIR:
    case FIELD_TEXT_NAME:
        return 2;
    case FIELD_JUMP:
        return 4;
    }
    return 1;
}

internal const SceneField* find_field(const SceneField* fields, u32 count, NvTextToken key)
{
    for (u32 i = 0; i < count; ++i)
        if (nv_text_token_is(key, fields[i].key))
            return &fields[i];
    return NULL;
}

internal b32 in_range(const SceneField* field, f64 value)
{
    b32 above = field->min_open ? value > field->min : value >= field->min;
    return above && value <= field->max;
}

internal void report_range(SceneReader* reader, u32 line, const SceneField* field)
{
    char text[64] = "";
    u32 used = 0;
    if (field->min > -NO_LIMIT)
        used += (u32)snprintf(text + used, sizeof(text) - used, "%s %.10g", field->min_open ? ">" : ">=", field->min);
    if (field->max < NO_LIMIT)
        used += (u32)snprintf(text + used, sizeof(text) - used, "%s<= %.10g", used ? " and " : "", field->max);
    fail(reader, line, "'%s' must be %s", field->key, text);
}

// Writes `value` as the field's kind says, at `destination`, for the i-th of the field's numbers.
internal void store_number(const SceneField* field, u8* destination, u32 i, f64 value)
{
    if (field->kind == FIELD_INTEGER || field->kind == FIELD_BOOL) {
        u32 stored = (u32)value;
        memcpy(destination + i * sizeof(u32), &stored, sizeof(stored));
    } else {
        f32 stored = (f32)(field->kind == FIELD_DEGREES ? value * (PI_F64 / 180.0) : value);
        memcpy(destination + i * sizeof(f32), &stored, sizeof(stored));
    }
}

// A field left out gets its fallback (a number) or stays as it was.
internal void apply_fallbacks(const SceneField* fields, u32 count, void* base)
{
    for (u32 f = 0; f < count; ++f) {
        const SceneField* field = &fields[f];
        if (field->required)
            continue;
        // The fallback is in run-time units, so a degrees field takes it as radians, without the file's conversion.
        u8* destination = (u8*)base + field->offset;
        if (field->kind == FIELD_NUMBER || field->kind == FIELD_DEGREES) {
            f32 value = (f32)field->fallback;
            for (u32 i = 0; i < field->count; ++i)
                memcpy(destination + i * sizeof(f32), &value, sizeof(value));
        } else if (field->kind == FIELD_INTEGER || field->kind == FIELD_BOOL) {
            u32 value = (u32)field->fallback;
            for (u32 i = 0; i < (field->kind == FIELD_BOOL ? 1u : field->count); ++i)
                memcpy(destination + i * sizeof(u32), &value, sizeof(value));
        }
    }
}

// Reads a line `key value...` into the struct at `base`. `seen` holds a bit per field already given in this block.
internal void read_field(SceneReader* reader, const NvTextLine* line, const SceneField* field, u32 index, void* base, u32* seen)
{
    if (*seen & (1u << index)) {
        fail(reader, line->number, "'%s' is given twice in this block", field->key);
        return;
    }
    *seen |= 1u << index; // a bad value is not "missing" on top of that
    u32 values = field_values(field);
    if (line->token_count != 1 + values) {
        fail(reader, line->number, "'%s' takes %u value%s", field->key, values, values > 1 ? "s" : "");
        return;
    }
    NvTextReader* text = &reader->text;
    u8* destination = (u8*)base + field->offset;
    const NvTextToken* token = &line->tokens[1];
    switch (field->kind) {
    case FIELD_NUMBER:
    case FIELD_DEGREES:
    case FIELD_INTEGER:
    case FIELD_BOOL: {
        f64 numbers[4];
        b32 integer = field->kind == FIELD_INTEGER || field->kind == FIELD_BOOL;
        for (u32 i = 0; i < values; ++i) {
            NvTextNumber number = nv_text_parse_number(text, line->number, token[i], integer);
            if (!number.ok)
                return;
            numbers[i] = number.value;
        }
        for (u32 i = 0; i < values; ++i) {
            if (field->kind == FIELD_BOOL ? (numbers[i] != 0 && numbers[i] != 1) : !in_range(field, numbers[i])) {
                if (field->kind == FIELD_BOOL)
                    fail(reader, line->number, "'%s' must be 0 or 1", field->key);
                else
                    report_range(reader, line->number, field);
                return;
            }
        }
        for (u32 i = 0; i < values; ++i)
            store_number(field, destination, i, numbers[i]);
    } break;
    case FIELD_NAME:
        nv_text_parse_name(text, line->number, token[0], (char*)destination, SCENE_ID_SIZE);
        break;
    case FIELD_NAME_PAIR: {
        char names[2][SCENE_ID_SIZE];
        if (nv_text_parse_name(text, line->number, token[0], names[0], SCENE_ID_SIZE) &&
            nv_text_parse_name(text, line->number, token[1], names[1], SCENE_ID_SIZE))
            memcpy(destination, names, sizeof(names));
    } break;
    case FIELD_TEXT:
        nv_text_parse_text(text, line->number, token[0], (char*)destination, field->size);
        break;
    case FIELD_WORD: {
        for (u32 w = 0; w < field->word_count; ++w) {
            if (nv_text_token_is(token[0], field->words[w])) {
                u32 value = field->first_word + w;
                memcpy(destination, &value, sizeof(value));
                return;
            }
        }
        char kinds[64] = "";
        for (u32 w = 0, used = 0; w < field->word_count; ++w)
            used += (u32)snprintf(kinds + used, sizeof(kinds) - used, "%s%s", w ? ", " : "", field->words[w]);
        fail(reader, line->number, "unknown %s '%.*s' (kinds: %s)", field->key, nv_text_shown(token[0]), token[0].text, kinds);
    } break;
    case FIELD_TEXT_NAME: {
        RootMotionDesc motion;
        if (nv_text_parse_text(text, line->number, token[0], motion.path, sizeof(motion.path)) &&
            nv_text_parse_name(text, line->number, token[1], motion.joint, sizeof(motion.joint)))
            memcpy(destination, &motion, sizeof(motion));
    } break;
    case FIELD_JUMP: {
        JumpDesc jump;
        for (u32 i = 0; i < 3; ++i)
            if (!nv_text_parse_name(text, line->number, token[i], jump.clip[i], SCENE_ID_SIZE))
                return;
        NvTextNumber seconds = nv_text_parse_number(text, line->number, token[3], 0);
        if (!seconds.ok)
            return;
        if (!in_range(field, seconds.value)) {
            report_range(reader, line->number, field);
            return;
        }
        jump.air_time = (f32)seconds.value;
        memcpy(destination, &jump, sizeof(jump));
    } break;
    }
}

// What a block is still missing when it ends.
internal void check_required(SceneReader* reader, const SceneField* fields, u32 count, u32 seen, u32 header_line, const char* what,
                             const char* name)
{
    for (u32 i = 0; i < count; ++i)
        if (fields[i].required && !(seen & (1u << i)))
            fail(reader, header_line, "%s '%s' is missing '%s'", what, name, fields[i].key);
}

//
// Shapes
//

typedef struct ShapeDef {
    const char* key;
    ShapeKind kind;
    u32 count;
    u32 first_integer; // the line's numbers from this one on are whole numbers (count: none)
} ShapeDef;

global const ShapeDef shape_defs[SHAPE_COUNT] = {
    [SHAPE_BOX] = {"box", SHAPE_BOX, 6, 6},
    [SHAPE_PLANE] = {"plane", SHAPE_PLANE, 2, 2},
    [SHAPE_SPHERE] = {"sphere", SHAPE_SPHERE, 6, 4},
    [SHAPE_CYLINDER] = {"cylinder", SHAPE_CYLINDER, 6, 5},
    [SHAPE_CONE] = {"cone", SHAPE_CONE, 6, 5},
    [SHAPE_CAPSULE] = {"capsule", SHAPE_CAPSULE, 7, 5},
    [SHAPE_TORUS] = {"torus", SHAPE_TORUS, 7, 5},
};

// The smallest whole number of the shape's i-th number (from `first_integer` on).
internal u32 shape_minimum(ShapeKind kind, u32 i)
{
    if (kind == SHAPE_SPHERE && i == 5)
        return 2; // rings
    if (kind == SHAPE_CAPSULE && i == 6)
        return 1;
    return 3; // segments around Y, and a torus's tube
}

// The vertices a shape adds (engine/mesh.h's macros), to keep a model under SCENE_MAX_VERTICES.
internal u32 shape_vertices(const ShapeDesc* shape)
{
    const f32* v = shape->value;
    switch (shape->kind) {
    case SHAPE_BOX: return NV_MESH_BOX_VERTICES;
    case SHAPE_PLANE: return NV_MESH_PLANE_VERTICES;
    case SHAPE_SPHERE: return NV_MESH_SPHERE_VERTICES((u32)v[4], (u32)v[5]);
    case SHAPE_CYLINDER: return NV_MESH_CYLINDER_VERTICES((u32)v[5]);
    case SHAPE_CONE: return NV_MESH_CONE_VERTICES((u32)v[5]);
    case SHAPE_CAPSULE: return NV_MESH_CAPSULE_VERTICES((u32)v[5], (u32)v[6]);
    case SHAPE_TORUS: return NV_MESH_TORUS_VERTICES((u32)v[5], (u32)v[6]);
    case SHAPE_COUNT: break;
    }
    return 0;
}

internal const ShapeDef* find_shape(NvTextToken key)
{
    for (u32 i = 0; i < SHAPE_COUNT; ++i)
        if (nv_text_token_is(key, shape_defs[i].key))
            return &shape_defs[i];
    return NULL;
}

// A shape line of a model: `box cx cy cz hx hy hz` and the others. The numbers keep the order of the mesh function's arguments;
// sizes must be above zero and the whole numbers (segments, rings) at least their minimum.
internal void read_shape(SceneReader* reader, const NvTextLine* line, const ShapeDef* def, ModelDesc* model)
{
    NvTextReader* text = &reader->text;
    if (line->token_count != 1 + def->count) {
        fail(reader, line->number, "'%s' takes %u values", def->key, def->count);
        return;
    }
    if (model->shape_count == SCENE_MAX_SHAPES) {
        fail(reader, line->number, "too many shapes in a model (at most %d)", SCENE_MAX_SHAPES);
        return;
    }
    ShapeDesc shape = {.kind = def->kind};
    for (u32 i = 0; i < def->count; ++i) {
        NvTextNumber number = nv_text_parse_number(text, line->number, line->tokens[1 + i], i >= def->first_integer);
        if (!number.ok)
            return;
        shape.value[i] = (f32)number.value;
    }
    // The sizes: a center (the first three numbers, or none for a plane) can be anywhere; the rest are above zero, except a
    // capsule's half height, which may be zero (two hemispheres).
    u32 first_size = def->kind == SHAPE_PLANE ? 0 : 3;
    for (u32 i = first_size; i < def->count; ++i) {
        if (i >= def->first_integer) {
            u32 minimum = shape_minimum(def->kind, i);
            if (shape.value[i] < (f32)minimum || shape.value[i] > 256.0f) {
                fail(reader, line->number, "'%s' needs %u to 256 here (value %u)", def->key, minimum, i + 1);
                return;
            }
        } else if (shape.value[i] < 0.0f || (shape.value[i] == 0.0f && !(def->kind == SHAPE_CAPSULE && i == 4))) {
            fail(reader, line->number, "'%s' sizes must be above 0 (value %u)", def->key, i + 1);
            return;
        }
    }
    model->shapes[model->shape_count++] = shape;
}

//
// Blocks
//

internal Block* top_block(SceneReader* reader)
{
    return &reader->stack[reader->depth - 1];
}

internal void open_block(SceneReader* reader, BlockKind kind, const NvTextLine* line, s32 index)
{
    NV_ASSERT(reader->depth < NV_ARRAY_COUNT(reader->stack));
    reader->stack[reader->depth++] = (Block){.kind = kind, .header_indent = (s32)line->indent, .child_indent = -1,
                                             .header_line = line->number, .index = index};
}

// The node whose lines are being read: the innermost open node.
internal s32 current_node(SceneReader* reader)
{
    for (u32 i = reader->depth; i-- > 0;)
        if (reader->stack[i].kind == BLOCK_NODE)
            return reader->stack[i].index;
    return -1;
}

internal u32 open_node_count(SceneReader* reader)
{
    u32 count = 0;
    for (u32 i = 0; i < reader->depth; ++i)
        count += reader->stack[i].kind == BLOCK_NODE;
    return count;
}

internal void normalize_spin(SceneReader* reader, BehaviorDesc* behavior)
{
    NvVec3 axis = behavior->spin.axis;
    f32 length = sqrtf(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (length < 1.0e-6f) {
        fail(reader, behavior->line, "spin's 'axis' must not be 0 0 0");
        return;
    }
    behavior->spin.axis = (NvVec3){axis.x / length, axis.y / length, axis.z / length};
}

internal void close_block(SceneReader* reader)
{
    Block* block = &reader->stack[--reader->depth];
    SceneDesc* desc = reader->desc;
    switch (block->kind) {
    case BLOCK_VIEW:
        check_required(reader, view_fields, (u32)NV_ARRAY_COUNT(view_fields), block->seen, block->header_line, "block", "view");
        break;
    case BLOCK_LIGHTING: // every key is optional
        break;
    case BLOCK_MODEL: {
        ModelDesc* model = &desc->models[block->index];
        b32 is_file = (block->seen & (1u << MODEL_FILE)) != 0;
        model->is_file = is_file;
        if (is_file && model->shape_count)
            fail(reader, block->header_line, "model '%s' has shapes and a 'file': it is one or the other", model->id);
        else if (!is_file && !model->shape_count)
            fail(reader, block->header_line, "model '%s' needs shape lines or a 'file'", model->id);
        if (!is_file && (block->seen & ((1u << MODEL_CLIPS) | (1u << MODEL_ROOT_MOTION))))
            fail(reader, block->header_line, "model '%s': 'clips' and 'root_motion_clips' go with a 'file'", model->id);
        u32 vertices = 0;
        for (u32 i = 0; i < model->shape_count; ++i)
            vertices += shape_vertices(&model->shapes[i]);
        if (vertices > SCENE_MAX_VERTICES)
            fail(reader, block->header_line, "model '%s' has %u vertices (at most %d)", model->id, vertices, SCENE_MAX_VERTICES);
    } break;
    case BLOCK_NODE: {
        NodeDesc* node = &desc->nodes[block->index];
        b32 rotation = (block->seen & (1u << NODE_ROTATION)) != 0, euler = (block->seen & (1u << NODE_EULER)) != 0;
        if (rotation && euler)
            fail(reader, block->header_line, "node '%s' has 'rotation' and 'euler': it is one or the other", node->id);
        else if (euler)
            node->rotation = nv_quat_from_euler(node->euler);
        if (rotation) {
            NvQuat q = node->rotation;
            if (q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w < 1.0e-12f)
                fail(reader, block->header_line, "node '%s': 'rotation' must not be 0 0 0 0", node->id);
        }
    } break;
    case BLOCK_MODEL_USE: {
        NodeDesc* node = &desc->nodes[block->index];
        node->has_color = (block->seen & (1u << USE_COLOR)) != 0;
    } break;
    case BLOCK_CAMERA: {
        NodeDesc* node = &desc->nodes[block->index];
        check_required(reader, camera_fields, (u32)NV_ARRAY_COUNT(camera_fields), block->seen, block->header_line, "node", node->id);
        b32 has_fov = (block->seen & (1u << CAMERA_FOV)) != 0, has_height = (block->seen & (1u << CAMERA_HEIGHT)) != 0;
        if (node->camera.projection == NV_PROJECTION_PERSPECTIVE) {
            if (!has_fov)
                fail(reader, block->header_line, "node '%s': a perspective camera needs 'fov'", node->id);
            if (has_height)
                fail(reader, block->header_line, "node '%s': 'height' is for an orthographic camera", node->id);
        } else if (node->camera.projection == NV_PROJECTION_ORTHOGRAPHIC) {
            if (!has_height)
                fail(reader, block->header_line, "node '%s': an orthographic camera needs 'height'", node->id);
            if (has_fov)
                fail(reader, block->header_line, "node '%s': 'fov' is for a perspective camera", node->id);
        }
        if (node->camera.near_z >= node->camera.far_z && (block->seen & (1u << CAMERA_NEAR)) && (block->seen & (1u << CAMERA_FAR)))
            fail(reader, block->header_line, "node '%s': 'near' must be less than 'far'", node->id);
        node->has_camera = 1;
    } break;
    case BLOCK_LIGHT: {
        NodeDesc* node = &desc->nodes[block->index];
        check_required(reader, light_fields, (u32)NV_ARRAY_COUNT(light_fields), block->seen, block->header_line, "node", node->id);
        b32 has_range = (block->seen & (1u << LIGHT_RANGE)) != 0, has_cone = (block->seen & (1u << LIGHT_CONE)) != 0;
        NvLightType type = node->light.type;
        if ((type == NV_LIGHT_POINT || type == NV_LIGHT_SPOT) && !has_range)
            fail(reader, block->header_line, "node '%s': a point or spot light needs 'range'", node->id);
        if (type == NV_LIGHT_DIRECTIONAL && has_range)
            fail(reader, block->header_line, "node '%s': a directional light has no 'range'", node->id);
        if (type == NV_LIGHT_SPOT && !has_cone)
            fail(reader, block->header_line, "node '%s': a spot light needs 'cone'", node->id);
        if (type != NV_LIGHT_SPOT && has_cone)
            fail(reader, block->header_line, "node '%s': 'cone' is for a spot light", node->id);
        if (has_cone && node->light.inner_cone >= node->light.outer_cone)
            fail(reader, block->header_line, "node '%s': the inner cone must be less than the outer", node->id);
        node->has_light = 1;
    } break;
    case BLOCK_BEHAVIOR: {
        BehaviorDesc* behavior = &desc->behaviors[block->index];
        const BehaviorDef* def = &behavior_defs[behavior->kind];
        check_required(reader, def->fields, def->field_count, block->seen, block->header_line, "behavior", def->name);
        if (behavior->kind == BEHAVIOR_SPIN && (block->seen & 1u))
            normalize_spin(reader, behavior);
    } break;
    case BLOCK_ROOT:
    case BLOCK_SKIP:
        break;
    }
}

//
// Top-level statements
//

internal const char* top_name(Top top)
{
    local_persist const char* const names[] = {"", "nvscene_version", "title", "view", "lighting", "model", "node"};
    // Every Top has a name; the check is what lets GCC see that no index past the table is read.
    return (u32)top < NV_ARRAY_COUNT(names) ? names[top] : "?";
}

// Whether the statement may come now. It is reported when not; the caller then skips its block.
internal b32 check_order(SceneReader* reader, const NvTextLine* line, Top top)
{
    if (reader->last > top) {
        fail(reader, line->number, "'%s' must come before '%s'", top_name(top), top_name(reader->last));
        return 0;
    }
    if (reader->last == top && top != TOP_MODEL && top != TOP_NODE) {
        fail(reader, line->number, "'%s' is given twice", top_name(top));
        return 0;
    }
    reader->last = top;
    reader->seen_top |= 1u << top;
    return 1;
}

internal void read_version(SceneReader* reader, const NvTextLine* line)
{
    if (reader->last != TOP_NONE) {
        fail(reader, line->number, "'nvscene_version' must be the first statement");
        return;
    }
    reader->last = TOP_VERSION;
    reader->seen_top |= 1u << TOP_VERSION;
    if (line->token_count != 2) {
        fail(reader, line->number, "'nvscene_version' takes one integer");
        return;
    }
    NvTextNumber number = nv_text_parse_number(&reader->text, line->number, line->tokens[1], 1);
    if (!number.ok)
        return;
    if (number.value < 1) {
        fail(reader, line->number, "'nvscene_version' must be 1 to %d", NVSCENE_VERSION);
    } else if (number.value > NVSCENE_VERSION) {
        // A newer format may mean anything by what follows, so the rest is not read.
        fail(reader, line->number, "format %.0f is newer than this build reads (%d)", number.value, NVSCENE_VERSION);
        reader->stop = 1;
    }
}

internal b32 id_is_free(SceneReader* reader, const NvTextLine* line, const char* what, s32 found)
{
    if (found >= 0) {
        fail(reader, line->number, "%s '%.*s' is defined twice", what, nv_text_shown(line->tokens[1]), line->tokens[1].text);
        return 0;
    }
    return 1;
}

internal void read_model_header(SceneReader* reader, const NvTextLine* line)
{
    SceneDesc* desc = reader->desc;
    char id[SCENE_ID_SIZE];
    if (line->token_count != 2) {
        fail(reader, line->number, "'model' takes an id");
        open_block(reader, BLOCK_SKIP, line, -1);
        return;
    }
    if (!nv_text_parse_name(&reader->text, line->number, line->tokens[1], id, sizeof(id)) ||
        !id_is_free(reader, line, "model", scene_find_model(desc, id))) {
        open_block(reader, BLOCK_SKIP, line, -1);
        return;
    }
    if (desc->model_count == SCENE_MAX_MODELS) {
        fail(reader, line->number, "too many models (at most %d)", SCENE_MAX_MODELS);
        open_block(reader, BLOCK_SKIP, line, -1);
        return;
    }
    ModelDesc* model = &desc->models[desc->model_count];
    memcpy(model->id, id, sizeof(id));
    model->line = line->number;
    open_block(reader, BLOCK_MODEL, line, (s32)desc->model_count++);
}

// `node <id>`, at the top level or inside a node.
internal void read_node_header(SceneReader* reader, const NvTextLine* line)
{
    SceneDesc* desc = reader->desc;
    char id[SCENE_ID_SIZE];
    if (line->token_count != 2) {
        fail(reader, line->number, "'node' takes an id");
        open_block(reader, BLOCK_SKIP, line, -1);
        return;
    }
    if (!nv_text_parse_name(&reader->text, line->number, line->tokens[1], id, sizeof(id)) ||
        !id_is_free(reader, line, "node", scene_find_node(desc, id))) {
        open_block(reader, BLOCK_SKIP, line, -1);
        return;
    }
    if (desc->node_count == SCENE_MAX_NODES) {
        fail(reader, line->number, "too many nodes (at most %d)", SCENE_MAX_NODES);
        open_block(reader, BLOCK_SKIP, line, -1);
        return;
    }
    if (open_node_count(reader) == SCENE_MAX_DEPTH) {
        fail(reader, line->number, "nodes are nested too deep (at most %d levels)", SCENE_MAX_DEPTH);
        open_block(reader, BLOCK_SKIP, line, -1);
        return;
    }
    NodeDesc* node = &desc->nodes[desc->node_count];
    memcpy(node->id, id, sizeof(id));
    memcpy(node->name, id, sizeof(id));
    node->line = line->number;
    node->parent = current_node(reader);
    node->rotation = nv_quat_identity();
    node->scale = (NvVec3){1.0f, 1.0f, 1.0f};
    node->model_index = -1;
    node->visible = 1;
    node->light.color = (NvVec3){1.0f, 1.0f, 1.0f};
    node->light.intensity = 1.0f;
    open_block(reader, BLOCK_NODE, line, (s32)desc->node_count++);
}

internal void read_top_line(SceneReader* reader, const NvTextLine* line)
{
    NvTextToken key = line->tokens[0];
    if (reader->first_statement) {
        reader->first_statement = 0;
        if (!nv_text_token_is(key, "nvscene_version")) {
            fail(reader, line->number, "the file must start with 'nvscene_version <version>'");
            reader->last = TOP_VERSION; // read on as if it had, so a missing line is one error
            reader->seen_top |= 1u << TOP_VERSION;
        }
    }

    if (nv_text_token_is(key, "nvscene_version")) {
        read_version(reader, line);
    } else if (nv_text_token_is(key, "title")) {
        if (!check_order(reader, line, TOP_TITLE))
            return;
        if (line->token_count != 2)
            fail(reader, line->number, "'title' takes quoted text");
        else
            nv_text_parse_text(&reader->text, line->number, line->tokens[1], reader->desc->title, sizeof(reader->desc->title));
    } else if (nv_text_token_is(key, "view")) {
        if (!check_order(reader, line, TOP_VIEW)) {
            open_block(reader, BLOCK_SKIP, line, -1);
            return;
        }
        if (line->token_count != 1)
            fail(reader, line->number, "'view' takes no value");
        reader->desc->view.target = (NvVec3){0};
        open_block(reader, BLOCK_VIEW, line, -1);
    } else if (nv_text_token_is(key, "lighting")) {
        if (!check_order(reader, line, TOP_LIGHTING)) {
            open_block(reader, BLOCK_SKIP, line, -1);
            return;
        }
        if (line->token_count != 1)
            fail(reader, line->number, "'lighting' takes no value");
        open_block(reader, BLOCK_LIGHTING, line, -1);
    } else if (nv_text_token_is(key, "model")) {
        if (!check_order(reader, line, TOP_MODEL))
            open_block(reader, BLOCK_SKIP, line, -1);
        else
            read_model_header(reader, line);
    } else if (nv_text_token_is(key, "node")) {
        if (!check_order(reader, line, TOP_NODE))
            open_block(reader, BLOCK_SKIP, line, -1);
        else
            read_node_header(reader, line);
    } else {
        fail(reader, line->number, "unknown key '%.*s'", nv_text_shown(key), key.text);
        open_block(reader, BLOCK_SKIP, line, -1);
    }
}

//
// Lines inside blocks
//

// A line in a node: a key, a component or a behavior (each opens a block), or a child node.
internal void read_node_line(SceneReader* reader, const NvTextLine* line, Block* block)
{
    SceneDesc* desc = reader->desc;
    NodeDesc* node = &desc->nodes[block->index];
    NvTextToken key = line->tokens[0];

    if (nv_text_token_is(key, "node")) {
        read_node_header(reader, line);
    } else if (nv_text_token_is(key, "model")) {
        if (node->model[0]) {
            fail(reader, line->number, "a node has at most one 'model'");
            open_block(reader, BLOCK_SKIP, line, -1);
        } else if (line->token_count != 2) {
            fail(reader, line->number, "'model' takes an id");
            open_block(reader, BLOCK_SKIP, line, -1);
        } else if (!nv_text_parse_name(&reader->text, line->number, line->tokens[1], node->model, sizeof(node->model))) {
            open_block(reader, BLOCK_SKIP, line, -1);
        } else {
            open_block(reader, BLOCK_MODEL_USE, line, block->index);
        }
    } else if (nv_text_token_is(key, "camera") || nv_text_token_is(key, "light")) {
        b32 camera = nv_text_token_is(key, "camera");
        if (line->token_count != 1)
            fail(reader, line->number, "'%s' takes no value", camera ? "camera" : "light");
        if (camera ? node->has_camera : node->has_light) {
            fail(reader, line->number, "a node has at most one '%s'", camera ? "camera" : "light");
            open_block(reader, BLOCK_SKIP, line, -1);
        } else {
            open_block(reader, camera ? BLOCK_CAMERA : BLOCK_LIGHT, line, block->index);
        }
    } else if (nv_text_token_is(key, "attach")) {
        if (node->attach[0])
            fail(reader, line->number, "a node has at most one 'attach'");
        else if (line->token_count != 2)
            fail(reader, line->number, "'attach' takes a joint name");
        else
            nv_text_parse_name(&reader->text, line->number, line->tokens[1], node->attach, sizeof(node->attach));
    } else {
        for (u32 k = 0; k < BEHAVIOR_COUNT; ++k) {
            const BehaviorDef* def = &behavior_defs[k];
            if (!nv_text_token_is(key, def->name))
                continue;
            if (line->token_count != 1)
                fail(reader, line->number, "'%s' takes no value", def->name);
            b32 repeated = 0;
            for (u32 b = 0; b < desc->behavior_count; ++b) {
                const BehaviorDesc* other = &desc->behaviors[b];
                if (other->node != block->index)
                    continue;
                if (other->kind == (BehaviorKind)k) {
                    fail(reader, line->number, "a node has at most one '%s'", def->name);
                    repeated = 1;
                }
            }
            if (!repeated && desc->behavior_count == SCENE_MAX_BEHAVIORS) {
                fail(reader, line->number, "too many behaviors (at most %d)", SCENE_MAX_BEHAVIORS);
                repeated = 1;
            }
            if (repeated) {
                open_block(reader, BLOCK_SKIP, line, -1);
                return;
            }
            BehaviorDesc* behavior = &desc->behaviors[desc->behavior_count];
            behavior->kind = (BehaviorKind)k;
            behavior->node = block->index;
            behavior->line = line->number;
            apply_fallbacks(def->fields, def->field_count, behavior_data(behavior));
            if (k == BEHAVIOR_CHARACTER)
                behavior->character.look_node_index = -1;
            open_block(reader, BLOCK_BEHAVIOR, line, (s32)desc->behavior_count++);
            return;
        }
        const SceneField* field = find_field(node_fields, (u32)NV_ARRAY_COUNT(node_fields), key);
        if (!field)
            fail(reader, line->number, "unknown key '%.*s'", nv_text_shown(key), key.text);
        else
            read_field(reader, line, field, (u32)(field - node_fields), node, &block->seen);
    }
}

internal void read_model_line(SceneReader* reader, const NvTextLine* line, Block* block)
{
    ModelDesc* model = &reader->desc->models[block->index];
    NvTextToken key = line->tokens[0];
    const ShapeDef* shape = find_shape(key);
    if (shape) {
        read_shape(reader, line, shape, model);
        return;
    }
    const SceneField* field = find_field(model_fields, (u32)NV_ARRAY_COUNT(model_fields), key);
    if (!field)
        fail(reader, line->number, "unknown key '%.*s'", nv_text_shown(key), key.text);
    else
        read_field(reader, line, field, (u32)(field - model_fields), model, &block->seen);
}

internal void read_scene_line(SceneReader* reader, const NvTextLine* line)
{
    SceneDesc* desc = reader->desc;
    while (reader->depth > 1 && (s32)line->indent <= top_block(reader)->header_indent)
        close_block(reader);
    Block* block = top_block(reader);
    if (block->kind == BLOCK_SKIP)
        return;
    if (block->child_indent < 0)
        block->child_indent = (s32)line->indent;
    if ((s32)line->indent != block->child_indent) {
        fail(reader, line->number, block->kind == BLOCK_ROOT ? "unexpected indentation" : "inconsistent indentation (a block's lines are indented alike)");
        return;
    }

    NvTextToken key = line->tokens[0];
    switch (block->kind) {
    case BLOCK_ROOT:
        read_top_line(reader, line);
        break;
    case BLOCK_VIEW: {
        const SceneField* field = find_field(view_fields, (u32)NV_ARRAY_COUNT(view_fields), key);
        if (!field)
            fail(reader, line->number, "unknown key '%.*s'", nv_text_shown(key), key.text);
        else
            read_field(reader, line, field, (u32)(field - view_fields), &desc->view, &block->seen);
    } break;
    case BLOCK_LIGHTING: {
        const SceneField* field = find_field(lighting_fields, (u32)NV_ARRAY_COUNT(lighting_fields), key);
        if (!field)
            fail(reader, line->number, "unknown key '%.*s'", nv_text_shown(key), key.text);
        else
            read_field(reader, line, field, (u32)(field - lighting_fields), &desc->lighting, &block->seen);
    } break;
    case BLOCK_MODEL:
        read_model_line(reader, line, block);
        break;
    case BLOCK_NODE:
        read_node_line(reader, line, block);
        break;
    case BLOCK_MODEL_USE: {
        const SceneField* field = find_field(model_use_fields, (u32)NV_ARRAY_COUNT(model_use_fields), key);
        if (!field)
            fail(reader, line->number, "unknown key '%.*s'", nv_text_shown(key), key.text);
        else
            read_field(reader, line, field, (u32)(field - model_use_fields), &desc->nodes[block->index], &block->seen);
    } break;
    case BLOCK_CAMERA:
    case BLOCK_LIGHT: {
        const SceneField* fields = block->kind == BLOCK_CAMERA ? camera_fields : light_fields;
        u32 count = block->kind == BLOCK_CAMERA ? (u32)NV_ARRAY_COUNT(camera_fields) : (u32)NV_ARRAY_COUNT(light_fields);
        const SceneField* field = find_field(fields, count, key);
        if (!field)
            fail(reader, line->number, "unknown key '%.*s'", nv_text_shown(key), key.text);
        else
            read_field(reader, line, field, (u32)(field - fields), &desc->nodes[block->index], &block->seen);
    } break;
    case BLOCK_BEHAVIOR: {
        BehaviorDesc* behavior = &desc->behaviors[block->index];
        const BehaviorDef* def = &behavior_defs[behavior->kind];
        const SceneField* field = find_field(def->fields, def->field_count, key);
        if (!field)
            fail(reader, line->number, "unknown key '%.*s'", nv_text_shown(key), key.text);
        else
            read_field(reader, line, field, (u32)(field - def->fields), behavior_data(behavior), &block->seen);
    } break;
    case BLOCK_SKIP:
        break;
    }
}

//
// References
//

s32 scene_find_node(const SceneDesc* desc, const char* id)
{
    for (u32 i = 0; i < desc->node_count; ++i)
        if (strcmp(desc->nodes[i].id, id) == 0)
            return (s32)i;
    return -1;
}

s32 scene_find_model(const SceneDesc* desc, const char* id)
{
    for (u32 i = 0; i < desc->model_count; ++i)
        if (strcmp(desc->models[i].id, id) == 0)
            return (s32)i;
    return -1;
}

// A reference by id: found, or reported at `line`.
internal s32 resolve_node(SceneReader* reader, u32 line, const char* what, const char* id)
{
    s32 index = scene_find_node(reader->desc, id);
    if (index < 0)
        fail(reader, line, "%s: no node '%s'", what, id);
    return index;
}

internal s32 resolve_model(SceneReader* reader, u32 line, const char* what, const char* id)
{
    s32 index = scene_find_model(reader->desc, id);
    if (index < 0)
        fail(reader, line, "%s: no model '%s'", what, id);
    return index;
}

internal void resolve(SceneReader* reader)
{
    SceneDesc* desc = reader->desc;
    ViewDesc* view = &desc->view;
    view->focus_index = view->select_index = -1;
    view->camera_index = resolve_node(reader, 0, "view 'camera'", view->camera);
    if (view->camera_index >= 0 && !desc->nodes[view->camera_index].has_camera)
        fail(reader, desc->nodes[view->camera_index].line, "view 'camera': node '%s' has no camera", view->camera);
    if (view->focus[0])
        view->focus_index = resolve_node(reader, 0, "view 'focus'", view->focus);
    if (view->select[0])
        view->select_index = resolve_node(reader, 0, "view 'select'", view->select);

    for (u32 n = 0; n < desc->node_count; ++n) {
        NodeDesc* node = &desc->nodes[n];
        if (node->model[0]) {
            node->model_index = resolve_model(reader, node->line, "model", node->model);
            b32 is_file = node->model_index >= 0 && desc->models[node->model_index].is_file;
            if (is_file && (node->has_color || node->double_sided))
                fail(reader, node->line, "node '%s': 'color' and 'double_sided' are for a shape model; a file model has its own materials", node->id);
        }
        if (node->attach[0]) {
            b32 found = 0;
            for (s32 p = node->parent; p >= 0 && !found; p = desc->nodes[p].parent)
                found = desc->nodes[p].model_index >= 0 && desc->models[desc->nodes[p].model_index].is_file;
            if (!found)
                fail(reader, node->line, "node '%s': 'attach' needs a node with a file model above it", node->id);
        }
    }

    for (u32 b = 0; b < desc->behavior_count; ++b) {
        BehaviorDesc* behavior = &desc->behaviors[b];
        const NodeDesc* node = &desc->nodes[behavior->node];
        switch (behavior->kind) {
        case BEHAVIOR_SPIN:
            break;
        case BEHAVIOR_CHARACTER: {
            CharacterDesc* character = &behavior->character;
            if (node->model_index < 0 || !desc->models[node->model_index].is_file)
                fail(reader, behavior->line, "character: node '%s' needs a file model", node->id);
            else if (!desc->models[node->model_index].clips[0])
                fail(reader, behavior->line, "character: model '%s' has no 'clips'", node->model);
            if (character->look_target[0][0])
                character->look_node_index = resolve_node(reader, behavior->line, "character look_target", character->look_target[1]);
            else if (character->look_at)
                fail(reader, behavior->line, "character: 'look_at' needs a 'look_target'");
        } break;
        case BEHAVIOR_COUNT:
            break;
        }
    }
}

//
// The file
//

SceneRead scene_read(SceneDesc* desc, const char* file_name, const char* text, umm size)
{
    memset(desc, 0, sizeof(*desc));
    for (u32 i = 0; i < 3; ++i)
        desc->lighting.ambient[i] = 0.15f;
    SceneRead result = {0};
    SceneReader* reader = &(SceneReader){0};
    reader->desc = desc;
    reader->text = (NvTextReader){.file_name = file_name,
                                  .log_source = "scene",
                                  .quotes = 1,
                                  .total = &result.error_count,
                                  .first_error = result.first_error,
                                  .first_error_capacity = sizeof(result.first_error)};
    reader->first_statement = 1;
    reader->stack[0] = (Block){.kind = BLOCK_ROOT, .header_indent = -1, .child_indent = 0};
    reader->depth = 1;

    NvTextLines lines = {text, size, 0, 0};
    for (NvTextLineRange range = nv_text_next_line(&lines); range.ok && !reader->stop; range = nv_text_next_line(&lines)) {
        NvTextTokenized tokenized = nv_text_tokenize(&reader->text, range.begin, range.end, lines.number);
        if (tokenized.ok && tokenized.line.token_count)
            read_scene_line(reader, &tokenized.line);
    }
    while (reader->depth > 1)
        close_block(reader);

    // What the whole file lacks, once the rest is right: the first of the missing parts, since they come in order.
    if (reader->text.errors == 0) {
        if (!(reader->seen_top & (1u << TOP_VERSION)))
            fail(reader, 0, "the file must start with 'nvscene_version <version>'");
        else if (!(reader->seen_top & (1u << TOP_TITLE)))
            fail(reader, 0, "missing 'title'");
        else if (!(reader->seen_top & (1u << TOP_VIEW)))
            fail(reader, 0, "missing 'view'");
        else if (!(reader->seen_top & (1u << TOP_NODE)))
            fail(reader, 0, "no nodes");
    }
    if (reader->text.errors == 0)
        resolve(reader);
    result.ok = reader->text.errors == 0;
    return result;
}

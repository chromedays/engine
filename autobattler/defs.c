// Reads the project file (docs/specs/abproj.md): the line-based text of data/default.abproj, with its rules, units and
// stage. It takes text in memory, so tests can try strings, and it uses no GPU or ImGui. A bad line never stops the read:
// every error is reported (nv_log, with the file name and line) and the read fails at the end.

#include "battle.h"

#include <engine/log.h>
#include <engine/text.h>

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NO_LIMIT          1e30

typedef struct Reader {
    NvTextReader text;
    BattleDefs* defs;
} Reader;

// A key whose values are numbers, stored at `offset` in the struct being read.
typedef enum FieldKind {
    FIELD_INTEGER, // u32
    FIELD_NUMBER,  // f32
    FIELD_ANGLE,   // f32, degrees in the file and radians when read
    FIELD_VECTOR,  // three f32
    FIELD_TICKS,   // u32, seconds in the file and whole ticks when read
    FIELD_GRID,    // two integers, the width and the length, into a BattleRules (their own ranges)
} FieldKind;

typedef struct Field {
    const char* key;
    FieldKind kind;
    u32 offset;
    f64 min, max; // the range of the value (in the file's units); NO_LIMIT for none
    b32 min_open, max_open; // the bound itself is not allowed
    b32 required;
} Field;

#define UNIT_FIELD(key, kind, member, min, min_open, required) \
    {key, kind, offsetof(UnitDef, member), min, NO_LIMIT, min_open, false, required}

global const Field unit_fields[] = {
    {"cost", FIELD_INTEGER, offsetof(UnitDef, cost), 1, 10000, false, false, true},
    UNIT_FIELD("health", FIELD_NUMBER, health, 0, true, true),
    UNIT_FIELD("armor", FIELD_NUMBER, armor, 0, false, false),
    UNIT_FIELD("speed", FIELD_NUMBER, speed, 0, false, true),
    UNIT_FIELD("radius", FIELD_NUMBER, radius, 0, true, true),
    UNIT_FIELD("height", FIELD_NUMBER, height, 0, true, true),
};

global const Field weapon_fields[] = {
    {"range", FIELD_NUMBER, offsetof(WeaponDef, range), 0, NO_LIMIT, true, false, true},
    {"damage", FIELD_NUMBER, offsetof(WeaponDef, damage), 0, NO_LIMIT, false, false, true},
    {"cooldown", FIELD_NUMBER, offsetof(WeaponDef, cooldown), 0, NO_LIMIT, true, false, true},
    {"launch_angle", FIELD_ANGLE, offsetof(WeaponDef, launch_angle), 0, 90, true, true, true},
    {"spread", FIELD_NUMBER, offsetof(WeaponDef, spread), 0, NO_LIMIT, false, false, false},
    {"muzzle", FIELD_VECTOR, offsetof(WeaponDef, muzzle), -NO_LIMIT, NO_LIMIT, false, false, false},
};

global const Field shield_fields[] = {
    {"radius", FIELD_NUMBER, offsetof(AbilityDef, shield.radius), 0, NO_LIMIT, true, false, true},
    {"capacity", FIELD_NUMBER, offsetof(AbilityDef, shield.capacity), 0, NO_LIMIT, true, false, true},
    {"regen", FIELD_NUMBER, offsetof(AbilityDef, shield.regen), 0, NO_LIMIT, false, false, true},
    {"regen_delay", FIELD_NUMBER, offsetof(AbilityDef, shield.regen_delay), 0, NO_LIMIT, false, false, true},
};

global const Field rules_fields[] = {
    {"cell_size", FIELD_NUMBER, offsetof(BattleRules, cell_size), 0, 10, true, false, true},
    {"grid", FIELD_GRID, 0, 0, 0, false, false, true},
    {"zone_rows", FIELD_INTEGER, offsetof(BattleRules, zone_rows), 1, BATTLE_MAX_ZONE_ROWS, false, false, true},
    {"round_time", FIELD_TICKS, offsetof(BattleRules, round_ticks), 0, 600, true, false, true},
    {"gravity", FIELD_NUMBER, offsetof(BattleRules, gravity), 0, 100, true, false, true},
    {"retarget_interval", FIELD_TICKS, offsetof(BattleRules, retarget_ticks), 0, 10, true, false, true},
    {"stop_fraction", FIELD_NUMBER, offsetof(BattleRules, stop_fraction), 0, 1, true, false, true},
    {"min_damage_fraction", FIELD_NUMBER, offsetof(BattleRules, min_damage_fraction), 0, 1, false, false, true},
};

global const Field stage_fields[] = {
    {"supply", FIELD_INTEGER, offsetof(BattleDefs, supply), 1, 100000, false, false, true},
    {"seed", FIELD_INTEGER, offsetof(BattleDefs, seed), 0, 4294967295.0, false, false, false},
};

#define FIELD_COUNT(fields) ((u32)NV_ARRAY_COUNT(fields))

internal u32 field_values(const Field* field)
{
    return field->kind == FIELD_VECTOR ? 3 : field->kind == FIELD_GRID ? 2 : 1;
}

internal const Field* find_field(const Field* fields, u32 count, NvTextToken key)
{
    for (u32 i = 0; i < count; ++i)
        if (nv_text_token_is(key, fields[i].key))
            return &fields[i];
    return NULL;
}

internal b32 in_range(const Field* field, f64 value)
{
    b32 above = field->min_open ? value > field->min : value >= field->min;
    b32 below = field->max_open ? value < field->max : value <= field->max;
    return above && below;
}

internal void report_range(Reader* reader, u32 line, const Field* field)
{
    char text[64] = "";
    u32 used = 0;
    if (field->min > -NO_LIMIT)
        used += (u32)snprintf(text + used, sizeof(text) - used, "%s %.10g", field->min_open ? ">" : ">=", field->min);
    if (field->max < NO_LIMIT)
        used += (u32)snprintf(text + used, sizeof(text) - used, "%s%s %.10g", used ? " and " : "", field->max_open ? "<" : "<=", field->max);
    nv_text_report(&reader->text, line, "'%s' must be %s", field->key, text);
}

// Reads a line `key value...` into the struct at `base`. `seen` holds a bit per field already given in this block.
internal void read_field(Reader* reader, const NvTextLine* line, const Field* field, u32 index, void* base, u32* seen)
{
    if (*seen & (1u << index)) {
        nv_text_report(&reader->text, line->number, "'%s' is given twice in this block", field->key);
        return;
    }
    *seen |= 1u << index; // a bad value is not "missing" on top of that
    u32 values = field_values(field);
    if (line->token_count != 1 + values) {
        nv_text_report(&reader->text, line->number, "'%s' takes %u value%s", field->key, values, values > 1 ? "s" : "");
        return;
    }
    f64 numbers[3];
    for (u32 i = 0; i < values; ++i) {
        NvTextNumber number = nv_text_parse_number(&reader->text, line->number, line->tokens[1 + i], field->kind == FIELD_INTEGER || field->kind == FIELD_GRID);
        if (!number.ok)
            return;
        numbers[i] = number.value;
    }
    if (field->kind == FIELD_GRID) {
        if (numbers[0] < 1 || numbers[0] > BATTLE_MAX_GRID_WIDTH) {
            nv_text_report(&reader->text, line->number, "'grid' width must be 1 to %d", BATTLE_MAX_GRID_WIDTH);
            return;
        }
        if (numbers[1] < 2 || numbers[1] > BATTLE_MAX_GRID_LENGTH) {
            nv_text_report(&reader->text, line->number, "'grid' length must be 2 to %d", BATTLE_MAX_GRID_LENGTH);
            return;
        }
    } else {
        for (u32 i = 0; i < values; ++i) {
            if (!in_range(field, numbers[i])) {
                report_range(reader, line->number, field);
                return;
            }
        }
    }
    u8* destination = (u8*)base + field->offset;
    switch (field->kind) {
    case FIELD_INTEGER: {
        u32 value = (u32)numbers[0];
        memcpy(destination, &value, sizeof(value));
    } break;
    case FIELD_NUMBER: {
        f32 value = (f32)numbers[0];
        memcpy(destination, &value, sizeof(value));
    } break;
    case FIELD_ANGLE: {
        f32 value = (f32)(numbers[0] * (NV_PI / 180.0));
        memcpy(destination, &value, sizeof(value));
    } break;
    case FIELD_VECTOR: {
        f32 value[3] = {(f32)numbers[0], (f32)numbers[1], (f32)numbers[2]};
        memcpy(destination, value, sizeof(value));
    } break;
    case FIELD_TICKS: {
        u32 value = battle_seconds_to_ticks((f32)numbers[0]);
        memcpy(destination, &value, sizeof(value));
    } break;
    case FIELD_GRID: {
        BattleRules* rules = base;
        rules->grid_width = (u32)numbers[0];
        rules->grid_length = (u32)numbers[1];
    } break;
    }
}

// What a block is still missing when it ends.
internal void check_required(Reader* reader, const Field* fields, u32 count, u32 seen, u32 header_line, const char* what, const char* name)
{
    for (u32 i = 0; i < count; ++i)
        if (fields[i].required && !(seen & (1u << i)))
            nv_text_report(&reader->text, header_line, "%s '%s' is missing '%s'", what, name, fields[i].key);
}

// The project file

typedef enum BlockKind { BLOCK_ROOT, BLOCK_RULES, BLOCK_UNIT, BLOCK_WEAPON, BLOCK_SHIELD, BLOCK_STAGE, BLOCK_SKIP } BlockKind;

enum { SEEN_WEAPON = 1 << 8, SEEN_ABILITY = 1 << 9 }; // beyond the unit's own fields in Block.seen

// An open block. Its lines are indented deeper than its header, all by the same amount.
typedef struct Block {
    BlockKind kind;
    s32 header_indent;
    s32 child_indent; // -1 until the first line inside it
    u32 header_line;
    u32 seen;
} Block;

// The last top-level statement that was in its place; they come in this order (docs/specs/abproj.md, "Syntax").
typedef enum Top { TOP_NONE, TOP_VERSION, TOP_RULES, TOP_UNIT, TOP_STAGE } Top;

typedef struct ProjectReader {
    Reader base;
    Block stack[3]; // root, then rules, unit or stage, then a unit's weapon or ability
    u32 depth;
    Top last;
    b32 first_statement; // the next top-level statement is the file's first
    b32 stop;            // nothing more can be read (a newer format)

    UnitDef unit;   // the unit being read
    b32 unit_named; // its name is a good one, so what it lacks can be reported by name
    b32 unit_kept;  // there was room for it

    u32 rules_errors_start, zone_rows_line;
    b32 rules_ok;   // the rules block was read without an error, so the grid is known
    u32 stage_errors_start, places_seen; // `place` lines, also those that could not be judged against a wrong grid
    u8 taken[BATTLE_MAX_ZONE_ROWS][BATTLE_MAX_GRID_WIDTH]; // enemy cells with a unit, by row in the zone
    u32 place_lines[BATTLE_MAX_PLACES];                   // the line of each enemy place
} ProjectReader;

internal void close_block(ProjectReader* reader)
{
    Reader* base = &reader->base;
    BattleDefs* defs = base->defs;
    Block* block = &reader->stack[--reader->depth];
    switch (block->kind) {
    case BLOCK_RULES: {
        check_required(base, rules_fields, FIELD_COUNT(rules_fields), block->seen, block->header_line, "block", "rules");
        const BattleRules* rules = &defs->rules;
        if (rules->grid_length && rules->zone_rows && rules->zone_rows * 2 > rules->grid_length)
            nv_text_report(&base->text, reader->zone_rows_line, "'zone_rows' must be at most %u (half the grid's length)", rules->grid_length / 2);
        reader->rules_ok = base->text.errors == reader->rules_errors_start;
    } break;
    case BLOCK_UNIT:
        if (reader->unit_named) {
            check_required(base, unit_fields, FIELD_COUNT(unit_fields), block->seen, block->header_line, "unit", reader->unit.name);
            if (!(block->seen & SEEN_WEAPON))
                nv_text_report(&base->text, block->header_line, "unit '%s' needs a weapon", reader->unit.name);
        }
        if (reader->unit_kept)
            defs->units[defs->unit_count++] = reader->unit;
        break;
    case BLOCK_WEAPON:
        check_required(base, weapon_fields, FIELD_COUNT(weapon_fields), block->seen, block->header_line, "weapon", reader->unit.weapon.name);
        break;
    case BLOCK_SHIELD:
        check_required(base, shield_fields, FIELD_COUNT(shield_fields), block->seen, block->header_line, "ability", "shield");
        break;
    case BLOCK_STAGE:
        check_required(base, stage_fields, FIELD_COUNT(stage_fields), block->seen, block->header_line, "block", "stage");
        if (reader->places_seen == 0 && base->text.errors == reader->stage_errors_start)
            nv_text_report(&base->text, block->header_line, "'stage' has no 'place' lines");
        if (defs->supply) {
            // The first place that takes the total over the supply is the one reported.
            u32 total = 0;
            for (u32 i = 0; i < defs->enemy_count; ++i) {
                total += defs->units[defs->enemy[i].def].cost;
                if (total > defs->supply) {
                    nv_text_report(&base->text, reader->place_lines[i], "the total cost is over the supply (%u)", defs->supply);
                    break;
                }
            }
        }
        break;
    case BLOCK_ROOT:
    case BLOCK_SKIP:
        break;
    }
}

internal void open_block(ProjectReader* reader, BlockKind kind, const NvTextLine* line)
{
    NV_ASSERT(reader->depth < NV_ARRAY_COUNT(reader->stack));
    reader->stack[reader->depth++] = (Block){.kind = kind, .header_indent = (s32)line->indent, .child_indent = -1, .header_line = line->number};
}

// Top-level statements

internal void read_version(ProjectReader* reader, const NvTextLine* line)
{
    Reader* base = &reader->base;
    if (reader->last != TOP_NONE) {
        nv_text_report(&base->text, line->number, "'abproj_version' must be the first statement");
        return;
    }
    reader->last = TOP_VERSION;
    if (line->token_count != 2) {
        nv_text_report(&base->text, line->number, "'abproj_version' takes one integer");
        return;
    }
    NvTextNumber number = nv_text_parse_number(&base->text, line->number, line->tokens[1], true);
    if (!number.ok)
        return;
    if (number.value < 1) {
        nv_text_report(&base->text, line->number, "'abproj_version' must be 1 to %d", ABPROJ_VERSION);
    } else if (number.value > ABPROJ_VERSION) {
        // A newer format may mean anything by what follows, so the rest is not read.
        nv_text_report(&base->text, line->number, "format %.0f is newer than this build reads (%d)", number.value, ABPROJ_VERSION);
        reader->stop = true;
    }
}

internal void read_unit_header(ProjectReader* reader, const NvTextLine* line)
{
    Reader* base = &reader->base;
    BattleDefs* defs = base->defs;
    reader->unit = (UnitDef){0};
    reader->unit_named = false;
    reader->unit_kept = false;
    open_block(reader, BLOCK_UNIT, line);
    if (line->token_count != 2) {
        nv_text_report(&base->text, line->number, "'unit' takes a name");
        return;
    }
    if (!nv_text_parse_name(&base->text, line->number, line->tokens[1], reader->unit.name, BATTLE_NAME_SIZE))
        return;
    reader->unit_named = true;
    for (u32 i = 0; i < defs->unit_count; ++i) {
        if (strcmp(defs->units[i].name, reader->unit.name) == 0) {
            nv_text_report(&base->text, line->number, "unit '%s' is defined twice", reader->unit.name);
            return;
        }
    }
    if (defs->unit_count == BATTLE_MAX_UNIT_DEFS) {
        nv_text_report(&base->text, line->number, "too many units (at most %d)", BATTLE_MAX_UNIT_DEFS);
        return;
    }
    reader->unit_kept = true;
}

// A statement at the left edge. Each is allowed in one place only; one out of place is reported and its block skipped, so
// that a swap of two blocks is one error and not the errors of everything that depends on them.
internal void read_top_line(ProjectReader* reader, const NvTextLine* line)
{
    Reader* base = &reader->base;
    NvTextToken key = line->tokens[0];
    if (reader->first_statement) {
        reader->first_statement = false;
        if (!nv_text_token_is(key, "abproj_version")) {
            nv_text_report(&base->text, line->number, "the file must start with 'abproj_version <version>'");
            reader->last = TOP_VERSION; // read on as if it had, so a missing line is one error
        }
    }

    if (nv_text_token_is(key, "abproj_version")) {
        read_version(reader, line);
    } else if (nv_text_token_is(key, "rules")) {
        if (reader->last == TOP_VERSION) {
            if (line->token_count != 1)
                nv_text_report(&base->text, line->number, "'rules' takes no value");
            reader->last = TOP_RULES;
            reader->rules_errors_start = base->text.errors;
            open_block(reader, BLOCK_RULES, line);
        } else {
            nv_text_report(&base->text, line->number, "'rules' is given twice");
            open_block(reader, BLOCK_SKIP, line);
        }
    } else if (nv_text_token_is(key, "unit")) {
        if (reader->last == TOP_RULES || reader->last == TOP_UNIT) {
            reader->last = TOP_UNIT;
            read_unit_header(reader, line);
        } else {
            nv_text_report(&base->text, line->number, reader->last == TOP_STAGE ? "'unit' must come before 'stage'" : "'unit' must come after 'rules'");
            open_block(reader, BLOCK_SKIP, line);
        }
    } else if (nv_text_token_is(key, "stage")) {
        if (reader->last == TOP_UNIT) {
            if (line->token_count != 1)
                nv_text_report(&base->text, line->number, "'stage' takes no value");
            reader->last = TOP_STAGE;
            reader->stage_errors_start = base->text.errors;
            base->defs->seed = 1;
            open_block(reader, BLOCK_STAGE, line);
        } else {
            nv_text_report(&base->text, line->number, reader->last == TOP_STAGE   ? "'stage' is given twice"
                                       : reader->last == TOP_RULES ? "'stage' must come after the units"
                                                                   : "'stage' must come after 'rules' and the units");
            open_block(reader, BLOCK_SKIP, line);
        }
    } else {
        nv_text_report(&base->text, line->number, "unknown key '%.*s'", nv_text_shown(key), key.text);
        open_block(reader, BLOCK_SKIP, line);
    }
}

// Lines inside blocks

internal void read_unit_line(ProjectReader* reader, const NvTextLine* line, Block* block)
{
    Reader* base = &reader->base;
    NvTextToken key = line->tokens[0];
    if (nv_text_token_is(key, "weapon") || nv_text_token_is(key, "ability")) {
        b32 is_weapon = nv_text_token_is(key, "weapon");
        u32 seen_bit = is_weapon ? SEEN_WEAPON : SEEN_ABILITY;
        b32 repeated = (block->seen & seen_bit) != 0;
        block->seen |= seen_bit;
        if (repeated)
            nv_text_report(&base->text, line->number, "a unit has at most one '%s'", is_weapon ? "weapon" : "ability");
        if (line->token_count != 2) {
            nv_text_report(&base->text, line->number, "'%s' takes %s", is_weapon ? "weapon" : "ability", is_weapon ? "a name" : "a kind");
            open_block(reader, BLOCK_SKIP, line);
        } else if (is_weapon) {
            // A repeated weapon is read into scratch: the first one stays what the unit has.
            b32 named = nv_text_parse_name(&base->text, line->number, line->tokens[1],
                                         repeated ? (char[BATTLE_NAME_SIZE]){0} : reader->unit.weapon.name, BATTLE_NAME_SIZE);
            open_block(reader, repeated || !named ? BLOCK_SKIP : BLOCK_WEAPON, line);
        } else if (nv_text_token_is(line->tokens[1], "shield") && !repeated) {
            reader->unit.ability.kind = ABILITY_SHIELD;
            open_block(reader, BLOCK_SHIELD, line);
        } else {
            if (!nv_text_token_is(line->tokens[1], "shield"))
                nv_text_report(&base->text, line->number, "unknown ability '%.*s' (kinds: shield)", nv_text_shown(line->tokens[1]), line->tokens[1].text);
            open_block(reader, BLOCK_SKIP, line);
        }
        return;
    }
    const Field* field = find_field(unit_fields, FIELD_COUNT(unit_fields), key);
    if (!field)
        nv_text_report(&base->text, line->number, "unknown key '%.*s'", nv_text_shown(key), key.text);
    else
        read_field(base, line, field, (u32)(field - unit_fields), &reader->unit, &block->seen);
}

typedef struct UnitIndex {
    b32 ok;
    u32 index; // into BattleDefs.units
} UnitIndex;

// Fails when no unit has that name.
internal UnitIndex find_unit(const BattleDefs* defs, NvTextToken name)
{
    for (u32 i = 0; i < defs->unit_count; ++i)
        if (nv_text_token_is(name, defs->units[i].name))
            return (UnitIndex){.ok = true, .index = i};
    return (UnitIndex){0};
}

internal void read_place(ProjectReader* reader, const NvTextLine* line)
{
    Reader* base = &reader->base;
    BattleDefs* defs = base->defs;
    const BattleRules* rules = &defs->rules;
    if (line->token_count != 4) {
        nv_text_report(&base->text, line->number, "'place' takes a unit name, a cell x and a cell row");
        return;
    }
    ++reader->places_seen;
    b32 ok = true;
    UnitIndex unit = find_unit(defs, line->tokens[1]);
    if (!unit.ok) {
        nv_text_report(&base->text, line->number, "unknown unit '%.*s'", nv_text_shown(line->tokens[1]), line->tokens[1].text);
        ok = false;
    }
    NvTextNumber x_number = nv_text_parse_number(&base->text, line->number, line->tokens[2], true);
    if (!x_number.ok)
        return;
    NvTextNumber row_number = nv_text_parse_number(&base->text, line->number, line->tokens[3], true);
    if (!row_number.ok)
        return;
    if (!reader->rules_ok)
        return; // the grid is not known, so a cell cannot be judged (and no cell is reported against a wrong grid)
    f64 x = x_number.value, row = row_number.value;
    u32 first_row = battle_enemy_first_row(rules);
    if (x < 0 || x >= rules->grid_width) {
        nv_text_report(&base->text, line->number, "cell x must be 0 to %u", rules->grid_width - 1);
        ok = false;
    }
    if (row < first_row || row >= rules->grid_length) {
        nv_text_report(&base->text, line->number, "cell row must be %u to %u (the enemy zone)", first_row, rules->grid_length - 1);
        ok = false;
    }
    if (!ok)
        return;
    u32 cell_x = (u32)x, cell_row = (u32)row - first_row;
    if (reader->taken[cell_row][cell_x]) {
        nv_text_report(&base->text, line->number, "cell (%u, %u) already has a unit", cell_x, (u32)row);
        return;
    }
    reader->taken[cell_row][cell_x] = 1;
    reader->place_lines[defs->enemy_count] = line->number;
    defs->enemy[defs->enemy_count++] = (StagePlace){(u8)unit.index, (u8)cell_x, (u8)row};
}

internal void read_project_line(ProjectReader* reader, const NvTextLine* line)
{
    Reader* base = &reader->base;
    while (reader->depth > 1 && (s32)line->indent <= reader->stack[reader->depth - 1].header_indent)
        close_block(reader);
    Block* block = &reader->stack[reader->depth - 1];
    if (block->kind == BLOCK_SKIP)
        return; // the inside of a block that was already reported
    if (block->child_indent < 0)
        block->child_indent = (s32)line->indent;
    if ((s32)line->indent != block->child_indent) {
        nv_text_report(&base->text, line->number, block->kind == BLOCK_ROOT ? "unexpected indentation" : "inconsistent indentation (a block's lines are indented alike)");
        return;
    }

    NvTextToken key = line->tokens[0];
    switch (block->kind) {
    case BLOCK_ROOT:
        read_top_line(reader, line);
        break;
    case BLOCK_UNIT:
        read_unit_line(reader, line, block);
        break;
    case BLOCK_RULES: {
        const Field* field = find_field(rules_fields, FIELD_COUNT(rules_fields), key);
        if (!field) {
            nv_text_report(&base->text, line->number, "unknown key '%.*s'", nv_text_shown(key), key.text);
            break;
        }
        if (nv_text_token_is(key, "zone_rows"))
            reader->zone_rows_line = line->number;
        read_field(base, line, field, (u32)(field - rules_fields), &base->defs->rules, &block->seen);
    } break;
    case BLOCK_STAGE: {
        if (nv_text_token_is(key, "place")) {
            read_place(reader, line);
            break;
        }
        const Field* field = find_field(stage_fields, FIELD_COUNT(stage_fields), key);
        if (!field)
            nv_text_report(&base->text, line->number, "unknown key '%.*s'", nv_text_shown(key), key.text);
        else
            read_field(base, line, field, (u32)(field - stage_fields), base->defs, &block->seen);
    } break;
    case BLOCK_WEAPON:
    case BLOCK_SHIELD: {
        const Field* fields = block->kind == BLOCK_WEAPON ? weapon_fields : shield_fields;
        u32 count = block->kind == BLOCK_WEAPON ? FIELD_COUNT(weapon_fields) : FIELD_COUNT(shield_fields);
        void* data = block->kind == BLOCK_WEAPON ? (void*)&reader->unit.weapon : (void*)&reader->unit.ability;
        const Field* field = find_field(fields, count, key);
        if (!field)
            nv_text_report(&base->text, line->number, "unknown key '%.*s'", nv_text_shown(key), key.text);
        else
            read_field(base, line, field, (u32)(field - fields), data, &block->seen);
    } break;
    case BLOCK_SKIP:
        break;
    }
}

b32 defs_read_project(BattleDefs* defs, const char* file_name, const char* text, umm size)
{
    ProjectReader reader = {
        .base = {.text = {.file_name = file_name,
                          .log_source = "battle",
                          .total = &defs->error_count,
                          .first_error = defs->first_error,
                          .first_error_capacity = sizeof(defs->first_error)},
                 .defs = defs},
        .first_statement = true,
    };
    reader.stack[0] = (Block){.kind = BLOCK_ROOT, .header_indent = -1, .child_indent = 0};
    reader.depth = 1;
    defs->seed = 1;

    NvTextLines lines = {text, size, 0, 0};
    for (NvTextLineRange text_line = nv_text_next_line(&lines); text_line.ok && !reader.stop; text_line = nv_text_next_line(&lines)) {
        NvTextTokenized tokenized = nv_text_tokenize(&reader.base.text, text_line.begin, text_line.end, lines.number);
        if (tokenized.ok && tokenized.line.token_count)
            read_project_line(&reader, &tokenized.line);
    }
    while (reader.depth > 1)
        close_block(&reader);
    // What the whole file lacks, once the rest is right: the first of the missing parts, since they come in order.
    if (reader.base.text.errors == 0) {
        if (reader.last < TOP_VERSION)
            nv_text_report(&reader.base.text, 0, "the file must start with 'abproj_version <version>'");
        else if (reader.last < TOP_RULES)
            nv_text_report(&reader.base.text, 0, "missing 'rules'");
        else if (reader.last < TOP_UNIT)
            nv_text_report(&reader.base.text, 0, "no units");
        else if (reader.last < TOP_STAGE)
            nv_text_report(&reader.base.text, 0, "missing 'stage'");
    }
    return reader.base.text.errors == 0;
}

// Editing the rules (docs/specs/abproj.md, "Editing the rules"). These work on the text of a project that was read without
// errors, so every rule has its one line in the `rules` block.

// Rule `key`'s line, its tokens pointing into the text. Fails (zeroed) when `key` is not a rule or its line is not there.
typedef struct RuleLine {
    b32 ok;
    const Field* field;
    NvTextLine line;
    const char* end; // where the line's text ends (its '\n', or the text's end)
} RuleLine;

internal RuleLine find_rule(const char* text, umm size, const char* key)
{
    const Field* field = NULL;
    for (u32 i = 0; i < FIELD_COUNT(rules_fields); ++i)
        if (strcmp(rules_fields[i].key, key) == 0)
            field = &rules_fields[i];
    if (!field)
        return (RuleLine){0};
    NvTextLines lines = {text, size, 0, 0};
    b32 in_rules = false;
    for (NvTextLineRange text_line = nv_text_next_line(&lines); text_line.ok; text_line = nv_text_next_line(&lines)) {
        NvTextTokenized tokenized = nv_text_tokenize(NULL, text_line.begin, text_line.end, lines.number);
        const NvTextLine* line = &tokenized.line;
        if (!tokenized.ok || !line->token_count)
            continue;
        if (line->indent == 0) // a top-level statement: the rules block starts or ends
            in_rules = nv_text_token_is(line->tokens[0], "rules");
        else if (in_rules && nv_text_token_is(line->tokens[0], key) && line->token_count == 1 + field_values(field))
            return (RuleLine){.ok = true, .field = field, .line = *line, .end = text_line.end};
    }
    return (RuleLine){0};
}

u32 defs_rule_get(const char* text, umm size, const char* key, f64 values[BATTLE_RULE_MAX_VALUES])
{
    RuleLine rule = find_rule(text, size, key);
    if (!rule.ok)
        return 0;
    u32 count = field_values(rule.field);
    NV_ASSERT(count <= BATTLE_RULE_MAX_VALUES);
    for (u32 i = 0; i < count; ++i) {
        NvTextNumber number = nv_text_parse_number(NULL, 0, rule.line.tokens[1 + i], false);
        if (!number.ok)
            return 0;
        values[i] = number.value;
    }
    return count;
}

// A value as the file writes it: an integer, or a number with at most four decimals and no trailing zeros ("9.8", "60").
// Never an exponent, which the format has not. Returns the length, 0 when it does not fit.
internal u32 format_value(char* out, umm capacity, f64 value, b32 integer)
{
    value = integer ? round(value) : round(value * 1e4) / 1e4;
    value += 0.0; // -0 becomes 0
    int length = snprintf(out, capacity, integer ? "%.0f" : "%.4f", value);
    if (length <= 0 || (umm)length >= capacity)
        return 0;
    if (!integer) {
        while (out[length - 1] == '0')
            --length;
        if (out[length - 1] == '.')
            --length;
        out[length] = 0;
    }
    return (u32)length;
}

umm defs_rule_set(const char* text, umm size, const char* key, const f64* values, u32 count, char* out, umm capacity)
{
    RuleLine rule = find_rule(text, size, key);
    if (!rule.ok || count != field_values(rule.field))
        return 0;
    b32 integer = rule.field->kind == FIELD_INTEGER || rule.field->kind == FIELD_GRID;
    char written[96];
    u32 used = 0;
    for (u32 i = 0; i < count; ++i) {
        if (i)
            written[used++] = ' ';
        u32 length = format_value(written + used, sizeof(written) - used, values[i], integer);
        if (!length)
            return 0;
        used += length;
    }

    // The values' old text, from the first value to the end of the last, is replaced. The spaces after it shrink or grow
    // with the change so that a comment keeps its column (one space at the least).
    const char* first = rule.line.tokens[1].text;
    const NvTextToken* last = &rule.line.tokens[count];
    const char* after = last->text + last->length;
    const char* rest = after;
    while (rest < rule.end && *rest == ' ')
        ++rest;
    s64 gap = rest - after;
    if (rest < rule.end && *rest == '#') {
        gap += (s64)(after - first) - (s64)used;
        if (gap < 1)
            gap = 1;
    }
    umm head = (umm)(first - text), tail = (umm)(text + size - rest);
    umm total = head + used + (umm)gap + tail;
    if (total > capacity)
        return 0;
    memmove(out, text, head);
    memcpy(out + head, written, used);
    memset(out + head + used, ' ', (umm)gap);
    memmove(out + head + used + (umm)gap, rest, tail);
    return total;
}

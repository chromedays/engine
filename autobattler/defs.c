// Reads the definition files (docs/specs/battle.md, "Definition files"): the line-based text of data/units.txt and
// data/stage.txt. It takes text in memory, so tests can try strings, and it uses no GPU or ImGui. A bad line never stops
// the read: every error is reported (nv_log, with the file name and line) and the read fails at the end.

#include "battle.h"

#include <engine/log.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LOGGED_ERRORS 20
#define MAX_TOKENS        8
#define NO_LIMIT          1e30

typedef struct Token {
    const char* text;
    u32 length;
} Token;

typedef struct Line {
    u32 number; // from 1
    u32 indent; // spaces
    u32 token_count;
    Token tokens[MAX_TOKENS];
} Line;

typedef struct Reader {
    BattleDefs* defs;
    const char* file_name;
    u32 errors; // found by this read
} Reader;

__attribute__((format(printf, 3, 4))) internal void report(Reader* reader, u32 line, const char* format, ...)
{
    char message[128];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    nv_utf8_trim(message); // a long message is cut at the buffer's end, perhaps inside a character

    BattleDefs* defs = reader->defs;
    char full[sizeof(defs->first_error)];
    if (line)
        snprintf(full, sizeof(full), "%s:%u: %s", reader->file_name, line, message);
    else
        snprintf(full, sizeof(full), "%s: %s", reader->file_name, message);
    nv_utf8_trim(full);
    ++reader->errors;
    ++defs->error_count;
    if (defs->error_count == 1)
        memcpy(defs->first_error, full, sizeof(full));
    if (defs->error_count <= MAX_LOGGED_ERRORS)
        nv_log(NV_LOG_ERROR, "battle", "%s", full);
    else if (defs->error_count == MAX_LOGGED_ERRORS + 1)
        nv_log(NV_LOG_ERROR, "battle", "more errors, not shown");
}

// How many bytes of a token a message shows: at most 24, cut before a character rather than inside it.
internal int shown(Token token)
{
    u32 length = token.length;
    if (length > 24) {
        length = 24;
        while (length > 0 && ((u8)token.text[length] & 0xC0) == 0x80)
            --length;
    }
    return (int)length;
}

internal b32 token_is(Token token, const char* word)
{
    return token.length == strlen(word) && memcmp(token.text, word, token.length) == 0;
}

// Splitting a line. A comment runs from '#' to the end of the line; a line with nothing else is blank. A tab is an error,
// since indentation is spaces only.
typedef enum LineResult { LINE_BLANK, LINE_OK, LINE_BAD } LineResult;

internal LineResult tokenize(Reader* reader, const char* begin, const char* end, u32 number, Line* line)
{
    const char* stop = end;
    for (const char* p = begin; p < stop; ++p)
        if (*p == '#')
            stop = p;
    while (stop > begin && (stop[-1] == ' ' || stop[-1] == '\r')) // trailing spaces and the CR of a CRLF
        --stop;
    for (const char* p = begin; p < stop; ++p) {
        if (*p == '\t') {
            report(reader, number, "tab (indent and separate with spaces)");
            return LINE_BAD;
        }
    }

    *line = (Line){.number = number};
    const char* p = begin;
    while (p < stop && *p == ' ') {
        ++line->indent;
        ++p;
    }
    if (p == stop)
        return LINE_BLANK;
    while (p < stop) {
        while (p < stop && *p == ' ')
            ++p;
        if (p == stop)
            break;
        const char* word = p;
        while (p < stop && *p != ' ')
            ++p;
        if (line->token_count == MAX_TOKENS) {
            report(reader, number, "too many values");
            return LINE_BAD;
        }
        line->tokens[line->token_count++] = (Token){word, (u32)(p - word)};
    }
    return LINE_OK;
}

// Values

internal b32 is_digit(char c)
{
    return c >= '0' && c <= '9';
}

// A decimal number: an optional sign, digits, an optional point with digits ("120", "0.5", "-3", ".5"); no exponent.
// With `integer`, a point is an error too.
internal b32 parse_number(Reader* reader, u32 line, Token token, b32 integer, f64* value)
{
    char buffer[40];
    u32 i = 0, digits = 0;
    b32 valid = token.length < sizeof(buffer);
    if (valid && i < token.length && (token.text[i] == '+' || token.text[i] == '-'))
        ++i;
    while (valid && i < token.length && is_digit(token.text[i])) {
        ++i;
        ++digits;
    }
    b32 has_point = valid && i < token.length && token.text[i] == '.';
    if (has_point) {
        ++i;
        while (i < token.length && is_digit(token.text[i])) {
            ++i;
            ++digits;
        }
    }
    valid = valid && i == token.length && digits > 0;
    if (!valid) {
        report(reader, line, "'%.*s' is not a number", shown(token), token.text);
        return false;
    }
    if (integer && has_point) {
        report(reader, line, "'%.*s' is not an integer", shown(token), token.text);
        return false;
    }
    memcpy(buffer, token.text, token.length);
    buffer[token.length] = 0;
    *value = strtod(buffer, NULL); // rounds correctly, so a file always gives the same values
    return true;
}

// A name: a letter or '_', then letters, digits and '_'; at most 31 bytes. Copies it into `destination`.
internal b32 parse_name(Reader* reader, u32 line, Token token, char destination[BATTLE_NAME_SIZE])
{
    b32 valid = token.length > 0 && token.length < BATTLE_NAME_SIZE;
    for (u32 i = 0; valid && i < token.length; ++i) {
        char c = token.text[i];
        b32 letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
        valid = letter || (i > 0 && is_digit(c));
    }
    if (!valid) {
        report(reader, line, "'%.*s' is not a name (letters, digits and _; at most %d bytes; not starting with a digit)",
               shown(token), token.text, BATTLE_NAME_SIZE - 1);
        return false;
    }
    memcpy(destination, token.text, token.length);
    destination[token.length] = 0;
    return true;
}

// A key whose values are numbers, stored at `offset` in the struct being read.
typedef enum FieldKind {
    FIELD_INTEGER, // u32
    FIELD_NUMBER,  // f32
    FIELD_ANGLE,   // f32, degrees in the file and radians when read
    FIELD_VECTOR,  // three f32
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

#define FIELD_COUNT(fields) ((u32)NV_ARRAY_COUNT(fields))

internal const Field* find_field(const Field* fields, u32 count, Token key)
{
    for (u32 i = 0; i < count; ++i)
        if (token_is(key, fields[i].key))
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
        used += (u32)snprintf(text + used, sizeof(text) - used, "%s %g", field->min_open ? ">" : ">=", field->min);
    if (field->max < NO_LIMIT)
        used += (u32)snprintf(text + used, sizeof(text) - used, "%s%s %g", used ? " and " : "", field->max_open ? "<" : "<=", field->max);
    report(reader, line, "'%s' must be %s", field->key, text);
}

// Reads a line `key value...` into the struct at `base`. `seen` holds a bit per field already given in this block.
internal void read_field(Reader* reader, const Line* line, const Field* field, u32 index, void* base, u32* seen)
{
    if (*seen & (1u << index)) {
        report(reader, line->number, "'%s' is given twice in this block", field->key);
        return;
    }
    *seen |= 1u << index; // a bad value is not "missing" on top of that
    u32 values = field->kind == FIELD_VECTOR ? 3 : 1;
    if (line->token_count != 1 + values) {
        report(reader, line->number, "'%s' takes %u value%s", field->key, values, values > 1 ? "s" : "");
        return;
    }
    f64 numbers[3];
    for (u32 i = 0; i < values; ++i)
        if (!parse_number(reader, line->number, line->tokens[1 + i], field->kind == FIELD_INTEGER, &numbers[i]))
            return;
    for (u32 i = 0; i < values; ++i) {
        if (!in_range(field, numbers[i])) {
            report_range(reader, line->number, field);
            return;
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
    }
}

// What a block is still missing when it ends.
internal void check_required(Reader* reader, const Field* fields, u32 count, u32 seen, u32 header_line, const char* what, const char* name)
{
    for (u32 i = 0; i < count; ++i)
        if (fields[i].required && !(seen & (1u << i)))
            report(reader, header_line, "%s '%s' is missing '%s'", what, name, fields[i].key);
}

// Lines of a file, with the position of each

typedef struct Lines {
    const char* text;
    umm size;
    umm cursor;
    u32 number;
} Lines;

internal b32 next_line(Lines* lines, const char** begin, const char** end)
{
    if (lines->cursor >= lines->size)
        return false;
    *begin = lines->text + lines->cursor;
    const char* limit = lines->text + lines->size;
    const char* p = *begin;
    while (p < limit && *p != '\n')
        ++p;
    *end = p;
    lines->cursor = (umm)(p - lines->text) + 1; // past the '\n'
    ++lines->number;
    return true;
}

// units.txt

typedef enum BlockKind { BLOCK_ROOT, BLOCK_UNIT, BLOCK_WEAPON, BLOCK_SHIELD, BLOCK_SKIP } BlockKind;

enum { SEEN_WEAPON = 1 << 8, SEEN_ABILITY = 1 << 9 }; // beyond the unit's own fields in Block.seen

// An open block. Its lines are indented deeper than its header, all by the same amount.
typedef struct Block {
    BlockKind kind;
    s32 header_indent;
    s32 child_indent; // -1 until the first line inside it
    u32 header_line;
    u32 seen;
} Block;

typedef struct UnitsReader {
    Reader base;
    Block stack[3]; // root, unit, weapon or ability
    u32 depth;
    UnitDef unit;   // the unit being read
    b32 unit_named; // its name is a good one, so what it lacks can be reported by name
    b32 unit_kept;  // there was room for it
} UnitsReader;

internal void close_block(UnitsReader* reader)
{
    Block* block = &reader->stack[--reader->depth];
    switch (block->kind) {
    case BLOCK_UNIT:
        if (reader->unit_named) {
            check_required(&reader->base, unit_fields, FIELD_COUNT(unit_fields), block->seen, block->header_line, "unit", reader->unit.name);
            if (!(block->seen & SEEN_WEAPON))
                report(&reader->base, block->header_line, "unit '%s' needs a weapon", reader->unit.name);
        }
        if (reader->unit_kept) {
            BattleDefs* defs = reader->base.defs;
            defs->units[defs->unit_count++] = reader->unit;
        }
        break;
    case BLOCK_WEAPON:
        check_required(&reader->base, weapon_fields, FIELD_COUNT(weapon_fields), block->seen, block->header_line, "weapon", reader->unit.weapon.name);
        break;
    case BLOCK_SHIELD:
        check_required(&reader->base, shield_fields, FIELD_COUNT(shield_fields), block->seen, block->header_line, "ability", "shield");
        break;
    case BLOCK_ROOT:
    case BLOCK_SKIP:
        break;
    }
}

internal void open_block(UnitsReader* reader, BlockKind kind, const Line* line)
{
    NV_ASSERT(reader->depth < NV_ARRAY_COUNT(reader->stack));
    reader->stack[reader->depth++] = (Block){.kind = kind, .header_indent = (s32)line->indent, .child_indent = -1, .header_line = line->number};
}

internal void read_unit_header(UnitsReader* reader, const Line* line)
{
    Reader* base = &reader->base;
    BattleDefs* defs = base->defs;
    reader->unit = (UnitDef){0};
    reader->unit_named = false;
    reader->unit_kept = false;
    open_block(reader, BLOCK_UNIT, line);
    if (line->token_count != 2) {
        report(base, line->number, "'unit' takes a name");
        return;
    }
    if (!parse_name(base, line->number, line->tokens[1], reader->unit.name))
        return;
    reader->unit_named = true;
    for (u32 i = 0; i < defs->unit_count; ++i) {
        if (strcmp(defs->units[i].name, reader->unit.name) == 0) {
            report(base, line->number, "unit '%s' is defined twice", reader->unit.name);
            return;
        }
    }
    if (defs->unit_count == BATTLE_MAX_UNIT_DEFS) {
        report(base, line->number, "too many units (at most %d)", BATTLE_MAX_UNIT_DEFS);
        return;
    }
    reader->unit_kept = true;
}

internal void read_unit_line(UnitsReader* reader, const Line* line, Block* block)
{
    Reader* base = &reader->base;
    Token key = line->tokens[0];
    if (token_is(key, "weapon") || token_is(key, "ability")) {
        b32 is_weapon = token_is(key, "weapon");
        u32 seen_bit = is_weapon ? SEEN_WEAPON : SEEN_ABILITY;
        b32 repeated = (block->seen & seen_bit) != 0;
        block->seen |= seen_bit;
        if (repeated)
            report(base, line->number, "a unit has at most one '%s'", is_weapon ? "weapon" : "ability");
        if (line->token_count != 2) {
            report(base, line->number, "'%s' takes %s", is_weapon ? "weapon" : "ability", is_weapon ? "a name" : "a kind");
            open_block(reader, BLOCK_SKIP, line);
        } else if (is_weapon) {
            // A repeated weapon is read into scratch: the first one stays what the unit has.
            b32 named = parse_name(base, line->number, line->tokens[1], repeated ? (char[BATTLE_NAME_SIZE]){0} : reader->unit.weapon.name);
            open_block(reader, repeated || !named ? BLOCK_SKIP : BLOCK_WEAPON, line);
        } else if (token_is(line->tokens[1], "shield") && !repeated) {
            reader->unit.ability.kind = ABILITY_SHIELD;
            open_block(reader, BLOCK_SHIELD, line);
        } else {
            if (!token_is(line->tokens[1], "shield"))
                report(base, line->number, "unknown ability '%.*s' (kinds: shield)", shown(line->tokens[1]), line->tokens[1].text);
            open_block(reader, BLOCK_SKIP, line);
        }
        return;
    }
    const Field* field = find_field(unit_fields, FIELD_COUNT(unit_fields), key);
    if (!field)
        report(base, line->number, "unknown key '%.*s'", shown(key), key.text);
    else
        read_field(base, line, field, (u32)(field - unit_fields), &reader->unit, &block->seen);
}

internal void read_units_line(UnitsReader* reader, const Line* line)
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
        report(base, line->number, block->kind == BLOCK_ROOT ? "unexpected indentation" : "inconsistent indentation (a block's lines are indented alike)");
        return;
    }

    Token key = line->tokens[0];
    switch (block->kind) {
    case BLOCK_ROOT:
        if (token_is(key, "unit"))
            read_unit_header(reader, line);
        else
            report(base, line->number, "unknown key '%.*s'", shown(key), key.text);
        break;
    case BLOCK_UNIT:
        read_unit_line(reader, line, block);
        break;
    case BLOCK_WEAPON:
    case BLOCK_SHIELD: {
        const Field* fields = block->kind == BLOCK_WEAPON ? weapon_fields : shield_fields;
        u32 count = block->kind == BLOCK_WEAPON ? FIELD_COUNT(weapon_fields) : FIELD_COUNT(shield_fields);
        void* data = block->kind == BLOCK_WEAPON ? (void*)&reader->unit.weapon : (void*)&reader->unit.ability;
        const Field* field = find_field(fields, count, key);
        if (!field)
            report(base, line->number, "unknown key '%.*s'", shown(key), key.text);
        else
            read_field(base, line, field, (u32)(field - fields), data, &block->seen);
    } break;
    case BLOCK_SKIP:
        break;
    }
}

b32 defs_read_units(BattleDefs* defs, const char* file_name, const char* text, umm size)
{
    UnitsReader reader = {.base = {defs, file_name, 0}};
    reader.stack[0] = (Block){.kind = BLOCK_ROOT, .header_indent = -1, .child_indent = 0};
    reader.depth = 1;

    Lines lines = {text, size, 0, 0};
    const char *begin, *end;
    while (next_line(&lines, &begin, &end)) {
        Line line;
        if (tokenize(&reader.base, begin, end, lines.number, &line) == LINE_OK)
            read_units_line(&reader, &line);
    }
    while (reader.depth > 1)
        close_block(&reader);
    if (defs->unit_count == 0 && reader.base.errors == 0)
        report(&reader.base, 0, "no units");
    return reader.base.errors == 0;
}

// stage.txt

internal const UnitDef* find_unit(const BattleDefs* defs, Token name, u32* index)
{
    for (u32 i = 0; i < defs->unit_count; ++i) {
        if (token_is(name, defs->units[i].name)) {
            *index = i;
            return &defs->units[i];
        }
    }
    return NULL;
}

internal void read_place(Reader* reader, const Line* line, u8 taken[BATTLE_ZONE_ROWS][BATTLE_GRID_WIDTH], u32 place_lines[BATTLE_MAX_PLACES])
{
    BattleDefs* defs = reader->defs;
    if (line->token_count != 4) {
        report(reader, line->number, "'place' takes a unit name, a cell x and a cell row");
        return;
    }
    u32 def_index = 0;
    b32 ok = true;
    if (!find_unit(defs, line->tokens[1], &def_index)) {
        report(reader, line->number, "unknown unit '%.*s'", shown(line->tokens[1]), line->tokens[1].text);
        ok = false;
    }
    f64 x = 0, row = 0;
    if (!parse_number(reader, line->number, line->tokens[2], true, &x) || !parse_number(reader, line->number, line->tokens[3], true, &row))
        return;
    if (x < 0 || x >= BATTLE_GRID_WIDTH) {
        report(reader, line->number, "cell x must be 0 to %d", BATTLE_GRID_WIDTH - 1);
        ok = false;
    }
    if (row < BATTLE_ENEMY_FIRST_ROW || row >= BATTLE_GRID_LENGTH) {
        report(reader, line->number, "cell row must be %d to %d (the enemy zone)", BATTLE_ENEMY_FIRST_ROW, BATTLE_GRID_LENGTH - 1);
        ok = false;
    }
    if (!ok)
        return;
    u32 cell_x = (u32)x, cell_row = (u32)row - BATTLE_ENEMY_FIRST_ROW;
    if (taken[cell_row][cell_x]) {
        report(reader, line->number, "cell (%u, %u) already has a unit", cell_x, (u32)row);
        return;
    }
    taken[cell_row][cell_x] = 1;
    place_lines[defs->enemy_count] = line->number;
    defs->enemy[defs->enemy_count++] = (StagePlace){(u8)def_index, (u8)cell_x, (u8)row};
}

b32 defs_read_stage(BattleDefs* defs, const char* file_name, const char* text, umm size)
{
    Reader reader = {defs, file_name, 0};
    u8 taken[BATTLE_ZONE_ROWS][BATTLE_GRID_WIDTH] = {0};
    u32 place_lines[BATTLE_MAX_PLACES];
    b32 has_supply = false, has_seed = false, supply_ok = false;
    f64 supply = 0, seed = 1;
    defs->seed = 1;

    Lines lines = {text, size, 0, 0};
    const char *begin, *end;
    while (next_line(&lines, &begin, &end)) {
        Line line;
        if (tokenize(&reader, begin, end, lines.number, &line) != LINE_OK)
            continue;
        Token key = line.tokens[0];
        if (line.indent != 0) {
            report(&reader, line.number, "unexpected indentation");
        } else if (token_is(key, "supply") || token_is(key, "seed")) {
            b32 is_supply = token_is(key, "supply");
            b32* seen = is_supply ? &has_supply : &has_seed;
            if (*seen) {
                report(&reader, line.number, "'%s' is given twice", is_supply ? "supply" : "seed");
                continue;
            }
            *seen = true;
            f64 value = 0;
            f64 lowest = is_supply ? 1 : 0, highest = is_supply ? 100000 : 4294967295.0;
            if (line.token_count != 2) {
                report(&reader, line.number, "'%s' takes one integer", is_supply ? "supply" : "seed");
            } else if (parse_number(&reader, line.number, line.tokens[1], true, &value)) {
                if (value < lowest || value > highest)
                    report(&reader, line.number, "'%s' must be %.0f to %.0f", is_supply ? "supply" : "seed", lowest, highest);
                else if (is_supply)
                    supply = value, supply_ok = true;
                else
                    seed = value;
            }
        } else if (token_is(key, "place")) {
            read_place(&reader, &line, taken, place_lines);
        } else {
            report(&reader, line.number, "unknown key '%.*s'", shown(key), key.text);
        }
    }

    if (!has_supply)
        report(&reader, 0, "missing 'supply'");
    if (defs->enemy_count == 0 && reader.errors == 0)
        report(&reader, 0, "no 'place' lines");
    if (supply_ok) {
        // The first place that takes the total over the supply is the one reported.
        u32 total = 0;
        for (u32 i = 0; i < defs->enemy_count; ++i) {
            total += defs->units[defs->enemy[i].def].cost;
            if (total > supply) {
                report(&reader, place_lines[i], "the total cost is over the supply (%.0f)", supply);
                break;
            }
        }
    }
    defs->supply = (u32)supply;
    defs->seed = (u32)seed;
    return reader.errors == 0;
}

// The auto-battler's project file and rules (autobattler/defs.c, battle.c; docs/specs/abproj.md and battle.md, "Tests"). No
// GPU. The format is tried only with text made here, so that changing a value in the repository's project file breaks no
// test; that file (BATTLE_DATA_DIR/default.abproj) is only checked to read and to play a round. The rules run on small
// scenes made here, where shells are sometimes put into the battle by hand so that one rule at a time is tried.

#include "autobattler/battle.h"
#include "engine/log.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

global int failures;
#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            printf("FAILED line %d: %s\n", __LINE__, #condition);                 \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

internal b32 near_value(f32 a, f32 b, f32 tolerance)
{
    return fabsf(a - b) <= tolerance;
}

global BattleDefs defs;
global Battle battle;

// Definition files

internal umm read_file(const char* name, char* buffer, umm capacity)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", BATTLE_DATA_DIR, name);
    FILE* file = fopen(path, "rb");
    if (!file) {
        printf("cannot open %s\n", path);
        ++failures;
        return 0;
    }
    umm size = fread(buffer, 1, capacity, file);
    fclose(file);
    return size;
}

// The parts of a project file. A test that tries one part is wrapped in good parts around it, so the lines before the
// fragment are counted (GOOD_*_LINES) and an expected line is the fragment's own.
#define FILE_NAME "default.abproj"

global const char good_header[] =
    "abproj_version 1\n"
    "rules\n"
    "    cell_size 2\n"
    "    grid 32 48\n"
    "    zone_rows 14\n"
    "    round_time 60\n"
    "    gravity 9.8\n"
    "    retarget_interval 0.25\n"
    "    stop_fraction 0.9\n"
    "    min_damage_fraction 0.25\n";
#define GOOD_HEADER_LINES 10

global const char good_units[] =
    "unit Crawler\n"
    "    cost 100\n"
    "    health 120\n"
    "    speed 5\n"
    "    radius 0.5\n"
    "    height 0.8\n"
    "    weapon Lobber\n"
    "        range 20\n"
    "        damage 30\n"
    "        cooldown 1.5\n"
    "        launch_angle 45\n";
#define GOOD_UNITS_LINES 11

// A stage's lines, not yet indented.
global const char good_stage[] =
    "supply 1000\n"
    "place Crawler 12 36\n";

global char project_text[32768];

// Appends `fragment` to project_text, each of its non-empty lines indented by `indent` spaces.
internal void append_indented(const char* fragment, u32 indent)
{
    u32 used = (u32)strlen(project_text);
    b32 line_start = true;
    for (const char* p = fragment; *p; ++p) {
        if (line_start && *p != '\n')
            for (u32 i = 0; i < indent; ++i)
                project_text[used++] = ' ';
        project_text[used++] = *p;
        line_start = *p == '\n';
    }
    project_text[used] = 0;
}

typedef enum Part { PART_RULES, PART_UNITS, PART_STAGE } Part;

// Makes project_text with `fragment` as the part named, the other parts good; returns the number of lines before the
// fragment's first. A units fragment is followed by no stage (a read of it fails, or the test adds `place_unit`'s: with a
// name, a stage that places that unit follows). A rules fragment replaces the `rules` block's lines.
internal u32 make_project(Part part, const char* fragment, const char* place_unit)
{
    project_text[0] = 0;
    u32 before = 0;
    if (part == PART_RULES) {
        append_indented("abproj_version 1\nrules\n", 0);
        before = 2;
        append_indented(fragment, 4);
        append_indented(good_units, 0);
        append_indented("stage\n", 0);
        append_indented(good_stage, 4);
        return before;
    }
    append_indented(good_header, 0);
    before = GOOD_HEADER_LINES;
    if (part == PART_UNITS) {
        append_indented(fragment, 0);
        if (place_unit) {
            append_indented("stage\n    supply 1000\n", 0);
            char place[96];
            snprintf(place, sizeof(place), "    place %s 12 36\n", place_unit);
            append_indented(place, 0);
        }
    } else {
        append_indented(good_units, 0);
        append_indented("stage\n", 0);
        before += GOOD_UNITS_LINES + 1;
        append_indented(fragment, 4);
    }
    return before;
}

// Reads `text` as a whole project file; returns what the read returned.
internal b32 read_project(const char* text)
{
    memset(&defs, 0, sizeof(defs));
    nv_log_clear();
    return defs_read_project(&defs, FILE_NAME, text, strlen(text));
}

// Reads a part wrapped in good ones.
internal b32 read_part(Part part, const char* fragment, const char* place_unit)
{
    make_project(part, fragment, place_unit);
    return read_project(project_text);
}

internal b32 read_units(const char* fragment, const char* place_unit)
{
    return read_part(PART_UNITS, fragment, place_unit);
}

internal b32 read_stage(const char* fragment)
{
    return read_part(PART_STAGE, fragment, NULL);
}

internal b32 first_error_has(const char* text)
{
    return strstr(defs.first_error, text) != NULL;
}

enum { WHOLE_FILE = 0, BLOCK_LINE = 0xFFFF }; // an expected line: none (an error about the whole file), or the part's header

// The read must fail with `count` errors, the first on `line` (of the fragment) with `message` in it. A fragment of a units
// part has no stage after it, so a good unit would not make it succeed: every fragment here fails.
internal void expect_error_in(Part part, const char* fragment, u32 count, u32 line, const char* message)
{
    u32 before = make_project(part, fragment, NULL);
    char prefix[48];
    if (line == WHOLE_FILE)
        snprintf(prefix, sizeof(prefix), "%s: ", FILE_NAME); // an error about the whole file
    else if (line == BLOCK_LINE)
        snprintf(prefix, sizeof(prefix), "%s:%u:", FILE_NAME, before); // the part's own header, the line before the fragment
    else
        snprintf(prefix, sizeof(prefix), "%s:%u:", FILE_NAME, before + line);
    b32 ok = read_project(project_text);
    if (ok || defs.error_count != count || strncmp(defs.first_error, prefix, strlen(prefix)) != 0 || !first_error_has(message)) {
        printf("FAILED expect_error: wanted %u error(s), first \"%s ... %s\"; got ok=%d, %u error(s), first \"%s\"\n", count, prefix,
               message, ok, defs.error_count, defs.first_error);
        ++failures;
    }
}

// The same for a whole file, with the line in the file.
internal void expect_error_file(const char* text, u32 count, u32 line, const char* message)
{
    char prefix[48];
    if (line)
        snprintf(prefix, sizeof(prefix), "%s:%u:", FILE_NAME, line);
    else
        snprintf(prefix, sizeof(prefix), "%s: ", FILE_NAME);
    b32 ok = read_project(text);
    if (ok || defs.error_count != count || strncmp(defs.first_error, prefix, strlen(prefix)) != 0 || !first_error_has(message)) {
        printf("FAILED expect_error_file: wanted %u error(s), first \"%s ... %s\"; got ok=%d, %u error(s), first \"%s\"\n", count,
               prefix, message, ok, defs.error_count, defs.first_error);
        ++failures;
    }
}

// Whether `text` is whole UTF-8 characters: no lead byte without its continuation bytes, and no stray continuation byte.
internal b32 valid_utf8(const char* text)
{
    for (const u8* p = (const u8*)text; *p;) {
        u32 length = *p < 0x80 ? 1 : (*p >> 5) == 0x6 ? 2 : (*p >> 4) == 0xE ? 3 : (*p >> 3) == 0x1E ? 4 : 0;
        if (!length)
            return false;
        for (u32 i = 1; i < length; ++i)
            if ((p[i] & 0xC0) != 0x80)
                return false;
        p += length;
    }
    return true;
}

internal b32 log_has(const char* text)
{
    for (u32 i = 0; i < nv_log_ring.count; ++i) {
        const NvLogMessage* message = nv_log_message(i);
        if (message->level == NV_LOG_ERROR && strncmp(nv_log_text(message), text, strlen(text)) == 0)
            return true;
    }
    return false;
}

// The values of the repository's default.abproj as they were when the rules moved out of code; a copy here, so that a
// designer changing the file breaks nothing. The rounds below (and their golden hashes) play on it.
global const char full_project[] =
    "# a project like autobattler/data/default.abproj\n"
    "abproj_version 1\n"
    "\n"
    "rules\n"
    "    cell_size 2              # m\n"
    "    grid 32 48               # cells across, rows long\n"
    "    zone_rows 14             # each side's deployment rows\n"
    "    round_time 60            # s\n"
    "    gravity 9.8              # m/s²\n"
    "    retarget_interval 0.25   # s\n"
    "    stop_fraction 0.9        # of the weapon's range\n"
    "    min_damage_fraction 0.25 # armor never takes more than 75% of a hit\n"
    "\n"
    "unit Crawler\n"
    "    cost 100\n"
    "    health 120\n"
    "    armor 5\n"
    "    speed 5            # m/s\n"
    "    radius 0.5\n"
    "    height 0.8\n"
    "    weapon Lobber\n"
    "        range 20\n"
    "        damage 30\n"
    "        cooldown 1.5\n"
    "        launch_angle 45    # degrees\n"
    "        spread 0.5\n"
    "        muzzle 0 0.6 0.3\n"
    "    ability shield\n"
    "        radius 1.2\n"
    "        capacity 60\n"
    "        regen 10           # per second\n"
    "        regen_delay 3\n"
    "\n"
    "stage\n"
    "    supply 1000\n"
    "    seed 1\n"
    "    place Crawler 12 36\n"
    "    place Crawler 14 36\n"
    "    place Crawler 16 36\n"
    "    place Crawler 18 36\n"
    "    place Crawler 13 38\n"
    "    place Crawler 15 38\n"
    "    place Crawler 17 38\n"
    "    place Crawler 14 40\n"
    "    place Crawler 16 40\n"
    "    place Crawler 15 42\n";

internal void test_project_file(void)
{
    CHECK(read_project(full_project));
    CHECK(defs.error_count == 0);
    const BattleRules* rules = &defs.rules;
    CHECK(rules->cell_size == 2.0f && rules->grid_width == 32 && rules->grid_length == 48 && rules->zone_rows == 14);
    CHECK(rules->round_ticks == 1800 && rules->retarget_ticks == 8); // 60 s and 0.25 s at 30 Hz
    CHECK(rules->gravity == 9.8f && rules->stop_fraction == 0.9f && rules->min_damage_fraction == 0.25f);
    CHECK(battle_enemy_first_row(rules) == 34);
    CHECK(defs.unit_count == 1);
    const UnitDef* crawler = &defs.units[0];
    CHECK(strcmp(crawler->name, "Crawler") == 0);
    CHECK(crawler->cost == 100 && crawler->health == 120.0f && crawler->armor == 5.0f && crawler->speed == 5.0f);
    CHECK(crawler->radius == 0.5f && crawler->height == 0.8f);
    CHECK(strcmp(crawler->weapon.name, "Lobber") == 0);
    CHECK(crawler->weapon.range == 20.0f && crawler->weapon.damage == 30.0f && crawler->weapon.cooldown == 1.5f);
    CHECK(near_value(crawler->weapon.launch_angle, NV_PI / 4.0f, 1e-6f)); // degrees in the file, radians here
    CHECK(crawler->weapon.spread == 0.5f);
    CHECK(crawler->weapon.muzzle.x == 0.0f && crawler->weapon.muzzle.y == 0.6f && crawler->weapon.muzzle.z == 0.3f);
    CHECK(crawler->ability.kind == ABILITY_SHIELD);
    CHECK(crawler->ability.shield.radius == 1.2f && crawler->ability.shield.capacity == 60.0f);
    CHECK(crawler->ability.shield.regen == 10.0f && crawler->ability.shield.regen_delay == 3.0f);
    CHECK(defs.supply == 1000 && defs.seed == 1 && defs.enemy_count == 10);
    CHECK(defs.enemy[0].def == 0 && defs.enemy[0].cell_x == 12 && defs.enemy[0].cell_row == 36);
    CHECK(defs.enemy[9].cell_x == 15 && defs.enemy[9].cell_row == 42);
}

#define RULE(key) ((DefsKey){DEFS_RULES, NULL, key})

// Editing the rules in a project's text (defs_value_get, defs_value_set): the values as the file writes them, a change that
// touches only its values (the comment keeps its column) and reads back, and what is refused.
internal void test_rule_editing(void)
{
    static char edited[sizeof(full_project) + 64];
    umm size = sizeof(full_project) - 1;
    f64 values[DEFS_MAX_VALUES] = {0};
    CHECK(defs_value_get(full_project, size, RULE("grid"), values) == 2 && values[0] == 32.0 && values[1] == 48.0);
    CHECK(defs_value_get(full_project, size, RULE("round_time"), values) == 1 && values[0] == 60.0); // seconds, not ticks
    CHECK(defs_value_get(full_project, size, RULE("retarget_interval"), values) == 1 && values[0] == 0.25);
    CHECK(defs_value_get(full_project, size, RULE("gravity"), values) == 1 && values[0] == 9.8);
    CHECK(defs_value_get(full_project, size, RULE("supply"), values) == 0); // the stage's, not a rule
    CHECK(defs_value_get(full_project, size, RULE("radius"), values) == 0);
    CHECK(defs_value_get(full_project, size, RULE("rules"), values) == 0);

    // Writing every rule's own values back gives the same text, byte for byte.
    const char* keys[] = {"cell_size", "grid", "zone_rows", "round_time", "gravity", "retarget_interval", "stop_fraction",
                          "min_damage_fraction"};
    for (u32 i = 0; i < NV_ARRAY_COUNT(keys); ++i) {
        u32 count = defs_value_get(full_project, size, RULE(keys[i]), values);
        CHECK(count > 0);
        umm written = defs_value_set(full_project, size, RULE(keys[i]), values, count, edited, sizeof(edited));
        CHECK(written == size && memcmp(edited, full_project, size) == 0);
    }

    // A longer value takes spaces from before the comment, a shorter one gives them back; other lines stay.
    f64 gravity = 12.3456789;
    umm written = defs_value_set(full_project, size, RULE("gravity"), &gravity, 1, edited, sizeof(edited));
    edited[written] = 0;
    CHECK(written == size && strstr(edited, "\n    gravity 12.3457          # m/s²\n"));
    gravity = 3.0;
    written = defs_value_set(full_project, size, RULE("gravity"), &gravity, 1, edited, sizeof(edited));
    edited[written] = 0;
    CHECK(written == size && strstr(edited, "\n    gravity 3                # m/s²\n"));
    CHECK(memcmp(edited, full_project, (umm)(strstr(full_project, "    gravity") - full_project)) == 0);
    CHECK(read_project(edited) && defs.rules.gravity == 3.0f && defs.rules.cell_size == 2.0f);
    f64 longer = 0.123; // past the comment's column: one space is kept
    written = defs_value_set(full_project, size, RULE("min_damage_fraction"), &longer, 1, edited, sizeof(edited));
    edited[written] = 0;
    CHECK(strstr(edited, "\n    min_damage_fraction 0.123 # armor"));
    f64 negative_zero = -0.00001;
    written = defs_value_set(full_project, size, RULE("min_damage_fraction"), &negative_zero, 1, edited, sizeof(edited));
    edited[written] = 0;
    CHECK(strstr(edited, "\n    min_damage_fraction 0    # armor"));

    // Integers are rounded; the grid takes two values; seconds stay seconds and become ticks when read.
    f64 grid[2] = {40.4, 47.6}; // the length stays 48: the stage's places are in the last 14 rows
    written = defs_value_set(full_project, size, RULE("grid"), grid, 2, edited, sizeof(edited));
    edited[written] = 0;
    CHECK(strstr(edited, "\n    grid 40 48               # cells across"));
    CHECK(read_project(edited) && defs.rules.grid_width == 40 && defs.rules.grid_length == 48);
    f64 round_time = 90.5;
    written = defs_value_set(full_project, size, RULE("round_time"), &round_time, 1, edited, sizeof(edited));
    edited[written] = 0;
    CHECK(read_project(edited) && defs.rules.round_ticks == 2715);
    CHECK(defs_value_get(edited, written, RULE("round_time"), values) == 1 && values[0] == 90.5);

    // A line without a comment, and CRLF line ends, keep what follows the values.
    const char* plain = "abproj_version 1\r\nrules\r\n    zone_rows 14\r\n";
    f64 rows = 9.0;
    written = defs_value_set(plain, strlen(plain), RULE("zone_rows"), &rows, 1, edited, sizeof(edited));
    CHECK(written == strlen(plain) - 1 && memcmp(edited, "abproj_version 1\r\nrules\r\n    zone_rows 9\r\n", written) == 0);

    // A value the rules refuse is still written: the reader judges it.
    f64 rows_too_many = 30.0; // over half the grid's 48 rows
    written = defs_value_set(full_project, size, RULE("zone_rows"), &rows_too_many, 1, edited, sizeof(edited));
    edited[written] = 0;
    CHECK(written > 0 && !read_project(edited));

    // Refused: not a rule, the wrong number of values, no room.
    f64 one = 1.0;
    CHECK(defs_value_set(full_project, size, RULE("supply"), &one, 1, edited, sizeof(edited)) == 0);
    CHECK(defs_value_set(full_project, size, RULE("grid"), &one, 1, edited, sizeof(edited)) == 0);
    CHECK(defs_value_set(full_project, size, RULE("gravity"), grid, 2, edited, sizeof(edited)) == 0);
    gravity = 9.8;
    CHECK(defs_value_set(full_project, size, RULE("gravity"), &gravity, 1, edited, size - 1) == 0);
    CHECK(defs_value_set(full_project, size, RULE("gravity"), &gravity, 1, edited, size) == size);
}

// Editing units in a project's text: a unit's own keys, its weapon's and its shield's, by the unit's name; a key the text
// omits is read as 0 and gets a line of its own when it is set.
internal void test_unit_editing(void)
{
    // Two units with the same keys, so that a change to one is seen to leave the other alone. Brute has no shield, no armor
    // and no spread, and its weapon is the file's last block.
    static const char two_units[] =
        "abproj_version 1\n"
        "rules\n"
        "    cell_size 2\n"
        "    grid 32 48\n"
        "    zone_rows 14\n"
        "    round_time 60\n"
        "    gravity 9.8\n"
        "    retarget_interval 0.25\n"
        "    stop_fraction 0.9\n"
        "    min_damage_fraction 0.25\n"
        "unit Crawler\n"
        "    cost 100\n"
        "    health 120\n"
        "    armor 5            # plates\n"
        "    radius 0.5\n"
        "    height 0.8\n"
        "    speed 5\n"
        "    weapon Lobber\n"
        "        range 20\n"
        "        damage 30\n"
        "        cooldown 1.5\n"
        "        launch_angle 45    # degrees\n"
        "    ability shield\n"
        "        radius 1.2\n"
        "        capacity 60\n"
        "        regen 10\n"
        "        regen_delay 3\n"
        "unit Brute\n"
        "    cost 200\n"
        "    health 300\n"
        "    speed 3\n"
        "    radius 0.9\n"
        "    height 1.4\n"
        "    weapon Fist\n"
        "        range 2\n"
        "        damage 50\n"
        "        cooldown 1\n"
        "        launch_angle 10\n"
        "stage\n"
        "    supply 1000\n"
        "    place Crawler 12 36\n"
        "    place Brute 14 36\n";
    static char edited[sizeof(two_units) + 256];
    umm size = sizeof(two_units) - 1;
    CHECK(read_project(two_units));
    f64 values[DEFS_MAX_VALUES] = {0};
    DefsKey crawler_radius = {DEFS_UNIT, "Crawler", "radius"};
    DefsKey shield_radius = {DEFS_ABILITY, "Crawler", "radius"};
    DefsKey brute_radius = {DEFS_UNIT, "Brute", "radius"};
    CHECK(defs_value_get(two_units, size, crawler_radius, values) == 1 && values[0] == 0.5);
    CHECK(defs_value_get(two_units, size, shield_radius, values) == 1 && values[0] == 1.2);
    CHECK(defs_value_get(two_units, size, brute_radius, values) == 1 && values[0] == 0.9);
    CHECK(defs_value_get(two_units, size, (DefsKey){DEFS_WEAPON, "Crawler", "launch_angle"}, values) == 1 && values[0] == 45.0);
    CHECK(defs_value_get(two_units, size, (DefsKey){DEFS_UNIT, "Crawler", "cost"}, values) == 1 && values[0] == 100.0);

    // Omitted keys read as the reader's default.
    values[0] = values[1] = values[2] = 7.0;
    CHECK(defs_value_get(two_units, size, (DefsKey){DEFS_UNIT, "Brute", "armor"}, values) == 1 && values[0] == 0.0);
    CHECK(defs_value_get(two_units, size, (DefsKey){DEFS_WEAPON, "Crawler", "muzzle"}, values) == 3);
    CHECK(values[0] == 0.0 && values[1] == 0.0 && values[2] == 0.0);
    // Not there: a unit without a shield, a name no unit has, a key the block has not, no unit name.
    CHECK(defs_value_get(two_units, size, (DefsKey){DEFS_ABILITY, "Brute", "radius"}, values) == 0);
    CHECK(defs_value_get(two_units, size, (DefsKey){DEFS_UNIT, "Ghost", "radius"}, values) == 0);
    CHECK(defs_value_get(two_units, size, (DefsKey){DEFS_UNIT, "Crawler", "range"}, values) == 0);
    CHECK(defs_value_get(two_units, size, (DefsKey){DEFS_WEAPON, "Crawler", "cost"}, values) == 0);
    CHECK(defs_value_get(two_units, size, (DefsKey){DEFS_UNIT, NULL, "radius"}, values) == 0);
    f64 one = 1.0;
    CHECK(defs_value_set(two_units, size, (DefsKey){DEFS_ABILITY, "Brute", "radius"}, &one, 1, edited, sizeof(edited)) == 0);
    CHECK(defs_value_set(two_units, size, (DefsKey){DEFS_UNIT, "Ghost", "radius"}, &one, 1, edited, sizeof(edited)) == 0);

    // A unit's radius is not its shield's, nor the other unit's.
    f64 radius = 0.65;
    umm written = defs_value_set(two_units, size, crawler_radius, &radius, 1, edited, sizeof(edited));
    edited[written] = 0;
    CHECK(written == size + 1 && strstr(edited, "\n    radius 0.65\n    height 0.8\n"));
    CHECK(defs_value_get(edited, written, shield_radius, values) == 1 && values[0] == 1.2);
    CHECK(defs_value_get(edited, written, brute_radius, values) == 1 && values[0] == 0.9);
    CHECK(read_project(edited) && defs.units[0].radius == 0.65f && defs.units[0].ability.shield.radius == 1.2f);
    f64 shield = 2.5;
    written = defs_value_set(two_units, size, shield_radius, &shield, 1, edited, sizeof(edited));
    edited[written] = 0;
    CHECK(strstr(edited, "\n        radius 2.5\n        capacity 60\n") && strstr(edited, "\n    radius 0.5\n"));
    CHECK(read_project(edited) && defs.units[0].ability.shield.radius == 2.5f && defs.units[0].radius == 0.5f);
    // The comment keeps its column; degrees stay degrees.
    f64 angle = 60.5;
    written = defs_value_set(two_units, size, (DefsKey){DEFS_WEAPON, "Crawler", "launch_angle"}, &angle, 1, edited, sizeof(edited));
    edited[written] = 0;
    CHECK(strstr(edited, "\n        launch_angle 60.5  # degrees\n"));
    CHECK(read_project(edited) && near_value(defs.units[0].weapon.launch_angle, 60.5f * NV_PI / 180.0f, 1e-6f));
    f64 cost = 150.4;
    written = defs_value_set(two_units, size, (DefsKey){DEFS_UNIT, "Brute", "cost"}, &cost, 1, edited, sizeof(edited));
    edited[written] = 0;
    CHECK(strstr(edited, "unit Brute\n    cost 150\n") && strstr(edited, "unit Crawler\n    cost 100\n"));

    // An omitted key gets a line after the block's last own line: Brute's armor after its height (before the weapon block),
    // Crawler's muzzle after its weapon's launch_angle (before the ability block), Brute's spread at the end of the file's
    // last unit block (before 'stage').
    f64 armor = 2.0;
    written = defs_value_set(two_units, size, (DefsKey){DEFS_UNIT, "Brute", "armor"}, &armor, 1, edited, sizeof(edited));
    edited[written] = 0;
    CHECK(strstr(edited, "\n    height 1.4\n    armor 2\n    weapon Fist\n"));
    CHECK(read_project(edited) && defs.units[1].armor == 2.0f && defs.units[0].armor == 5.0f);
    f64 muzzle[3] = {0.1, 0.6, -0.25};
    written = defs_value_set(two_units, size, (DefsKey){DEFS_WEAPON, "Crawler", "muzzle"}, muzzle, 3, edited, sizeof(edited));
    edited[written] = 0;
    CHECK(strstr(edited, "\n        launch_angle 45    # degrees\n        muzzle 0.1 0.6 -0.25\n    ability shield\n"));
    CHECK(read_project(edited) && defs.units[0].weapon.muzzle.x == 0.1f && defs.units[0].weapon.muzzle.z == -0.25f);
    CHECK(defs_value_get(edited, written, (DefsKey){DEFS_WEAPON, "Crawler", "muzzle"}, values) == 3 && values[1] == 0.6);
    f64 spread = 0.75;
    written = defs_value_set(two_units, size, (DefsKey){DEFS_WEAPON, "Brute", "spread"}, &spread, 1, edited, sizeof(edited));
    edited[written] = 0;
    CHECK(strstr(edited, "\n        launch_angle 10\n        spread 0.75\nstage\n"));
    CHECK(read_project(edited) && defs.units[1].weapon.spread == 0.75f);
    // Set again, the inserted line is changed in place, and stays at 0.
    static char again[sizeof(edited)];
    f64 zero = 0.0;
    umm again_size = defs_value_set(edited, written, (DefsKey){DEFS_WEAPON, "Brute", "spread"}, &zero, 1, again, sizeof(again));
    again[again_size] = 0;
    CHECK(again_size == written - 3 && strstr(again, "\n        spread 0\nstage\n"));

    // CRLF line ends, and a block that ends the text without a line end.
    const char* crlf = "abproj_version 1\r\nrules\r\n    zone_rows 14\r\nunit A\r\n    cost 1\r\n    weapon W\r\n        range 2\r\n";
    written = defs_value_set(crlf, strlen(crlf), (DefsKey){DEFS_UNIT, "A", "armor"}, &armor, 1, edited, sizeof(edited));
    edited[written] = 0;
    CHECK(strcmp(edited, "abproj_version 1\r\nrules\r\n    zone_rows 14\r\nunit A\r\n    cost 1\r\n    armor 2\r\n    weapon W\r\n        range 2\r\n") == 0);
    const char* unended = "unit A\n    cost 1\n    weapon W\n        range 2";
    written = defs_value_set(unended, strlen(unended), (DefsKey){DEFS_WEAPON, "A", "spread"}, &spread, 1, edited, sizeof(edited));
    edited[written] = 0;
    CHECK(strcmp(edited, "unit A\n    cost 1\n    weapon W\n        range 2\n        spread 0.75") == 0);
}

// The repository's own file: it reads without errors, whatever values a designer has put in it, and the round it plays ends
// within its time limit. (Its values are not checked; test_project_file does that on a copy.)
internal void test_repository_file(void)
{
    static char text[16384];
    umm size = read_file("default.abproj", text, sizeof(text) - 1);
    CHECK(size > 0 && size < sizeof(text) - 1);
    text[size] = 0;
    CHECK(read_project(text));
    CHECK(defs.error_count == 0 && defs.unit_count >= 1 && defs.enemy_count >= 1);

    // Fill the player's zone, from the row nearest the enemy and every other cell, with the first unit while the supply lasts.
    battle_init(&battle, &defs);
    for (s32 row = (s32)defs.rules.zone_rows - 1; row >= 0; --row)
        for (s32 x = 0; x < (s32)defs.rules.grid_width; x += 2)
            battle_place(&battle, 0, x, row);
    CHECK(battle_start(&battle));
    while (battle.phase == BATTLE_FIGHT) {
        battle_tick(&battle);
        battle.event_count = 0;
    }
    CHECK(battle.phase == BATTLE_RESULT && battle.tick <= defs.rules.round_ticks && battle.outcome != OUTCOME_NONE);
}

internal void test_definition_syntax(void)
{
    // Comments, blank lines and CRLF line ends give the same values as plain text.
    CHECK(read_units(good_units, "Crawler"));
    UnitDef plain = defs.units[0];
    const char* messy =
        "# a comment line\r\n"
        "\r\n"
        "unit Crawler   # trailing comment\r\n"
        "    cost   100\r\n"
        "\r\n"
        "    health 120\r\n"
        "    speed 5\r\n"
        "    radius 0.5\r\n"
        "    height .8\r\n" // no digit before the point
        "    weapon Lobber\r\n"
        "        range +20\r\n" // a sign
        "        damage 30\r\n"
        "        cooldown 1.5\r\n"
        "        launch_angle 45\r\n";
    CHECK(read_units(messy, "Crawler"));
    CHECK(memcmp(&defs.units[0], &plain, sizeof(plain)) == 0);
    // The last line may lack its line end.
    CHECK(read_project("abproj_version 1\nrules\n cell_size 2\n grid 32 48\n zone_rows 14\n round_time 60\n gravity 9.8\n retarget_interval 0.25\n"
                       " stop_fraction 0.9\n min_damage_fraction 0.25\n"
                       "unit A\n cost 1\n health 1\n speed 0\n radius 1\n height 1\n weapon W\n  range 1\n  damage 0\n  cooldown 1\n  launch_angle 1\n"
                       "stage\n supply 100\n place A 12 36"));

    // Defaults: no armor, no spread, a muzzle at the origin, no ability.
    CHECK(read_units(good_units, "Crawler"));
    CHECK(defs.units[0].armor == 0.0f && defs.units[0].weapon.spread == 0.0f && defs.units[0].weapon.muzzle.z == 0.0f);
    CHECK(defs.units[0].ability.kind == ABILITY_NONE);

    // Two units, and the shield's keys can come in any order.
    CHECK(read_units("unit A\n"
                     "  cost 5\n  health 10\n  speed 1\n  radius 1\n  height 1\n"
                     "  ability shield\n    regen_delay 1\n    regen 2\n    capacity 3\n    radius 4\n"
                     "  weapon W\n    range 1\n    damage 0\n    cooldown 1\n    launch_angle 30\n"
                     "unit B\n"
                     "  cost 6\n  health 10\n  speed 1\n  radius 1\n  height 1\n"
                     "  weapon W\n    range 1\n    damage 0\n    cooldown 1\n    launch_angle 30\n",
                     "A"));
    CHECK(defs.unit_count == 2 && defs.units[0].ability.shield.regen_delay == 1.0f && defs.units[0].ability.shield.radius == 4.0f);
    CHECK(defs.units[1].cost == 6 && strcmp(defs.units[1].name, "B") == 0 && defs.units[1].ability.kind == ABILITY_NONE);

    // The stage: the seed defaults to 1 and may be given; the keys come in any order.
    CHECK(read_stage(good_stage));
    CHECK(defs.seed == 1 && defs.supply == 1000 && defs.enemy_count == 1);
    CHECK(read_stage("seed 4294967295\nplace Crawler 0 47\nplace Crawler 31 34\nsupply 1000\n"));
    CHECK(defs.seed == 4294967295u && defs.enemy_count == 2 && defs.enemy[1].cell_x == 31 && defs.enemy[1].cell_row == 34);
}

// The version line, the order of the top-level statements, and the rules block.
internal void test_project_structure(void)
{
    // The version: the first statement, an integer from 1 to ABPROJ_VERSION.
    CHECK(ABPROJ_VERSION == 1);
    char text[4096];
    snprintf(text, sizeof(text), "%s%sstage\n    supply 1000\n    place Crawler 12 36\n", good_header, good_units);
    CHECK(read_project(text));
    // No version line: the first statement is something else. One error, at that statement (the read goes on as if it had one).
    snprintf(text, sizeof(text), "%s%sstage\n    supply 1000\n    place Crawler 12 36\n", strstr(good_header, "rules"), good_units);
    expect_error_file(text, 1, 1, "the file must start with 'abproj_version <version>'");
    expect_error_file("", 1, 0, "the file must start with 'abproj_version <version>'");
    snprintf(text, sizeof(text), "# only a comment\n\nabproj_version 1\n%s%sstage\n    supply 1000\n    place Crawler 12 36\n",
             strstr(good_header, "rules"), good_units);
    CHECK(read_project(text)); // comments and blank lines before it are not statements
    snprintf(text, sizeof(text), "abproj_version 1\nabproj_version 1\n%s%sstage\n    supply 1000\n    place Crawler 12 36\n",
             strstr(good_header, "rules"), good_units);
    expect_error_file(text, 1, 2, "'abproj_version' must be the first statement");
    snprintf(text, sizeof(text), "abproj_version 0\n%s%sstage\n    supply 1000\n    place Crawler 12 36\n", strstr(good_header, "rules"), good_units);
    expect_error_file(text, 1, 1, "'abproj_version' must be 1 to 1");
    snprintf(text, sizeof(text), "abproj_version 1.5\n%s%sstage\n    supply 1000\n    place Crawler 12 36\n", strstr(good_header, "rules"), good_units);
    expect_error_file(text, 1, 1, "not an integer");
    expect_error_file("abproj_version\n", 1, 1, "'abproj_version' takes one integer");
    // A newer format is one error and the rest is not read (it may mean anything by it).
    expect_error_file("abproj_version 2\nrules\n    bogus 1\nunit\n", 1, 1, "format 2 is newer than this build reads (1)");

    // The order: version, rules, units, stage. A statement out of place is one error and its block is skipped.
    char swapped[4096];
    snprintf(swapped, sizeof(swapped), "abproj_version 1\n%s%s%s", good_units, strstr(good_header, "rules"),
             "stage\n    supply 1000\n    place Crawler 12 36\n");
    expect_error_file(swapped, 2, 2, "'unit' must come after 'rules'"); // and the stage, with no unit read before it, is out of place too
    snprintf(swapped, sizeof(swapped), "%s%s%s", good_header, "stage\n    supply 1000\n    place Crawler 12 36\n", good_units);
    expect_error_file(swapped, 1, 11, "'stage' must come after the units"); // the units after it are read all the same
    snprintf(swapped, sizeof(swapped), "%s%s%s%s", good_header, good_units, "stage\n    supply 1000\n    place Crawler 12 36\n", good_units);
    expect_error_file(swapped, 1, 25, "'unit' must come before 'stage'");
    snprintf(swapped, sizeof(swapped), "%s%s%s%s", good_header, good_units, "stage\n    supply 1000\n    place Crawler 12 36\n",
             "stage\n    supply 1000\n");
    expect_error_file(swapped, 1, 25, "'stage' is given twice");
    snprintf(swapped, sizeof(swapped), "%s%s%s", good_header, strstr(good_header, "rules"), good_units);
    expect_error_file(swapped, 1, 11, "'rules' is given twice");
    snprintf(swapped, sizeof(swapped), "abproj_version 1\n%s", "stage\n    supply 1000\n");
    expect_error_file(swapped, 1, 2, "'stage' must come after 'rules' and the units");
    // What a whole file lacks, once the rest is right: the first missing part.
    expect_error_file("abproj_version 1\n", 1, 0, "missing 'rules'");
    expect_error_file(good_header, 1, 0, "no units");
    snprintf(text, sizeof(text), "%s%s", good_header, good_units);
    expect_error_file(text, 1, 0, "missing 'stage'");
    expect_error_file("abproj_version 1\nbogus\n    child 1\n", 1, 2, "unknown key 'bogus'"); // its inside is skipped
    expect_error_file("abproj_version 1\n unit A\n", 1, 2, "unexpected indentation");

    // The rules: every key is required, with a range; a bad value is one error.
    {
        // clang-format off
        expect_error_in(PART_RULES, "", 8, BLOCK_LINE, "missing 'cell_size'"); // all eight keys are missing, at the block's line
        const char* all =
            "cell_size 2\ngrid 32 48\nzone_rows 14\nround_time 60\ngravity 9.8\nretarget_interval 0.25\nstop_fraction 0.9\nmin_damage_fraction 0.25\n";
        CHECK(read_part(PART_RULES, all, NULL));
        const struct { const char* key; const char* bad; const char* message; } bad_values[] = {
            {"cell_size 2\n", "cell_size 0\n", "'cell_size' must be > 0 and <= 10"},
            {"cell_size 2\n", "cell_size 10.5\n", "<= 10"},
            {"cell_size 2\n", "cell_size 2 3\n", "'cell_size' takes 1 value"},
            {"grid 32 48\n", "grid 0 48\n", "'grid' width must be 1 to 64"},
            {"grid 32 48\n", "grid 65 48\n", "'grid' width must be 1 to 64"},
            {"grid 32 48\n", "grid 32 1\n", "'grid' length must be 2 to 128"},
            {"grid 32 48\n", "grid 32 129\n", "'grid' length must be 2 to 128"},
            {"grid 32 48\n", "grid 32\n", "'grid' takes 2 values"},
            {"grid 32 48\n", "grid 32.5 48\n", "not an integer"},
            {"zone_rows 14\n", "zone_rows 0\n", "'zone_rows' must be >= 1 and <= 32"},
            {"zone_rows 14\n", "zone_rows 33\n", "<= 32"},
            {"round_time 60\n", "round_time 0\n", "'round_time' must be > 0 and <= 600"},
            {"round_time 60\n", "round_time 601\n", "<= 600"},
            {"gravity 9.8\n", "gravity 0\n", "'gravity' must be > 0 and <= 100"},
            {"gravity 9.8\n", "gravity -9.8\n", "> 0"},
            {"retarget_interval 0.25\n", "retarget_interval 0\n", "'retarget_interval' must be > 0 and <= 10"},
            {"retarget_interval 0.25\n", "retarget_interval 11\n", "<= 10"},
            {"stop_fraction 0.9\n", "stop_fraction 0\n", "'stop_fraction' must be > 0 and <= 1"},
            {"stop_fraction 0.9\n", "stop_fraction 1.1\n", "<= 1"},
            {"min_damage_fraction 0.25\n", "min_damage_fraction -0.1\n", "'min_damage_fraction' must be >= 0 and <= 1"},
            {"min_damage_fraction 0.25\n", "min_damage_fraction 1.5\n", "<= 1"},
        };
        // clang-format on
        for (u32 i = 0; i < NV_ARRAY_COUNT(bad_values); ++i) {
            // The bad line replaces the good one at its place in the block; its line is counted in the fragment.
            char rules[512];
            const char* at = strstr(all, bad_values[i].key);
            u32 before = (u32)(at - all), after = (u32)strlen(at + strlen(bad_values[i].key));
            memcpy(rules, all, before);
            memcpy(rules + before, bad_values[i].bad, strlen(bad_values[i].bad));
            memcpy(rules + before + strlen(bad_values[i].bad), at + strlen(bad_values[i].key), after + 1);
            u32 line = 1;
            for (const char* p = all; p < at; ++p)
                line += *p == '\n';
            expect_error_in(PART_RULES, rules, 1, line, bad_values[i].message);
        }
        // A key missing on its own is one error at the block's line; a key twice is one at the second; unknown keys are reported.
        expect_error_in(PART_RULES, "grid 32 48\nzone_rows 14\nround_time 60\ngravity 9.8\nretarget_interval 0.25\nstop_fraction 0.9\nmin_damage_fraction 0.25\n",
                        1, BLOCK_LINE, "block 'rules' is missing 'cell_size'");
        expect_error_in(PART_RULES, "gravity 3\ncell_size 2\ngrid 32 48\nzone_rows 14\nround_time 60\ngravity 9.8\nretarget_interval 0.25\nstop_fraction 0.9\nmin_damage_fraction 0.25\n",
                        1, 6, "'gravity' is given twice in this block");
        expect_error_in(PART_RULES, "wind 3\ncell_size 2\ngrid 32 48\nzone_rows 14\nround_time 60\ngravity 9.8\nretarget_interval 0.25\nstop_fraction 0.9\nmin_damage_fraction 0.25\n",
                        1, 1, "unknown key 'wind'");
    }
    // The zones may not overlap: zone_rows is at most half the grid's length (reported at the zone_rows line).
    CHECK(read_part(PART_RULES, "cell_size 2\ngrid 32 48\nzone_rows 24\nround_time 60\ngravity 9.8\nretarget_interval 0.25\nstop_fraction 0.9\nmin_damage_fraction 0.25\n", NULL));
    expect_error_in(PART_RULES, "cell_size 2\ngrid 32 48\nzone_rows 25\nround_time 60\ngravity 9.8\nretarget_interval 0.25\nstop_fraction 0.9\nmin_damage_fraction 0.25\n",
                    1, 3, "'zone_rows' must be at most 24"); // one error: the stage's cells are not judged against a grid that is wrong

    // Times become whole ticks, rounded up, at least 1.
    CHECK(read_part(PART_RULES, "cell_size 2\ngrid 32 48\nzone_rows 14\nround_time 10\ngravity 9.8\nretarget_interval 1\nstop_fraction 0.9\nmin_damage_fraction 0.25\n", NULL));
    CHECK(defs.rules.round_ticks == 300 && defs.rules.retarget_ticks == 30);
    CHECK(read_part(PART_RULES, "cell_size 2\ngrid 32 48\nzone_rows 14\nround_time 0.01\ngravity 9.8\nretarget_interval 0.05\nstop_fraction 0.9\nmin_damage_fraction 0.25\n", NULL));
    CHECK(defs.rules.round_ticks == 1 && defs.rules.retarget_ticks == 2); // 0.3 and 1.5 ticks
    CHECK(read_project(full_project) && defs.rules.round_ticks == 1800 && defs.rules.retarget_ticks == 8); // 7.5 ticks up to 8

    // The stage's cells follow the rules: another grid and zone move the enemy's rows.
    CHECK(read_part(PART_RULES, "cell_size 3\ngrid 10 20\nzone_rows 4\nround_time 60\ngravity 9.8\nretarget_interval 0.25\nstop_fraction 0.9\nmin_damage_fraction 0.25\n", NULL) == false);
    CHECK(first_error_has("cell x must be 0 to 9") || first_error_has("cell row must be 16 to 19"));
}

internal void test_definition_errors(void)
{
    // Each error: how many errors, the line of the first (of the fragment), and what it says.
    // clang-format off
    expect_error_in(PART_UNITS, "unit A\n  cost 1\n  healt 5\n", 6, 3, "unknown key 'healt'"); // and health, speed, radius, height, a weapon are missing
    expect_error_in(PART_UNITS, "bogus\n", 1, 1, "unknown key 'bogus'");                         // and no units
    expect_error_in(PART_UNITS, "unit A\n  cost 1\n  cost 2\n", 6, 3, "'cost' is given twice");
    expect_error_in(PART_UNITS, "unit A\n  cost 1\n", 5, 1, "missing 'health'");              // the unit's line
    expect_error_in(PART_UNITS, "unit A\n  cost 1 2\n", 6, 2, "'cost' takes 1 value");
    expect_error_in(PART_UNITS, "unit A\n  cost 0\n", 6, 2, "'cost' must be >= 1");
    expect_error_in(PART_UNITS, "unit A\n  cost 10001\n", 6, 2, "<= 10000");
    expect_error_in(PART_UNITS, "unit A\n  cost 1.5\n", 6, 2, "not an integer");
    expect_error_in(PART_UNITS, "unit A\n  cost 1\n  health 0\n", 5, 3, "'health' must be > 0");
    expect_error_in(PART_UNITS, "unit A\n  cost 1\n  health 1e3\n", 5, 3, "not a number");   // no exponents
    expect_error_in(PART_UNITS, "unit A\n  cost 1\n  health abc\n", 5, 3, "not a number");
    expect_error_in(PART_UNITS, "unit A\n  cost 1\n  health\n", 5, 3, "'health' takes 1 value");
    expect_error_in(PART_UNITS, "unit A\n  cost 1\n  armor -1\n", 6, 3, "'armor' must be >= 0");
    expect_error_in(PART_UNITS, "unit A\n\tcost 1\n", 7, 2, "tab");
    expect_error_in(PART_UNITS, "unit 1A\n", 1, 1, "not a name");
    expect_error_in(PART_UNITS, "unit A B\n", 1, 1, "'unit' takes a name");
    expect_error_in(PART_UNITS, "range 5\n", 1, 1, "unknown key 'range'");                         // a weapon's key outside its block
    expect_error_in(PART_UNITS, " unit A\n", 1, 1, "inconsistent indentation"); // taken for a line of the rules block before it
    expect_error_in(PART_UNITS, "unit A\n  cost 1\n    health 2\n", 6, 3, "inconsistent indentation");
    expect_error_in(PART_UNITS, "unit A\n  cost 1\n health 2\n", 6, 3, "inconsistent indentation");
    expect_error_in(PART_UNITS, "", 1, 0, "no units");
    // clang-format on

    // A unit needs exactly one weapon, and at most one ability.
    expect_error_in(PART_UNITS, "unit A\n  cost 1\n  health 1\n  speed 0\n  radius 1\n  height 1\n", 1, 1, "needs a weapon");
    static const char one_weapon[] = "unit A\n  cost 1\n  health 1\n  speed 0\n  radius 1\n  height 1\n"
                                     "  weapon W\n    range 1\n    damage 0\n    cooldown 1\n    launch_angle 30\n";
    char text[1024];
    snprintf(text, sizeof(text), "%s  weapon V\n    range 1\n", one_weapon);
    expect_error_in(PART_UNITS, text, 1, 12, "at most one 'weapon'");
    snprintf(text, sizeof(text), "%s  ability shield\n    radius 1\n    capacity 1\n    regen 1\n    regen_delay 1\n  ability shield\n", one_weapon);
    expect_error_in(PART_UNITS, text, 1, 17, "at most one 'ability'");
    snprintf(text, sizeof(text), "%s  ability jump\n    distance 5\n", one_weapon);
    expect_error_in(PART_UNITS, text, 1, 12, "unknown ability 'jump'"); // its inside is skipped, not reported again
    snprintf(text, sizeof(text), "%s  ability shield\n    radius 1\n", one_weapon);
    expect_error_in(PART_UNITS, text, 3, 12, "missing 'capacity'");
    // The launch angle is above 0 and below 90 degrees.
    snprintf(text, sizeof(text), "unit A\n  cost 1\n  health 1\n  speed 0\n  radius 1\n  height 1\n"
                                 "  weapon W\n    range 1\n    damage 0\n    cooldown 1\n    launch_angle 90\n");
    expect_error_in(PART_UNITS, text, 1, 11, "'launch_angle' must be > 0 and < 90");
    // The muzzle takes three numbers.
    snprintf(text, sizeof(text), "%s    muzzle 1 2\n", one_weapon);
    expect_error_in(PART_UNITS, text, 1, 12, "'muzzle' takes 3 values");

    // The same name twice, at the second one's line.
    snprintf(text, sizeof(text), "%sunit A\n  cost 1\n", one_weapon);
    CHECK(!read_units(text, NULL) && first_error_has(FILE_NAME ":22:") && first_error_has("defined twice"));

    // Every error is reported, not only the first: three lines, three logged messages.
    CHECK(!read_units("unit A\n  cost 0\n  health 0\n  speed -1\n  radius 1\n  height 1\n  weapon W\n    range 1\n    damage 0\n    cooldown 1\n    launch_angle 30\n", NULL));
    CHECK(defs.error_count == 3);
    CHECK(log_has(FILE_NAME ":12:") && log_has(FILE_NAME ":13:") && log_has(FILE_NAME ":14:"));

    // More than 20 errors: all counted, 20 logged and a note.
    char many[2048] = "";
    for (u32 i = 0; i < 25; ++i)
        strcat(many, "bogus\n");
    CHECK(!read_units(many, NULL));
    CHECK(defs.error_count == 25);
    CHECK(nv_log_ring.count == 21 && log_has(FILE_NAME ":30:") && !log_has(FILE_NAME ":31:") && log_has("more errors"));

    // At most 16 units.
    char sixteen[8192] = "";
    for (u32 i = 0; i < 17; ++i) {
        char one[512];
        snprintf(one, sizeof(one), "unit U%u\n  cost 1\n  health 1\n  speed 0\n  radius 1\n  height 1\n  weapon W\n    range 1\n    damage 0\n    cooldown 1\n    launch_angle 30\n", i);
        strcat(sixteen, one);
    }
    CHECK(!read_units(sixteen, NULL) && defs.error_count == 1 && defs.unit_count == 16 && first_error_has(FILE_NAME ":187:") && first_error_has("too many units"));

    // The stage.
    expect_error_in(PART_STAGE, "supply 1000\nplace Crawler 12 33\n", 1, 2, "cell row must be 34 to 47");
    expect_error_in(PART_STAGE, "supply 1000\nplace Crawler 32 36\n", 1, 2, "cell x must be 0 to 31");
    expect_error_in(PART_STAGE, "supply 1000\nplace Nobody 12 36\n", 1, 2, "unknown unit 'Nobody'");
    expect_error_in(PART_STAGE, "supply 1000\nplace Crawler 12 36\nplace Crawler 12 36\n", 1, 3, "already has a unit");
    expect_error_in(PART_STAGE, "supply 1000\nplace Crawler 12.5 36\n", 1, 2, "not an integer");
    expect_error_in(PART_STAGE, "supply 1000\nplace Crawler 12\n", 1, 2, "'place' takes");
    expect_error_in(PART_STAGE, "place Crawler 12 36\n", 1, BLOCK_LINE, "block 'stage' is missing 'supply'");
    expect_error_in(PART_STAGE, "supply 1000\nsupply 500\nplace Crawler 12 36\n", 1, 2, "'supply' is given twice");
    expect_error_in(PART_STAGE, "supply 0\nplace Crawler 12 36\n", 1, 1, "'supply' must be >= 1 and <= 100000");
    expect_error_in(PART_STAGE, "supply 1000\nseed -1\nplace Crawler 12 36\n", 1, 2, "'seed' must be >= 0 and <= 4294967295");
    expect_error_in(PART_STAGE, "supply 1000\n", 1, BLOCK_LINE, "'stage' has no 'place' lines");
    expect_error_in(PART_STAGE, "supply 1000\nplace Crawler 12 36\n  place Crawler 13 36\n", 1, 3, "inconsistent indentation");
    expect_error_in(PART_STAGE, "supply 1000\nbogus 1\nplace Crawler 12 36\n", 1, 2, "unknown key 'bogus'");
    // Supply: the place that takes the total over it is the one reported, wherever the supply line is.
    expect_error_in(PART_STAGE, "supply 250\nplace Crawler 1 34\nplace Crawler 2 34\nplace Crawler 3 34\nplace Crawler 4 34\n", 1, 4, "over the supply");
    expect_error_in(PART_STAGE, "place Crawler 1 34\nplace Crawler 2 34\nsupply 150\n", 1, 2, "over the supply");
    CHECK(read_stage("supply 200\nplace Crawler 1 34\nplace Crawler 2 34\n")); // exactly the supply is fine

    // A long token in a message is cut before a character, not inside one (Hangul is 3 bytes a syllable; 24 is not a
    // multiple of 3 after the one ASCII byte), in the logged message and in first_error alike.
    CHECK(!read_units("unit A\n  cost 1\n  health x\xEC\xB2\xB4\xEB\xA0\xA5\xEC\xB2\xB4\xEB\xA0\xA5\xEC\xB2\xB4\xEB\xA0\xA5"
                      "\xEC\xB2\xB4\xEB\xA0\xA5\xEC\xB2\xB4\xEB\xA0\xA5\n", NULL));
    CHECK(first_error_has("is not a number") && valid_utf8(defs.first_error));
    CHECK(!read_units("x\xEA\xB0\x80\xEA\xB0\x80\xEA\xB0\x80\xEA\xB0\x80\xEA\xB0\x80\xEA\xB0\x80\xEA\xB0\x80\xEA\xB0\x80"
                      "\xEA\xB0\x80\xEA\xB0\x80 1\n", NULL));
    CHECK(first_error_has("unknown key") && valid_utf8(defs.first_error));
    CHECK(valid_utf8(nv_log_text(nv_log_message(0))));
}

// Scenes. Two kinds of unit: 0 "Shooter" and 1 "Dummy", both 100 health, radius 0.5, height 0.8, standing still, with a
// weapon that reaches 1 m (so it never fires unless a scene sets the range). A scene sets what it needs in `defs` before
// scene_start; each test begins with scene_defs.

internal void scene_defs(void)
{
    memset(&defs, 0, sizeof(defs));
    defs.rules = (BattleRules){.cell_size = 2.0f, .grid_width = 32, .grid_length = 48, .zone_rows = 14, .round_ticks = 1800,
                               .retarget_ticks = 8, .gravity = 9.8f, .stop_fraction = 0.9f, .min_damage_fraction = 0.25f};
    defs.supply = 1000;
    defs.seed = 1;
    defs.unit_count = 2;
    const char* names[2] = {"Shooter", "Dummy"};
    for (u32 i = 0; i < 2; ++i) {
        UnitDef* def = &defs.units[i];
        snprintf(def->name, sizeof(def->name), "%s", names[i]);
        def->cost = 100;
        def->health = 100.0f;
        def->radius = 0.5f;
        def->height = 0.8f;
        snprintf(def->weapon.name, sizeof(def->weapon.name), "Gun");
        def->weapon.range = 1.0f;
        def->weapon.damage = 30.0f;
        def->weapon.cooldown = 1.5f;
        def->weapon.launch_angle = NV_PI / 4.0f;
        def->weapon.muzzle = nv_vec3(0.0f, 0.6f, 0.3f);
    }
}

internal void give_shield(u32 def, f32 capacity)
{
    defs.units[def].ability.kind = ABILITY_SHIELD;
    defs.units[def].ability.shield.radius = 1.2f;
    defs.units[def].ability.shield.capacity = capacity;
    defs.units[def].ability.shield.regen = 10.0f;
    defs.units[def].ability.shield.regen_delay = 3.0f;
}

// Units 1..player_count are the player's (one per cell of row 0), the enemy's follow (one per cell of row 34); `*_defs` are
// their unit defs. Starts the battle. Slots: the player's first.
internal void scene_start(const u8* player_defs, u32 player_count, const u8* enemy_defs, u32 enemy_count)
{
    defs.enemy_count = enemy_count;
    for (u32 i = 0; i < enemy_count; ++i)
        defs.enemy[i] = (StagePlace){enemy_defs[i], (u8)i, (u8)battle_enemy_first_row(&defs.rules)};
    battle_init(&battle, &defs);
    for (u32 i = 0; i < player_count; ++i)
        CHECK(battle_place(&battle, player_defs[i], (s32)i, 0));
    CHECK(battle_start(&battle));
}

internal void put(u32 unit, f32 x, f32 z)
{
    battle.units[unit].position = battle.units[unit].previous_position = nv_vec3(x, 0.0f, z);
}

internal void run(u32 ticks)
{
    for (u32 i = 0; i < ticks; ++i)
        battle_tick(&battle);
}

internal u32 events_of(BattleEventKind kind)
{
    u32 count = 0;
    for (u32 i = 0; i < battle.event_count; ++i)
        count += battle.events[i].kind == kind;
    return count;
}

// A shell put into the battle by hand.
internal void inject_shell(u32 team, NvVec3 position, NvVec3 velocity, f32 damage)
{
    NV_ASSERT(battle.projectile_count < BATTLE_MAX_PROJECTILES);
    battle.projectiles[battle.projectile_count++] = (Projectile){.team = (u8)team, .shooter = {1}, .position = position,
                                                                 .previous_position = position, .velocity = velocity, .damage = damage};
}

// A fast player shell that crosses `x`, `z` from 4.5 m before it, level at `y`, in two ticks (3 m per tick).
internal void inject_at(f32 x, f32 z, f32 y, f32 damage)
{
    inject_shell(TEAM_PLAYER, nv_vec3(x, y, z - 4.5f), nv_vec3(0.0f, 0.0f, 90.0f), damage);
}

global const u8 one_shooter[] = {0};
global const u8 one_dummy[] = {1};

internal void test_deployment(void)
{
    scene_defs();
    defs.enemy_count = 1;
    defs.enemy[0] = (StagePlace){0, 5, 40};
    battle_init(&battle, &defs);
    CHECK(battle.phase == BATTLE_DEPLOY && battle.unit_count == 1 && battle.units[1].team == TEAM_ENEMY);
    CHECK(!battle_start(&battle)); // nothing of the player's

    // The supply buys ten units of cost 100; the eleventh does not fit; removing one gives its supply back.
    for (s32 i = 0; i < 10; ++i)
        CHECK(battle_place(&battle, 0, i, 3));
    CHECK(battle_supply_left(&battle) == 0 && battle.unit_count == 11);
    CHECK(!battle_place(&battle, 0, 10, 3));
    CHECK(!battle_place(&battle, 0, 0, 3)); // occupied
    CHECK(battle_remove(&battle, 4, 3) && battle_supply_left(&battle) == 100 && battle.unit_count == 10);
    CHECK(!battle_remove(&battle, 4, 3)); // already empty
    CHECK(!battle_place(&battle, 2, 4, 3)); // no such unit
    CHECK(!battle_place(&battle, 0, 4, 14) && !battle_place(&battle, 0, 32, 3) && !battle_place(&battle, 0, -1, 3)); // outside the zone
    CHECK(battle_place(&battle, 1, 4, 13) && battle_supply_left(&battle) == 0);

    // The units are the placement then the stage: the player's in row order, then the enemy's, at their cell centers.
    CHECK(battle.unit_count == 11);
    CHECK(battle.units[1].team == TEAM_PLAYER && battle.units[1].position.x == 1.0f && battle.units[1].position.z == 7.0f);
    CHECK(battle.units[4].position.x == 7.0f);                     // x = 3
    CHECK(battle.units[5].position.x == 11.0f);                    // x = 5 (4 was removed)
    CHECK(battle.units[10].def == 1 && battle.units[10].position.x == 9.0f && battle.units[10].position.z == 27.0f); // row 13: the last
    CHECK(battle.units[11].team == TEAM_ENEMY && battle.units[11].position.x == 11.0f && battle.units[11].position.z == 81.0f);
    CHECK(battle.units[1].yaw == 0.0f && near_value(battle.units[11].yaw, NV_PI, 1e-6f)); // facing each other
    CHECK(battle.units[1].health == 100.0f && battle.units[1].target.index == 0);

    // Retry goes back to deployment with the same placement and fresh units; Reset clears the placement.
    CHECK(battle_start(&battle) && battle.phase == BATTLE_FIGHT);
    CHECK(!battle_place(&battle, 0, 4, 3) && !battle_remove(&battle, 0, 3)); // not while fighting
    run(3);
    battle.units[1].health = 1.0f;
    battle_retry(&battle);
    CHECK(battle.phase == BATTLE_DEPLOY && battle.tick == 0 && battle.unit_count == 11 && battle.units[1].health == 100.0f);
    CHECK(battle.units[1].position.z == 7.0f);
    battle_clear_placement(&battle);
    CHECK(battle.unit_count == 1 && battle_supply_left(&battle) == 1000 && !battle_start(&battle));
}

internal void test_targets_and_movement(void)
{
    // The nearest enemy is picked; equal distances go to the lower slot.
    scene_defs();
    u8 enemies[3] = {1, 1, 1};
    scene_start(one_shooter, 1, enemies, 3);
    put(1, 10.0f, 10.0f);
    put(2, 10.0f, 30.0f);
    put(3, 10.0f, 20.0f);
    put(4, 14.0f, 30.0f); // none of these move: the units stand still
    run(1);
    CHECK(battle.units[1].target.index == 3);
    // Distances are on the ground: height does not count.
    battle.units[2].position.y = 100.0f;
    put(2, 10.0f, 20.0f);
    battle.units[2].position.y = 100.0f; // as near as slot 3 on the ground, much higher
    battle.tick = 8;                     // a tick that picks again
    run(1);
    CHECK(battle.units[1].target.index == 2); // the tie goes to the lower slot

    // Targets are picked again every 8 ticks, and at once when the target dies.
    scene_start(one_shooter, 1, enemies, 3);
    put(1, 10.0f, 10.0f);
    put(2, 10.0f, 20.0f);
    put(3, 10.0f, 30.0f);
    put(4, 14.0f, 40.0f);
    run(1);
    CHECK(battle.units[1].target.index == 2);
    put(3, 10.0f, 15.0f); // now nearest
    for (u32 i = 1; i < 8; ++i) {
        run(1);
        CHECK(battle.units[1].target.index == 2); // ticks 1 to 7: unchanged
    }
    run(1); // the tick numbered 8
    CHECK(battle.units[1].target.index == 3);
    battle.units[3].flags |= UNIT_DEAD;
    run(1); // not a tick that picks again, but the target is dead
    CHECK(battle.units[1].target.index == 2);

    // Units walk straight at their target and stop at 90% of their range, facing it.
    scene_defs();
    for (u32 i = 0; i < 2; ++i) {
        defs.units[i].speed = 5.0f;
        defs.units[i].weapon.range = 20.0f;
        defs.units[i].weapon.damage = 0.0f;
    }
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 20.0f, 10.0f);
    put(2, 20.0f, 50.0f);
    run(1);
    CHECK(near_value(battle.units[1].position.z, 10.0f + 5.0f / 30.0f, 1e-4f) && battle.units[1].velocity.z > 4.99f);
    CHECK(near_value(battle.units[1].yaw, 0.0f, 1e-6f) && near_value(fabsf(battle.units[2].yaw), NV_PI, 1e-5f)); // faces its way
    run(300);
    f32 gap = battle.units[2].position.z - battle.units[1].position.z;
    CHECK(gap <= 18.001f && gap > 17.6f); // 0.9 of 20 m; the two close in together, so it may end a step short
    CHECK(battle.units[1].velocity.z == 0.0f && battle.units[2].velocity.z == 0.0f);
    CHECK(near_value(battle.units[1].yaw, 0.0f, 1e-5f) && near_value(fabsf(battle.units[2].yaw), NV_PI, 1e-5f));
    CHECK(battle.units[1].position.x == 20.0f && battle.units[2].position.x == 20.0f); // straight

    // Overlapping units push each other apart, half the overlap each; on one spot they part along X.
    scene_defs();
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 20.0f, 20.0f);
    put(2, 20.4f, 20.0f);
    run(1);
    CHECK(near_value(battle.units[1].position.x, 19.7f, 1e-4f) && near_value(battle.units[2].position.x, 20.7f, 1e-4f));
    put(1, 20.0f, 20.0f);
    put(2, 20.0f, 20.0f);
    run(1);
    CHECK(near_value(battle.units[1].position.x, 19.5f, 1e-4f) && near_value(battle.units[2].position.x, 20.5f, 1e-4f));
    // The field's edge holds a unit in, by its radius.
    put(1, 0.1f, 0.2f);
    put(2, 63.9f, 95.9f);
    run(1);
    CHECK(near_value(battle.units[1].position.x, 0.5f, 1e-5f) && near_value(battle.units[1].position.z, 0.5f, 1e-5f));
    CHECK(near_value(battle.units[2].position.x, 63.5f, 1e-5f) && near_value(battle.units[2].position.z, 95.5f, 1e-5f));
}

internal void test_weapon(void)
{
    // The cooldown (1.5 s = 45 ticks) and the muzzle: a shot at ticks 0, 45 and 90.
    scene_defs();
    defs.units[0].weapon.range = 30.0f;
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 33.0f, 9.0f);
    put(2, 33.0f, 24.0f);
    u32 fire_ticks[8] = {0};
    u32 fires = 0;
    for (u32 i = 0; i < 100; ++i) {
        u32 tick = battle.tick;
        battle.event_count = 0;
        run(1);
        if (events_of(BATTLE_EVENT_FIRE) && fires < 8)
            fire_ticks[fires++] = tick;
        if (tick == 0) {
            CHECK(battle.event_count == 1 && battle.events[0].kind == BATTLE_EVENT_FIRE);
            CHECK(near_value(battle.events[0].position.x, 33.0f, 1e-5f) && near_value(battle.events[0].position.y, 0.6f, 1e-5f)
                  && near_value(battle.events[0].position.z, 9.3f, 1e-4f)); // 0.3 m ahead of a unit facing +Z
            CHECK(battle.events[0].team == TEAM_PLAYER && battle.events[0].direction.z > 0.0f && battle.events[0].direction.y > 0.0f);
        }
    }
    CHECK(fires == 3 && fire_ticks[0] == 0 && fire_ticks[1] == 45 && fire_ticks[2] == 90);

    // Out of range it does not fire; in range it does.
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 33.0f, 9.0f);
    put(2, 33.0f, 39.1f);
    run(1);
    CHECK(battle.projectile_count == 0);
    put(2, 33.0f, 38.9f);
    run(1);
    CHECK(battle.projectile_count == 1);

    // A spread of 0 lands on the target (to the float's accuracy); a spread of 0.5 lands within 0.5 of it, and not always on
    // it. The target is tiny so that no shell touches it: the landing is where the shell's arc meets the ground.
    for (u32 pass = 0; pass < 2; ++pass) {
        f32 spread = pass ? 0.5f : 0.0f;
        f32 farthest = 0.0f, nearest = 100.0f;
        f32 sum_x = 0.0f;
        for (u32 seed = 1; seed <= 40; ++seed) {
            scene_defs();
            defs.seed = seed;
            defs.units[0].weapon.range = 30.0f;
            defs.units[0].weapon.cooldown = 1000.0f; // one shot
            defs.units[0].weapon.spread = spread;
            defs.units[1].radius = 0.001f;
            defs.units[1].height = 0.001f;
            scene_start(one_shooter, 1, one_dummy, 1);
            put(1, 33.0f, 9.0f);
            put(2, 33.0f, 24.0f);
            Projectile last = {0};
            b32 landed = false;
            for (u32 i = 0; i < 200 && !landed; ++i) {
                if (battle.projectile_count == 1)
                    last = battle.projectiles[0];
                run(1);
                landed = last.damage > 0.0f && battle.projectile_count == 0;
            }
            CHECK(landed);
            // The arc from the last position seen: y0 + vy t - g t^2 / 2 = 0.
            f32 t = (last.velocity.y + sqrtf(last.velocity.y * last.velocity.y + 2.0f * defs.rules.gravity * last.position.y)) / defs.rules.gravity;
            f32 x = last.position.x + last.velocity.x * t - 33.0f, z = last.position.z + last.velocity.z * t - 24.0f;
            f32 offset = sqrtf(x * x + z * z);
            farthest = fmaxf(farthest, offset);
            nearest = fminf(nearest, offset);
            sum_x += x;
            if (pass == 1)
                CHECK(battle.units[2].health == 100.0f); // with no spread the shell lands on the target's axis, inside it
        }
        if (pass == 0) {
            CHECK(farthest < 0.02f);
        } else {
            CHECK(farthest <= 0.5f + 0.01f);
            CHECK(farthest > 0.35f); // 40 draws in a disk of radius 0.5 reach near the edge
            CHECK(nearest < 0.25f);
            CHECK(fabsf(sum_x / 40.0f) < 0.15f); // and are not all on one side
        }
    }

    // A shell that lands on the target hurts it: a spread of 0 and a target of size.
    scene_defs();
    defs.units[0].weapon.range = 30.0f;
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 33.0f, 9.0f);
    put(2, 33.0f, 24.0f);
    run(70);
    CHECK(battle.units[2].health == 70.0f); // one 30 damage hit, no armor (the second shell, fired at tick 45, is still in the air)
    CHECK(events_of(BATTLE_EVENT_HIT) == 1);
}

// The muzzle is in the unit's space: x to its right. Facing +Z (the player's way) the right is -X; facing -Z, +X.
internal void test_muzzle(void)
{
    scene_defs();
    defs.units[0].weapon.range = 30.0f;
    defs.units[0].weapon.muzzle = nv_vec3(0.5f, 0.6f, 0.3f);
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 33.0f, 9.0f);
    put(2, 33.0f, 24.0f);
    run(1);
    CHECK(events_of(BATTLE_EVENT_FIRE) == 1);
    CHECK(near_value(battle.events[0].position.x, 32.5f, 1e-4f) && near_value(battle.events[0].position.z, 9.3f, 1e-4f));

    u8 shooter_enemy[1] = {0};
    scene_start(one_dummy, 1, shooter_enemy, 1);
    put(1, 33.0f, 9.0f);
    put(2, 33.0f, 24.0f);
    run(1);
    CHECK(events_of(BATTLE_EVENT_FIRE) == 1);
    CHECK(near_value(battle.events[0].position.x, 33.5f, 1e-4f) && near_value(battle.events[0].position.z, 23.7f, 1e-4f));

    // A muzzle under the ground leaves the shell no flight: it is fired still and lands at once, with no crash and no damage.
    scene_defs();
    defs.units[0].weapon.range = 30.0f;
    defs.units[0].weapon.muzzle = nv_vec3(0.0f, -5.0f, 0.0f);
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 33.0f, 9.0f);
    put(2, 33.0f, 12.0f);
    run(1);
    CHECK(events_of(BATTLE_EVENT_FIRE) == 1 && battle.projectile_count == 0);
    NvVec3 direction = battle.events[0].direction;
    CHECK(isfinite(direction.x) && isfinite(direction.y) && isfinite(direction.z) && direction.y == -1.0f);
    CHECK(battle.units[2].health == 100.0f);
}

internal void test_lead_aim(void)
{
    // A target walking straight at the shooter: aiming where it is would land the shell ten meters behind it.
    scene_defs();
    defs.units[0].weapon.range = 30.0f;
    defs.units[0].weapon.cooldown = 1000.0f; // one shot
    defs.units[1].speed = 5.0f;
    defs.units[1].health = 1000.0f;
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 33.0f, 10.0f);
    put(2, 33.0f, 45.0f);
    u32 ticks = 0;
    while (battle.projectile_count == 0 && ticks++ < 200)
        run(1);
    CHECK(ticks < 200 && battle.units[2].velocity.z < -4.99f);
    while (battle.projectile_count > 0 && ticks++ < 400)
        run(1);
    CHECK(events_of(BATTLE_EVENT_HIT) == 1);
    CHECK(battle.units[2].health == 970.0f);
}

// Shells by hand

internal void test_damage(void)
{
    // Damage is max(damage - armor, damage * 0.25): armor 5 takes 30 to 25; armor 28 takes 30 to 7.5.
    scene_defs();
    defs.units[1].armor = 5.0f;
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 10.0f, 10.0f);
    put(2, 10.0f, 30.0f);
    inject_at(10.0f, 30.0f, 0.4f, 30.0f);
    run(1);
    CHECK(battle.projectile_count == 1 && battle.units[2].health == 100.0f); // not there yet
    run(1);
    CHECK(battle.projectile_count == 0 && battle.units[2].health == 75.0f && events_of(BATTLE_EVENT_HIT) == 1);
    CHECK(battle.events[0].kind == BATTLE_EVENT_HIT && battle.events[0].size == 25.0f && battle.events[0].team == TEAM_ENEMY);

    defs.units[1].armor = 28.0f;
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 10.0f, 10.0f);
    put(2, 10.0f, 30.0f);
    inject_at(10.0f, 30.0f, 0.4f, 30.0f);
    run(2);
    CHECK(battle.units[2].health == 92.5f);

    // A shell on the ground vanishes without damage. A shell hurts the first enemy it touches and then is gone, so a second
    // enemy behind the first is not hurt; a tick's hits add up, and the unit dies at the tick's end.
    scene_defs();
    u8 enemies[2] = {1, 1};
    scene_start(one_shooter, 1, enemies, 2);
    put(1, 10.0f, 10.0f);
    put(2, 20.0f, 30.0f);
    put(3, 20.0f, 31.2f); // behind slot 2 along the shell's way
    inject_shell(TEAM_PLAYER, nv_vec3(20.0f, 0.4f, 25.5f), nv_vec3(0.0f, 0.0f, 90.0f), 40.0f);
    inject_shell(TEAM_PLAYER, nv_vec3(20.0f, 0.4f, 25.5f), nv_vec3(0.0f, 0.0f, 90.0f), 70.0f);
    run(2);
    CHECK(battle.projectile_count == 0 && battle.units[3].health == 100.0f);
    CHECK(battle.units[2].health == 0.0f && (battle.units[2].flags & UNIT_DEAD) && events_of(BATTLE_EVENT_DEATH) == 1);
    inject_shell(TEAM_PLAYER, nv_vec3(40.0f, 0.1f, 40.0f), nv_vec3(0.0f, -20.0f, 0.0f), 30.0f);
    run(1);
    CHECK(battle.projectile_count == 0 && battle.units[3].health == 100.0f);

    // A shell passes allies: a player's shell goes through a player unit that stands in its way.
    scene_defs();
    defs.units[0].height = 2.0f;
    defs.units[0].radius = 0.3f;
    u8 players[2] = {0, 0};
    scene_start(players, 2, one_dummy, 1);
    put(1, 50.0f, 10.0f); // the ally in the way
    put(2, 10.0f, 10.0f);
    put(3, 50.0f, 11.0f);
    inject_shell(TEAM_PLAYER, nv_vec3(50.0f, 0.4f, 5.5f), nv_vec3(0.0f, 0.0f, 90.0f), 30.0f);
    run(3);
    CHECK(battle.units[1].health == 100.0f && battle.units[3].health == 70.0f);

    // A shell that moves fast enough to cross a whole unit between two ticks still hits it (the segment is tested).
    scene_defs();
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 10.0f, 10.0f);
    put(2, 40.0f, 40.0f);
    inject_shell(TEAM_PLAYER, nv_vec3(40.0f, 0.4f, 38.6f), nv_vec3(0.0f, 0.0f, 90.0f), 30.0f); // 38.6 to 41.6 in a tick
    run(1);
    CHECK(battle.units[2].health == 70.0f);
    // ... and a shell passing above or beside a unit does not.
    inject_shell(TEAM_PLAYER, nv_vec3(40.0f, 1.0f, 38.6f), nv_vec3(0.0f, 0.0f, 90.0f), 30.0f);
    inject_shell(TEAM_PLAYER, nv_vec3(40.6f, 0.4f, 38.6f), nv_vec3(0.0f, 0.0f, 90.0f), 30.0f);
    run(1);
    CHECK(battle.units[2].health == 70.0f && battle.projectile_count == 2);
}

internal void test_shield(void)
{
    // A shield takes a shell's damage as energy and the shell is gone.
    scene_defs();
    give_shield(1, 60.0f);
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 10.0f, 10.0f);
    put(2, 10.0f, 30.0f);
    CHECK(battle.units[2].ability.shield.energy == 60.0f);
    inject_at(10.0f, 30.0f, 0.4f, 30.0f);
    run(2);
    CHECK(battle.units[2].ability.shield.energy == 30.0f && battle.units[2].health == 100.0f && battle.projectile_count == 0);
    CHECK(events_of(BATTLE_EVENT_SHIELD_HIT) == 1 && events_of(BATTLE_EVENT_HIT) == 0);
    CHECK(battle.events[0].size == 30.0f && battle.events[0].team == TEAM_ENEMY);
    // The shield's boundary is where it was crossed: 1.2 m before the unit's center on this line.
    CHECK(near_value(battle.events[0].position.z, 30.0f - 1.2f, 1e-3f));

    // A shell the shield cannot take whole: it takes what it has, and the rest goes on to the unit (armor applies to it).
    defs.units[1].armor = 5.0f;
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 10.0f, 10.0f);
    put(2, 10.0f, 30.0f);
    battle.units[2].ability.shield.energy = 10.0f;
    inject_at(10.0f, 30.0f, 0.4f, 30.0f);
    run(2);
    CHECK(battle.units[2].ability.shield.energy == 0.0f); // 10 taken, 20 left, 15 after armor
    CHECK(battle.units[2].health == 85.0f && battle.projectile_count == 0);
    CHECK(events_of(BATTLE_EVENT_SHIELD_HIT) == 1 && events_of(BATTLE_EVENT_HIT) == 1);
    CHECK(battle.events[0].kind == BATTLE_EVENT_SHIELD_HIT && battle.events[0].size == 10.0f);
    CHECK(battle.events[1].kind == BATTLE_EVENT_HIT && battle.events[1].size == 15.0f);

    // At zero energy the shield is off: the shell goes through and hurts the unit.
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 10.0f, 10.0f);
    put(2, 10.0f, 30.0f);
    battle.units[2].ability.shield.energy = 0.0f;
    inject_at(10.0f, 30.0f, 0.4f, 30.0f);
    run(2);
    CHECK(battle.units[2].health == 75.0f && events_of(BATTLE_EVENT_SHIELD_HIT) == 0);

    // A shield takes a shell whose damage equals its energy exactly, and then is empty.
    defs.units[1].armor = 0.0f;
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 10.0f, 10.0f);
    put(2, 10.0f, 30.0f);
    battle.units[2].ability.shield.energy = 30.0f;
    inject_at(10.0f, 30.0f, 0.4f, 30.0f);
    run(2);
    CHECK(battle.units[2].ability.shield.energy == 0.0f && battle.units[2].health == 100.0f && events_of(BATTLE_EVENT_HIT) == 0);

    // Refill: after 3 s (90 ticks) without a hit it gains 10 energy per second, up to its capacity.
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 10.0f, 10.0f);
    put(2, 10.0f, 30.0f);
    battle.units[2].ability.shield.energy = 0.0f;
    run(89);
    CHECK(battle.units[2].ability.shield.energy == 0.0f);
    run(1);
    CHECK(near_value(battle.units[2].ability.shield.energy, 10.0f / 30.0f, 1e-5f));
    run(30);
    CHECK(near_value(battle.units[2].ability.shield.energy, 31.0f * 10.0f / 30.0f, 1e-4f));
    run(300);
    CHECK(battle.units[2].ability.shield.energy == 60.0f);
    // A hit starts the wait again.
    inject_at(10.0f, 30.0f, 0.4f, 30.0f);
    run(2);
    CHECK(battle.units[2].ability.shield.energy == 30.0f);
    run(88); // the hit's own tick counts as the first of the 90
    CHECK(battle.units[2].ability.shield.energy == 30.0f);
    run(1);
    CHECK(battle.units[2].ability.shield.energy > 30.0f);

    // The shield guards its neighbors: a shell on its way to another unit is stopped by the shield it crosses.
    scene_defs();
    give_shield(1, 60.0f);
    u8 enemies[2] = {1, 0}; // a Dummy with a shield, and a Shooter without one, behind it
    scene_start(one_shooter, 1, enemies, 2);
    put(1, 10.0f, 10.0f);
    put(2, 20.0f, 30.0f);
    put(3, 20.0f, 32.5f);
    inject_at(20.0f, 32.5f, 0.4f, 30.0f);
    run(3);
    CHECK(battle.units[3].health == 100.0f && battle.units[2].ability.shield.energy == 30.0f);

    // A shell is not taken by its own side's shields, nor by a shield it starts inside of.
    scene_defs();
    give_shield(0, 60.0f);
    give_shield(1, 60.0f);
    u8 players[2] = {0, 0};
    scene_start(players, 2, one_dummy, 1);
    put(1, 10.0f, 10.0f);
    put(2, 30.0f, 10.0f);
    put(3, 30.0f, 40.0f);
    inject_shell(TEAM_PLAYER, nv_vec3(30.0f, 0.4f, 5.5f), nv_vec3(0.0f, 0.0f, 90.0f), 30.0f); // through its own shield
    run(2);
    CHECK(battle.units[2].ability.shield.energy == 60.0f && battle.units[2].health == 100.0f && battle.projectile_count == 1);
    battle.projectile_count = 0;
    inject_shell(TEAM_PLAYER, nv_vec3(30.0f, 0.4f, 39.0f), nv_vec3(0.0f, 0.0f, 90.0f), 30.0f); // starts inside the shield
    run(1);
    CHECK(battle.units[3].ability.shield.energy == 60.0f && battle.units[3].health == 70.0f);
}

internal void test_round_end(void)
{
    // A side wiped out ends the round.
    scene_defs();
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 10.0f, 10.0f);
    put(2, 10.0f, 30.0f);
    inject_at(10.0f, 30.0f, 0.4f, 100.0f);
    run(1);
    CHECK(battle.phase == BATTLE_FIGHT);
    run(1);
    CHECK(battle.phase == BATTLE_RESULT && battle.outcome == OUTCOME_PLAYER && battle.tick == 2);
    CHECK(battle_alive_count(&battle, TEAM_ENEMY) == 0 && battle_alive_count(&battle, TEAM_PLAYER) == 1);
    u32 hash = battle_hash(&battle);
    run(10); // nothing runs after the round
    CHECK(battle.tick == 2 && battle_hash(&battle) == hash);

    // The enemy wins by wiping the player out; both sides wiped out on one tick is a draw.
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 10.0f, 10.0f);
    put(2, 10.0f, 30.0f);
    inject_shell(TEAM_ENEMY, nv_vec3(10.0f, 0.4f, 5.5f), nv_vec3(0.0f, 0.0f, 90.0f), 100.0f);
    run(2);
    CHECK(battle.outcome == OUTCOME_ENEMY);
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 10.0f, 10.0f);
    put(2, 10.0f, 30.0f);
    inject_shell(TEAM_ENEMY, nv_vec3(10.0f, 0.4f, 5.5f), nv_vec3(0.0f, 0.0f, 90.0f), 100.0f);
    inject_at(10.0f, 30.0f, 0.4f, 100.0f);
    run(2);
    CHECK(battle.outcome == OUTCOME_DRAW && battle.phase == BATTLE_RESULT);
    CHECK(events_of(BATTLE_EVENT_DEATH) == 2);

    // At 60 s (1800 ticks) the round ends; the side with more remaining value (cost * health / max health) wins.
    scene_defs();
    const u32 round_ticks = defs.rules.round_ticks;
    CHECK(round_ticks == 1800);
    u8 two[2] = {0, 0};
    scene_start(two, 2, one_dummy, 1);
    run(round_ticks - 1);
    CHECK(battle.phase == BATTLE_FIGHT);
    run(1);
    CHECK(battle.phase == BATTLE_RESULT && battle.tick == round_ticks && battle.outcome == OUTCOME_PLAYER); // 200 against 100
    CHECK(battle_remaining_value(&battle, TEAM_PLAYER) == 200.0f && battle_remaining_value(&battle, TEAM_ENEMY) == 100.0f);

    scene_start(two, 1, one_dummy, 1);
    run(round_ticks);
    CHECK(battle.outcome == OUTCOME_DRAW); // 100 against 100

    scene_start(two, 1, one_dummy, 1);
    battle.units[1].health = 50.0f;
    run(round_ticks);
    CHECK(battle.outcome == OUTCOME_ENEMY && battle_remaining_value(&battle, TEAM_PLAYER) == 50.0f);

    // Dead units do not count.
    scene_start(two, 2, one_dummy, 1);
    battle.units[2].flags |= UNIT_DEAD;
    run(round_ticks);
    CHECK(battle.outcome == OUTCOME_DRAW);
}

// The rules in use: each rule of the project file changes what a round does, and not only what the file says.

internal void test_rules_in_use(void)
{
    // The cell size and the grid: another field, its cells and its zones. A 10 x 20 grid of 3 m cells with 4-row zones is
    // 30 m by 60 m; the enemy's rows are 16 to 19.
    scene_defs();
    defs.rules.cell_size = 3.0f;
    defs.rules.grid_width = 10;
    defs.rules.grid_length = 20;
    defs.rules.zone_rows = 4;
    CHECK(battle_enemy_first_row(&defs.rules) == 16);
    defs.enemy_count = 1;
    defs.enemy[0] = (StagePlace){1, 5, 18};
    battle_init(&battle, &defs);
    CHECK(battle_place(&battle, 0, 1, 0) && battle_place(&battle, 0, 9, 3));
    CHECK(!battle_place(&battle, 0, 10, 0) && !battle_place(&battle, 0, 0, 4) && !battle_remove(&battle, 10, 0)); // outside the zone
    CHECK(!battle_can_place(&battle, 0, 0, 14)); // a cell of the old zone, now outside the grid
    CHECK(battle.units[1].position.x == 4.5f && battle.units[1].position.z == 1.5f); // (1 + 0.5) * 3
    CHECK(battle.units[2].position.x == 28.5f && battle.units[2].position.z == 10.5f);
    CHECK(battle.units[3].team == TEAM_ENEMY && battle.units[3].position.x == 16.5f && battle.units[3].position.z == 55.5f);
    // The field's edge holds a unit in: 30 m by 60 m.
    CHECK(battle_start(&battle));
    put(1, -5.0f, -5.0f);
    put(2, 99.0f, 99.0f);
    run(1);
    CHECK(near_value(battle.units[1].position.x, 0.5f, 1e-5f) && near_value(battle.units[1].position.z, 0.5f, 1e-5f));
    CHECK(near_value(battle.units[2].position.x, 29.5f, 1e-5f) && near_value(battle.units[2].position.z, 59.5f, 1e-5f));
    // The smallest field: 1 x 2 cells, with a zone of one row each side.
    scene_defs();
    defs.rules.grid_width = 1;
    defs.rules.grid_length = 2;
    defs.rules.zone_rows = 1;
    defs.enemy_count = 1;
    defs.enemy[0] = (StagePlace){1, 0, 1};
    battle_init(&battle, &defs);
    CHECK(battle_place(&battle, 0, 0, 0) && !battle_place(&battle, 0, 0, 1) && !battle_place(&battle, 0, 1, 0));
    CHECK(battle_start(&battle));
    run(300);
    CHECK(isfinite(battle.units[1].position.z) && battle.units[1].position.z >= 0.5f && battle.units[2].position.z <= 3.5f);

    // The round time: a round of 10 s ends at tick 300 and not before.
    scene_defs();
    defs.rules.round_ticks = 300;
    u8 two[2] = {0, 0};
    scene_start(two, 2, one_dummy, 1);
    run(299);
    CHECK(battle.phase == BATTLE_FIGHT);
    run(1);
    CHECK(battle.phase == BATTLE_RESULT && battle.tick == 300 && battle.outcome == OUTCOME_PLAYER);

    // The retarget interval: targets are picked again every 30 ticks when it is 1 s.
    scene_defs();
    defs.rules.retarget_ticks = 30;
    u8 enemies[2] = {1, 1};
    scene_start(one_shooter, 1, enemies, 2);
    put(1, 10.0f, 10.0f);
    put(2, 10.0f, 20.0f);
    put(3, 10.0f, 30.0f);
    run(1);
    CHECK(battle.units[1].target.index == 2);
    put(3, 10.0f, 15.0f); // now nearest
    run(28);              // ticks 1 to 28: unchanged
    CHECK(battle.units[1].target.index == 2);
    run(2);               // the tick numbered 30
    CHECK(battle.units[1].target.index == 3);

    // The stop fraction: units walking at each other stop at half the range when it is 0.5.
    scene_defs();
    defs.rules.stop_fraction = 0.5f;
    for (u32 i = 0; i < 2; ++i) {
        defs.units[i].speed = 5.0f;
        defs.units[i].weapon.range = 20.0f;
        defs.units[i].weapon.damage = 0.0f;
    }
    scene_start(one_shooter, 1, one_dummy, 1);
    put(1, 20.0f, 10.0f);
    put(2, 20.0f, 50.0f);
    run(300);
    f32 gap = battle.units[2].position.z - battle.units[1].position.z;
    CHECK(gap <= 10.001f && gap > 9.6f);

    // Gravity: a shell is solved to land on its aim point whatever the pull, so it does, and flies longer under less.
    u32 flight[2] = {0, 0};
    for (u32 pass = 0; pass < 2; ++pass) {
        scene_defs();
        defs.rules.gravity = pass ? 4.9f : 19.6f;
        defs.units[0].weapon.range = 30.0f;
        defs.units[0].weapon.cooldown = 1000.0f;
        defs.units[1].radius = 0.001f;
        defs.units[1].height = 0.001f;
        scene_start(one_shooter, 1, one_dummy, 1);
        put(1, 33.0f, 9.0f);
        put(2, 33.0f, 24.0f);
        Projectile last = {0};
        for (u32 i = 0; i < 400 && !(last.damage > 0.0f && battle.projectile_count == 0); ++i) {
            if (battle.projectile_count == 1) {
                last = battle.projectiles[0];
                ++flight[pass];
            }
            run(1);
        }
        f32 t = (last.velocity.y + sqrtf(last.velocity.y * last.velocity.y + 2.0f * defs.rules.gravity * last.position.y)) / defs.rules.gravity;
        f32 x = last.position.x + last.velocity.x * t - 33.0f, z = last.position.z + last.velocity.z * t - 24.0f;
        CHECK(sqrtf(x * x + z * z) < 0.02f);
    }
    CHECK(flight[1] > flight[0] * 3 / 2); // a quarter of the pull: twice the time in the air

    // The minimum damage fraction: armor 28 against a 30 shell leaves max(2, 30 * fraction).
    for (u32 pass = 0; pass < 2; ++pass) {
        scene_defs();
        defs.rules.min_damage_fraction = pass ? 1.0f : 0.0f;
        defs.units[1].armor = 28.0f;
        scene_start(one_shooter, 1, one_dummy, 1);
        put(1, 10.0f, 10.0f);
        put(2, 10.0f, 30.0f);
        inject_at(10.0f, 30.0f, 0.4f, 30.0f);
        run(2);
        CHECK(battle.units[2].health == (pass ? 70.0f : 98.0f)); // 1: armor takes nothing off; 0: armor takes its 28
    }
}

// Whole rounds on a copy of the repository's project (full_project).

internal void load_full_project(void)
{
    CHECK(read_project(full_project));
}

internal u32 play_round(u32 seed, b32* finished)
{
    defs.seed = seed;
    battle_init(&battle, &defs);
    const s32 cells[10][2] = {{12, 11}, {14, 11}, {16, 11}, {18, 11}, {13, 9}, {15, 9}, {17, 9}, {14, 7}, {16, 7}, {15, 5}};
    for (u32 i = 0; i < 10; ++i)
        CHECK(battle_place(&battle, 0, cells[i][0], cells[i][1]));
    CHECK(battle_start(&battle));
    while (battle.phase == BATTLE_FIGHT) {
        battle_tick(&battle);
        battle.event_count = 0;
    }
    *finished = battle.phase == BATTLE_RESULT && battle.tick <= defs.rules.round_ticks;
    return battle_hash(&battle);
}

internal void test_whole_round(void)
{
    load_full_project();
    b32 finished = false;
    u32 first = play_round(1, &finished);
    CHECK(finished && battle.outcome != OUTCOME_NONE);
    CHECK(battle.tick > 100); // they have to walk to each other
    u32 killed = 20 - battle_alive_count(&battle, TEAM_PLAYER) - battle_alive_count(&battle, TEAM_ENEMY);
    CHECK(killed > 0); // the shells do hurt
    for (u32 i = 1; i <= battle.unit_count; ++i) {
        const Unit* unit = &battle.units[i];
        CHECK(isfinite(unit->position.x) && isfinite(unit->position.z) && isfinite(unit->health) && isfinite(unit->ability.shield.energy));
        CHECK(unit->position.x >= 0.5f && unit->position.x <= 63.5f && unit->position.z >= 0.5f && unit->position.z <= 95.5f);
    }
    // The same deployment and seed give the same round, however many times.
    b32 finished_again = false;
    CHECK(play_round(1, &finished_again) == first && finished_again);
    CHECK(play_round(1, &finished_again) == first);
    // Another seed changes the spread, so the round.
    CHECK(play_round(2, &finished_again) != first);
}

// Rounds on full_project came to these when the rules were still constants in code (the hash at tick 300, and at the end):
// moving them into the project file changed no result. A change to the rules that changes a round changes these on purpose.
internal void test_golden_rounds(void)
{
    load_full_project();
    const struct { u32 seed, ticks, outcome, at_300, at_end, enemies_left; } golden[] = {
        {1, 660, OUTCOME_ENEMY, 0x5e581846u, 0x8bfe30bfu, 2},
        {2, 660, OUTCOME_ENEMY, 0xcbe96705u, 0xe2593868u, 2},
        {3, 705, OUTCOME_ENEMY, 0x6b8799cfu, 0xa0dde613u, 5},
        {4, 660, OUTCOME_ENEMY, 0x4836461du, 0xcb3225cfu, 1},
    };
    const s32 cells[10][2] = {{12, 11}, {14, 11}, {16, 11}, {18, 11}, {13, 9}, {15, 9}, {17, 9}, {14, 7}, {16, 7}, {15, 5}};
    for (u32 g = 0; g < NV_ARRAY_COUNT(golden); ++g) {
        defs.seed = golden[g].seed;
        battle_init(&battle, &defs);
        for (u32 i = 0; i < 10; ++i)
            CHECK(battle_place(&battle, 0, cells[i][0], cells[i][1]));
        CHECK(battle_start(&battle));
        u32 at_300 = 0;
        while (battle.phase == BATTLE_FIGHT) {
            battle_tick(&battle);
            battle.event_count = 0;
            if (battle.tick == 300)
                at_300 = battle_hash(&battle);
        }
        CHECK(battle.tick == golden[g].ticks && battle.outcome == golden[g].outcome && at_300 == golden[g].at_300);
        CHECK(battle_hash(&battle) == golden[g].at_end && battle_alive_count(&battle, TEAM_ENEMY) == golden[g].enemies_left);
    }
}

// Stepping back: a round put back at a tick by battle_seek is the state it had at that tick, from the fight and from the
// result alike; seeking forward runs on to the tick; in deployment it does nothing.
internal void test_seek(void)
{
    load_full_project();
    battle_init(&battle, &defs);
    const s32 cells[6][2] = {{12, 11}, {14, 11}, {16, 11}, {13, 9}, {15, 9}, {14, 7}};
    for (u32 i = 0; i < 6; ++i)
        CHECK(battle_place(&battle, 0, cells[i][0], cells[i][1]));
    u32 hash_before = battle_hash(&battle);
    battle_seek(&battle, 10);
    CHECK(battle.phase == BATTLE_DEPLOY && battle_hash(&battle) == hash_before);

    static u32 hashes[1800 + 1];
    CHECK(defs.rules.round_ticks == 1800);
    CHECK(battle_start(&battle));
    hashes[0] = battle_hash(&battle);
    while (battle.phase == BATTLE_FIGHT) {
        battle_tick(&battle);
        battle.event_count = 0;
        hashes[battle.tick] = battle_hash(&battle);
    }
    u32 last = battle.tick;
    CHECK(battle.phase == BATTLE_RESULT && last > 200);

    const u32 ticks[5] = {last - 1, 200, 57, 1, 0};
    for (u32 i = 0; i < 5; ++i) {
        battle_seek(&battle, ticks[i]);
        CHECK(battle.tick == ticks[i] && battle.phase == BATTLE_FIGHT && battle_hash(&battle) == hashes[ticks[i]]);
        CHECK(battle.event_count == 0);
    }
    battle_seek(&battle, 120); // forward from 0
    CHECK(battle.tick == 120 && battle_hash(&battle) == hashes[120]);
    battle_tick(&battle); // and on from there as if it had never stopped
    CHECK(battle_hash(&battle) == hashes[121]);
    battle_seek(&battle, last + 50); // past the end: the round ends where it ends
    CHECK(battle.phase == BATTLE_RESULT && battle.tick == last && battle_hash(&battle) == hashes[last]);
}

int main(void)
{
    test_project_file();
    test_rule_editing();
    test_unit_editing();
    test_repository_file();
    test_definition_syntax();
    test_project_structure();
    test_definition_errors();
    test_deployment();
    test_targets_and_movement();
    test_weapon();
    test_muzzle();
    test_lead_aim();
    test_damage();
    test_shield();
    test_round_end();
    test_rules_in_use();
    test_whole_round();
    test_golden_rounds();
    test_seek();
    if (failures) {
        printf("battle_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("battle_test: all passed\n");
    return 0;
}

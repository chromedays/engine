// The auto-battler's definition files and rules (autobattler/defs.c, battle.c; docs/specs/battle.md, "Tests"). No GPU.
// The files are read from the repository's autobattler/data (BATTLE_DATA_DIR); the rules run on small scenes made here,
// where shells are sometimes put into the battle by hand so that one rule at a time is tried.

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

global const char good_stage[] =
    "supply 1000\n"
    "place Crawler 12 36\n";

// Reads `text` as units.txt (or, with `stage`, as stage.txt after the good units); returns what the read returned.
internal b32 read_text(const char* text, b32 stage)
{
    memset(&defs, 0, sizeof(defs));
    nv_log_clear();
    if (stage) {
        CHECK(defs_read_units(&defs, "units.txt", good_units, sizeof(good_units) - 1));
        return defs_read_stage(&defs, "stage.txt", text, strlen(text));
    }
    return defs_read_units(&defs, "units.txt", text, strlen(text));
}

internal b32 first_error_has(const char* text)
{
    return strstr(defs.first_error, text) != NULL;
}

// The read must fail with `count` errors, the first on `line` with `message` in it.
internal void expect_error(b32 stage, const char* text, u32 count, u32 line, const char* message)
{
    char prefix[32];
    if (line)
        snprintf(prefix, sizeof(prefix), "%s:%u:", stage ? "stage.txt" : "units.txt", line);
    else
        snprintf(prefix, sizeof(prefix), "%s: ", stage ? "stage.txt" : "units.txt"); // an error about the whole file
    b32 ok = read_text(text, stage);
    if (ok || defs.error_count != count || strncmp(defs.first_error, prefix, strlen(prefix)) != 0 || !first_error_has(message)) {
        printf("FAILED expect_error: wanted %u error(s), first \"%s ... %s\"; got ok=%d, %u error(s), first \"%s\"\n", count, prefix,
               message, ok, defs.error_count, defs.first_error);
        ++failures;
    }
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

internal void test_definition_files(void)
{
    static char units_text[8192], stage_text[8192];
    umm units_size = read_file("units.txt", units_text, sizeof(units_text));
    umm stage_size = read_file("stage.txt", stage_text, sizeof(stage_text));
    memset(&defs, 0, sizeof(defs));
    CHECK(defs_read_units(&defs, "units.txt", units_text, units_size));
    CHECK(defs_read_stage(&defs, "stage.txt", stage_text, stage_size));
    CHECK(defs.error_count == 0);
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

internal void test_definition_syntax(void)
{
    // Comments, blank lines and CRLF line ends give the same values as plain text.
    CHECK(read_text(good_units, false));
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
    CHECK(read_text(messy, false));
    CHECK(memcmp(&defs.units[0], &plain, sizeof(plain)) == 0);
    // The last line may lack its line end.
    CHECK(read_text("unit A\n cost 1\n health 1\n speed 0\n radius 1\n height 1\n weapon W\n  range 1\n  damage 0\n  cooldown 1\n  launch_angle 1", false));

    // Defaults: no armor, no spread, a muzzle at the origin, no ability.
    CHECK(read_text(good_units, false));
    CHECK(defs.units[0].armor == 0.0f && defs.units[0].weapon.spread == 0.0f && defs.units[0].weapon.muzzle.z == 0.0f);
    CHECK(defs.units[0].ability.kind == ABILITY_NONE);

    // Two units, and the shield's keys can come in any order.
    CHECK(read_text("unit A\n"
                    "  cost 5\n  health 10\n  speed 1\n  radius 1\n  height 1\n"
                    "  ability shield\n    regen_delay 1\n    regen 2\n    capacity 3\n    radius 4\n"
                    "  weapon W\n    range 1\n    damage 0\n    cooldown 1\n    launch_angle 30\n"
                    "unit B\n"
                    "  cost 6\n  health 10\n  speed 1\n  radius 1\n  height 1\n"
                    "  weapon W\n    range 1\n    damage 0\n    cooldown 1\n    launch_angle 30\n",
                    false));
    CHECK(defs.unit_count == 2 && defs.units[0].ability.shield.regen_delay == 1.0f && defs.units[0].ability.shield.radius == 4.0f);
    CHECK(defs.units[1].cost == 6 && strcmp(defs.units[1].name, "B") == 0 && defs.units[1].ability.kind == ABILITY_NONE);

    // The stage: the seed defaults to 1 and may be given.
    CHECK(read_text(good_stage, true));
    CHECK(defs.seed == 1 && defs.supply == 1000 && defs.enemy_count == 1);
    CHECK(read_text("supply 1000\nseed 4294967295\nplace Crawler 0 47\nplace Crawler 31 34\n", true));
    CHECK(defs.seed == 4294967295u && defs.enemy_count == 2 && defs.enemy[1].cell_x == 31 && defs.enemy[1].cell_row == 34);
}

internal void test_definition_errors(void)
{
    // Each error: how many errors, the line of the first, and what it says.
    // clang-format off
    expect_error(false, "unit A\n  cost 1\n  healt 5\n", 6, 3, "unknown key 'healt'"); // and health, speed, radius, height, a weapon are missing
    expect_error(false, "bogus\n", 1, 1, "unknown key 'bogus'");                         // and no units
    expect_error(false, "unit A\n  cost 1\n  cost 2\n", 6, 3, "'cost' is given twice");
    expect_error(false, "unit A\n  cost 1\n", 5, 1, "missing 'health'");              // the unit's line
    expect_error(false, "unit A\n  cost 1 2\n", 6, 2, "'cost' takes 1 value");
    expect_error(false, "unit A\n  cost 0\n", 6, 2, "'cost' must be >= 1");
    expect_error(false, "unit A\n  cost 10001\n", 6, 2, "<= 10000");
    expect_error(false, "unit A\n  cost 1.5\n", 6, 2, "not an integer");
    expect_error(false, "unit A\n  cost 1\n  health 0\n", 5, 3, "'health' must be > 0");
    expect_error(false, "unit A\n  cost 1\n  health 1e3\n", 5, 3, "not a number");   // no exponents
    expect_error(false, "unit A\n  cost 1\n  health abc\n", 5, 3, "not a number");
    expect_error(false, "unit A\n  cost 1\n  health\n", 5, 3, "'health' takes 1 value");
    expect_error(false, "unit A\n  cost 1\n  armor -1\n", 6, 3, "'armor' must be >= 0");
    expect_error(false, "unit A\n\tcost 1\n", 7, 2, "tab");
    expect_error(false, "unit 1A\n", 1, 1, "not a name");
    expect_error(false, "unit A B\n", 1, 1, "'unit' takes a name");
    expect_error(false, "range 5\n", 1, 1, "unknown key 'range'");                         // a weapon's key outside its block
    expect_error(false, " unit A\n", 1, 1, "unexpected indentation");
    expect_error(false, "unit A\n  cost 1\n    health 2\n", 6, 3, "inconsistent indentation");
    expect_error(false, "unit A\n  cost 1\n health 2\n", 6, 3, "inconsistent indentation");
    expect_error(false, "", 1, 0, "no units");
    // clang-format on

    // A unit needs exactly one weapon, and at most one ability.
    expect_error(false, "unit A\n  cost 1\n  health 1\n  speed 0\n  radius 1\n  height 1\n", 1, 1, "needs a weapon");
    static const char one_weapon[] = "unit A\n  cost 1\n  health 1\n  speed 0\n  radius 1\n  height 1\n"
                                     "  weapon W\n    range 1\n    damage 0\n    cooldown 1\n    launch_angle 30\n";
    char text[1024];
    snprintf(text, sizeof(text), "%s  weapon V\n    range 1\n", one_weapon);
    expect_error(false, text, 1, 12, "at most one 'weapon'");
    snprintf(text, sizeof(text), "%s  ability shield\n    radius 1\n    capacity 1\n    regen 1\n    regen_delay 1\n  ability shield\n", one_weapon);
    expect_error(false, text, 1, 17, "at most one 'ability'");
    snprintf(text, sizeof(text), "%s  ability jump\n    distance 5\n", one_weapon);
    expect_error(false, text, 1, 12, "unknown ability 'jump'"); // its inside is skipped, not reported again
    snprintf(text, sizeof(text), "%s  ability shield\n    radius 1\n", one_weapon);
    expect_error(false, text, 3, 12, "missing 'capacity'");
    // The launch angle is above 0 and below 90 degrees.
    snprintf(text, sizeof(text), "unit A\n  cost 1\n  health 1\n  speed 0\n  radius 1\n  height 1\n"
                                 "  weapon W\n    range 1\n    damage 0\n    cooldown 1\n    launch_angle 90\n");
    expect_error(false, text, 1, 11, "'launch_angle' must be > 0 and < 90");
    // The muzzle takes three numbers.
    snprintf(text, sizeof(text), "%s    muzzle 1 2\n", one_weapon);
    expect_error(false, text, 1, 12, "'muzzle' takes 3 values");

    // The same name twice, at the second one's line.
    snprintf(text, sizeof(text), "%sunit A\n  cost 1\n", one_weapon);
    CHECK(!read_text(text, false) && first_error_has("units.txt:12:") && first_error_has("defined twice"));

    // Every error is reported, not only the first: three lines, three logged messages.
    CHECK(!read_text("unit A\n  cost 0\n  health 0\n  speed -1\n  radius 1\n  height 1\n  weapon W\n    range 1\n    damage 0\n    cooldown 1\n    launch_angle 30\n", false));
    CHECK(defs.error_count == 3);
    CHECK(log_has("units.txt:2:") && log_has("units.txt:3:") && log_has("units.txt:4:"));

    // More than 20 errors: all counted, 20 logged and a note.
    char many[2048] = "";
    for (u32 i = 0; i < 25; ++i)
        strcat(many, "bogus\n");
    CHECK(!read_text(many, false));
    CHECK(defs.error_count == 25);
    CHECK(nv_log_ring.count == 21 && log_has("units.txt:20:") && !log_has("units.txt:21:") && log_has("more errors"));

    // At most 16 units.
    char sixteen[8192] = "";
    for (u32 i = 0; i < 17; ++i) {
        char one[512];
        snprintf(one, sizeof(one), "unit U%u\n  cost 1\n  health 1\n  speed 0\n  radius 1\n  height 1\n  weapon W\n    range 1\n    damage 0\n    cooldown 1\n    launch_angle 30\n", i);
        strcat(sixteen, one);
    }
    CHECK(!read_text(sixteen, false) && defs.error_count == 1 && defs.unit_count == 16 && first_error_has("units.txt:177:") && first_error_has("too many units"));

    // The stage.
    expect_error(true, "supply 1000\nplace Crawler 12 33\n", 1, 2, "cell row must be 34 to 47");
    expect_error(true, "supply 1000\nplace Crawler 32 36\n", 1, 2, "cell x must be 0 to 31");
    expect_error(true, "supply 1000\nplace Nobody 12 36\n", 1, 2, "unknown unit 'Nobody'");
    expect_error(true, "supply 1000\nplace Crawler 12 36\nplace Crawler 12 36\n", 1, 3, "already has a unit");
    expect_error(true, "supply 1000\nplace Crawler 12.5 36\n", 1, 2, "not an integer");
    expect_error(true, "supply 1000\nplace Crawler 12\n", 1, 2, "'place' takes");
    expect_error(true, "place Crawler 12 36\n", 1, 0, "missing 'supply'");
    expect_error(true, "supply 1000\nsupply 500\nplace Crawler 12 36\n", 1, 2, "'supply' is given twice");
    expect_error(true, "supply 0\nplace Crawler 12 36\n", 1, 1, "'supply' must be 1 to 100000");
    expect_error(true, "supply 1000\nseed -1\nplace Crawler 12 36\n", 1, 2, "'seed' must be 0 to 4294967295");
    expect_error(true, "supply 1000\n", 1, 0, "no 'place' lines");
    expect_error(true, "supply 1000\nplace Crawler 12 36\n  place Crawler 13 36\n", 1, 3, "unexpected indentation");
    expect_error(true, "supply 1000\nbogus 1\nplace Crawler 12 36\n", 1, 2, "unknown key 'bogus'");
    // Supply: the place that takes the total over it is the one reported, wherever the supply line is.
    expect_error(true, "supply 250\nplace Crawler 1 34\nplace Crawler 2 34\nplace Crawler 3 34\nplace Crawler 4 34\n", 1, 4, "over the supply");
    expect_error(true, "place Crawler 1 34\nplace Crawler 2 34\nsupply 150\n", 1, 2, "over the supply");
    CHECK(read_text("supply 200\nplace Crawler 1 34\nplace Crawler 2 34\n", true)); // exactly the supply is fine
}

// Scenes. Two kinds of unit: 0 "Shooter" and 1 "Dummy", both 100 health, radius 0.5, height 0.8, standing still, with a
// weapon that reaches 1 m (so it never fires unless a scene sets the range). A scene sets what it needs in `defs` before
// scene_start; each test begins with scene_defs.

internal void scene_defs(void)
{
    memset(&defs, 0, sizeof(defs));
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
        defs.enemy[i] = (StagePlace){enemy_defs[i], (u8)i, BATTLE_ENEMY_FIRST_ROW};
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
            f32 t = (last.velocity.y + sqrtf(last.velocity.y * last.velocity.y + 2.0f * BATTLE_GRAVITY * last.position.y)) / BATTLE_GRAVITY;
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
    u8 two[2] = {0, 0};
    scene_start(two, 2, one_dummy, 1);
    run(BATTLE_MAX_TICKS - 1);
    CHECK(battle.phase == BATTLE_FIGHT);
    run(1);
    CHECK(battle.phase == BATTLE_RESULT && battle.tick == BATTLE_MAX_TICKS && battle.outcome == OUTCOME_PLAYER); // 200 against 100
    CHECK(battle_remaining_value(&battle, TEAM_PLAYER) == 200.0f && battle_remaining_value(&battle, TEAM_ENEMY) == 100.0f);

    scene_start(two, 1, one_dummy, 1);
    run(BATTLE_MAX_TICKS);
    CHECK(battle.outcome == OUTCOME_DRAW); // 100 against 100

    scene_start(two, 1, one_dummy, 1);
    battle.units[1].health = 50.0f;
    run(BATTLE_MAX_TICKS);
    CHECK(battle.outcome == OUTCOME_ENEMY && battle_remaining_value(&battle, TEAM_PLAYER) == 50.0f);

    // Dead units do not count.
    scene_start(two, 2, one_dummy, 1);
    battle.units[2].flags |= UNIT_DEAD;
    run(BATTLE_MAX_TICKS);
    CHECK(battle.outcome == OUTCOME_DRAW);
}

// A whole round on the repository's definitions.

internal void load_repository_defs(void)
{
    static char units_text[8192], stage_text[8192];
    umm units_size = read_file("units.txt", units_text, sizeof(units_text));
    umm stage_size = read_file("stage.txt", stage_text, sizeof(stage_text));
    memset(&defs, 0, sizeof(defs));
    CHECK(defs_read_units(&defs, "units.txt", units_text, units_size));
    CHECK(defs_read_stage(&defs, "stage.txt", stage_text, stage_size));
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
    *finished = battle.phase == BATTLE_RESULT && battle.tick <= BATTLE_MAX_TICKS;
    return battle_hash(&battle);
}

internal void test_whole_round(void)
{
    load_repository_defs();
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

int main(void)
{
    test_definition_files();
    test_definition_syntax();
    test_definition_errors();
    test_deployment();
    test_targets_and_movement();
    test_weapon();
    test_lead_aim();
    test_damage();
    test_shield();
    test_round_end();
    test_whole_round();
    if (failures) {
        printf("battle_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("battle_test: all passed\n");
    return 0;
}

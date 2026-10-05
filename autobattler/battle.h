#pragma once

// The auto-battler's rules and the project file they read (docs/specs/battle.md, "Stage A rules"; docs/specs/abproj.md,
// "default.abproj"). Nothing here draws or reads the frame's time: the view steps the battle with battle_tick at 30 Hz and
// copies what it needs into nodes and effects, so tests can run whole rounds.

#include <engine/base.h>
#include <engine/math.h>

#include <stdbool.h>

// The tick rate is the engine's decision, not data: every time in the project file becomes whole ticks by it.
#define BATTLE_TICK_RATE      30
#define BATTLE_TICK_SECONDS   (1.0f / BATTLE_TICK_RATE)

// Capacities. The sizes the project file's rules can ask for stop here (the ranges of `grid` and `zone_rows`).
#define BATTLE_MAX_GRID_WIDTH  64
#define BATTLE_MAX_GRID_LENGTH 128
#define BATTLE_MAX_ZONE_ROWS   32
#define BATTLE_MAX_PLACES      (BATTLE_MAX_ZONE_ROWS * BATTLE_MAX_GRID_WIDTH) // one unit per cell of a zone
#define BATTLE_MAX_UNIT_DEFS   16
#define BATTLE_NAME_SIZE       32 // a name is at most 31 bytes
#define BATTLE_MAX_UNITS       (1 + 2 * BATTLE_MAX_PLACES) // slot 0 is "none"
#define BATTLE_MAX_PROJECTILES 4096
#define BATTLE_MAX_EVENTS      4096

// The newest format of the project file this build reads (docs/specs/abproj.md, "Versions").
#define ABPROJ_VERSION 1

// Definitions: what autobattler/data/default.abproj says. Zero is empty.

// The rules of the `rules` block, times already in ticks.
typedef struct BattleRules {
    f32 cell_size;               // meters
    u32 grid_width, grid_length; // cells across, rows long
    u32 zone_rows;               // each side's deployment rows: the player's are 0 to zone_rows - 1, the enemy's the last zone_rows
    u32 round_ticks;             // the time limit
    u32 retarget_ticks;          // how often units pick targets again
    f32 gravity;                 // m/s²
    f32 stop_fraction;           // a unit stops once its target is within this share of its weapon's range
    f32 min_damage_fraction;     // armor never takes more than 1 minus this off a hit
} BattleRules;

typedef struct WeaponDef {
    char name[BATTLE_NAME_SIZE];
    f32 range, damage, cooldown; // meters, damage, seconds
    f32 launch_angle;            // radians
    f32 spread;                  // meters: the shell lands within this radius of its aim point
    NvVec3 muzzle;               // in the unit's space: x to its right, y up, z forward (facing +Z, its right is -X)
} WeaponDef;

typedef enum AbilityKind { ABILITY_NONE, ABILITY_SHIELD } AbilityKind;

typedef struct AbilityDef {
    AbilityKind kind;
    union { // tagged by kind
        struct { f32 radius, capacity, regen, regen_delay; } shield; // meters, energy, energy per second, seconds
    };
} AbilityDef;

typedef struct UnitDef {
    char name[BATTLE_NAME_SIZE];
    u32 cost;
    f32 health, armor, speed; // speed in m/s
    f32 radius, height;       // meters
    WeaponDef weapon;
    AbilityDef ability;
} UnitDef;

typedef struct StagePlace {
    u8 def; // index into BattleDefs.units
    u8 cell_x, cell_row;
} StagePlace;

typedef struct BattleDefs {
    BattleRules rules;
    UnitDef units[BATTLE_MAX_UNIT_DEFS];
    u32 unit_count;
    u32 supply; // each side's
    u32 seed;   // for the random numbers of the whole round
    StagePlace enemy[BATTLE_MAX_PLACES];
    u32 enemy_count;
    u32 error_count;        // errors found by the reads, all of them (only the first 20 are logged)
    char first_error[160];  // "default.abproj:12: unknown key 'healt'", for the panel
} BattleDefs;

// Reads the text of a project file (`text` need not end in a NUL). Returns false when anything in it is wrong: each error
// is logged with its file name and line (nv_log, source "battle"), counted in `defs->error_count`, and the first is kept
// in `defs->first_error`. Reading goes on to the end of the text to report every error. `defs` starts zeroed.
b32 defs_read_project(BattleDefs* defs, const char* file_name, const char* text, umm size);

// Editing the rules in a project's text (docs/specs/abproj.md, "Editing the rules"): the text must have been read without
// errors. A rule's values are those of its key in the `rules` block, as the file writes them (seconds, not ticks); `grid`
// has two, every other rule one.
#define BATTLE_RULE_MAX_VALUES 2
// Reads rule `key`'s values; returns how many, 0 when `key` is not a rule.
u32 defs_rule_get(const char* text, umm size, const char* key, f64 values[BATTLE_RULE_MAX_VALUES]);
// Writes the text with rule `key`'s values changed to `values` (`count` of them) into `out`, which may not overlap `text`; the
// rest of the line (its indentation, its comment, which keeps its column where it can) and every other line stay as they
// were. Returns the new text's size, 0 when `key` is not a rule, `count` is not its number of values or the text does not fit
// in `capacity` bytes. The values are not checked against the rules' ranges: read the result with defs_read_project.
umm defs_rule_set(const char* text, umm size, const char* key, const f64* values, u32 count, char* out, umm capacity);

// Seconds as whole ticks, rounded up (with room for float error, so 1.5 s is 45 ticks and not 46); at least 1.
u32 battle_seconds_to_ticks(f32 seconds);

// The first row of the enemy's zone.
u32 battle_enemy_first_row(const BattleRules* rules);

// The simulation.

typedef enum BattleTeam { TEAM_PLAYER, TEAM_ENEMY } BattleTeam;

typedef struct UnitId { u32 index; } UnitId; // a slot in Battle.units; 0 = none

enum { UNIT_DEAD = 1 << 0 };

typedef struct Unit {
    u32 flags;
    u8 team;
    u8 def; // index into BattleDefs.units
    NvVec3 position, previous_position; // previous: the position before this tick, for drawing between ticks
    NvVec3 velocity;                    // what the unit moved at this tick, m/s; leading aim reads it
    f32 yaw, previous_yaw;              // radians; the unit faces (sin yaw, 0, cos yaw)
    f32 health;
    f32 damage_taken; // this tick; applied at the tick's end
    UnitId target;
    u32 cooldown_ticks; // until the weapon can fire
    union { // tagged by UnitDef.ability.kind
        struct { f32 energy; u32 since_hit_ticks; } shield;
    } ability;
} Unit;

typedef struct Projectile { // a shell
    u8 team;
    UnitId shooter;
    NvVec3 position, previous_position, velocity;
    f32 damage; // before armor; a shield that takes only part of it leaves the rest
} Projectile;

typedef enum BattleEventKind {
    BATTLE_EVENT_FIRE,       // position: the muzzle, direction: where the shell goes
    BATTLE_EVENT_HIT,        // a shell hurt a unit: position: where, size: the damage after armor
    BATTLE_EVENT_SHIELD_HIT, // a shield took a shell: position: where it crossed, size: the energy it took
    BATTLE_EVENT_DEATH,      // position: where the unit died
} BattleEventKind;

typedef struct BattleEvent { // for the view only
    BattleEventKind kind;
    u8 team, def; // of the unit the event is about: the shooter, the one hit, the one that died
    NvVec3 position, direction;
    f32 size;
} BattleEvent;

typedef enum BattlePhase { BATTLE_DEPLOY, BATTLE_FIGHT, BATTLE_RESULT } BattlePhase;
typedef enum BattleOutcome { OUTCOME_NONE, OUTCOME_PLAYER, OUTCOME_ENEMY, OUTCOME_DRAW } BattleOutcome;

typedef struct Battle {
    const BattleDefs* defs;
    BattlePhase phase;
    BattleOutcome outcome;
    u32 tick;
    NvRandom rng; // seeded at Start from the stage's seed
    u8 placed[BATTLE_MAX_ZONE_ROWS][BATTLE_MAX_GRID_WIDTH]; // the player's deployment: unit def + 1, 0 = empty
    u32 supply_used;
    Unit units[BATTLE_MAX_UNITS]; // [0] is unused; slots 1 to unit_count are the units, the player's first
    u32 unit_count;
    Projectile projectiles[BATTLE_MAX_PROJECTILES];
    u32 projectile_count;
    // Events pile up over ticks until the view has read them and set event_count back to 0; when the list is full,
    // events (and only events) are dropped and counted.
    BattleEvent events[BATTLE_MAX_EVENTS];
    u32 event_count, events_dropped;
    NvVec3 push[BATTLE_MAX_UNITS]; // scratch: this tick's separation, by unit
} Battle;

// Zeroes `battle` and starts an empty deployment. `defs` must stay valid and unchanged while the battle uses it.
void battle_init(Battle* battle, const BattleDefs* defs);

// The center of a cell on the ground. The player's side is -Z, the enemy's +Z.
NvVec3 battle_cell_center(const BattleRules* rules, u32 cell_x, u32 cell_row);

// Deployment (only in the BATTLE_DEPLOY phase; the others return false). A unit def can be placed on an empty cell of the
// player's zone while the supply lasts. Each change rebuilds `units`.
u32 battle_supply_left(const Battle* battle);
b32 battle_can_place(const Battle* battle, u32 def, s32 cell_x, s32 cell_row);
b32 battle_place(Battle* battle, u32 def, s32 cell_x, s32 cell_row);
b32 battle_remove(Battle* battle, s32 cell_x, s32 cell_row); // false when the cell is empty
void battle_clear_placement(Battle* battle);                 // Reset

// Start needs at least one player unit. Retry goes back to deployment with the same placement, from any phase.
b32 battle_start(Battle* battle);
void battle_retry(Battle* battle);

// One 30 Hz tick (does nothing outside BATTLE_FIGHT).
void battle_tick(Battle* battle);

// Puts a started round (fighting or over) back at `tick`, by playing it again from its start with the same placement and seed:
// the rules are deterministic, so that is the state it had then. Events of the replayed ticks are dropped. The cost grows with
// `tick` (each step back replays the round so far).
void battle_seek(Battle* battle, u32 tick);

u32 battle_alive_count(const Battle* battle, u32 team);
// The sum over a team's living units of cost * health / max health.
f32 battle_remaining_value(const Battle* battle, u32 team);
// FNV-1a over the state a round's outcome depends on, so two runs can be compared.
u32 battle_hash(const Battle* battle);

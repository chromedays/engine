// The rules of one round (docs/specs/battle.md, "Stage A rules"). A tick runs these steps in order: record the previous
// positions, pick targets again, move and separate, fire weapons, advance shells, apply deaths, refill shields, check the
// end. Every loop runs in slot order and nothing reads a clock, so the same deployment and seed always play the same.

#include "battle.h"

#include <float.h>
#include <math.h>
#include <string.h>

// What a hit is (shells test these three kinds, nearest first).
typedef enum HitKind { HIT_NONE, HIT_SHIELD, HIT_UNIT, HIT_GROUND } HitKind;

internal b32 is_alive(const Unit* unit)
{
    return !(unit->flags & UNIT_DEAD);
}

u32 battle_seconds_to_ticks(f32 seconds)
{
    u32 ticks = (u32)ceilf(seconds * BATTLE_TICK_RATE - 0.0001f);
    return ticks ? ticks : 1;
}

u32 battle_enemy_first_row(const BattleRules* rules)
{
    return rules->grid_length - rules->zone_rows;
}

internal f32 distance_xz_squared(NvVec3 a, NvVec3 b)
{
    f32 dx = a.x - b.x, dz = a.z - b.z;
    return dx * dx + dz * dz;
}

internal void add_event(Battle* battle, BattleEventKind kind, const Unit* unit, NvVec3 position, NvVec3 direction, f32 size)
{
    if (battle->event_count == BATTLE_MAX_EVENTS) {
        ++battle->events_dropped;
        return;
    }
    BattleEvent* event = &battle->events[battle->event_count++];
    *event = (BattleEvent){.kind = kind, .team = unit->team, .def = unit->def, .position = position, .direction = direction, .size = size};
}

NvVec3 battle_cell_center(const BattleRules* rules, u32 cell_x, u32 cell_row)
{
    return nv_vec3(((f32)cell_x + 0.5f) * rules->cell_size, 0.0f, ((f32)cell_row + 0.5f) * rules->cell_size);
}

internal void make_unit(Battle* battle, u32 def_index, u32 team, u32 cell_x, u32 cell_row)
{
    NV_ASSERT(battle->unit_count + 1 < BATTLE_MAX_UNITS);
    const UnitDef* def = &battle->defs->units[def_index];
    Unit* unit = &battle->units[++battle->unit_count];
    *unit = (Unit){0};
    unit->team = (u8)team;
    unit->def = (u8)def_index;
    unit->position = unit->previous_position = battle_cell_center(&battle->defs->rules, cell_x, cell_row);
    unit->yaw = unit->previous_yaw = team == TEAM_PLAYER ? 0.0f : NV_PI; // the sides face each other
    unit->health = def->health;
    if (def->ability.kind == ABILITY_SHIELD)
        unit->ability.shield.energy = def->ability.shield.capacity;
}

// The units are what the placement and the stage say: the player's in row order, then the enemy's in file order.
internal void rebuild_units(Battle* battle)
{
    memset(battle->units, 0, sizeof(battle->units));
    battle->unit_count = 0;
    battle->projectile_count = 0;
    battle->event_count = 0;
    battle->events_dropped = 0;
    battle->tick = 0;
    battle->outcome = OUTCOME_NONE;
    const BattleRules* rules = &battle->defs->rules;
    for (u32 row = 0; row < rules->zone_rows; ++row)
        for (u32 x = 0; x < rules->grid_width; ++x)
            if (battle->placed[row][x])
                make_unit(battle, battle->placed[row][x] - 1u, TEAM_PLAYER, x, row);
    for (u32 i = 0; i < battle->defs->enemy_count; ++i) {
        const StagePlace* place = &battle->defs->enemy[i];
        make_unit(battle, place->def, TEAM_ENEMY, place->cell_x, place->cell_row);
    }
}

void battle_init(Battle* battle, const BattleDefs* defs)
{
    memset(battle, 0, sizeof(*battle));
    battle->defs = defs;
    battle->phase = BATTLE_DEPLOY;
    rebuild_units(battle);
}

u32 battle_supply_left(const Battle* battle)
{
    return battle->defs->supply - battle->supply_used;
}

b32 battle_can_place(const Battle* battle, u32 def, s32 cell_x, s32 cell_row)
{
    const BattleRules* rules = &battle->defs->rules;
    return battle->phase == BATTLE_DEPLOY && def < battle->defs->unit_count && cell_x >= 0 && cell_x < (s32)rules->grid_width
        && cell_row >= 0 && cell_row < (s32)rules->zone_rows && !battle->placed[cell_row][cell_x]
        && battle->defs->units[def].cost <= battle_supply_left(battle);
}

b32 battle_place(Battle* battle, u32 def, s32 cell_x, s32 cell_row)
{
    if (!battle_can_place(battle, def, cell_x, cell_row))
        return false;
    battle->placed[cell_row][cell_x] = (u8)(def + 1);
    battle->supply_used += battle->defs->units[def].cost;
    rebuild_units(battle);
    return true;
}

b32 battle_remove(Battle* battle, s32 cell_x, s32 cell_row)
{
    const BattleRules* rules = &battle->defs->rules;
    if (battle->phase != BATTLE_DEPLOY || cell_x < 0 || cell_x >= (s32)rules->grid_width || cell_row < 0 || cell_row >= (s32)rules->zone_rows
        || !battle->placed[cell_row][cell_x])
        return false;
    battle->supply_used -= battle->defs->units[battle->placed[cell_row][cell_x] - 1].cost;
    battle->placed[cell_row][cell_x] = 0;
    rebuild_units(battle);
    return true;
}

void battle_clear_placement(Battle* battle)
{
    if (battle->phase != BATTLE_DEPLOY)
        return;
    memset(battle->placed, 0, sizeof(battle->placed));
    battle->supply_used = 0;
    rebuild_units(battle);
}

u32 battle_alive_count(const Battle* battle, u32 team)
{
    u32 count = 0;
    for (u32 i = 1; i <= battle->unit_count; ++i)
        count += battle->units[i].team == team && is_alive(&battle->units[i]);
    return count;
}

f32 battle_remaining_value(const Battle* battle, u32 team)
{
    f32 value = 0.0f;
    for (u32 i = 1; i <= battle->unit_count; ++i) {
        const Unit* unit = &battle->units[i];
        if (unit->team == team && is_alive(unit)) {
            const UnitDef* def = &battle->defs->units[unit->def];
            value += (f32)def->cost * unit->health / def->health;
        }
    }
    return value;
}

b32 battle_start(Battle* battle)
{
    if (battle->phase != BATTLE_DEPLOY || battle_alive_count(battle, TEAM_PLAYER) == 0)
        return false;
    nv_random_seed(&battle->rng, battle->defs->seed, 1);
    battle->phase = BATTLE_FIGHT;
    return true;
}

void battle_retry(Battle* battle)
{
    battle->phase = BATTLE_DEPLOY;
    rebuild_units(battle);
}

// TODO: Replaying from the start is cheap for stage A's armies (a few dozen units); with B's larger ones, keep snapshots every
// second of battle and replay from the nearest one.
void battle_seek(Battle* battle, u32 tick)
{
    if (battle->phase == BATTLE_DEPLOY)
        return;
    battle_retry(battle);
    b32 started = battle_start(battle);
    NV_ASSERT(started); // a round that was started has player units to start with
    (void)started;
    while (battle->tick < tick && battle->phase == BATTLE_FIGHT) {
        battle_tick(battle);
        battle->event_count = 0;
    }
    battle->events_dropped = 0;
}

// The nearest living enemy on the ground (XZ), the lower slot on a tie; 0 when there is none.
internal u32 nearest_enemy(const Battle* battle, const Unit* unit)
{
    u32 best = 0;
    f32 best_distance = FLT_MAX;
    for (u32 j = 1; j <= battle->unit_count; ++j) {
        const Unit* other = &battle->units[j];
        if (other->team == unit->team || !is_alive(other))
            continue;
        f32 distance = distance_xz_squared(unit->position, other->position);
        if (distance < best_distance) {
            best_distance = distance;
            best = j;
        }
    }
    return best;
}

internal void pick_targets(Battle* battle)
{
    for (u32 i = 1; i <= battle->unit_count; ++i) {
        Unit* unit = &battle->units[i];
        if (!is_alive(unit))
            continue;
        b32 has_target = unit->target.index && is_alive(&battle->units[unit->target.index]);
        if (!has_target || battle->tick % battle->defs->rules.retarget_ticks == 0)
            unit->target.index = nearest_enemy(battle, unit);
    }
}

// Each unit steers straight at its target and stops once it is within the rules' stop_fraction of its range; then overlapping
// units push each other apart, each by half the overlap (the same weight), and the field's edge holds them in. Turning is
// instant: a moving unit faces its velocity, a stopped one its target.
internal void move_units(Battle* battle)
{
    const BattleRules* rules = &battle->defs->rules;
    const f32 field_width = (f32)rules->grid_width * rules->cell_size;
    const f32 field_length = (f32)rules->grid_length * rules->cell_size;

    for (u32 i = 1; i <= battle->unit_count; ++i) {
        Unit* unit = &battle->units[i];
        unit->velocity = nv_vec3(0.0f, 0.0f, 0.0f);
        if (!is_alive(unit) || !unit->target.index)
            continue;
        const UnitDef* def = &battle->defs->units[unit->def];
        NvVec3 to_target = nv_vec3_sub(battle->units[unit->target.index].position, unit->position);
        to_target.y = 0.0f;
        f32 distance = sqrtf(nv_vec3_dot(to_target, to_target));
        f32 stop_distance = def->weapon.range * rules->stop_fraction;
        if (distance > stop_distance) {
            f32 step = fminf(def->speed * BATTLE_TICK_SECONDS, distance - stop_distance); // no overshooting the stop
            unit->velocity = nv_vec3_scale(to_target, step / distance / BATTLE_TICK_SECONDS);
            unit->position = nv_vec3_add(unit->position, nv_vec3_scale(unit->velocity, BATTLE_TICK_SECONDS));
            unit->yaw = atan2f(unit->velocity.x, unit->velocity.z);
        } else if (distance > 0.0f) {
            unit->yaw = atan2f(to_target.x, to_target.z);
        }
    }

    memset(battle->push, 0, sizeof(battle->push[0]) * (battle->unit_count + 1));
    for (u32 i = 1; i <= battle->unit_count; ++i) {
        const Unit* a = &battle->units[i];
        if (!is_alive(a))
            continue;
        for (u32 j = i + 1; j <= battle->unit_count; ++j) {
            const Unit* b = &battle->units[j];
            if (!is_alive(b))
                continue;
            f32 reach = battle->defs->units[a->def].radius + battle->defs->units[b->def].radius;
            f32 dx = b->position.x - a->position.x, dz = b->position.z - a->position.z;
            f32 distance_squared = dx * dx + dz * dz;
            if (distance_squared >= reach * reach)
                continue;
            f32 distance = sqrtf(distance_squared);
            // Units on exactly the same spot are pushed apart along X, the lower slot to the left.
            NvVec3 normal = distance > 1e-6f ? nv_vec3(dx / distance, 0.0f, dz / distance) : nv_vec3(1.0f, 0.0f, 0.0f);
            NvVec3 half = nv_vec3_scale(normal, (reach - distance) * 0.5f);
            battle->push[i] = nv_vec3_sub(battle->push[i], half);
            battle->push[j] = nv_vec3_add(battle->push[j], half);
        }
    }
    for (u32 i = 1; i <= battle->unit_count; ++i) {
        Unit* unit = &battle->units[i];
        if (!is_alive(unit))
            continue;
        f32 radius = battle->defs->units[unit->def].radius;
        unit->position = nv_vec3_add(unit->position, battle->push[i]);
        unit->position.x = nv_clamp_f32(unit->position.x, radius, field_width - radius);
        unit->position.z = nv_clamp_f32(unit->position.z, radius, field_length - radius);
    }
}

// How long a shell flies from `from` to land on the ground at `aim`, at `tan_angle`: it climbs d * tan(angle) over the
// way and the muzzle's height sets how far it falls, so the time follows from the height alone.
internal f32 flight_time(NvVec3 from, NvVec3 aim, f32 tan_angle, f32 gravity)
{
    f32 distance = sqrtf(distance_xz_squared(from, aim));
    f32 rise = distance * tan_angle - (aim.y - from.y);
    return rise > 0.0f ? sqrtf(2.0f * rise / gravity) : 0.0f;
}

internal void fire_shell(Battle* battle, u32 unit_index)
{
    Unit* unit = &battle->units[unit_index];
    const WeaponDef* weapon = &battle->defs->units[unit->def].weapon;
    const Unit* target = &battle->units[unit->target.index];

    // The unit's axes: forward (sin yaw, 0, cos yaw), up +Y, and right = forward x up = (-cos yaw, 0, sin yaw) (Y up and
    // right-handed, so a unit facing +Z has its right toward -X).
    f32 sin_yaw = sinf(unit->yaw), cos_yaw = cosf(unit->yaw);
    NvVec3 muzzle = unit->position;
    muzzle.x += -cos_yaw * weapon->muzzle.x + sin_yaw * weapon->muzzle.z;
    muzzle.y += weapon->muzzle.y;
    muzzle.z += sin_yaw * weapon->muzzle.x + cos_yaw * weapon->muzzle.z;

    // Lead the target: where it will be when the shell lands, from its velocity now. The landing spot depends on the
    // flight time and the flight time on the landing spot, so they are solved in turn.
    f32 tan_angle = tanf(weapon->launch_angle);
    f32 gravity = battle->defs->rules.gravity;
    NvVec3 aim = nv_vec3(target->position.x, 0.0f, target->position.z);
    for (u32 pass = 0; pass < 3; ++pass) {
        f32 time = flight_time(muzzle, aim, tan_angle, gravity);
        aim.x = target->position.x + target->velocity.x * time;
        aim.z = target->position.z + target->velocity.z * time;
    }
    // Spread: a point within a circle around it, evenly (the radius goes with the square root of the draw). Both numbers
    // are drawn even when the spread is 0, so changing it does not shift the draws that follow.
    f32 spread_radius = weapon->spread * sqrtf(nv_random_f32(&battle->rng));
    f32 spread_angle = 2.0f * NV_PI * nv_random_f32(&battle->rng);
    aim.x += spread_radius * cosf(spread_angle);
    aim.z += spread_radius * sinf(spread_angle);

    // The launch velocity that lands on `aim` at the weapon's angle.
    f32 time = flight_time(muzzle, aim, tan_angle, gravity);
    NvVec3 velocity = nv_vec3(0.0f, 0.0f, 0.0f);
    if (time > 0.0f) {
        f32 distance = sqrtf(distance_xz_squared(muzzle, aim));
        velocity = nv_vec3((aim.x - muzzle.x) / time, tan_angle * distance / time, (aim.z - muzzle.z) / time);
    }

    Projectile* shell = &battle->projectiles[battle->projectile_count++];
    *shell = (Projectile){.team = unit->team, .shooter = {unit_index}, .position = muzzle, .previous_position = muzzle,
                          .velocity = velocity, .damage = weapon->damage};
    // No flight time (a muzzle at or below the ground right over its aim) leaves the shell still: it falls, and lands at once.
    NvVec3 direction = time > 0.0f ? nv_vec3_normalize(velocity) : nv_vec3(0.0f, -1.0f, 0.0f);
    add_event(battle, BATTLE_EVENT_FIRE, unit, muzzle, direction, weapon->damage);
}

internal void fire_weapons(Battle* battle)
{
    for (u32 i = 1; i <= battle->unit_count; ++i) {
        Unit* unit = &battle->units[i];
        if (!is_alive(unit))
            continue;
        if (unit->cooldown_ticks)
            --unit->cooldown_ticks;
        if (unit->cooldown_ticks || !unit->target.index || battle->projectile_count == BATTLE_MAX_PROJECTILES)
            continue; // with the pool full the weapon waits, ready, for a slot
        const WeaponDef* weapon = &battle->defs->units[unit->def].weapon;
        const Unit* target = &battle->units[unit->target.index];
        if (distance_xz_squared(unit->position, target->position) > weapon->range * weapon->range)
            continue;
        fire_shell(battle, i);
        u32 cooldown = battle_seconds_to_ticks(weapon->cooldown);
        unit->cooldown_ticks = cooldown ? cooldown : 1;
    }
}

// Where the segment `start` to `end` first enters the upright cylinder standing on `base` (as a share of the segment, 0 to
// 1), or 2 when it never does. A segment that starts inside enters at 0.
internal f32 segment_cylinder(NvVec3 start, NvVec3 end, NvVec3 base, f32 radius, f32 height)
{
    f32 enter = 0.0f, leave = 1.0f;
    // The slab between the cylinder's bottom and top.
    f32 dy = end.y - start.y;
    if (fabsf(dy) < 1e-9f) {
        if (start.y < base.y || start.y > base.y + height)
            return 2.0f;
    } else {
        f32 t_bottom = (base.y - start.y) / dy, t_top = (base.y + height - start.y) / dy;
        enter = fmaxf(enter, fminf(t_bottom, t_top));
        leave = fminf(leave, fmaxf(t_bottom, t_top));
    }
    // The disk, as the roots of a t^2 + 2 b t + c = 0.
    f32 dx = end.x - start.x, dz = end.z - start.z;
    f32 fx = start.x - base.x, fz = start.z - base.z;
    f32 a = dx * dx + dz * dz;
    f32 c = fx * fx + fz * fz - radius * radius;
    if (a < 1e-12f) {
        if (c > 0.0f)
            return 2.0f;
    } else {
        f32 b = fx * dx + fz * dz;
        f32 discriminant = b * b - a * c;
        if (discriminant < 0.0f)
            return 2.0f;
        f32 root = sqrtf(discriminant);
        enter = fmaxf(enter, (-b - root) / a);
        leave = fminf(leave, (-b + root) / a);
    }
    return enter <= leave ? enter : 2.0f;
}

// Where the segment crosses the boundary of a sphere it starts outside of (0 to 1), or 2 when it does not.
internal f32 segment_sphere_entry(NvVec3 start, NvVec3 end, NvVec3 center, f32 radius)
{
    NvVec3 d = nv_vec3_sub(end, start), f = nv_vec3_sub(start, center);
    f32 c = nv_vec3_dot(f, f) - radius * radius;
    f32 a = nv_vec3_dot(d, d);
    if (c <= 0.0f || a < 1e-12f)
        return 2.0f; // starts inside (the shield does not take what is already in it), or does not move
    f32 b = nv_vec3_dot(f, d);
    f32 discriminant = b * b - a * c;
    if (discriminant < 0.0f)
        return 2.0f;
    f32 t = (-b - sqrtf(discriminant)) / a;
    return t >= 0.0f && t <= 1.0f ? t : 2.0f;
}

typedef struct Hit {
    HitKind kind;
    u32 unit; // the shield's or the unit's slot
    f32 t;    // along the segment, 0 to 1
} Hit;

// The first thing the shell's segment touches among enemy shields, enemy units and the ground; on a tie the earlier of
// those kinds and the lower slot.
internal Hit find_hit(const Battle* battle, u8 team, NvVec3 start, NvVec3 end)
{
    Hit hit = {HIT_NONE, 0, 2.0f};
    for (u32 i = 1; i <= battle->unit_count; ++i) {
        const Unit* unit = &battle->units[i];
        const UnitDef* def = &battle->defs->units[unit->def];
        if (unit->team == team || !is_alive(unit) || def->ability.kind != ABILITY_SHIELD || unit->ability.shield.energy <= 0.0f)
            continue;
        NvVec3 center = unit->position;
        center.y += def->height * 0.5f;
        f32 t = segment_sphere_entry(start, end, center, def->ability.shield.radius);
        if (t < hit.t)
            hit = (Hit){HIT_SHIELD, i, t};
    }
    for (u32 i = 1; i <= battle->unit_count; ++i) {
        const Unit* unit = &battle->units[i];
        if (unit->team == team || !is_alive(unit))
            continue;
        const UnitDef* def = &battle->defs->units[unit->def];
        f32 t = segment_cylinder(start, end, unit->position, def->radius, def->height);
        if (t < hit.t)
            hit = (Hit){HIT_UNIT, i, t};
    }
    if (end.y <= 0.0f) {
        f32 t = start.y > 0.0f ? start.y / (start.y - end.y) : 0.0f;
        if (t < hit.t)
            hit = (Hit){HIT_GROUND, 0, t};
    }
    return hit;
}

internal void remove_projectile(Battle* battle, u32 index)
{
    battle->projectiles[index] = battle->projectiles[--battle->projectile_count];
}

// Advances the shells one tick. Gravity is applied so that a tick's motion is exact for a constant pull (a shell lands
// where the launch solved for, whatever the tick length). The segment each shell moved is tested for the first shield,
// unit or ground it touches. A shield takes damage as energy, up to what it has: a shell it cannot take whole goes on from
// the shield's boundary with the damage that is left, to whatever it touches next.
internal void step_projectiles(Battle* battle)
{
    const BattleRules* rules = &battle->defs->rules;
    for (u32 i = 0; i < battle->projectile_count;) {
        Projectile* shell = &battle->projectiles[i];
        shell->previous_position = shell->position;
        shell->position = nv_vec3_add(shell->position, nv_vec3_scale(shell->velocity, BATTLE_TICK_SECONDS));
        shell->position.y -= 0.5f * rules->gravity * BATTLE_TICK_SECONDS * BATTLE_TICK_SECONDS;
        shell->velocity.y -= rules->gravity * BATTLE_TICK_SECONDS;

        NvVec3 start = shell->previous_position;
        b32 gone = false;
        for (;;) {
            Hit hit = find_hit(battle, shell->team, start, shell->position);
            if (hit.kind == HIT_NONE)
                break;
            NvVec3 point = nv_vec3_add(start, nv_vec3_scale(nv_vec3_sub(shell->position, start), hit.t));
            if (hit.kind == HIT_SHIELD) {
                Unit* shield_unit = &battle->units[hit.unit];
                f32 energy = shield_unit->ability.shield.energy;
                f32 taken = fminf(energy, shell->damage);
                shield_unit->ability.shield.energy = energy - taken;
                shield_unit->ability.shield.since_hit_ticks = 0;
                add_event(battle, BATTLE_EVENT_SHIELD_HIT, shield_unit, point, nv_vec3_normalize(shell->velocity), taken);
                shell->damage -= taken;
                if (shell->damage <= 0.0f) {
                    gone = true;
                    break;
                }
                start = point; // goes on, past the shield that is now off
            } else {
                if (hit.kind == HIT_UNIT) {
                    Unit* unit = &battle->units[hit.unit];
                    f32 armor = battle->defs->units[unit->def].armor;
                    f32 damage = fmaxf(shell->damage - armor, shell->damage * rules->min_damage_fraction);
                    unit->damage_taken += damage;
                    add_event(battle, BATTLE_EVENT_HIT, unit, point, nv_vec3_normalize(shell->velocity), damage);
                }
                gone = true;
                break;
            }
        }
        if (gone)
            remove_projectile(battle, i); // the last shell takes this slot and is stepped next, so `i` stays
        else
            ++i;
    }
}

internal void apply_deaths(Battle* battle)
{
    for (u32 i = 1; i <= battle->unit_count; ++i) {
        Unit* unit = &battle->units[i];
        if (!is_alive(unit))
            continue;
        unit->health -= unit->damage_taken;
        unit->damage_taken = 0.0f;
        if (unit->health <= 0.0f) {
            unit->health = 0.0f;
            unit->flags |= UNIT_DEAD;
            unit->velocity = nv_vec3(0.0f, 0.0f, 0.0f);
            add_event(battle, BATTLE_EVENT_DEATH, unit, unit->position, nv_vec3(0.0f, 1.0f, 0.0f), 0.0f);
        }
    }
}

// A shield that has not been hit for its delay refills; it is on again once it holds energy.
internal void refill_shields(Battle* battle)
{
    for (u32 i = 1; i <= battle->unit_count; ++i) {
        Unit* unit = &battle->units[i];
        const AbilityDef* ability = &battle->defs->units[unit->def].ability;
        if (!is_alive(unit) || ability->kind != ABILITY_SHIELD)
            continue;
        if (unit->ability.shield.since_hit_ticks < UINT32_MAX)
            ++unit->ability.shield.since_hit_ticks;
        if (unit->ability.shield.since_hit_ticks >= battle_seconds_to_ticks(ability->shield.regen_delay))
            unit->ability.shield.energy = fminf(ability->shield.capacity,
                                                unit->ability.shield.energy + ability->shield.regen * BATTLE_TICK_SECONDS);
    }
}

// The round ends when a side is wiped out (both at once is a draw) or at the time limit, when the side with more
// remaining value wins.
internal void check_end(Battle* battle)
{
    u32 player = battle_alive_count(battle, TEAM_PLAYER), enemy = battle_alive_count(battle, TEAM_ENEMY);
    if (!player || !enemy) {
        battle->outcome = !player && !enemy ? OUTCOME_DRAW : !player ? OUTCOME_ENEMY : OUTCOME_PLAYER;
    } else if (battle->tick >= battle->defs->rules.round_ticks) {
        f32 player_value = battle_remaining_value(battle, TEAM_PLAYER), enemy_value = battle_remaining_value(battle, TEAM_ENEMY);
        battle->outcome = player_value > enemy_value ? OUTCOME_PLAYER : player_value < enemy_value ? OUTCOME_ENEMY : OUTCOME_DRAW;
    } else {
        return;
    }
    battle->phase = BATTLE_RESULT;
}

void battle_tick(Battle* battle)
{
    if (battle->phase != BATTLE_FIGHT)
        return;
    for (u32 i = 1; i <= battle->unit_count; ++i) {
        battle->units[i].previous_position = battle->units[i].position;
        battle->units[i].previous_yaw = battle->units[i].yaw;
    }
    pick_targets(battle);
    move_units(battle);
    fire_weapons(battle);
    step_projectiles(battle);
    apply_deaths(battle);
    refill_shields(battle);
    ++battle->tick;
    check_end(battle);
}


u32 battle_hash(const Battle* battle)
{
    u32 hash = NV_FNV1A_SEED;
    hash = nv_fnv1a(hash, &battle->tick, sizeof(battle->tick));
    hash = nv_fnv1a(hash, &battle->outcome, sizeof(battle->outcome));
    hash = nv_fnv1a(hash, &battle->rng, sizeof(battle->rng));
    for (u32 i = 1; i <= battle->unit_count; ++i) {
        const Unit* unit = &battle->units[i];
        hash = nv_fnv1a(hash, &unit->flags, sizeof(unit->flags));
        hash = nv_fnv1a(hash, &unit->position, sizeof(unit->position));
        hash = nv_fnv1a(hash, &unit->velocity, sizeof(unit->velocity));
        hash = nv_fnv1a(hash, &unit->yaw, sizeof(unit->yaw));
        hash = nv_fnv1a(hash, &unit->health, sizeof(unit->health));
        hash = nv_fnv1a(hash, &unit->target, sizeof(unit->target));
        hash = nv_fnv1a(hash, &unit->cooldown_ticks, sizeof(unit->cooldown_ticks));
        hash = nv_fnv1a(hash, &unit->ability.shield, sizeof(unit->ability.shield));
    }
    for (u32 i = 0; i < battle->projectile_count; ++i) {
        const Projectile* shell = &battle->projectiles[i];
        hash = nv_fnv1a(hash, &shell->position, sizeof(shell->position));
        hash = nv_fnv1a(hash, &shell->velocity, sizeof(shell->velocity));
        hash = nv_fnv1a(hash, &shell->damage, sizeof(shell->damage));
    }
    return hash;
}

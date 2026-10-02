// The effects the app fires and the View tab's Effects section (docs/specs/vfx.md). The engine
// (engine/vfx.h) simulates and draws the particles; this file says what an explosion, sparks, smoke and
// a missile's trail are made of, and the buttons that fire them at a point beside the orbit point.

#include "app.h"

#include <math.h>

#define RGBA(r, g, b, a) {r, g, b, a}

// An effect is a group of emitters; each is a stream of particles with its own look. Colors are linear
// and may exceed 1, which is what bloom spreads.
internal NvVfxEffectId make_explosion(NvVfx* vfx)
{
    NvVfxEffectDesc effect = {.name = "Explosion", .emitter_count = 5};
    // The flash, and the shockwave ring behind it.
    effect.emitters[0] = (NvVfxEmitterDesc){
        .count = 1, .life_min = 0.25f, .life_max = 0.25f, .size_start = 0.6f, .size_end = 3.2f,
        .colors = {RGBA(7.0f, 4.5f, 1.5f, 1.0f), RGBA(3.0f, 1.4f, 0.4f, 0.8f), RGBA(1.0f, 0.3f, 0.1f, 0.0f)},
        .shape = NV_VFX_SHAPE_DISC, .blend = NV_VFX_BLEND_ADD};
    effect.emitters[1] = (NvVfxEmitterDesc){
        .count = 1, .life_min = 0.55f, .life_max = 0.55f, .size_start = 0.3f, .size_end = 4.8f,
        .colors = {RGBA(1.6f, 1.3f, 0.9f, 0.7f), RGBA(0.6f, 0.45f, 0.3f, 0.4f), RGBA(0.1f, 0.1f, 0.1f, 0.0f)},
        .shape = NV_VFX_SHAPE_RING, .blend = NV_VFX_BLEND_ADD};
    // Sparks thrown in every direction, falling, bouncing on the ground.
    effect.emitters[2] = (NvVfxEmitterDesc){
        .count = 140, .life_min = 0.5f, .life_max = 1.3f, .speed_min = 4.0f, .speed_max = 15.0f, .cone = NV_PI,
        .gravity = 9.0f, .drag = 1.1f, .use_ground = 1, .ground = 0.02f, .restitution = 0.4f, .friction = 0.3f,
        .size_start = 0.07f, .size_end = 0.02f, .stretch = 0.3f,
        .colors = {RGBA(9.0f, 5.5f, 1.6f, 1.0f), RGBA(4.0f, 1.5f, 0.3f, 1.0f), RGBA(0.7f, 0.1f, 0.02f, 0.0f)},
        .shape = NV_VFX_SHAPE_STREAK, .blend = NV_VFX_BLEND_ADD};
    // Smoke that rises and swirls.
    effect.emitters[3] = (NvVfxEmitterDesc){
        .count = 30, .delay = 0.05f, .life_min = 1.6f, .life_max = 3.2f, .speed_min = 0.5f, .speed_max = 3.0f, .cone = 1.2f,
        .gravity = -0.6f, .drag = 0.8f, .turbulence = 1.4f, .turbulence_scale = 0.7f,
        .size_start = 0.4f, .size_end = 1.5f, .spin = 1.0f,
        .colors = {RGBA(0.9f, 0.55f, 0.25f, 0.85f), RGBA(0.22f, 0.2f, 0.18f, 0.7f), RGBA(0.05f, 0.05f, 0.05f, 0.0f)},
        .shape = NV_VFX_SHAPE_PUFF, .blend = NV_VFX_BLEND_ALPHA};
    // Dark debris.
    effect.emitters[4] = (NvVfxEmitterDesc){
        .count = 24, .life_min = 1.5f, .life_max = 3.0f, .speed_min = 5.0f, .speed_max = 12.0f, .cone = 1.1f,
        .gravity = 12.0f, .drag = 0.2f, .use_ground = 1, .ground = 0.05f, .restitution = 0.45f, .friction = 0.4f,
        .size_start = 0.08f, .size_end = 0.08f,
        .colors = {RGBA(0.16f, 0.11f, 0.08f, 1.0f), RGBA(0.14f, 0.1f, 0.07f, 1.0f), RGBA(0.1f, 0.07f, 0.05f, 0.0f)},
        .shape = NV_VFX_SHAPE_DISC, .blend = NV_VFX_BLEND_ALPHA};
    return nv_vfx_add_effect(vfx, &effect);
}

internal NvVfxEffectId make_sparks(NvVfx* vfx)
{
    NvVfxEffectDesc effect = {.name = "Sparks", .emitter_count = 1};
    effect.emitters[0] = (NvVfxEmitterDesc){
        .count = 70, .life_min = 0.3f, .life_max = 0.9f, .speed_min = 4.0f, .speed_max = 12.0f, .cone = 0.7f,
        .gravity = 9.8f, .drag = 0.6f, .use_ground = 1, .ground = 0.02f, .restitution = 0.5f, .friction = 0.25f,
        .size_start = 0.06f, .size_end = 0.02f, .stretch = 0.35f,
        .colors = {RGBA(10.0f, 7.0f, 2.5f, 1.0f), RGBA(4.5f, 2.0f, 0.5f, 1.0f), RGBA(0.8f, 0.15f, 0.03f, 0.0f)},
        .shape = NV_VFX_SHAPE_STREAK, .blend = NV_VFX_BLEND_ADD};
    return nv_vfx_add_effect(vfx, &effect);
}

// Three overlapping puffs of different colors, nearly opaque: the particles are not sorted, so where they
// overlap the order they were drawn in shows (docs/specs/vfx.md says that is a known issue to fix later).
internal NvVfxEffectId make_smoke(NvVfx* vfx)
{
    // TODO: Alpha particles are not sorted by depth, so overlapping smoke flickers; GPU sorting is a later step.
    NvVfxEffectDesc effect = {.name = "Smoke", .emitter_count = 3};
    f32 tints[3][3] = {{0.95f, 0.95f, 0.95f}, {0.25f, 0.25f, 0.28f}, {0.8f, 0.45f, 0.25f}};
    for (u32 i = 0; i < 3; ++i) {
        effect.emitters[i] = (NvVfxEmitterDesc){
            .count = 14, .life_min = 3.0f, .life_max = 5.0f, .speed_min = 0.3f, .speed_max = 1.6f, .cone = 0.5f,
            .gravity = -0.25f, .drag = 0.5f, .turbulence = 0.9f, .turbulence_scale = 0.5f,
            .size_start = 0.5f, .size_end = 1.3f, .spin = 0.6f,
            .colors = {RGBA(tints[i][0], tints[i][1], tints[i][2], 0.95f), RGBA(tints[i][0], tints[i][1], tints[i][2], 0.95f),
                       RGBA(tints[i][0], tints[i][1], tints[i][2], 0.0f)},
            .shape = NV_VFX_SHAPE_PUFF, .blend = NV_VFX_BLEND_ALPHA};
    }
    return nv_vfx_add_effect(vfx, &effect);
}

// What a missile leaves along its flight: a hot flame and smoke.
internal NvVfxEffectId make_missile(NvVfx* vfx)
{
    NvVfxEffectDesc effect = {.name = "Missile", .emitter_count = 2};
    effect.emitters[0] = (NvVfxEmitterDesc){
        .count = 1, .life_min = 0.25f, .life_max = 0.45f, .speed_min = 0.0f, .speed_max = 0.6f, .cone = NV_PI,
        .size_start = 0.16f, .size_end = 0.03f,
        .colors = {RGBA(8.0f, 4.0f, 1.0f, 1.0f), RGBA(3.0f, 1.0f, 0.2f, 0.9f), RGBA(0.5f, 0.1f, 0.02f, 0.0f)},
        .shape = NV_VFX_SHAPE_DISC, .blend = NV_VFX_BLEND_ADD};
    effect.emitters[1] = (NvVfxEmitterDesc){
        .count = 1, .life_min = 1.4f, .life_max = 2.4f, .speed_min = 0.1f, .speed_max = 0.7f, .cone = NV_PI,
        .gravity = -0.3f, .drag = 0.6f, .turbulence = 0.7f, .turbulence_scale = 1.0f,
        .size_start = 0.14f, .size_end = 0.6f, .spin = 1.0f,
        .colors = {RGBA(0.7f, 0.65f, 0.6f, 0.45f), RGBA(0.35f, 0.34f, 0.33f, 0.35f), RGBA(0.1f, 0.1f, 0.1f, 0.0f)},
        .shape = NV_VFX_SHAPE_PUFF, .blend = NV_VFX_BLEND_ALPHA};
    return nv_vfx_add_effect(vfx, &effect);
}

// The strips and the mark the test effects use: a thin hot trail behind a missile, a laser that
// flickers and runs, and a scorch mark where something hit the ground.
internal void make_styles(Effects* effects)
{
    effects->missile_trail = (NvVfxLineStyle){
        .life = 0.9f, .width_start = 0.09f, .width_end = 0.0f,
        .colors = {RGBA(4.0f, 2.2f, 0.7f, 0.9f), RGBA(0.6f, 0.2f, 0.05f, 0.0f)}};
    effects->laser = (NvVfxLineStyle){
        .life = 0.4f, .width_start = 0.12f, .width_end = 0.05f,
        .colors = {RGBA(0.6f, 3.0f, 8.0f, 1.0f), RGBA(0.1f, 0.5f, 2.0f, 0.0f)}, .flicker = 0.35f, .scroll = 5.0f};
    effects->scorch = (NvVfxDecalStyle){
        .life = 12.0f, .fade = 0.4f, .color = RGBA(0.02f, 0.015f, 0.01f, 0.8f), .shape = NV_VFX_SHAPE_PUFF};
}

// Embers thrown up and falling back: what the stress scene's Effects workload fires in bursts to hold a
// number of live particles. Small, additive, bouncing on the ground.
internal NvVfxEffectId make_swarm(NvVfx* vfx)
{
    NvVfxEffectDesc effect = {.name = "Swarm", .emitter_count = 1};
    effect.emitters[0] = (NvVfxEmitterDesc){
        .count = 512, .life_min = 2.0f, .life_max = 3.0f, .speed_min = 2.0f, .speed_max = 10.0f, .cone = 1.2f,
        .gravity = 6.0f, .drag = 0.3f, .use_ground = 1, .ground = 0.02f, .restitution = 0.4f, .friction = 0.3f,
        .size_start = 0.05f, .size_end = 0.03f,
        .colors = {RGBA(3.0f, 1.4f, 0.4f, 1.0f), RGBA(1.5f, 0.5f, 0.1f, 0.8f), RGBA(0.3f, 0.08f, 0.02f, 0.0f)},
        .shape = NV_VFX_SHAPE_DISC, .blend = NV_VFX_BLEND_ADD};
    return nv_vfx_add_effect(vfx, &effect);
}

void effects_init(App* app)
{
    Effects* effects = &app->effects;
    nv_random_seed(&effects->rng, 0x9E3779B9u, 1);
    make_styles(effects);
    nv_vfx_init(&app->vfx, &app->gpu, (NvVfxCapacity){0}, &app->permanent);
    effects->explosion = make_explosion(&app->vfx);
    effects->sparks = make_sparks(&app->vfx);
    effects->smoke = make_smoke(&app->vfx);
    effects->missile = make_missile(&app->vfx);
    effects->swarm = make_swarm(&app->vfx);
    app->renderer.vfx = &app->vfx;
}

// Where the test effects go: beside the orbit point, to the right of what the camera sees, on the ground.
NvVec3 effects_test_point(App* app)
{
    SceneView* view = app_view(app);
    NvNode* camera = nv_scene_get(view->scene, view->scene->active_camera);
    NvVec3 right = nv_vec3(camera->world.e[0], 0.0f, camera->world.e[2]);
    f32 length = sqrtf(right.x * right.x + right.z * right.z);
    if (length > 0.001f)
        right = nv_vec3_scale(right, 1.0f / length);
    else
        right = nv_vec3(1.0f, 0.0f, 0.0f);
    return nv_vec3(view->orbit_point.x + right.x * 2.5f, 0.05f, view->orbit_point.z + right.z * 2.5f);
}

// A scorch mark on the ground below `point`, turned at random.
internal void scorch_at(App* app, NvVec3 point, f32 size)
{
    f32 angle = (f32)(app->vfx.random & 1023u) * (6.2831853f / 1024.0f);
    nv_vfx_decal(&app->vfx, &app->effects.scorch, nv_vec3(point.x, 0.02f, point.z), angle, size);
}

// Fires one of the test effects (0 explosion, 1 sparks, 2 smoke, 3 missile, 4 laser, 5 scorch).
void effects_fire(App* app, u32 which)
{
    Effects* effects = &app->effects;
    NvVec3 point = effects_test_point(app);
    NvVec3 up = nv_vec3(0.0f, 1.0f, 0.0f);
    switch (which) {
    case 0: nv_vfx_burst(&app->vfx, effects->explosion, point, up, 1.0f); break;
    case 1: nv_vfx_burst(&app->vfx, effects->sparks, nv_vec3_add(point, nv_vec3(0.0f, 0.1f, 0.0f)), up, 1.0f); break;
    case 2: nv_vfx_burst(&app->vfx, effects->smoke, nv_vec3_add(point, nv_vec3(0.0f, 0.2f, 0.0f)), up, 1.0f); break;
    case 3:
        // A missile that climbs out of the ground toward a target some meters on, then explodes there.
        if (effects->flight_count < NV_ARRAY_COUNT(effects->flights)) {
            effects->flights[effects->flight_count++] = (Flight){
                .pos = nv_vec3_add(point, nv_vec3(-3.0f, 0.5f, 0.0f)), .vel = nv_vec3(4.0f, 2.0f, 0.0f), .life = 1.2f};
        }
        break;
    case 4: {
        // A laser from above and to the side onto the ground: a beam, sparks where it hits and a mark.
        NvVec3 from = nv_vec3_add(point, nv_vec3(-3.0f, 3.0f, -1.5f));
        nv_vfx_beam(&app->vfx, &effects->laser, from, point, effects->laser.life);
        nv_vfx_burst(&app->vfx, effects->sparks, point, up, 0.6f);
        scorch_at(app, point, 0.9f);
        break;
    }
    case 5: scorch_at(app, point, 2.0f); break;
    }
}

void effects_clear(App* app)
{
    nv_vfx_clear(&app->vfx);
    app->effects.flight_count = 0;
    app->effects.stress_flight_count = 0;
    app->effects.swarm_carry = app->effects.explosion_carry = app->effects.decal_carry = 0.0f;
}

//
// The stress scene's Effects workload
//

// A point on the ground in the field around the orbit point.
internal NvVec3 field_point(App* app, f32 height)
{
    Effects* effects = &app->effects;
    NvVec3 orbit = app_view(app)->orbit_point;
    return nv_vec3(orbit.x + (nv_random_f32(&effects->rng) * 2.0f - 1.0f) * 16.0f, height, orbit.z + (nv_random_f32(&effects->rng) * 2.0f - 1.0f) * 12.0f);
}

internal void new_flight(App* app, Flight* flight)
{
    Effects* effects = &app->effects;
    f32 angle = nv_random_f32(&effects->rng) * 6.2831853f;
    f32 speed = 6.0f + nv_random_f32(&effects->rng) * 4.0f;
    flight->pos = field_point(app, 0.3f);
    flight->vel = nv_vec3(cosf(angle) * speed, 3.0f + nv_random_f32(&effects->rng) * 2.0f, sinf(angle) * speed);
    flight->life = 1.2f + nv_random_f32(&effects->rng) * 0.8f;
}

void effects_stress_update(App* app, const StressWorkloads* want, f32 dt)
{
    Effects* effects = &app->effects;
    NvVfx* vfx = &app->vfx;
    if (dt > 0.1f)
        dt = 0.1f;
    b32 on = want->effects_on;
    u32 missiles = on ? (u32)want->effect_missiles : 0;
    if (missiles > STRESS_MAX_MISSILES)
        missiles = STRESS_MAX_MISSILES;
    u32 beams = on ? (u32)want->effect_beams : 0;
    if (beams > STRESS_MAX_BEAMS)
        beams = STRESS_MAX_BEAMS;

    // Missiles that fly, leave a trail and smoke, and start again from another point when their time is up.
    if (effects->stress_flight_count > missiles)
        effects->stress_flight_count = missiles;
    while (effects->stress_flight_count < missiles) {
        Flight* flight = &effects->stress_flights[effects->stress_flight_count++];
        new_flight(app, flight);
        flight->life *= nv_random_f32(&effects->rng); // out of step with each other
    }
    for (u32 i = 0; i < effects->stress_flight_count; ++i) {
        Flight* flight = &effects->stress_flights[i];
        NvVec3 before = flight->pos;
        flight->vel.y -= 4.0f * dt;
        flight->pos = nv_vec3_add(flight->pos, nv_vec3_scale(flight->vel, dt));
        flight->life -= dt;
        if (dt > 0.0f) {
            NvVec3 step = nv_vec3_sub(flight->pos, before);
            // A thinner smoke than the Missile button's: 2,000 of them would fill the particle buffer.
            nv_vfx_emit(vfx, effects->missile, before, flight->pos, (u32)(sqrtf(nv_vec3_dot(step, step)) * 12.0f) + 1);
            nv_vfx_trail(vfx, &effects->missile_trail, before, flight->pos);
        }
        if (flight->life <= 0.0f || flight->pos.y < 0.05f)
            new_flight(app, flight);
    }

    // Beams: each is held by calling again every frame with a short life, between fixed points of its own.
    for (u32 i = 0; i < beams; ++i) {
        u32 h = i * 2654435761u + 12345u;
        NvVec3 orbit = app_view(app)->orbit_point;
        f32 ax = (f32)(h & 1023u) / 1023.0f, az = (f32)((h >> 10) & 1023u) / 1023.0f;
        f32 bx = (f32)((h >> 20) & 1023u) / 1023.0f, bz = (f32)((h * 40503u >> 8) & 1023u) / 1023.0f;
        NvVec3 from = nv_vec3(orbit.x + (ax * 2.0f - 1.0f) * 16.0f, 7.0f, orbit.z + (az * 2.0f - 1.0f) * 12.0f);
        NvVec3 to = nv_vec3(orbit.x + (bx * 2.0f - 1.0f) * 16.0f, 0.05f, orbit.z + (bz * 2.0f - 1.0f) * 12.0f);
        nv_vfx_beam(vfx, &effects->laser, from, to, dt * 1.5f + 0.02f);
    }

    if (!on)
        return;

    // Particles: embers in bursts of 512, at the rate that replaces what dies (they live about 2.5 s).
    f32 rate = (f32)want->effect_particles / 2.5f;
    effects->swarm_carry += rate * dt / 512.0f;
    u32 limit = 4000; // bursts a frame
    while (effects->swarm_carry >= 1.0f) {
        effects->swarm_carry -= 1.0f;
        if (limit) {
            nv_vfx_burst(vfx, effects->swarm, field_point(app, 0.05f), nv_vec3(0.0f, 1.0f, 0.0f), 1.0f);
            --limit;
        }
    }
    effects->explosion_carry += (f32)want->effect_explosions * dt;
    for (u32 n = 0; effects->explosion_carry >= 1.0f && n < 300; ++n) {
        effects->explosion_carry -= 1.0f;
        nv_vfx_burst(vfx, effects->explosion, field_point(app, 0.05f), nv_vec3(0.0f, 1.0f, 0.0f), 1.0f);
    }
    if (effects->explosion_carry > 1.0f)
        effects->explosion_carry = 0.0f;
    effects->decal_carry += (f32)want->effect_decals * dt;
    for (u32 n = 0; effects->decal_carry >= 1.0f && n < 2000; ++n) {
        effects->decal_carry -= 1.0f;
        scorch_at(app, field_point(app, 0.0f), 0.6f + nv_random_f32(&effects->rng) * 1.2f);
    }
}

void effects_update(App* app, f32 dt)
{
    Effects* effects = &app->effects;
    nv_vfx_update(&app->vfx, dt);
    // The test missiles fly in a straight line, leave a trail and explode where they end.
    for (u32 i = 0; i < effects->flight_count;) {
        Flight* flight = &effects->flights[i];
        NvVec3 before = flight->pos;
        flight->vel.y -= 1.2f * dt;
        flight->pos = nv_vec3_add(flight->pos, nv_vec3_scale(flight->vel, dt));
        flight->life -= dt;
        // About 60 particles per meter along the path, so the trail is unbroken at any frame rate.
        f32 length = sqrtf(nv_vec3_dot(nv_vec3_sub(flight->pos, before), nv_vec3_sub(flight->pos, before)));
        if (dt > 0.0f) {
            nv_vfx_emit(&app->vfx, effects->missile, before, flight->pos, (u32)(length * 60.0f) + 1);
            nv_vfx_trail(&app->vfx, &effects->missile_trail, before, flight->pos);
        }
        if (flight->life <= 0.0f) {
            nv_vfx_burst(&app->vfx, effects->explosion, flight->pos, nv_vec3(0.0f, 1.0f, 0.0f), 0.8f);
            scorch_at(app, flight->pos, 1.8f);
            *flight = effects->flights[--effects->flight_count];
        } else {
            ++i;
        }
    }
}

// The View tab's Effects section.
void effects_ui(App* app)
{
    search_section(app, "Effects");
    if (search_group(app, "Fire", "effect explosion sparks smoke missile laser scorch test particles")) {
        const char* names[6] = {TL("Explosion"), TL("Sparks"), TL("Smoke"), TL("Missile"), TL("Laser"), TL("Scorch")};
        for (u32 i = 0; i < 6; ++i) {
            if (i)
                ui_same_line_if_fits(igCalcTextSize(names[i], NULL, true, -1.0f).x + igGetStyle()->FramePadding.x * 2.0f);
            if (igButton(names[i], (ImVec2_c){0, 0}))
                effects_fire(app, i);
        }
    }
    if (search_row(app, "Wind", "particles force"))
        igSliderFloat(TL("Wind"), &app->vfx.wind.x, -6.0f, 6.0f, "%.1f", 0);
    if (search_plain(app)) {
        NvVfxStats stats = nv_vfx_stats(&app->vfx);
        igText(T("Particles: %u alive, %u visible, %u dropped"), stats.alive, stats.visible, (u32)stats.dropped);
        igText(T("Segments: %u, decals: %u"), stats.segments, stats.decals);
    }
}

// Captures the regression cases of the shader port (docs/specs/slang_shader_port.md): screens S1-S10 as PNGs and
// effects V1-V12 as JSON, from a Debug build of the sandbox.
//   node capture.mjs --build <build dir> --out <output dir> [--cases S1,V3] [--list]
import fs from 'fs';
import path from 'path';
import { serve, launch, openSandbox, parseArgs, sleep } from './lib.mjs';

const args = parseArgs(process.argv.slice(2));
const SCENES = ['showcase', 'stress', 'chess'];
const TONE = { clamp: 0, neutral: 1, aces: 2 };
const FIT = { whole: 0, viewport: 1, stretch: 2 };

// Frames and the step of the fixed game clock for a still screen: long enough for the viewport to settle (targets
// remade, shadow map filled, clips restarted), after which the clock is stopped.
const STILL_FRAMES = 6;

// ---- Page helpers ----------------------------------------------------------------------------

const call = (page, name, ...a) => page.evaluate(([n, v]) => Module['_sandbox_debug_' + n](...v), [name, a]);

async function waitFrames(page, frames) {
    await page.waitForFunction((n) => Module._sandbox_debug_frames() >= n, frames, { timeout: 900000, polling: 20 });
}

// Shows scene `index` and waits until it is built and shown.
async function showScene(page, index) {
    await call(page, 'show_scene', index);
    await page.waitForFunction((i) => Module._sandbox_debug_scenes(0, 0) === i && Module._sandbox_debug_scenes(2, i) === 1,
        index, { timeout: 180000, polling: 50 });
}

// Starts the fixed game clock (the effects are cleared and their clock and random numbers reset), restarts the
// clips and runs `frames` frames at `dt`; afterwards frames run with a stopped clock.
async function stillScreen(page, frames = STILL_FRAMES, dt = 1 / 60) {
    await call(page, 'fixed_dt', dt);
    await call(page, 'restart_clips');
    await call(page, 'run_frames', frames);
    await waitFrames(page, frames);
    await sleep(400); // a few frames more at dt 0 for what is drawn a frame late
}

// A few frames of the page, for what the last call has to show (a burst is emitted by the next frame's compute pass).
const frames = (page, n = 4) => page.evaluate((count) => new Promise((resolve) => {
    const tick = (left) => (left ? requestAnimationFrame(() => tick(left - 1)) : resolve());
    tick(count);
}), n);

async function viewportRect(page) {
    return page.evaluate(() => [0, 1, 2, 3].map((c) => Module._sandbox_debug_layout(0, c)));
}

// The scene's viewport as a PNG, with the build label (its commit differs between builds) masked.
async function shootViewport(page, out, name) {
    const [x, y, width, height] = await viewportRect(page);
    await page.screenshot({ path: path.join(out, `${name}.png`), clip: { x, y, width, height } });
    fs.writeFileSync(path.join(out, `${name}.json`), JSON.stringify({ masks: [[0, 0, Math.round(width), 48]] }));
}

async function shootRect(page, out, name, rect, masks = []) {
    await page.screenshot({ path: path.join(out, `${name}.png`), clip: { x: rect[0], y: rect[1], width: rect[2], height: rect[3] } });
    fs.writeFileSync(path.join(out, `${name}.json`), JSON.stringify({ masks }));
}

async function shootPage(page, out, name, masks) {
    await page.screenshot({ path: path.join(out, `${name}.png`) });
    fs.writeFileSync(path.join(out, `${name}.json`), JSON.stringify({ masks }));
}

const DEFAULTS = (page) => page.evaluate(() => {
    Module._sandbox_debug_set_msaa(4);
    Module._sandbox_debug_set_post(1, 1.0, 1, 0.04);
    Module._sandbox_debug_set_shadows(2048, 0);
    Module._sandbox_debug_set_resolution(0, 1, 0);
});

// ---- Screens ---------------------------------------------------------------------------------

// S1-S6: one page for the three scenes of a row of settings.
const SCENE_SETTINGS = {
    S1: { label: 'default', apply: () => {} },
    S2: { label: 'msaa1', apply: () => Module._sandbox_debug_set_msaa(1) },
    S3: { label: 'clamp', apply: () => Module._sandbox_debug_set_post(0, 1.0, 1, 0.04) },
    S4: { label: 'aces', apply: () => Module._sandbox_debug_set_post(2, 1.0, 1, 0.04) },
    S5: { label: 'bloom-off', apply: () => Module._sandbox_debug_set_post(1, 1.0, 0, 0.04) },
    S6: { label: 'shadows-off', apply: () => Module._sandbox_debug_set_shadows(0, 0) },
};

async function sceneScreens(context, wanted, out) {
    const { page, problems } = await openSandbox(context.browser, context.url);
    for (let scene = 0; scene < SCENES.length; ++scene) {
        await showScene(page, scene);
        for (const [id, setting] of Object.entries(SCENE_SETTINGS)) {
            if (!wanted(id)) continue;
            await DEFAULTS(page);
            await page.evaluate(setting.apply);
            await stillScreen(page);
            await shootViewport(page, out, `${id}-${SCENES[scene]}-${setting.label}`);
            console.log('  ', `${id}-${SCENES[scene]}-${setting.label}`);
        }
    }
    await DEFAULTS(page);
    await page.context().close();
    return problems;
}

async function resolutionScreens(context, wanted, out) {
    const { page, problems } = await openSandbox(context.browser, context.url);
    await DEFAULTS(page);
    if (wanted('S7')) {
        await page.evaluate(() => Module._sandbox_debug_set_resolution(0, 2, 0));
        await stillScreen(page);
        await shootViewport(page, out, 'S7-showcase-scale2');
        console.log('   S7');
    }
    if (wanted('S8')) {
        for (const [name, fit] of Object.entries(FIT)) {
            await page.evaluate((f) => { Module._sandbox_debug_set_resolution(1, 320, 180); Module._sandbox_debug_set_fit(f); }, fit);
            await stillScreen(page);
            await shootViewport(page, out, `S8-showcase-fixed320x180-${name}`);
            console.log('   S8', name);
        }
    }
    await page.context().close();
    return problems;
}

async function texturesScreens(context, wanted, out) {
    if (!wanted('S9')) return [];
    const problems = [];
    const IMAGE = 12;
    const picks = [
        { name: 'color', msaa: 4, kind: 0 },
        { name: 'depth', msaa: 1, kind: 2 },
        { name: 'depth-multisampled', msaa: 4, kind: 2 },
    ];
    // A narrow dock shows the list or the picked texture, so each pick starts from a fresh page.
    for (const pick of picks) {
        const { page, problems: p } = await openSandbox(context.browser, context.url);
        problems.push(...p);
        const press = async (x, y) => {
            await page.mouse.move(x, y);
            await page.mouse.down();
            await sleep(120);
            await page.mouse.up();
            await sleep(400);
        };
        const dock = await page.evaluate(() => [0, 1, 2, 3].map((c) => Module._sandbox_debug_layout(3, c)));
        await press(dock[0] + 137, dock[1] + 19); // the Textures tab
        await page.evaluate((m) => Module._sandbox_debug_set_msaa(m), pick.msaa);
        await stillScreen(page);
        await sleep(800);
        // The list is the materials, then the render targets in gather's order: the shadow map, the depth target, ...
        const materials = await page.evaluate(() => Module._sandbox_debug_texture_count());
        const index = pick.kind === 0 ? 0 : materials + 1;
        const row = await page.evaluate((i) => [0, 1, 2, 3].map((c) => Module._sandbox_debug_textures_row(i, c)), index);
        await press((row[0] + row[2]) / 2, (row[1] + row[3]) / 2);
        const selected = await page.evaluate(() => Module._sandbox_debug_textures_selected());
        const found = Math.floor(selected / 1000) === pick.kind;
        if (!found) throw new Error(`S9: no row of kind ${pick.kind} (${pick.name})`);
        await stillScreen(page);
        await sleep(800);
        const image = await page.evaluate((i) => [0, 1, 2, 3].map((c) => Module._sandbox_debug_textures_rect(i, c)), IMAGE);
        await shootRect(page, out, `S9-textures-${pick.name}`, [image[0], image[1], image[2] - image[0], image[3] - image[1]]);
        console.log('   S9', pick.name);
        await page.context().close();
    }
    return problems;
}

async function uiScreens(context, wanted, out) {
    if (!wanted('S10')) return [];
    const problems = [];
    // Desktop: the panels and a menu. The viewport (the scene moves), the Console (times) and the top bar's right end (the
    // frame time) are masked: this case is about ImGui's own drawing.
    {
        const { page, problems: p } = await openSandbox(context.browser, context.url);
        problems.push(...p);
        await stillScreen(page);
        const vp = await viewportRect(page);
        const bottom = await page.evaluate(() => [0, 1, 2, 3].map((c) => Module._sandbox_debug_layout(4, c)));
        const masks = [vp, bottom, [1000, 0, 280, 28]].map((r) => r.map(Math.round));
        await shootPage(page, out, 'S10-desktop', masks);
        await page.mouse.move(139, 15); // the View menu
        await page.mouse.down(); await sleep(120); await page.mouse.up();
        await sleep(600);
        await shootPage(page, out, 'S10-desktop-menu', masks);
        console.log('   S10 desktop');
        await page.context().close();
    }
    // Phone: touch, a narrow screen.
    {
        const { page, problems: p } = await openSandbox(context.browser, context.url, { width: 390, height: 844 }, true);
        problems.push(...p);
        await stillScreen(page);
        const vp = await viewportRect(page);
        await shootPage(page, out, 'S10-phone', [vp.map(Math.round), [0, 0, 390, 48]]);
        console.log('   S10 phone');
        await page.context().close();
    }
    return problems;
}

// ---- Effects ---------------------------------------------------------------------------------

const EXPLOSION = 0, SPARKS = 1, SMOKE = 2, MISSILE = 3, LASER = 4, SCORCH = 5, SWARM = 6;
const range = (a, b) => Array.from({ length: b - a + 1 }, (_, i) => a + i);

// dt: the fixed step; fire: [effect, count] at frame 0 (or `fireAt`: frame -> list); dump: frames to dump, `full` or counters only;
// shots: frames to photograph (at the default settings); fast: a small low-quality scene for cases with many frames.
const VFX = {
    V1: { dt: 1 / 60, fire: [[SPARKS, 1]], dump: range(0, 60), shots: [10, 40] },
    V2: { dt: 1 / 60, fire: [[EXPLOSION, 1]], dump: [1, 2, 3, 4, 5, 30, 120, 200], shots: [4, 30, 120] },
    V3: { dt: 1 / 60, fire: [[SWARM, 1]], dump: [1, 2, 3, 60, 120, 200], shots: [60] },
    V4: { dt: 1 / 60, fire: [[SMOKE, 1]], dump: [1, 60, 180, 300, 320], shots: [180] },
    V5: { dt: 1 / 30, fire: [[SPARKS, 1], [EXPLOSION, 1]], dump: [1, 2, 15, 50, 100] },
    V6: { dt: 1 / 240, fire: [[SPARKS, 1], [EXPLOSION, 1]], dump: [1, 2, 60, 240, 800], fast: true },
    V7: { dt: 1 / 60, fire: [[SPARKS, 10], [EXPLOSION, 5], [SWARM, 3]], dump: [1, 2, 3, 60, 200] },
    V8: { dt: 1 / 60, fire: [], fireAt: Object.fromEntries(range(0, 12).map((i) => [i * 5, [[SPARKS, 1]]])), dump: [30, 60, 61, 120] },
    V9: { dt: 1 / 60, fire: [[SWARM, 4200]], dump: [1, 2, 200], counters: true, fast: true },
    V10: { dt: 1 / 60, fire: [[SWARM, 4200]], dump: [], afterwards: { at: 200, fire: [[SPARKS, 1]], dump: [201, 260] }, fast: true },
    // The stress scene's `effects` behavior at the particle counts of its file; it runs in Play, so the steps are the
    // behavior's own (dt 0.1: the particles live about 2.5 s). Counters only.
    'V12-250k': { dt: 0.1, scene: 1, play: [250000], dump: [5, 25, 50], counters: true, fast: true },
    'V12-1M': { dt: 0.1, scene: 1, play: [1000000], dump: [5, 25, 50], counters: true, fast: true },
    'V12-2M': { dt: 0.1, scene: 1, play: [2000000], dump: [5, 25, 50], counters: true, fast: true },
    V11: { dt: 1 / 60, fire: [[MISSILE, 3], [LASER, 1], [SCORCH, 1]], dump: [30, 90, 200], shots: [30, 90] },
};

async function dumpOnce(page, counters) {
    await page.evaluate((full) => Module._sandbox_debug_vfx_dump_start(full ? 1 : 0), !counters);
    await page.waitForFunction(() => Module._sandbox_debug_vfx_dump_state() === 2, null, { timeout: 300000, polling: 25 });
    const result = await page.evaluate(() => {
        const get = Module._sandbox_debug_vfx_dump_get;
        const round = (v) => Number(Number(v).toPrecision(9));
        const n = get(3, 0, 0);
        const particles = [];
        for (let i = 0; i < n; ++i) {
            const row = [get(4, i, 0), get(4, i, 1)];
            for (let f = 2; f <= 11; ++f) row.push(round(get(4, i, f)));
            particles.push(row);
        }
        return {
            capacity: get(5, 0, 0),
            counters: Array.from({ length: 16 }, (_, i) => get(0, i, 0)),
            simulateArgs: [0, 1, 2, 3].map((i) => get(1, 0, i)),
            drawArgs: [0, 1, 2, 3, 4, 5, 6, 7].map((i) => get(1, 1, i)),
            violations: get(2, 0, 0),
            segments: Module._sandbox_debug_vfx(4),
            decals: Module._sandbox_debug_vfx(5),
            particles,
        };
    });
    await page.evaluate(() => Module._sandbox_debug_vfx_dump_finish());
    return result;
}

async function vfxCase(context, id, spec, out) {
    const { page, problems } = await openSandbox(context.browser, context.url);
    await showScene(page, spec.scene || 0);
    await DEFAULTS(page);
    if (spec.play) {
        const [x, y, w, h] = await page.evaluate(() => [0, 1, 2, 3].map((c) => Module._sandbox_debug_layout(6, c)));
        await page.mouse.move(x + w / 2, y + h / 2);
        await page.mouse.down(); await sleep(120); await page.mouse.up();
        await page.waitForFunction(() => Module._sandbox_debug_playing() === 1, null, { timeout: 60000 });
        await page.evaluate((n) => Module._sandbox_debug_set_effects(n, 0, 0, 0, 0), spec.play[0]);
    }
    if (spec.fast) await page.evaluate(() => { Module._sandbox_debug_set_resolution(0, 4, 0); Module._sandbox_debug_set_msaa(1); });
    await page.waitForTimeout(500);
    await call(page, 'fixed_dt', spec.dt); // clears the effects, so everything below is counted from here
    const fireList = (list) => page.evaluate((l) => { for (const [effect, count] of l) Module._sandbox_debug_vfx_fire_n(effect, count); }, list);
    const events = new Map(); // frame -> { fire, dump, shot }
    const at = (frame) => { if (!events.has(frame)) events.set(frame, {}); return events.get(frame); };
    at(0).fire = spec.fire || [];
    for (const [frame, list] of Object.entries(spec.fireAt || {})) at(Number(frame)).fire = [...(at(Number(frame)).fire || []), ...list];
    for (const frame of spec.dump) at(frame).dump = true;
    for (const frame of spec.shots || []) at(frame).shot = true;
    if (spec.afterwards) {
        at(spec.afterwards.at).fire = spec.afterwards.fire;
        for (const frame of spec.afterwards.dump) at(frame).dump = true;
    }
    const result = { case: id, dt: spec.dt, counters: !!spec.counters, frames: {} };
    for (const frame of [...events.keys()].sort((a, b) => a - b)) {
        const event = events.get(frame);
        await call(page, 'run_frames', frame);
        await waitFrames(page, frame);
        if (event.fire) { await fireList(event.fire); await frames(page); }
        if (event.shot) {
            await sleep(500);
            await shootViewport(page, out, `${id}-f${String(frame).padStart(3, '0')}`);
        }
        if (event.dump) {
            await sleep(100);
            result.frames[frame] = await dumpOnce(page, spec.counters);
            console.log('   ', id, 'frame', frame, 'alive', result.frames[frame].counters[1], 'violations', result.frames[frame].violations);
        }
    }
    fs.writeFileSync(path.join(out, `${id}.json`), JSON.stringify(result));
    await page.context().close();
    return problems;
}

// ---- Main ------------------------------------------------------------------------------------

const wantedIds = args.cases ? String(args.cases).split(',') : null;
const wanted = (id) => !wantedIds || wantedIds.includes(id);
const allIds = [...Object.keys(SCENE_SETTINGS), 'S7', 'S8', 'S9', 'S10', ...Object.keys(VFX)];
if (args.list) { console.log(allIds.join(' ')); process.exit(0); }
if (!args.build || !args.out) { console.error('usage: capture.mjs --build <build dir> --out <dir> [--cases S1,V3]'); process.exit(2); }

const out = path.resolve(args.out);
fs.mkdirSync(out, { recursive: true });
const server = await serve(path.resolve(args.build, 'sandbox'));
const browser = await launch();
const context = { browser, url: server.url };
const problems = [];
const started = Date.now();
try {
    if (Object.keys(SCENE_SETTINGS).some(wanted)) problems.push(...await sceneScreens(context, wanted, out));
    if (wanted('S7') || wanted('S8')) problems.push(...await resolutionScreens(context, wanted, out));
    problems.push(...await texturesScreens(context, wanted, out));
    problems.push(...await uiScreens(context, wanted, out));
    for (const [id, spec] of Object.entries(VFX)) if (wanted(id)) problems.push(...await vfxCase(context, id, spec, out));
} finally {
    await browser.close();
    server.close();
}
console.log(`done in ${Math.round((Date.now() - started) / 1000)} s; ${problems.length} problem line(s)`);
for (const line of new Set(problems)) console.log('  problem:', line.slice(0, 300));
fs.writeFileSync(path.join(out, 'problems.json'), JSON.stringify([...new Set(problems)], null, 1));
process.exit(problems.length ? 1 : 0);

// Compares two capture directories of the shader port (docs/specs/slang_shader_port.md): `node compare.mjs <baseline> <current>`.
// Screens: the largest channel difference may be 2/255 and at most 0.1% of the pixels may exceed it (the masks of
// the baseline's sidecar are left out). Effects: counters, arguments and counts are exact, the live particles are compared
// sorted by (emitter, seed) with a relative error of 1e-4 (1e-3, a half float's ulp, for the values the shader keeps
// as halves). Exits with 1 when anything differs.
import fs from 'fs';
import path from 'path';
import { launch } from './lib.mjs';

const [baseline, current] = process.argv.slice(2).map((d) => path.resolve(d));
if (!baseline || !current) { console.error('usage: compare.mjs <baseline dir> <current dir>'); process.exit(2); }

const CHANNEL_TOLERANCE = 2, PIXEL_FRACTION = 0.001;
const FIELD_NAMES = ['emitter', 'seed', 'x', 'y', 'z', 'age', 'vx', 'vy', 'vz', 'life', 'scale', 'rotation'];
const HALF_FIELDS = new Set([6, 7, 8, 9, 10, 11]);

const browser = await launch();
const page = await browser.newPage();
await page.setContent('<canvas id="a"></canvas><canvas id="b"></canvas>');

async function comparePng(name, masks) {
    const a = fs.readFileSync(path.join(baseline, name)).toString('base64');
    const b = fs.readFileSync(path.join(current, name)).toString('base64');
    return page.evaluate(async ({ a, b, masks, tolerance }) => {
        const load = (data) => new Promise((resolve, reject) => {
            const image = new Image();
            image.onload = () => resolve(image);
            image.onerror = reject;
            image.src = 'data:image/png;base64,' + data;
        });
        const [x, y] = await Promise.all([load(a), load(b)]);
        if (x.width !== y.width || x.height !== y.height) return { error: `size ${x.width}x${x.height} against ${y.width}x${y.height}` };
        const read = (image) => {
            const canvas = document.createElement('canvas');
            canvas.width = image.width; canvas.height = image.height;
            const context = canvas.getContext('2d', { willReadFrequently: true });
            context.drawImage(image, 0, 0);
            return context.getImageData(0, 0, image.width, image.height).data;
        };
        const p = read(x), q = read(y);
        const masked = new Uint8Array(x.width * x.height);
        for (const [mx, my, mw, mh] of masks)
            for (let j = Math.max(0, my); j < Math.min(x.height, my + mh); ++j)
                for (let i = Math.max(0, mx); i < Math.min(x.width, mx + mw); ++i) masked[j * x.width + i] = 1;
        let counted = 0, over = 0, max = 0;
        for (let k = 0; k < x.width * x.height; ++k) {
            if (masked[k]) continue;
            ++counted;
            let d = 0;
            for (let c = 0; c < 3; ++c) d = Math.max(d, Math.abs(p[k * 4 + c] - q[k * 4 + c]));
            if (d > max) max = d;
            if (d > tolerance) ++over;
        }
        return { counted, over, max };
    }, { a, b, masks, tolerance: CHANNEL_TOLERANCE });
}

function close(a, b, half) {
    const scale = Math.max(Math.abs(a), Math.abs(b));
    return Math.abs(a - b) <= (half ? 1e-3 : 1e-4) * scale + (half ? 6e-8 : 1e-6);
}

function compareEffects(a, b) {
    const problems = [];
    const frames = new Set([...Object.keys(a.frames), ...Object.keys(b.frames)]);
    for (const frame of frames) {
        const x = a.frames[frame], y = b.frames[frame];
        if (!x || !y) { problems.push(`frame ${frame}: only in one`); continue; }
        const where = `frame ${frame}`;
        if (y.violations !== 0) problems.push(`${where}: invariants violated (${y.violations})`);
        for (const key of ['capacity', 'counters', 'simulateArgs', 'drawArgs', 'segments', 'decals']) {
            if (JSON.stringify(x[key]) !== JSON.stringify(y[key])) problems.push(`${where}: ${key} ${JSON.stringify(x[key])} against ${JSON.stringify(y[key])}`);
        }
        if (x.particles.length !== y.particles.length) { problems.push(`${where}: ${x.particles.length} against ${y.particles.length} particles`); continue; }
        let bad = 0, example = '';
        for (let i = 0; i < x.particles.length; ++i) {
            for (let f = 0; f < 12; ++f) {
                const ok = f < 2 ? x.particles[i][f] === y.particles[i][f] : close(x.particles[i][f], y.particles[i][f], HALF_FIELDS.has(f));
                if (!ok) { if (!bad++) example = `particle ${i} ${FIELD_NAMES[f]} ${x.particles[i][f]} against ${y.particles[i][f]}`; }
            }
        }
        if (bad) problems.push(`${where}: ${bad} value(s) differ, first: ${example}`);
    }
    return problems;
}

let failed = 0, checked = 0;
const report = (name, problems, note = '') => {
    ++checked;
    if (problems.length) { ++failed; console.log(`FAIL ${name}`); for (const p of problems.slice(0, 6)) console.log(`     ${p}`); }
    else console.log(`ok   ${name}${note ? '  ' + note : ''}`);
};

const names = fs.readdirSync(baseline).sort();
for (const name of names) {
    if (name.endsWith('.png')) {
        if (!fs.existsSync(path.join(current, name))) { report(name, ['missing from the current run']); continue; }
        const sidecar = JSON.parse(fs.readFileSync(path.join(baseline, name.replace(/\.png$/, '.json')), 'utf8'));
        const result = await comparePng(name, sidecar.masks || []);
        if (result.error) report(name, [result.error]);
        else {
            const fraction = result.over / result.counted;
            report(name, fraction > PIXEL_FRACTION ? [`${(fraction * 100).toFixed(3)}% of the pixels differ by more than ${CHANNEL_TOLERANCE}/255 (largest ${result.max})`] : [],
                `largest ${result.max}/255, ${(fraction * 100).toFixed(4)}% over`);
        }
    } else if (/^V\d+[-\w]*\.json$/.test(name)) {
        if (!fs.existsSync(path.join(current, name))) { report(name, ['missing from the current run']); continue; }
        const a = JSON.parse(fs.readFileSync(path.join(baseline, name), 'utf8'));
        const b = JSON.parse(fs.readFileSync(path.join(current, name), 'utf8'));
        report(name, compareEffects(a, b), `${Object.keys(a.frames).length} frame(s)`);
    }
}
await browser.close();
console.log(`${checked - failed} of ${checked} match`);
process.exit(failed ? 1 : 0);

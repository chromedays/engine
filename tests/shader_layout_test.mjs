// Reads slangc's reflection of each shader (<build>/shaders/<name>.json) and checks what the C side relies on: which binding
// is which resource, the size of every uniform block, the entry points' names (docs/specs/slang_shader_port.md). The sizes
// here are the C structs' (`sizeof`, each held by a _Static_assert next to the struct), the bindings those of the bind
// group layouts in the C code; a shader that drifts from them draws wrong without an error.
// Usage: node shader_layout_test.mjs <build>/shaders
import fs from 'fs';
import path from 'path';

const dir = process.argv[2];
if (!dir) { console.error('usage: shader_layout_test.mjs <shaders dir>'); process.exit(2); }

// kind: constantBuffer (size, fields: name -> offset), samplerState, texture2D, ...; storage: a structured buffer's element size.
const EXPECTED = {
    imgui: {
        entryPoints: ['vs_main', 'fs_main'],
        bindings: {
            0: { name: 'u', kind: 'constantBuffer', size: 64, fields: { mvp: 0 } }, // NvMat4
            1: { name: 's', kind: 'samplerState' },
            2: { name: 't', kind: 'resource', shape: 'texture2D' },
        },
    },
    vfx_compute: {
        entryPoints: ['init_free', 'emit', 'prepare_a', 'simulate', 'prepare_b'],
        bindings: {
            0: { name: 'frame', kind: 'constantBuffer', size: 240 }, // VfxFrameUniforms
            1: { name: 'particles', kind: 'resource', element: 'Particle', size: 32 }, // PARTICLE_BYTES
            2: { name: 'free_list', kind: 'resource', size: 4 },
            3: { name: 'alive_cur', kind: 'resource', size: 4 },
            4: { name: 'alive_next', kind: 'resource', size: 4 },
            5: { name: 'visible', kind: 'resource', size: 4 },
            6: { name: 'counters', kind: 'resource', size: 4 },
            7: { name: 'emitters', kind: 'resource', element: 'Emitter', size: 128 }, // EMITTER_BYTES
            8: { name: 'bursts', kind: 'resource', element: 'Burst', size: 64 }, // NvVfxBurst
            9: { name: 'jobs', kind: 'resource', size: 8 },
            10: { name: 'sim_args', kind: 'resource', size: 4 },
            11: { name: 'draw_args', kind: 'resource', size: 4 },
        },
    },
};

let failures = 0;
const fail = (shader, message) => { ++failures; console.error(`${shader}: ${message}`); };
const uniform = (type) => (type.sizes || []).find((s) => s.kind === 'uniform');

for (const [shader, expected] of Object.entries(EXPECTED)) {
    const file = path.join(dir, `${shader}.json`);
    if (!fs.existsSync(file)) { fail(shader, `no reflection at ${file}`); continue; }
    const reflection = JSON.parse(fs.readFileSync(file, 'utf8'));
    const names = reflection.entryPoints.map((e) => e.name).sort().join(',');
    if (names !== [...expected.entryPoints].sort().join(',')) fail(shader, `entry points ${names}`);
    const found = new Map(reflection.parameters.map((p) => [p.binding.index, p]));
    for (const [index, want] of Object.entries(expected.bindings)) {
        const p = found.get(Number(index));
        if (!p) { fail(shader, `binding ${index} (${want.name}) is missing`); continue; }
        if (p.name !== want.name) fail(shader, `binding ${index} is ${p.name}, not ${want.name}`);
        const type = p.type;
        if (type.kind !== want.kind) { fail(shader, `${want.name}: ${type.kind}, not ${want.kind}`); continue; }
        if (want.shape && type.baseShape !== want.shape) fail(shader, `${want.name}: ${type.baseShape}, not ${want.shape}`);
        if (want.size && type.kind === 'resource') {
            const size = uniform(type.resultType || {});
            if (!size || size.value !== want.size) fail(shader, `${want.name}: element size ${size && size.value}, not ${want.size}`);
            if (want.element && type.resultType.name !== want.element) fail(shader, `${want.name}: element ${type.resultType.name}, not ${want.element}`);
        }
        if (want.kind === 'constantBuffer') {
            const size = uniform(type.elementType);
            if (!size || size.value !== want.size) fail(shader, `${want.name}: size ${size && size.value}, not ${want.size}`);
            for (const field of want.fields ? type.elementType.fields : []) {
                const offset = uniform(field.type) ? field.binding.offset : undefined;
                if (!(field.name in want.fields)) fail(shader, `${want.name}.${field.name} is not expected`);
                else if (offset !== want.fields[field.name]) fail(shader, `${want.name}.${field.name} at ${offset}, not ${want.fields[field.name]}`);
            }
        }
    }
    for (const [index, p] of found) if (!(index in expected.bindings)) fail(shader, `binding ${index} (${p.name}) is not expected`);
}
if (failures) process.exit(1);
console.log(`${Object.keys(EXPECTED).length} shader(s) match their layouts`);

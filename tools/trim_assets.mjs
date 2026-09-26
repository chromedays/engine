// Trims the Quaternius packs down to what the engine ships. Run through tools/trim_assets.sh.
import { createRequire } from 'node:module';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';

// npx installs the tools in a temporary node_modules; resolve them from there.
const require = createRequire(join(process.env.NV_NODE_MODULES, 'resolve.js'));
const { NodeIO } = require('@gltf-transform/core');
const { dedup, prune, resample, textureCompress } = require('@gltf-transform/functions');
const sharp = require('sharp');

const { NV_CHARACTERS: charactersDir, NV_ANIMATIONS: animationsDir, NV_OUT: outDir } = process.env;
const io = new NodeIO();

const CHARACTER = join(charactersDir, 'Base Characters', 'Godot - UE', 'Superhero_Male_FullBody.gltf');
const CLIPS = join(animationsDir, 'Unreal-Godot', 'UAL1_Standard.glb');
const CLIPS_ROOT_MOTION = join(animationsDir, 'Unreal-Godot', 'UAL1_Standard_RM.glb');

const KEEP_CLIPS = ['Idle_Loop', 'Walk_Loop', 'Jog_Fwd_Loop', 'Sprint_Loop',
                    'Jump_Start', 'Jump_Loop', 'Jump_Land', 'Dance_Loop'];
const KEEP_ROOT_MOTION_CLIPS = ['Walk_Loop', 'Jog_Fwd_Loop', 'Sprint_Loop'];
const KEEP_ATTRIBUTES = ['POSITION', 'NORMAL', 'TEXCOORD_0', 'JOINTS_0', 'WEIGHTS_0'];

// Character: base color only (no PBR shading yet), and only the vertex attributes we draw with.
{
    // NOTE: The pack's .gltf names a few normal maps that are not in the pack. Only base color is
    // kept, so missing images are read as a 1x1 placeholder that prune() then removes.
    const json = JSON.parse(readFileSync(CHARACTER, 'utf8'));
    const resources = {};
    const placeholder = await sharp({ create: { width: 1, height: 1, channels: 4, background: '#000' } }).png().toBuffer();
    for (const { uri } of [...json.buffers, ...json.images]) {
        const file = join(dirname(CHARACTER), decodeURIComponent(uri));
        resources[uri] = existsSync(file) ? new Uint8Array(readFileSync(file)) : new Uint8Array(placeholder);
    }
    const doc = await io.readJSON({ json, resources });
    const root = doc.getRoot();
    for (const material of root.listMaterials()) {
        material.setNormalTexture(null);
        material.setMetallicRoughnessTexture(null);
        material.setOcclusionTexture(null);
        material.setEmissiveTexture(null);
    }
    for (const mesh of root.listMeshes()) {
        for (const prim of mesh.listPrimitives()) {
            for (const semantic of prim.listSemantics()) {
                if (!KEEP_ATTRIBUTES.includes(semantic)) prim.setAttribute(semantic, null);
            }
        }
    }
    await doc.transform(
        prune(),
        dedup(),
        textureCompress({ encoder: sharp, targetFormat: 'jpeg', resize: [1024, 1024], quality: 85 }),
    );
    await io.write(join(outDir, 'character.glb'), doc);
}

// Clip files: the rig and the chosen clips, no meshes.
async function writeClips(source, keep, outName) {
    const doc = await io.read(source);
    const root = doc.getRoot();
    for (const animation of root.listAnimations()) {
        if (keep.includes(animation.getName())) continue;
        // Samplers keep their keyframe accessors alive, so they go explicitly.
        for (const channel of animation.listChannels()) channel.dispose();
        for (const sampler of animation.listSamplers()) sampler.dispose();
        animation.dispose();
    }
    for (const node of root.listNodes()) {
        node.setMesh(null);
        node.setSkin(null);
    }
    for (const mesh of root.listMeshes()) mesh.dispose();
    for (const skin of root.listSkins()) skin.dispose();
    for (const material of root.listMaterials()) material.dispose();
    // resample drops keyframes that linear interpolation reproduces anyway.
    await doc.transform(resample(), prune(), dedup());
    await io.write(join(outDir, outName), doc);
}
await writeClips(CLIPS, KEEP_CLIPS, 'clips.glb');
await writeClips(CLIPS_ROOT_MOTION, KEEP_ROOT_MOTION_CLIPS, 'clips_rm.glb');

writeFileSync(join(outDir, 'LICENSE.txt'), `Quaternius assets, CC0 1.0 Universal (public domain dedication)
https://creativecommons.org/publicdomain/zero/1.0/

character.glb   Universal Base Characters [Standard], Superhero_Male_FullBody
                https://quaternius.itch.io/universal-base-characters
clips.glb       Universal Animation Library [Standard], UAL1_Standard.glb
clips_rm.glb    Universal Animation Library [Standard], UAL1_Standard_RM.glb (root motion)
                https://quaternius.itch.io/universal-animation-library

Trimmed with tools/trim_assets.sh: see docs/specs/animation.md for what is kept.
Models by @Quaternius - https://www.patreon.com/quaternius
`);
console.log('wrote', outDir);

// Writes engine/icons.h, a UTF-8 string macro per icon of the Lucide icon font (docs/specs/fonts.md, "Icons"), from the
// codepoints.json of the same lucide-static release as assets/fonts/Lucide.ttf. Lucide's code points can move between
// releases, so the header and the font are always taken from one release together:
//
//   npm pack lucide-static@<version>          (in an empty folder; then tar -xzf the .tgz)
//   cp package/font/lucide.ttf <engine>/assets/fonts/Lucide.ttf
//   cp package/LICENSE <engine>/assets/fonts/Lucide-LICENSE.txt
//   node <engine>/tools/lucide_icons.mjs package/font/codepoints.json <version>
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const [codepointsPath, version] = process.argv.slice(2);
if (!codepointsPath || !version) {
    console.error('usage: node tools/lucide_icons.mjs <codepoints.json> <lucide-static version>');
    process.exit(1);
}
const codepoints = JSON.parse(readFileSync(codepointsPath, 'utf8'));

// "\xee\x84\xbc" for U+E13C: escapes, so the header is plain ASCII and the bytes do not depend on the compiler's charset.
function utf8Literal(codepoint) {
    const bytes = Buffer.from(String.fromCodePoint(codepoint), 'utf8');
    return '"' + [...bytes].map(b => '\\x' + b.toString(16).padStart(2, '0')).join('') + '"';
}

const names = Object.keys(codepoints).sort();
const lines = names.map(name => {
    const macro = 'NV_ICON_' + name.toUpperCase().replace(/-/g, '_');
    const codepoint = codepoints[name];
    if (!/^NV_ICON_[A-Z0-9_]+$/.test(macro) || !(codepoint >= 0xe000 && codepoint <= 0xf8ff))
        throw new Error(`unexpected icon ${name} at ${codepoint}`);
    return `#define ${macro} ${utf8Literal(codepoint)} // U+${codepoint.toString(16).toUpperCase()}`;
});

const header = `#pragma once

// The Lucide icons (https://lucide.dev, ISC License; assets/fonts/Lucide.ttf), merged into the UI font by
// nv_imgui_load_ui_font (docs/specs/fonts.md, "Icons"): each is a UTF-8 string that draws that icon in any ImGui text, so it
// can stand alone as a button's label or go beside words (NV_ICON_PLAY " Play"). The names are Lucide's own, upper-cased.
//
// Generated from lucide-static ${version} by tools/lucide_icons.mjs; do not edit. The font must be from the same release.

${lines.join('\n')}
`;
const out = join(dirname(fileURLToPath(import.meta.url)), '..', 'engine', 'icons.h');
writeFileSync(out, header);
console.log(`${names.length} icons written to ${out}`);

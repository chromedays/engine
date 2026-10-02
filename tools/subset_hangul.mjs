// Cuts the Hangul face the UI falls back to out of Pretendard. Run through tools/subset_hangul.sh.
import { createRequire } from 'node:module';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

// npx installs the tool in a temporary node_modules; resolve it from there.
const require = createRequire(join(process.env.NV_NODE_MODULES, 'resolve.js'));
const subsetFont = require('subset-font');

const { NV_INPUT: input, NV_OUT: output, NV_STRINGS: strings } = process.env;

// KS X 1001's 2,350 everyday syllables: rows 0xB0 to 0xC8 of the EUC-KR table.
let text = '';
const decoder = new TextDecoder('euc-kr');
for (let hi = 0xb0; hi <= 0xc8; ++hi) {
  for (let lo = 0xa1; lo <= 0xfe; ++lo) {
    const ch = decoder.decode(Uint8Array.of(hi, lo));
    if (/^[가-힣]$/.test(ch)) text += ch;
  }
}
const syllables = text.length;
// Compatibility jamo (ㄱ to ㅣ): what an input method shows while a syllable is being composed.
for (let c = 0x3131; c <= 0x318e; ++c) text += String.fromCodePoint(c);
// Every non-ASCII character the Korean strings use, so none is missing even outside the table. `strings` lists the tables
// of every executable, separated by ":".
let extra = 0;
for (const table of (strings || '').split(':').filter(Boolean)) {
  if (!existsSync(table)) continue;
  for (const ch of readFileSync(table, 'utf8')) {
    if (ch.codePointAt(0) > 0x7f && !text.includes(ch)) { text += ch; ++extra; }
  }
}

const font = await subsetFont(readFileSync(input), text, { targetFormat: 'truetype' });
writeFileSync(output, font);
console.log(`${syllables} syllables, jamo, ${extra} more from the strings: ${font.length} bytes -> ${output}`);

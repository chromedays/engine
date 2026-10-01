// Checks the Korean UI strings (docs/specs/korean.md): every English text the app code wraps in
// T() or TL(), or hands to the search, the shortcut table and the stats, has a row in
// app/strings.c, and a row keeps the printf conversions of its English text, in order.
//   node tests/strings_test.mjs [--missing]   (--missing prints the missing rows as table lines)
import { readFileSync, readdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..', 'app');
const unescape = (text) => text.replace(/\\(["\\n])/g, (_, c) => (c === 'n' ? '\n' : c));
const str = String.raw`"((?:[^"\\]|\\.)*)"`;

// The table: {"english", "korean"} rows between the markers.
const table = new Map();
const source = readFileSync(join(root, 'strings.c'), 'utf8');
for (const m of source.matchAll(new RegExp(String.raw`\{\s*${str}\s*,\s*${str}\s*\}`, 'g'))) {
  if (table.has(m[1])) console.log(`duplicate row: ${m[1]}`);
  table.set(unescape(m[1]), unescape(m[2]));
}

// What the code uses: the text before any "##id" part.
const used = new Map(); // english -> "file:line"
const note = (english, where) => {
  const text = unescape(english).split('##')[0];
  if (/[A-Za-z]/.test(text) && !used.has(text)) used.set(text, where);
};
for (const file of readdirSync(root).filter((f) => f.endsWith('.c') && f !== 'strings.c')) {
  const lines = readFileSync(join(root, file), 'utf8').split('\n');
  lines.forEach((line, i) => {
    const where = `${file}:${i + 1}`;
    if (/^\s*\/\//.test(line)) return;
    // Every literal inside a T( ... ) or TL( ... ) call, a condition between two texts included.
    for (const m of line.matchAll(/(?<![A-Za-z0-9_])TL?\(/g)) {
      let depth = 1, i = m.index + m[0].length, text = '';
      for (; i < line.length && depth > 0; ++i) {
        if (line[i] === '"') { const end = line.slice(i + 1).search(/(?<!\\)"/); text += line.slice(i, i + end + 2); i += end + 1; continue; }
        if (line[i] === '(') ++depth;
        if (line[i] === ')') --depth;
        if (depth > 0) text += line[i];
      }
      for (const q of text.matchAll(new RegExp(str, 'g'))) note(q[1], where);
    }
    for (const m of line.matchAll(new RegExp(String.raw`search_(?:row|group|section)\(app, ${str}`, 'g'))) note(m[1], where);
    // The shortcut table: group, name and the keys text ("Ctrl (held)").
    const row = line.match(new RegExp(String.raw`\[SC_\w+\] = \{${str}, ${str}, \{[^}]*\}, (?:NULL|${str})`));
    if (row) { note(row[1], where); note(row[2], where); if (row[3]) note(row[3], where); }
    for (const m of line.matchAll(new RegExp(String.raw`\.note = [^"]*${str}`, 'g'))) note(m[1], where);
    if (/\bstat(?:_text)?\(stats/.test(line)) for (const m of line.matchAll(new RegExp(str, 'g'))) if (!m[1].includes('%')) note(m[1], where);
    for (const m of line.matchAll(new RegExp(String.raw`\bsection\(app, ${str}`, 'g'))) note(m[1], where);
    if (/panel_names\[[^\]]*\]\s*=/.test(line)) for (const m of line.matchAll(new RegExp(str, 'g'))) note(m[1], where);
  });
}

const conversions = (text) => (text.match(/%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z)?[diouxXfFeEgGcsp]/g) || []).join(' ');
let failed = 0;
const missing = [];
for (const [english, where] of used) {
  if (!table.has(english)) { ++failed; missing.push(english); console.log(`no Korean for "${english}" (${where})`); }
}
for (const [english, korean] of table) {
  if (conversions(english) !== conversions(korean)) {
    ++failed;
    console.log(`conversions differ: "${english}" (${conversions(english)}) and "${korean}" (${conversions(korean)})`);
  }
  if (!/[\uac00-\ud7a3\u3131-\u318e]/.test(korean) && /[A-Za-z]{3}/.test(english) && english !== korean)
    console.log(`note: "${english}" has no Hangul in its translation`);
}
for (const english of table.keys()) if (!used.has(english)) console.log(`note: row not used by the code: "${english}"`);
if (process.argv.includes('--missing'))
  for (const english of missing) console.log(`    {"${english.replace(/["\\]/g, '\\$&').replace(/\n/g, '\\n')}", ""},`);
console.log(failed ? `${failed} problem(s)` : `ok: ${table.size} rows, ${used.size} strings used`);
process.exit(failed ? 1 : 0);

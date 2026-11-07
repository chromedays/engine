// Compares the visual regression test's captures with its baselines (docs/specs/visual_regression.md):
// every PNG of the baseline directory is a case, compared with the same name in the actual directory.
//   node tools/visual/compare.mjs --odiff <odiff> --baseline tests/visual/baseline --actual <capture dir>
// odiff does the pixel work and writes <case>.diff.png beside the actual image; this script owns the
// pass rule, which odiff does not have: at the threshold, differing pixels may be at most
// --max-diff-percent of the image (default 0.1). A size difference always fails (--fail-on-layout).
// Exits 0 when every case passes, 1 when one does not, 2 when the run itself is wrong.

import { spawnSync } from 'child_process';
import fs from 'fs';
import path from 'path';

function parseArgs(argv) {
    const args = {};
    for (let i = 0; i < argv.length; ++i) {
        if (argv[i].startsWith('--'))
            args[argv[i].slice(2)] = argv[i + 1] && !argv[i + 1].startsWith('--') ? argv[++i] : true;
    }
    return args;
}

const args = parseArgs(process.argv.slice(2));
const threshold = args.threshold ?? '0.05';
const maxDiffPercent = Number(args['max-diff-percent'] ?? '0.1');
if (!args.odiff || !args.baseline || !args.actual) {
    console.error('usage: compare.mjs --odiff <odiff> --baseline <dir> --actual <dir> [--threshold 0.05] [--max-diff-percent 0.1]');
    process.exit(2);
}
if (!fs.existsSync(args.odiff)) {
    console.error(`compare: no odiff at ${args.odiff} (the build downloads it: cmake/odiff.cmake)`);
    process.exit(2);
}
if (!fs.existsSync(args.baseline)) {
    console.error(`compare: no baseline directory ${args.baseline}: capture one with 'just visual-update-ref'`);
    process.exit(2);
}

const cases = fs.readdirSync(args.baseline).filter((name) => name.endsWith('.png')).sort();
if (cases.length === 0) {
    console.error(`compare: no baseline PNGs in ${args.baseline}: capture them with 'just visual-update-ref'`);
    process.exit(2);
}

let failed = 0;
for (const name of cases) {
    const baseline = path.join(args.baseline, name);
    const actual = path.join(args.actual, name);
    const diff = path.join(args.actual, `${path.basename(name, '.png')}.diff.png`);
    if (!fs.existsSync(actual)) {
        console.error(`FAIL ${name}: no capture at ${actual}`);
        ++failed;
        continue;
    }
    const run = spawnSync(args.odiff,
        [baseline, actual, diff, '--threshold', String(threshold), '--fail-on-layout', '--parsable-stdout'],
        { encoding: 'utf8' });
    if (run.error) {
        console.error(`compare: running odiff failed: ${run.error.message}`);
        process.exit(2);
    }
    // Exit codes: 0 matching, 21 layout difference, 22 pixel differences. With --parsable-stdout the
    // pixel case prints "<different pixels>;<percent>" on stdout.
    if (run.status === 0) {
        console.log(`PASS ${name}: identical at threshold ${threshold}`);
    } else if (run.status === 21) {
        console.error(`FAIL ${name}: size differs from the baseline\n  baseline ${baseline}\n  actual   ${actual}`);
        ++failed;
    } else if (run.status === 22) {
        const parsed = /(\d+);([0-9.]+)\s*$/.exec(run.stdout.trim());
        const percent = parsed ? Number(parsed[2]) : NaN;
        if (parsed && percent <= maxDiffPercent) {
            console.log(`PASS ${name}: ${parsed[1]} pixels differ (${parsed[2]}%, at most ${maxDiffPercent}% allowed); diff at ${diff}`);
        } else {
            console.error(`FAIL ${name}: ${parsed ? `${parsed[1]} pixels differ (${parsed[2]}%)` : 'pixels differ'}\n  baseline ${baseline}\n  actual   ${actual}\n  diff     ${diff}`);
            ++failed;
        }
    } else {
        console.error(`compare: odiff exited ${run.status} on ${name}: ${run.stderr.trim() || run.stdout.trim()}`);
        process.exit(2);
    }
}

if (failed > 0) {
    console.error(`compare: ${failed} of ${cases.length} case(s) differ. If the change is intended, view the diff and run 'just visual-update-ref'.`);
    process.exit(1);
}
console.log(`compare: ${cases.length} case(s) pass`);

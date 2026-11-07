// Compares the visual regression test's captures with its baselines (docs/specs/visual_regression.md):
// every PNG of the baseline directory is a case, compared with the same name in the actual directory.
//   node tools/visual/compare.mjs --odiff <odiff> --baseline tests/visual/baseline --actual <capture dir>
//       [--threshold 0.05] [--max-diff-percent 0.1] [--report <dir>] [--no-report]
// odiff does the pixel work and writes <case>.diff.png beside the actual image (--diff-overlay 1: white
// where the images match, red where they differ; no file when they match everywhere); this script owns the
// pass rule, which odiff does not have: at the threshold, differing pixels may be at most
// --max-diff-percent of the image (default 0.1). A size difference always fails (--fail-on-layout).
// It then writes report.html and report.json (docs/specs/visual_report.md; by default into the actual
// directory) and prints the page's file:// URL as its last line.
// Exits 0 when every case passes, 1 when one does not, 2 when the run itself is wrong.

import { spawnSync } from 'child_process';
import fs from 'fs';
import path from 'path';
import { fileURLToPath, pathToFileURL } from 'url';
import { writeReport } from './report.mjs';

function parseArgs(argv) {
    const args = {};
    for (let i = 0; i < argv.length; ++i) {
        if (argv[i].startsWith('--'))
            args[argv[i].slice(2)] = argv[i + 1] && !argv[i + 1].startsWith('--') ? argv[++i] : true;
    }
    return args;
}

// Width and height from a PNG's IHDR chunk, or null when the file is not a PNG.
function pngSize(file) {
    try {
        const fd = fs.openSync(file, 'r');
        const header = Buffer.alloc(24);
        fs.readSync(fd, header, 0, 24, 0);
        fs.closeSync(fd);
        if (header.readUInt32BE(0) !== 0x89504e47 || header.toString('latin1', 12, 16) !== 'IHDR')
            return null;
        return { width: header.readUInt32BE(16), height: header.readUInt32BE(20) };
    } catch {
        return null;
    }
}

// The short hash (with '+' when the tree has uncommitted changes) and subject of the checkout this
// script is in, the way the build label writes them; nulls when Git or the repository is missing.
function gitInfo() {
    const cwd = path.dirname(fileURLToPath(import.meta.url));
    const git = (...gitArgs) => {
        const run = spawnSync('git', gitArgs, { cwd, encoding: 'utf8' });
        return run.status === 0 ? run.stdout.trim() : null;
    };
    const hash = git('rev-parse', '--short', 'HEAD');
    if (hash === null)
        return { commit: null, subject: null };
    const dirty = git('status', '--porcelain');
    return { commit: dirty ? `${hash}+` : hash, subject: git('log', '-1', '--format=%s') };
}

function odiffVersion(odiff) {
    const run = spawnSync(odiff, ['--version'], { encoding: 'utf8' });
    // odiff 4.5.0 prints its version on stderr.
    const match = run.status === 0 ? /odiff\s+(\d\S*)/.exec(`${run.stdout}\n${run.stderr}`) : null;
    return match ? match[1] : null;
}

function main() {
    const args = parseArgs(process.argv.slice(2));
    const threshold = args.threshold ?? '0.05';
    const maxDiffPercent = Number(args['max-diff-percent'] ?? '0.1');
    if (!args.odiff || !args.baseline || !args.actual) {
        console.error('usage: compare.mjs --odiff <odiff> --baseline <dir> --actual <dir> [--threshold 0.05] [--max-diff-percent 0.1] [--report <dir>] [--no-report]');
        return 2;
    }
    if (!fs.existsSync(args.odiff)) {
        console.error(`compare: no odiff at ${args.odiff} (the build downloads it: cmake/odiff.cmake)`);
        return 2;
    }
    if (!fs.existsSync(args.baseline)) {
        console.error(`compare: no baseline directory ${args.baseline}: capture one with 'just visual-update-ref'`);
        return 2;
    }

    const names = fs.readdirSync(args.baseline).filter((name) => name.endsWith('.png')).sort();
    if (names.length === 0) {
        console.error(`compare: no baseline PNGs in ${args.baseline}: capture them with 'just visual-update-ref'`);
        return 2;
    }

    const run = {
        version: 1,
        time: new Date().toISOString().replace(/\.\d+Z$/, 'Z'),
        ...gitInfo(),
        odiff: odiffVersion(args.odiff),
        threshold: Number(threshold),
        maxDiffPercent,
        passed: 0,
        failed: 0,
        error: null,
        cases: [],
    };

    // Writes the report (unless --no-report) and prints its URL as the last line; returns the exit code.
    const finish = (code) => {
        run.passed = run.cases.filter((c) => c.status === 'pass').length;
        run.failed = run.cases.length - run.passed;
        if (args['no-report'])
            return code;
        const dir = path.resolve(typeof args.report === 'string' ? args.report : args.actual);
        try {
            const file = writeReport(dir, run);
            console.log(`report: ${pathToFileURL(file).href}`);
        } catch (error) {
            console.error(`compare: writing the report into ${dir} failed: ${error.message}`);
            return 2;
        }
        return code;
    };

    for (const name of names) {
        const baseline = path.resolve(args.baseline, name);
        const actual = path.resolve(args.actual, name);
        const diff = path.resolve(args.actual, `${path.basename(name, '.png')}.diff.png`);
        const baselineSize = pngSize(baseline);
        const entry = {
            name: path.basename(name, '.png'),
            status: 'fail',
            reason: null,
            width: baselineSize?.width ?? null,
            height: baselineSize?.height ?? null,
            diffPixels: null,
            diffPercent: null,
            baseline,
            actual: null,
            diff: null,
        };
        // odiff writes no diff image when the images match, so one from an earlier run would stay.
        fs.rmSync(diff, { force: true });
        if (!fs.existsSync(actual)) {
            console.error(`FAIL ${name}: no capture at ${actual}`);
            entry.reason = 'missing';
            entry.missing = actual;
            run.cases.push(entry);
            continue;
        }
        entry.actual = actual;
        const odiff = spawnSync(args.odiff,
            [baseline, actual, diff, '--threshold', String(threshold), '--diff-overlay', '1', '--fail-on-layout', '--parsable-stdout'],
            { encoding: 'utf8' });
        if (odiff.error) {
            run.error = `running odiff failed: ${odiff.error.message}`;
            console.error(`compare: ${run.error}`);
            return finish(2);
        }
        // Exit codes: 0 matching, 21 layout difference, 22 pixel differences. With --parsable-stdout the
        // pixel case prints "<different pixels>;<percent>" on stdout.
        if (odiff.status === 0) {
            console.log(`PASS ${name}: identical at threshold ${threshold}`);
            Object.assign(entry, { status: 'pass', reason: 'identical', diffPixels: 0, diffPercent: 0 });
        } else if (odiff.status === 21) {
            console.error(`FAIL ${name}: size differs from the baseline\n  baseline ${baseline}\n  actual   ${actual}`);
            const actualSize = pngSize(actual);
            Object.assign(entry, { reason: 'layout', actualWidth: actualSize?.width ?? null, actualHeight: actualSize?.height ?? null });
        } else if (odiff.status === 22) {
            const parsed = /(\d+);([0-9.]+)\s*$/.exec(odiff.stdout.trim());
            const percent = parsed ? Number(parsed[2]) : NaN;
            if (parsed) {
                entry.diffPixels = Number(parsed[1]);
                entry.diffPercent = percent;
            }
            if (fs.existsSync(diff))
                entry.diff = diff;
            if (parsed && percent <= maxDiffPercent) {
                console.log(`PASS ${name}: ${parsed[1]} pixels differ (${parsed[2]}%, at most ${maxDiffPercent}% allowed); diff at ${diff}`);
                Object.assign(entry, { status: 'pass', reason: 'within' });
            } else {
                console.error(`FAIL ${name}: ${parsed ? `${parsed[1]} pixels differ (${parsed[2]}%)` : 'pixels differ'}\n  baseline ${baseline}\n  actual   ${actual}\n  diff     ${diff}`);
                entry.reason = 'pixels';
            }
        } else {
            run.error = `odiff exited ${odiff.status} on ${name}: ${odiff.stderr.trim() || odiff.stdout.trim()}`;
            console.error(`compare: ${run.error}`);
            return finish(2);
        }
        run.cases.push(entry);
    }

    const failed = run.cases.filter((c) => c.status === 'fail').length;
    if (failed > 0) {
        console.error(`compare: ${failed} of ${names.length} case(s) differ. If the change is intended, view the diff and run 'just visual-update-ref'.`);
        return finish(1);
    }
    console.log(`compare: ${names.length} case(s) pass`);
    return finish(0);
}

process.exitCode = main();

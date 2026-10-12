// Shared by capture.mjs and compare.mjs: the regression harness of the D3D12 port (docs/specs/d3d12_port.md).
// Run with Playwright on the module path, for example
//   NODE_PATH=$(npm root -g) node tools/d3d12_port/capture.mjs --build build-debug --out out/current
// The sandbox's debug exports only exist in Debug builds, so every run uses a Debug build.
import { createRequire } from 'module';
import http from 'http';
import fs from 'fs';
import path from 'path';

const { chromium } = createRequire(import.meta.url)('playwright');

const TYPES = {
    '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
    '.data': 'application/octet-stream', '.json': 'application/json',
};

// Serves the build's sandbox directory; resolves to { url, close }.
export function serve(directory) {
    const server = http.createServer((request, response) => {
        let file = path.join(directory, request.url.split('?')[0]);
        if (file.endsWith('/')) file += 'index.html';
        fs.readFile(file, (error, data) => {
            if (error) { response.statusCode = 404; response.end(); return; }
            response.setHeader('content-type', TYPES[path.extname(file)] || 'application/octet-stream');
            response.end(data);
        });
    });
    return new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve({
        url: `http://127.0.0.1:${server.address().port}/`,
        close: () => server.close(),
    })));
}

export const BROWSER_ARGS = [
    '--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan=swiftshader',
    '--use-webgpu-adapter=swiftshader', '--use-angle=swiftshader', '--disable-vulkan-fallback-to-gl-for-testing',
];

export async function launch() {
    return chromium.launch({ args: BROWSER_ARGS, executablePath: process.env.CHROMIUM || undefined });
}

// A fresh page (empty storage) on the sandbox, started and idle. `size` is the window in CSS pixels.
export async function openSandbox(browser, url, size = { width: 1280, height: 720 }, touch = false) {
    const context = await browser.newContext({ viewport: size, deviceScaleFactor: 1, hasTouch: touch, isMobile: touch });
    const page = await context.newPage();
    const problems = [];
    // The exports abort the module when called before it runs, so the first frame's log line says it is safe.
    let running;
    const started = new Promise((resolve) => { running = resolve; });
    page.on('console', (message) => {
        const text = message.text();
        if (/scene color target/.test(text)) running();
        // Chromium's note that WebGPU is experimental on this platform is not a problem.
        if (/^WebGPU is experimental/.test(text)) return;
        if (message.type() === 'error' || /wgpu|WebGPU|error \(/i.test(text) && !/^\[nv\] GPU/.test(text)) problems.push(text);
    });
    page.on('pageerror', (error) => problems.push(`pageerror: ${error.message}`));
    await page.goto(url);
    await Promise.race([started, sleep(180000).then(() => { throw new Error('the sandbox did not start'); })]);
    await page.waitForTimeout(1500);
    return { page, context, problems };
}

export const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

export function parseArgs(argv) {
    const args = {};
    for (let i = 0; i < argv.length; ++i) {
        if (argv[i].startsWith('--')) {
            const key = argv[i].slice(2);
            args[key] = argv[i + 1] && !argv[i + 1].startsWith('--') ? argv[++i] : true;
        }
    }
    return args;
}

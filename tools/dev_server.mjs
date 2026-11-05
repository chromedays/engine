// The development server for web builds (docs/specs/dev_server.md): serves a build's page, rebuilds its target when a source
// file is saved, and reloads the open pages when the build's version (<target>.manifest.json) changes.
//   node tools/dev_server.mjs --build <dir> [--root <dir>] [--target <name>] [--port 8000] [--host 127.0.0.1] [--no-build]
// Only Node's own modules are used: the Emscripten SDK brings Node, so a machine that builds for the web can run it.
import { spawn } from 'node:child_process';
import { existsSync, readdirSync, readFileSync, statSync, unwatchFile, watch, watchFile } from 'node:fs';
import { readFile, stat } from 'node:fs/promises';
import { createServer } from 'node:http';
import { basename, extname, join, relative, resolve, sep } from 'node:path';
import { performance } from 'node:perf_hooks';
import { pathToFileURL } from 'node:url';
import { parseArgs } from 'node:util';

const EVENTS_PATH = '/__dev/events';
const DEBOUNCE_MS = 200;      // editors write a file in several steps; one save is one build
const MANIFEST_POLL_MS = 300; // stat polling sees a manifest that is rewritten, replaced or made anew alike
const HEARTBEAT_MS = 15000;   // keeps idle event streams open through proxies and sleeping tabs

const CONTENT_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.wasm': 'application/wasm',
  '.data': 'application/octet-stream',
  '.css': 'text/css; charset=utf-8',
  '.png': 'image/png',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
  '.txt': 'text/plain; charset=utf-8',
};

export function contentType(name) {
  return CONTENT_TYPES[extname(name).toLowerCase()] || 'application/octet-stream';
}

// The file a URL path names under root, or null when it would leave root (`..`, an encoded `..`, a NUL).
export function resolveInside(root, urlPath) {
  let path;
  try {
    path = decodeURIComponent(urlPath);
  } catch {
    return null;
  }
  if (path.includes('\0')) return null;
  if (path.endsWith('/')) path += 'index.html';
  const base = resolve(root);
  const file = resolve(base, '.' + (path.startsWith('/') ? path : '/' + path));
  return file.startsWith(base + sep) ? file : null;
}

// The version a manifest names, or null when the text is not a whole manifest (the build may be writing it).
export function manifestVersion(text) {
  try {
    const manifest = JSON.parse(text);
    return typeof manifest.version === 'string' ? manifest.version : null;
  } catch {
    return null;
  }
}

// A directory the watch skips: dot directories (.git, .vscode), node_modules and build directories (they hold a
// CMakeCache.txt; watching them would rebuild on the build's own output).
export function ignoredDirectory(dir) {
  const name = basename(dir);
  return name.startsWith('.') || name === 'node_modules' || existsSync(join(dir, 'CMakeCache.txt'));
}

// A file whose change is no edit of the sources: dot files and editors' backup and swap files (vim's `4913` probe,
// JetBrains' safe-write copies).
export function ignoredFile(name) {
  return name.startsWith('.') || name.endsWith('~') || /\.sw[a-p]$/.test(name) || /^\d+$/.test(name) ||
         /___jb_(?:tmp|old)___$/.test(name);
}

// The build queue: one build at a time, and the changes made during a build make exactly one more after it.
//   state: {building, pending (the first file changed during the build, or null)}
//   event: {type: 'change', file} or {type: 'done'}
// Returns the next state and the file whose change starts a build now (null for none).
export function nextBuild(state, event) {
  if (event.type === 'change') {
    if (state.building) return { state: { building: true, pending: state.pending ?? event.file }, start: null };
    return { state: { building: true, pending: null }, start: event.file };
  }
  if (state.pending !== null) return { state: { building: true, pending: null }, start: state.pending };
  return { state: { building: false, pending: null }, start: null };
}

// The page as served: a script before </body> that reloads it when the build's version is not the one it was served with,
// and shows the build's state in a corner badge that takes no input.
export function injectReload(html, version) {
  const script = `<script>
// Added by the dev server (tools/dev_server.mjs, docs/specs/dev_server.md).
(() => {
  const loaded = ${JSON.stringify(version ?? '').replace(/</g, '\\u003c')};
  const badge = document.createElement("div");
  badge.style.cssText = "position: fixed; left: 8px; bottom: 8px; z-index: 10; display: none; padding: 4px 8px; " +
    "border-radius: 4px; color: #fff; font: 12px/1.4 system-ui, sans-serif; pointer-events: none;";
  document.body.appendChild(badge);
  const show = (text, color) => {
    badge.textContent = text;
    badge.style.background = color;
    badge.style.display = text ? "block" : "none";
  };
  const events = new EventSource(${JSON.stringify(EVENTS_PATH)});
  events.addEventListener("version", (event) => { if (event.data !== loaded) location.reload(); });
  events.addEventListener("build", (event) => {
    if (event.data === "building") show("Building…", "#2d5fa8");
    else if (event.data === "failed") show("Build failed: see the terminal", "#a83232");
    else show("", "");
  });
  // EventSource reconnects by itself; the server sends the version and the build's state again when it does.
  events.addEventListener("error", () => show("Dev server stopped", "#555"));
})();
</script>
`;
  const at = html.toLowerCase().lastIndexOf('</body>');
  return at < 0 ? html + script : html.slice(0, at) + script + html.slice(at);
}

// The directory CMake configured the build directory from (its source root), or null.
function sourceDirectory(buildDir) {
  const cache = join(buildDir, 'CMakeCache.txt');
  if (!existsSync(cache)) return null;
  const line = readFileSync(cache, 'utf8').match(/^CMAKE_HOME_DIRECTORY:[A-Z]+=(.*)$/m);
  return line ? line[1].trim() : null;
}

// Watches every directory under dir but the ignored ones, one non-recursive watcher each (Node's recursive watch on Linux
// would walk into the build directories too), and adds directories made later. dir itself is watched even when its name
// would be ignored below it (a source root inside a dot directory).
function watchTree(dir, watchers, onChange) {
  if (watchers.has(dir)) return;
  let watcher;
  try {
    watcher = watch(dir, (event, name) => {
      if (!name) return;
      const path = join(dir, name.toString());
      let isDirectory = false;
      try {
        isDirectory = statSync(path).isDirectory();
      } catch {
        // Removed: a watched directory's watcher closes through its error event.
      }
      if (isDirectory) {
        if (event === 'rename' && !ignoredDirectory(path)) watchTree(path, watchers, onChange);
        return;
      }
      if (!ignoredFile(basename(path))) onChange(path);
    });
  } catch {
    return; // removed before it could be watched
  }
  watcher.on('error', () => {
    watcher.close();
    watchers.delete(dir);
  });
  watchers.set(dir, watcher);
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const path = join(dir, entry.name);
    if (entry.isDirectory() && !ignoredDirectory(path)) watchTree(path, watchers, onChange);
  }
}

// Starts the server. options: {build, root, target, port, host, runBuilds, log}. Resolves to {port, close} once it listens.
export async function startDevServer(options) {
  const log = options.log ?? console.log;
  const buildDir = resolve(options.build);
  const root = resolve(options.root ?? buildDir);
  const manifestPath = join(root, `${options.target}.manifest.json`);
  const readVersion = () => (existsSync(manifestPath) ? manifestVersion(readFileSync(manifestPath, 'utf8')) : null);

  const clients = new Set();
  let version = readVersion();
  let buildState = 'ok'; // what the badge shows: 'building', 'failed' or 'ok' (nothing)
  const send = (response, event, data) => response.write(`event: ${event}\ndata: ${data}\n\n`);
  const broadcast = (event, data) => {
    for (const response of clients) send(response, event, data);
  };

  watchFile(manifestPath, { interval: MANIFEST_POLL_MS }, () => {
    const now = readVersion();
    if (now === null || now === version) return; // half written, removed, or the same build again
    version = now;
    log(`[dev] new build ${version}: reloading ${clients.size} page(s)`);
    broadcast('version', version);
  });

  const server = createServer(async (request, response) => {
    const url = new URL(request.url, 'http://localhost');
    if (url.pathname === EVENTS_PATH) {
      response.writeHead(200, { 'Content-Type': 'text/event-stream', 'Cache-Control': 'no-store', Connection: 'keep-alive' });
      response.write('retry: 1000\n\n');
      if (version !== null) send(response, 'version', version);
      send(response, 'build', buildState);
      clients.add(response);
      request.on('close', () => clients.delete(response));
      return;
    }
    if (request.method !== 'GET' && request.method !== 'HEAD') {
      response.writeHead(405, { Allow: 'GET, HEAD' }).end();
      return;
    }
    const file = resolveInside(root, url.pathname);
    let body = null;
    try {
      if (file && (await stat(file)).isFile()) body = await readFile(file);
    } catch {
      body = null;
    }
    if (body === null) {
      response.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8', 'Cache-Control': 'no-store' }).end('Not found\n');
      return;
    }
    // The page names every other file by its build's version (docs/specs/page_loading.md); no-store also keeps the page and
    // its manifest from going stale.
    if (basename(file) === 'index.html') body = Buffer.from(injectReload(body.toString('utf8'), version), 'utf8');
    response.writeHead(200, { 'Content-Type': contentType(file), 'Content-Length': body.length, 'Cache-Control': 'no-store' });
    response.end(request.method === 'HEAD' ? undefined : body);
  });
  const heartbeat = setInterval(() => {
    for (const response of clients) response.write(': \n\n');
  }, HEARTBEAT_MS);
  heartbeat.unref();

  const watchers = new Map();
  if (options.runBuilds) {
    const source = sourceDirectory(buildDir);
    let queue = { building: false, pending: null };
    let builds = 0;
    const runBuild = (changed) => {
      builds += 1;
      log(`[dev] build ${builds}: ${relative(source, changed)}`);
      buildState = 'building';
      broadcast('build', buildState);
      const started = performance.now();
      const child = spawn('cmake', ['--build', buildDir, '--target', options.target], { stdio: 'inherit' });
      let finished = false;
      const finish = (ok, why) => {
        if (finished) return;
        finished = true;
        const seconds = ((performance.now() - started) / 1000).toFixed(1);
        log(ok ? `[dev] ok in ${seconds} s` : `[dev] failed (${why}) in ${seconds} s`);
        buildState = ok ? 'ok' : 'failed';
        broadcast('build', buildState);
        const step = nextBuild(queue, { type: 'done' });
        queue = step.state;
        if (step.start) runBuild(step.start);
      };
      child.on('error', (error) => finish(false, error.message));
      child.on('exit', (code, signal) => finish(code === 0, signal ?? `exit ${code}`));
    };
    let firstChange = null;
    let timer = null;
    watchTree(source, watchers, (path) => {
      firstChange ??= path;
      clearTimeout(timer);
      timer = setTimeout(() => {
        const step = nextBuild(queue, { type: 'change', file: firstChange });
        firstChange = null;
        queue = step.state;
        if (step.start) runBuild(step.start);
      }, DEBOUNCE_MS);
    });
    log(`[dev] watching ${source} (${watchers.size} directories)`);
  }

  await new Promise((resolveListen, rejectListen) => {
    server.once('error', rejectListen);
    server.listen(options.port, options.host, resolveListen);
  });
  const port = server.address().port;
  return {
    port,
    close: () => {
      unwatchFile(manifestPath);
      for (const watcher of watchers.values()) watcher.close();
      clearInterval(heartbeat);
      for (const response of clients) response.end();
      server.closeAllConnections();
      return new Promise((resolveClose) => server.close(resolveClose));
    },
  };
}

async function main() {
  const { values } = parseArgs({
    options: {
      build: { type: 'string' },
      root: { type: 'string' },
      target: { type: 'string' },
      port: { type: 'string', default: '8000' },
      host: { type: 'string', default: '127.0.0.1' },
      'no-build': { type: 'boolean', default: false },
    },
  });
  const fail = (message) => {
    console.error(`[dev] ${message}`);
    process.exit(1);
  };
  if (!values.build) fail('usage: node tools/dev_server.mjs --build <dir> [--root <dir>] [--target <name>] [--port 8000] [--host 127.0.0.1] [--no-build]');
  if (!sourceDirectory(values.build))
    fail(`${values.build} is no configured build directory. Configure it first, for example:\n` +
         `  emcmake cmake -S . -B ${values.build} -G Ninja -DCMAKE_BUILD_TYPE=Release`);
  const root = values.root ?? values.build;
  let target = values.target;
  if (!target) {
    const manifests = existsSync(root) ? readdirSync(root).filter((name) => name.endsWith('.manifest.json')) : [];
    if (manifests.length !== 1)
      fail(`${root} has ${manifests.length} *.manifest.json files: build once (cmake --build ${values.build}) or pass --target <name>`);
    target = manifests[0].slice(0, -'.manifest.json'.length);
  }
  let server;
  try {
    server = await startDevServer({ build: values.build, root, target, port: Number(values.port), host: values.host,
                                    runBuilds: !values['no-build'] });
  } catch (error) {
    fail(error.code === 'EADDRINUSE' ? `port ${values.port} is in use: pass --port <n>` : error.message);
  }
  const shown = values.host === '127.0.0.1' || values.host === '0.0.0.0' ? 'localhost' : values.host;
  console.log(`[dev] serving ${resolve(root)} at http://${shown}:${server.port}/` +
              (values['no-build'] ? ' (no builds: reloads when the build changes)' : ''));
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) main();

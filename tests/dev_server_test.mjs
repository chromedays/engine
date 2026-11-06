// The development server (tools/dev_server.mjs, docs/specs/dev_server.md): its plain functions, then a server on a
// temporary build directory, read without a browser: the page gets the reload script, files come back uncached, nothing
// outside the served folder is given, and a new manifest reaches an open event stream as a new version.
//   node tests/dev_server_test.mjs
import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { contentType, ignoredDirectory, ignoredFile, injectReload, manifestVersion, nextBuild, resolveInside,
         startDevServer } from '../tools/dev_server.mjs';

// A hung stream must fail the test, not stop ctest.
setTimeout(() => {
  console.error('dev_server_test: timed out');
  process.exit(1);
}, 20000).unref();

const temp = mkdtempSync(join(tmpdir(), 'nv-dev-server-'));
try {
  // The page: the script goes before </body>, carries the version it was served with, and cannot close its own tag.
  {
    const page = injectReload('<html><body><p>x</p></body></html>', 'abc123');
    assert.match(page, /<p>x<\/p><script>[\s\S]*<\/script>\n<\/body><\/html>$/);
    assert.ok(page.includes('const loaded = "abc123";'));
    assert.ok(page.includes('new EventSource("/__dev/events")'));
    assert.ok(injectReload('<p>no body</p>', 'v').startsWith('<p>no body</p><script>'));
    assert.ok(injectReload('<BODY></BODY>', 'v').endsWith('</BODY>'));
    assert.ok(injectReload('<body></body>', null).includes('const loaded = "";'));
    assert.ok(!injectReload('<body></body>', '</script>').includes('"</script>"'));
  }

  // Paths: inside the root or nothing.
  {
    const root = join(temp, 'root');
    assert.equal(resolveInside(root, '/'), join(root, 'index.html'));
    assert.equal(resolveInside(root, '/app.wasm'), join(root, 'app.wasm'));
    assert.equal(resolveInside(root, '/sub/'), join(root, 'sub', 'index.html'));
    assert.equal(resolveInside(root, '/a%20b.txt'), join(root, 'a b.txt'));
    assert.equal(resolveInside(root, '/../secret'), null);
    assert.equal(resolveInside(root, '/%2e%2e/secret'), null);
    assert.equal(resolveInside(root, '/sub/../../secret'), null);
    assert.equal(resolveInside(root, '/%00'), null);
    assert.equal(resolveInside(root, '/%E0%A4%A'), null);
    assert.equal(resolveInside(root, '/..'), null);
  }

  assert.equal(contentType('app.wasm'), 'application/wasm');
  assert.equal(contentType('index.html'), 'text/html; charset=utf-8');
  assert.equal(contentType('app.data'), 'application/octet-stream');
  assert.equal(contentType('APP.JS'), 'text/javascript; charset=utf-8');
  assert.equal(contentType('noextension'), 'application/octet-stream');

  assert.equal(manifestVersion('{"version": "19a88f79ea4cc8ab", "files": {}}'), '19a88f79ea4cc8ab');
  assert.equal(manifestVersion('{"version": "19a8'), null);
  assert.equal(manifestVersion(''), null);
  assert.equal(manifestVersion('{"files": {}}'), null);

  // What the watch skips.
  {
    const source = join(temp, 'source');
    mkdirSync(join(source, 'build'), { recursive: true });
    writeFileSync(join(source, 'build', 'CMakeCache.txt'), '');
    mkdirSync(join(source, 'src'));
    assert.equal(ignoredDirectory(join(source, 'build')), true);
    assert.equal(ignoredDirectory(join(source, '.git')), true);
    assert.equal(ignoredDirectory(join(source, 'node_modules')), true);
    assert.equal(ignoredDirectory(join(source, 'src')), false);
    for (const name of ['main.c~', '.main.c.swp', 'main.c.swp', '4913', '.#main.c', 'main.c___jb_tmp___'])
      assert.equal(ignoredFile(name), true, name);
    for (const name of ['main.c', 'default.abproj', 'CMakeLists.txt', 'shader.slang', 'v2.h'])
      assert.equal(ignoredFile(name), false, name);
  }

  // The build queue.
  {
    let step = nextBuild({ building: false, pending: null }, { type: 'change', file: 'a.c' });
    assert.deepEqual(step, { state: { building: true, pending: null }, start: 'a.c' });
    step = nextBuild(step.state, { type: 'change', file: 'b.c' });
    assert.deepEqual(step, { state: { building: true, pending: 'b.c' }, start: null });
    step = nextBuild(step.state, { type: 'change', file: 'c.c' }); // still one more build, named by its first change
    assert.deepEqual(step, { state: { building: true, pending: 'b.c' }, start: null });
    step = nextBuild(step.state, { type: 'done' });
    assert.deepEqual(step, { state: { building: true, pending: null }, start: 'b.c' });
    step = nextBuild(step.state, { type: 'done' });
    assert.deepEqual(step, { state: { building: false, pending: null }, start: null });
  }

  // A server on a build directory whose page is in a folder below it, as the sandbox's is.
  {
    const build = join(temp, 'build');
    const root = join(build, 'page');
    mkdirSync(root, { recursive: true });
    writeFileSync(join(build, 'CMakeCache.txt'), `CMAKE_HOME_DIRECTORY:INTERNAL=${temp}\n`);
    writeFileSync(join(root, 'index.html'), '<!doctype html><html><body><canvas></canvas></body></html>\n');
    writeFileSync(join(root, 'app.wasm'), Buffer.from([0, 97, 115, 109]));
    const manifest = join(root, 'app.manifest.json');
    writeFileSync(manifest, '{"version": "v1", "files": {"app.wasm": 4}}\n');

    const server = await startDevServer({ build, root, target: 'app', port: 0, host: '127.0.0.1', runBuilds: false,
                                          log: () => {} });
    const base = `http://127.0.0.1:${server.port}`;
    try {
      let response = await fetch(`${base}/`);
      assert.equal(response.status, 200);
      assert.equal(response.headers.get('cache-control'), 'no-store');
      let text = await response.text();
      assert.ok(text.includes('<canvas></canvas><script>'));
      assert.ok(text.includes('const loaded = "v1";'));

      response = await fetch(`${base}/app.wasm?v=v1`);
      assert.equal(response.status, 200);
      assert.equal(response.headers.get('content-type'), 'application/wasm');
      assert.equal(response.headers.get('cache-control'), 'no-store');
      assert.deepEqual([...new Uint8Array(await response.arrayBuffer())], [0, 97, 115, 109]);

      response = await fetch(`${base}/app.wasm`, { method: 'HEAD' });
      assert.equal(response.status, 200);
      assert.equal(response.headers.get('content-length'), '4');

      for (const path of ['/missing.js', '/%2e%2e/CMakeCache.txt', '/..%2fCMakeCache.txt', '/page']) {
        response = await fetch(`${base}${path}`);
        assert.equal(response.status, 404, path);
        await response.arrayBuffer();
      }
      response = await fetch(`${base}/index.html`, { method: 'POST' });
      assert.equal(response.status, 405);
      await response.arrayBuffer();

      // The event stream: the version and the build's state on connect, then each new version.
      const events = await fetch(`${base}/__dev/events`);
      assert.equal(events.headers.get('content-type'), 'text/event-stream');
      const reader = events.body.getReader();
      const decoder = new TextDecoder();
      let buffered = '';
      const nextEvent = async () => {
        for (;;) {
          const end = buffered.indexOf('\n\n');
          if (end >= 0) {
            const block = buffered.slice(0, end);
            buffered = buffered.slice(end + 2);
            const event = block.match(/^event: (.*)$/m);
            if (event) return { event: event[1], data: block.match(/^data: (.*)$/m)[1] };
            continue; // retry: and heartbeats
          }
          const { done, value } = await reader.read();
          assert.ok(!done, 'the stream ended');
          buffered += decoder.decode(value, { stream: true });
        }
      };
      assert.deepEqual(await nextEvent(), { event: 'version', data: 'v1' });
      assert.deepEqual(await nextEvent(), { event: 'build', data: 'ok' });

      // A manifest half written is no version; the whole one is.
      writeFileSync(manifest, '{"version": "v2"');
      await new Promise((done) => setTimeout(done, 700));
      writeFileSync(manifest, '{"version": "v2", "files": {"app.wasm": 4}}\n');
      assert.deepEqual(await nextEvent(), { event: 'version', data: 'v2' });

      response = await fetch(`${base}/`);
      assert.ok((await response.text()).includes('const loaded = "v2";'));
      await reader.cancel();
    } finally {
      await server.close();
    }
  }
} finally {
  rmSync(temp, { recursive: true, force: true });
}
console.log('dev_server_test: ok');

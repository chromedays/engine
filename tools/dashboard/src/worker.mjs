// The build dashboard's Worker (docs/specs/build_dashboard.md): serves the page and the builds in R2, takes GitHub's webhooks
// and CI's build records, and pushes every change to the open pages over a WebSocket. One Durable Object holds the state
// and the sockets; the logic itself is in state.mjs.
import { DurableObject } from 'cloudflare:workers';
import { applyBuild, applyResync, applyWebhook, emptyState, snapshot } from './state.mjs';

const RESYNC_MS = 5 * 60 * 1000;
const BRANCH_PAGES = 5; // 500 branches at most; past that the resync deletes nothing

const encoder = new TextEncoder();

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const dashboard = () => env.DASHBOARD.get(env.DASHBOARD.idFromName('main'));

    if (url.pathname === '/webhook') {
      if (request.method !== 'POST') return status(405);
      const body = await request.text();
      if (!(await signed(env.GITHUB_WEBHOOK_SECRET, body, request.headers.get('X-Hub-Signature-256')))) return status(401);
      await dashboard().webhook(request.headers.get('X-GitHub-Event'), JSON.parse(body));
      return status(204);
    }
    if (url.pathname === '/api/builds') {
      if (request.method !== 'POST') return status(405);
      if (!(await bearer(env.DASHBOARD_TOKEN, request.headers.get('Authorization')))) return status(401);
      try {
        await dashboard().addBuild(await request.json());
      } catch (e) {
        return new Response(String(e.message || e), { status: 400 });
      }
      return status(204);
    }
    if (url.pathname === '/api/state') return Response.json(await dashboard().state());
    if (url.pathname === '/ws') {
      if (request.headers.get('Upgrade') !== 'websocket') return status(426);
      // Only the dashboard's own page connects.
      if (request.headers.get('Origin') !== url.origin) return status(403);
      return dashboard().fetch(request);
    }
    if (url.pathname.startsWith('/builds/')) return serveBuild(request, env, url);
    return status(404);
  },
};

export class Dashboard extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.options = { defaultBranch: env.DEFAULT_BRANCH, repo: env.REPO };
    // The pages ping to keep their connection; the answer does not wake the object.
    ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair('ping', 'pong'));
    ctx.blockConcurrencyWhile(async () => {
      this.data = (await ctx.storage.get('state')) || emptyState();
      // A new object reads GitHub at once; afterwards every RESYNC_MS.
      if ((await ctx.storage.getAlarm()) === null) await ctx.storage.setAlarm(Date.now() + 1000);
    });
  }

  // A page connects: it gets the whole state now and again after every change.
  async fetch() {
    const { 0: client, 1: server } = new WebSocketPair();
    this.ctx.acceptWebSocket(server);
    server.send(this.message());
    return new Response(null, { status: 101, webSocket: client });
  }

  async webhook(event, payload) {
    await this.commit(applyWebhook(this.data, event, payload, { defaultBranch: this.options.defaultBranch, now: Date.now() }));
  }

  async addBuild(record) {
    await this.commit(applyBuild(this.data, record));
  }

  state() {
    return snapshot(this.data, this.options);
  }

  async alarm() {
    try {
      const read = await this.readGitHub();
      if (read) await this.commit(applyResync(this.data, read, { defaultBranch: this.options.defaultBranch, now: Date.now() }));
    } finally {
      await this.ctx.storage.setAlarm(Date.now() + RESYNC_MS);
    }
  }

  webSocketClose(ws, code) {
    ws.close(code, 'closing');
  }

  message() {
    return JSON.stringify({ type: 'snapshot', state: snapshot(this.data, this.options) });
  }

  async commit({ changed, removeBuilds }) {
    if (!changed) return;
    await this.ctx.storage.put('state', this.data);
    for (const folder of removeBuilds) await deletePrefix(this.env.BUILDS, `builds/${folder}/`);
    const message = this.message();
    for (const ws of this.ctx.getWebSockets()) {
      try {
        ws.send(message);
      } catch {
        // A socket closing meanwhile; its page reconnects and gets a snapshot.
      }
    }
  }

  // Reads what the resync needs; null when GitHub did not answer, so nothing is changed on partial data.
  async readGitHub() {
    const repo = this.options.repo;
    const get = async (path) => {
      const response = await fetch(`https://api.github.com/repos/${repo}/${path}`, {
        headers: {
          Accept: 'application/vnd.github+json',
          'User-Agent': 'nv-build-dashboard',
          ...(this.env.GITHUB_TOKEN && { Authorization: `Bearer ${this.env.GITHUB_TOKEN}` }),
        },
      });
      if (!response.ok) throw new Error(`GitHub ${path}: ${response.status}`);
      return response.json();
    };
    try {
      const branches = [];
      let complete = false;
      for (let page = 1; page <= BRANCH_PAGES; ++page) {
        const list = await get(`branches?per_page=100&page=${page}`);
        branches.push(...list);
        if (list.length < 100) {
          complete = true;
          break;
        }
      }
      const pulls = await get('pulls?state=all&sort=updated&direction=desc&per_page=100');
      const runs = (await get('actions/workflows/build.yml/runs?event=push&per_page=100')).workflow_runs;
      return { branches, complete, pulls, runs, repo };
    } catch (e) {
      console.log(`resync skipped: ${e.message}`);
      return null;
    }
  }
}

async function deletePrefix(bucket, prefix) {
  let cursor;
  do {
    const list = await bucket.list({ prefix, cursor });
    if (list.objects.length) await bucket.delete(list.objects.map((o) => o.key));
    cursor = list.truncated ? list.cursor : undefined;
  } while (cursor);
}

const TYPES = { html: 'text/html; charset=utf-8', js: 'text/javascript', wasm: 'application/wasm', json: 'application/json', data: 'application/octet-stream' };

// /builds/<folder>/<release|debug>/<file>, from the bucket. The file names are not hashed, so every response is
// revalidated (an unchanged file answers 304 by its ETag).
async function serveBuild(request, env, url) {
  if (request.method !== 'GET' && request.method !== 'HEAD') return status(405);
  let key = decodeURIComponent(url.pathname.slice(1));
  if (key.split('/').some((part) => part === '..' || part === '.')) return status(400);
  if (key.endsWith('/')) key += 'index.html';
  else if (!key.slice(key.lastIndexOf('/') + 1).includes('.')) return Response.redirect(`${url.origin}${url.pathname}/`, 301);

  const object = request.method === 'HEAD' ? await env.BUILDS.head(key) : await env.BUILDS.get(key, { onlyIf: request.headers });
  if (!object) return status(404);
  const headers = new Headers();
  object.writeHttpMetadata(headers);
  if (!headers.has('Content-Type')) headers.set('Content-Type', TYPES[key.slice(key.lastIndexOf('.') + 1)] || 'application/octet-stream');
  headers.set('ETag', object.httpEtag);
  headers.set('Cache-Control', 'no-cache');
  if (request.method === 'HEAD') return new Response(null, { headers: withLength(headers, object.size) });
  if (!('body' in object) || !object.body) return new Response(null, { status: 304, headers });
  return new Response(object.body, { headers: withLength(headers, object.size) });
}

function withLength(headers, size) {
  headers.set('Content-Length', String(size));
  return headers;
}

function status(code) {
  return new Response(null, { status: code });
}

// GitHub signs each webhook's body with the shared secret: X-Hub-Signature-256 is "sha256=" and the HMAC in hex.
async function signed(secret, body, header) {
  if (!secret || !header || !/^sha256=[0-9a-f]{64}$/.test(header)) return false;
  const key = await crypto.subtle.importKey('raw', encoder.encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['verify']);
  const mac = new Uint8Array(header.slice(7).match(/../g).map((h) => parseInt(h, 16)));
  return crypto.subtle.verify('HMAC', key, mac, encoder.encode(body));
}

// CI registers builds with "Authorization: Bearer <DASHBOARD_TOKEN>".
async function bearer(token, header) {
  if (!token || !header || !header.startsWith('Bearer ')) return false;
  const given = encoder.encode(header.slice(7));
  const expected = encoder.encode(token);
  return given.byteLength === expected.byteLength && crypto.subtle.timingSafeEqual(given, expected);
}

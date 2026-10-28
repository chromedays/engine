// The dashboard's state, and how GitHub's events, CI's build records and the periodic resync change it
// (docs/specs/build_dashboard.md). Plain functions on plain data, with no Cloudflare API, so tests/dashboard_test.mjs runs
// them under Node.
//
// state = {
//   branches: { [name]: {
//     head:  { sha, subject, date } | null,   the newest commit GitHub told of
//     ci:    "running" | "passed" | "failed" | null,   the build workflow on that commit
//     pr:    { number, title, state: "open" | "merged" | "closed" } | null,   the branch's newest pull request
//     build: { sha, subject, date, release, debug, stored, ahead } | null,   what CI uploaded (sizes in bytes)
//   } },
//   activity: [{ id, at, kind, branch, number?, title?, sha?, subject? }],   newest last, ACTIVITY_KEEP at most
//   nextId,
// }
//
// Every apply function returns { changed, removeBuilds }: whether the state must be saved and sent to the pages, and the
// folders whose builds must leave the bucket.

export const ACTIVITY_KEEP = 50;
export const BUILD_WORKFLOW = '.github/workflows/build.yml';

const NONE = { changed: false, removeBuilds: [] };
const CHANGED = { changed: true, removeBuilds: [] };

export function emptyState() {
  return { branches: {}, activity: [], nextId: 1 };
}

// A branch's folder in the bucket: "/" becomes "-", as the Pages folders did.
export function folderOf(branch) {
  return branch.replaceAll('/', '-');
}

// The page shows the default branch and every branch with a pull request; the others are only followed.
export function isShown(state, name, defaultBranch) {
  const b = state.branches[name];
  return !!b && (name === defaultBranch || !!b.pr);
}

// What the page receives: the shown branches and the activity.
export function snapshot(state, { repo, defaultBranch }) {
  const branches = {};
  for (const name of Object.keys(state.branches)) {
    if (isShown(state, name, defaultBranch)) branches[name] = state.branches[name];
  }
  return { repo, defaultBranch, branches, activity: state.activity };
}

function branchOf(state, name) {
  return (state.branches[name] ||= { head: null, ci: null, pr: null, build: null });
}

function firstLine(message) {
  return message ? message.split('\n')[0] : null;
}

function note(state, name, entry, now, defaultBranch) {
  if (!isShown(state, name, defaultBranch)) return;
  const pr = state.branches[name].pr;
  state.activity.push({ id: state.nextId++, at: now, branch: name, ...(pr && { number: pr.number, title: pr.title }), ...entry });
  if (state.activity.length > ACTIVITY_KEEP) state.activity.splice(0, state.activity.length - ACTIVITY_KEEP);
}

function removeBranch(state, name, now, defaultBranch) {
  if (!state.branches[name] || name === defaultBranch) return NONE;
  note(state, name, { kind: 'delete' }, now, defaultBranch);
  delete state.branches[name];
  return { changed: true, removeBuilds: [folderOf(name)] };
}

function prState(pr) {
  return pr.state === 'open' ? 'open' : pr.merged || pr.merged_at ? 'merged' : 'closed';
}

// A branch can have several pull requests over time: the newest one (the highest number) is its own.
function setPr(b, pr) {
  if (b.pr && b.pr.number > pr.number) return false;
  const next = { number: pr.number, title: pr.title, state: prState(pr) };
  if (b.pr && b.pr.number === next.number && b.pr.title === next.title && b.pr.state === next.state) return false;
  b.pr = next;
  if (next.state === 'merged' && b.build) b.build.ahead = 0;
  return true;
}

// One webhook: `event` is its X-GitHub-Event header, `payload` its body, `now` the time in milliseconds.
export function applyWebhook(state, event, payload, { defaultBranch, now }) {
  switch (event) {
    case 'push': {
      if (!payload.ref || !payload.ref.startsWith('refs/heads/')) return NONE;
      const name = payload.ref.slice('refs/heads/'.length);
      if (payload.deleted) return removeBranch(state, name, now, defaultBranch);
      const b = branchOf(state, name);
      const commit = payload.head_commit;
      b.head = { sha: payload.after, subject: firstLine(commit && commit.message), date: (commit && commit.timestamp) || new Date(now).toISOString() };
      b.ci = 'running';
      note(state, name, { kind: 'push', sha: payload.after, subject: b.head.subject }, now, defaultBranch);
      return CHANGED;
    }
    case 'pull_request': {
      const pr = payload.pull_request;
      // A fork's pull request is never built here: its code does not run with the secrets.
      if (!pr || !pr.head.repo || pr.head.repo.full_name !== payload.repository.full_name) return NONE;
      const name = pr.head.ref;
      const b = branchOf(state, name);
      if (!setPr(b, pr)) return NONE;
      if (!b.head) b.head = { sha: pr.head.sha, subject: null, date: pr.updated_at };
      const kind = { opened: 'opened', reopened: 'opened', closed: b.pr.state }[payload.action];
      if (kind) note(state, name, { kind }, now, defaultBranch);
      return CHANGED;
    }
    case 'delete':
      return payload.ref_type === 'branch' ? removeBranch(state, payload.ref, now, defaultBranch) : NONE;
    case 'workflow_run': {
      const run = payload.workflow_run;
      if (!run || run.path !== BUILD_WORKFLOW || run.event !== 'push') return NONE;
      const b = state.branches[run.head_branch];
      if (!b || !b.head || b.head.sha !== run.head_sha) return NONE;
      if (run.status !== 'completed') {
        if (b.ci === 'running') return NONE;
        b.ci = 'running';
        return CHANGED;
      }
      const result = { success: 'passed', failure: 'failed' }[run.conclusion] || null;
      if (b.ci === result) return NONE;
      b.ci = result;
      if (result) note(state, run.head_branch, { kind: result, sha: run.head_sha }, now, defaultBranch);
      return CHANGED;
    }
  }
  return NONE;
}

// A build CI uploaded (tools/dashboard/publish.sh). Throws on a malformed record.
export function applyBuild(state, record) {
  const text = (v) => typeof v === 'string' && v.length > 0 && v.length < 1000;
  const size = (v) => Number.isSafeInteger(v) && v >= 0;
  if (!text(record.branch) || !/^[0-9a-f]{40}$/.test(record.sha) || !text(record.date)
      || !size(record.release) || !size(record.debug) || !size(record.stored)
      || !(record.ahead === null || size(record.ahead)) || !(record.subject === null || text(record.subject))) {
    throw new Error('malformed build record');
  }
  const b = branchOf(state, record.branch);
  const { sha, subject, date, release, debug, stored, ahead } = record;
  b.build = { sha, subject, date, release, debug, stored, ahead: b.pr && b.pr.state === 'merged' ? 0 : ahead };
  if (!b.head) b.head = { sha, subject, date };
  if (b.head.sha === sha) {
    if (!b.head.subject) b.head.subject = subject;
    // The upload is the build workflow's last step, so a build that is in the bucket passed CI.
    b.ci = 'passed';
  }
  return CHANGED;
}

// What the resync read from GitHub's API: the branches, the newest pull requests and the build workflow's newest runs (each
// list as the API returns it; `complete` says the branch list has every branch, so a branch missing from it was deleted).
// Fixes whatever a missed webhook left behind.
export function applyResync(state, { branches, complete, pulls, runs, repo }, { defaultBranch, now }) {
  const result = { changed: false, removeBuilds: [] };
  const merge = (r) => {
    result.changed ||= r.changed;
    result.removeBuilds.push(...r.removeBuilds);
  };

  const heads = new Map(branches.map((b) => [b.name, b.commit.sha]));
  if (complete) {
    for (const name of Object.keys(state.branches)) {
      if (!heads.has(name)) merge(removeBranch(state, name, now, defaultBranch));
    }
  }
  const ours = pulls.filter((pr) => pr.head.repo && pr.head.repo.full_name === repo);
  const withPr = new Set(ours.map((pr) => pr.head.ref));

  // The newest run of each branch, which the API lists first; its commit message names the head.
  const newestRun = new Map();
  for (const run of runs) {
    if (run.path === BUILD_WORKFLOW && run.event === 'push' && !newestRun.has(run.head_branch)) newestRun.set(run.head_branch, run);
  }

  for (const [name, sha] of heads) {
    const known = state.branches[name];
    const run = newestRun.get(name);
    if (!known && !run && !withPr.has(name) && name !== defaultBranch) continue; // nothing to show for it
    const b = branchOf(state, name);
    if (!b.head || b.head.sha !== sha) {
      const fromRun = run && run.head_sha === sha && run.head_commit;
      b.head = { sha, subject: fromRun ? firstLine(run.head_commit.message) : null, date: fromRun ? run.head_commit.timestamp : null };
      result.changed = true;
    }
    if (run && run.head_sha === sha) {
      const ci = run.status !== 'completed' ? 'running' : { success: 'passed', failure: 'failed' }[run.conclusion] || null;
      if (b.ci !== ci) {
        b.ci = ci;
        result.changed = true;
      }
    }
  }

  for (const pr of ours) {
    const b = state.branches[pr.head.ref];
    if (b && setPr(b, pr)) result.changed = true;
  }
  return result;
}

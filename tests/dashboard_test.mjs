// The build dashboard's state logic (tools/dashboard/src/state.mjs, docs/specs/build_dashboard.md): GitHub's events, CI's
// build records and the resync, on plain data. The Worker around it is tried with `wrangler dev` (tools/dashboard/README.md).
//   node tests/dashboard_test.mjs
import assert from 'node:assert/strict';
import { applyBuild, applyResync, applyWebhook, emptyState, folderOf, snapshot } from '../tools/dashboard/src/state.mjs';

const opts = { defaultBranch: 'master', now: Date.parse('2026-10-28T10:00:00Z') };
const repo = { full_name: 'chromedays/engine' };
const sha = (c) => c.repeat(40);
const pull = (number, ref, state, extra = {}) => ({ number, title: `PR ${number}`, state, head: { ref, sha: sha('1'), repo }, ...extra });
const push = (ref, after, message = 'A commit\n\nWith a body') => ({ ref: `refs/heads/${ref}`, after, head_commit: { message, timestamp: '2026-10-28T09:00:00Z' } });
const run = (branch, head, conclusion, status = 'completed') =>
  ({ path: '.github/workflows/build.yml', event: 'push', head_branch: branch, head_sha: head, status, conclusion, head_commit: { message: 'From the run', timestamp: '2026-10-28T08:00:00Z' } });
const record = (branch, s, extra = {}) =>
  ({ branch, sha: s, subject: 'Built', date: '2026-10-28T09:30:00Z', release: 4000000, debug: 7000000, stored: 20000000, ahead: 2, ...extra });

let failures = 0;
function test(name, body) {
  try {
    body();
    console.log(`ok   ${name}`);
  } catch (e) {
    ++failures;
    console.log(`FAIL ${name}\n${e.stack}`);
  }
}

test('a branch is shown once it has a pull request; master always', () => {
  const s = emptyState();
  assert.equal(applyWebhook(s, 'push', push('master', sha('a')), opts).changed, true);
  applyWebhook(s, 'push', push('feature/x', sha('b')), opts);
  assert.deepEqual(Object.keys(snapshot(s, { repo: 'r', defaultBranch: 'master' }).branches), ['master']);
  assert.deepEqual(s.activity.map((a) => a.kind), ['push'], 'a push to a branch without a pull request makes no activity');
  assert.equal(s.branches.master.head.subject, 'A commit', 'the subject is the first line');

  applyWebhook(s, 'pull_request', { action: 'opened', repository: repo, pull_request: pull(14, 'feature/x', 'open') }, opts);
  assert.deepEqual(Object.keys(snapshot(s, { repo: 'r', defaultBranch: 'master' }).branches).sort(), ['feature/x', 'master']);
  const opened = s.activity.at(-1);
  assert.equal(opened.kind, 'opened');
  assert.equal(opened.number, 14);
  assert.equal(opened.title, 'PR 14');
});

test("a fork's pull request is ignored", () => {
  const s = emptyState();
  const fork = pull(15, 'patch-1', 'open', { head: { ref: 'patch-1', sha: sha('d'), repo: { full_name: 'someone/engine' } } });
  assert.equal(applyWebhook(s, 'pull_request', { action: 'opened', repository: repo, pull_request: fork }, opts).changed, false);
  assert.equal(s.branches['patch-1'], undefined);
});

test('CI results follow the newest commit only', () => {
  const s = emptyState();
  applyWebhook(s, 'pull_request', { action: 'opened', repository: repo, pull_request: pull(14, 'x', 'open') }, opts);
  applyWebhook(s, 'push', push('x', sha('b')), opts);
  assert.equal(s.branches.x.ci, 'running');
  assert.equal(applyWebhook(s, 'workflow_run', { workflow_run: run('x', sha('a'), 'success') }, opts).changed, false, 'an older commit');
  assert.equal(applyWebhook(s, 'workflow_run', { workflow_run: { ...run('x', sha('b'), 'success'), path: '.github/workflows/pages.yml' } }, opts).changed, false, 'another workflow');
  applyWebhook(s, 'workflow_run', { workflow_run: run('x', sha('b'), 'failure') }, opts);
  assert.equal(s.branches.x.ci, 'failed');
  assert.equal(s.activity.at(-1).kind, 'failed');
  assert.equal(applyWebhook(s, 'workflow_run', { workflow_run: run('x', sha('b'), 'failure') }, opts).changed, false, 'a repeat');
});

test('a build record sets the build; a newer push keeps it until its own', () => {
  const s = emptyState();
  applyWebhook(s, 'pull_request', { action: 'opened', repository: repo, pull_request: pull(14, 'x', 'open') }, opts);
  applyWebhook(s, 'push', push('x', sha('b')), opts);
  applyBuild(s, record('x', sha('b')));
  assert.equal(s.branches.x.build.sha, sha('b'));
  assert.equal(s.branches.x.ci, 'passed', 'the upload is the last step of a passing run');
  applyWebhook(s, 'push', push('x', sha('c')), opts);
  assert.equal(s.branches.x.build.sha, sha('b'));
  assert.equal(s.branches.x.head.sha, sha('c'));
  assert.equal(s.branches.x.ci, 'running');
  assert.throws(() => applyBuild(s, record('x', 'not a sha')));
  assert.throws(() => applyBuild(s, record('x', sha('c'), { release: -1 })));
  assert.throws(() => applyBuild(s, { ...record('x', sha('c')), branch: '' }));
});

test('merging counts the branch as in master', () => {
  const s = emptyState();
  applyWebhook(s, 'pull_request', { action: 'opened', repository: repo, pull_request: pull(14, 'x', 'open') }, opts);
  applyBuild(s, record('x', sha('b')));
  applyWebhook(s, 'pull_request', { action: 'closed', repository: repo, pull_request: pull(14, 'x', 'closed', { merged: true }) }, opts);
  assert.equal(s.branches.x.pr.state, 'merged');
  assert.equal(s.branches.x.build.ahead, 0);
  assert.equal(s.activity.at(-1).kind, 'merged');
  applyBuild(s, record('x', sha('c'), { ahead: 5 }));
  assert.equal(s.branches.x.build.ahead, 0, 'still merged');
});

test('closing without merging; an older pull request does not replace a newer one', () => {
  const s = emptyState();
  applyWebhook(s, 'pull_request', { action: 'opened', repository: repo, pull_request: pull(14, 'x', 'open') }, opts);
  applyWebhook(s, 'pull_request', { action: 'closed', repository: repo, pull_request: pull(14, 'x', 'closed') }, opts);
  assert.equal(s.branches.x.pr.state, 'closed');
  assert.equal(s.activity.at(-1).kind, 'closed');
  applyWebhook(s, 'pull_request', { action: 'edited', repository: repo, pull_request: pull(9, 'x', 'closed', { merged: true }) }, opts);
  assert.equal(s.branches.x.pr.number, 14);
});

test('deleting a branch removes it and its builds, once', () => {
  const s = emptyState();
  applyWebhook(s, 'pull_request', { action: 'opened', repository: repo, pull_request: pull(14, 'feature/x', 'open') }, opts);
  const r = applyWebhook(s, 'delete', { ref: 'feature/x', ref_type: 'branch' }, opts);
  assert.deepEqual(r.removeBuilds, ['feature-x']);
  assert.equal(s.branches['feature/x'], undefined);
  assert.equal(s.activity.at(-1).kind, 'delete');
  assert.equal(s.activity.at(-1).number, 14, 'the entry keeps the pull request it belonged to');
  assert.equal(applyWebhook(s, 'push', { ref: 'refs/heads/feature/x', after: sha('0'), deleted: true }, opts).changed, false);
  assert.equal(applyWebhook(s, 'delete', { ref: 'v1', ref_type: 'tag' }, opts).changed, false);
});

test('the activity keeps the newest entries with rising ids', () => {
  const s = emptyState();
  for (let i = 0; i < 60; ++i) applyWebhook(s, 'push', push('master', sha(String(i % 10))), opts);
  assert.equal(s.activity.length, 50);
  assert.equal(s.activity.at(-1).id, 60);
  assert.equal(s.activity[0].id, 11);
});

test('folders', () => {
  assert.equal(folderOf('claude/festive-noether-jxj7qi'), 'claude-festive-noether-jxj7qi');
  assert.equal(folderOf('master'), 'master');
});

test('the resync fills a new state and fixes what webhooks missed', () => {
  const s = emptyState();
  const read = {
    repo: 'chromedays/engine',
    complete: true,
    branches: [{ name: 'master', commit: { sha: sha('a') } }, { name: 'x', commit: { sha: sha('b') } }, { name: 'quiet', commit: { sha: sha('c') } }],
    pulls: [pull(14, 'x', 'open'), pull(12, 'x', 'closed', { merged_at: '2026-10-01T00:00:00Z' })],
    runs: [run('x', sha('b'), 'failure'), run('x', sha('0'), 'success'), run('master', sha('a'), null, 'in_progress')],
  };
  assert.equal(applyResync(s, read, opts).changed, true);
  assert.equal(s.branches.x.pr.number, 14);
  assert.equal(s.branches.x.ci, 'failed', "the branch's newest run");
  assert.equal(s.branches.x.head.subject, 'From the run');
  assert.equal(s.branches.master.ci, 'running');
  assert.equal(s.branches.quiet, undefined, 'a branch with no pull request and no run is not followed');
  assert.equal(s.activity.length, 0, 'the resync writes no activity for what it fills in');
  assert.equal(applyResync(s, read, opts).changed, false, 'nothing new the second time');

  const gone = applyResync(s, { ...read, branches: read.branches.filter((b) => b.name !== 'x') }, opts);
  assert.deepEqual(gone.removeBuilds, ['x'], 'a deleted branch whose webhook was missed');
  applyWebhook(s, 'pull_request', { action: 'opened', repository: repo, pull_request: pull(16, 'y', 'open') }, opts);
  const partial = applyResync(s, { ...read, complete: false, branches: [] }, opts);
  assert.deepEqual(partial.removeBuilds, [], 'an incomplete branch list deletes nothing');
  assert.ok(s.branches.y);
});

if (failures) {
  console.log(`${failures} failed`);
  process.exit(1);
}

// Writes the visual regression test's result page (docs/specs/visual_report.md): report.json, the run for
// scripts and agents, and report.html, one self-contained page (no CDN, fonts or other files) with every
// image inside as a data: URI, so it opens offline, from file://, and after being moved to another computer.
// The page only shows the run: pass and fail and the counts of differing pixels are compare.mjs's alone.

import fs from 'fs';
import path from 'path';

// The page's style and script. String.raw keeps their backslashes; neither may contain a backtick or a
// dollar sign followed by an opening brace.
const STYLE = String.raw`
:root {
  color-scheme: light;
  --bg: #f6f7f9; --surface: #ffffff; --text: #1d2024; --muted: #5d6570; --border: #d9dde3;
  --pass: #1a7f37; --pass-bg: #e6f4ea; --fail: #c62828; --fail-bg: #fdecea;
  --accent: #2f5fd0; --stage: #808080; --mark: #ff00ff;
}
@media (prefers-color-scheme: dark) {
  :root {
    color-scheme: dark;
    --bg: #15171a; --surface: #1e2125; --text: #e6e8eb; --muted: #9aa3ad; --border: #33383f;
    --pass: #4cc26a; --pass-bg: #1b3324; --fail: #ff6b6b; --fail-bg: #3a1e1e;
    --accent: #7ea2ff;
  }
}
* { box-sizing: border-box; }
[hidden] { display: none !important; }
html { -webkit-text-size-adjust: 100%; }
body { margin: 0; background: var(--bg); color: var(--text);
  font: 14px/1.45 system-ui, -apple-system, "Segoe UI", sans-serif; }
code, .mono { font-family: ui-monospace, "Cascadia Mono", "DejaVu Sans Mono", Menlo, monospace; font-size: 12.5px; }
header, main { max-width: 1600px; margin: 0 auto; padding: 16px; }
header { padding-bottom: 0; }
h1 { font-size: 22px; margin: 4px 0 6px; }
h1.pass { color: var(--pass); }
h1.fail { color: var(--fail); }
.meta { color: var(--muted); margin: 0 0 8px; overflow-wrap: anywhere; }
.meta span + span::before { content: "\00b7"; margin: 0 0.45em; }
.banner { background: var(--fail-bg); color: var(--fail); border: 1px solid var(--fail);
  border-radius: 6px; padding: 8px 12px; margin-bottom: 8px; overflow-wrap: anywhere; }
.hint { margin: 0 0 12px; }
button { font: inherit; color: inherit; background: var(--surface); border: 1px solid var(--border);
  border-radius: 6px; padding: 3px 10px; cursor: pointer; }
button:hover:not(:disabled) { border-color: var(--accent); }
button:disabled { opacity: 0.45; cursor: default; }
button[aria-pressed="true"] { background: var(--accent); border-color: var(--accent); color: var(--surface); }
button.copy { font-family: ui-monospace, "Cascadia Mono", "DejaVu Sans Mono", Menlo, monospace; font-size: 12.5px; }
.group { display: inline-flex; flex-wrap: wrap; gap: 4px; }
.filter { margin-bottom: 4px; }
main { display: flex; flex-direction: column; gap: 12px; }
details.case { background: var(--surface); border: 1px solid var(--border); border-radius: 8px; }
details.case > summary { display: flex; flex-wrap: wrap; align-items: baseline; gap: 4px 10px;
  padding: 10px 14px; cursor: pointer; list-style: none; }
details.case > summary::-webkit-details-marker { display: none; }
details.case > summary::before { content: "\25b8"; color: var(--muted); width: 1em; }
details.case[open] > summary::before { content: "\25be"; }
.badge { font-weight: 700; font-size: 12px; letter-spacing: 0.04em; padding: 1px 7px; border-radius: 4px; }
.badge.pass { color: var(--pass); background: var(--pass-bg); }
.badge.fail { color: var(--fail); background: var(--fail-bg); }
.name { font-weight: 600; }
.facts { color: var(--muted); overflow-wrap: anywhere; }
.body { padding: 0 14px 14px; }
.toolbar { display: flex; flex-wrap: wrap; gap: 8px 16px; align-items: center; margin-bottom: 10px; }
.toolbar label { display: inline-flex; align-items: center; gap: 4px; cursor: pointer; }
.stage { display: grid; gap: 8px; grid-template-columns: 1fr; }
.stage.side { grid-template-columns: repeat(3, minmax(0, 1fr)); }
@media (max-width: 760px) { .stage.side { grid-template-columns: 1fr; } }
.pane { position: relative; overflow: hidden; background: var(--stage); border-radius: 4px;
  max-height: 75vh; touch-action: none; cursor: grab; user-select: none; -webkit-user-select: none;
  outline: 1px solid var(--border); }
.pane.dragging { cursor: grabbing; }
.pane .layer { position: absolute; left: 0; top: 0; transform-origin: 0 0; max-width: none; pointer-events: none; }
.pane.pixelated .layer { image-rendering: pixelated; }
.pane .clip { position: absolute; inset: 0; pointer-events: none; }
.pane .label { position: absolute; top: 6px; left: 6px; background: rgba(0, 0, 0, 0.62); color: #fff;
  font-size: 12px; padding: 1px 7px; border-radius: 4px; pointer-events: none; }
.pane .label.right { left: auto; right: 6px; }
.pane .empty { position: absolute; inset: 0; display: flex; align-items: center; justify-content: center;
  text-align: center; padding: 16px; color: #fff; text-shadow: 0 1px 2px rgba(0, 0, 0, 0.6); pointer-events: none; }
.pane .mark { position: absolute; border: 1px solid var(--mark); outline: 1px solid rgba(0, 0, 0, 0.7);
  pointer-events: none; }
.pane .divider { position: absolute; top: 0; bottom: 0; width: 16px; margin-left: -8px; cursor: ew-resize; }
.pane .divider::after { content: ""; position: absolute; top: 0; bottom: 0; left: 7px; width: 2px;
  background: #fff; box-shadow: 0 0 2px rgba(0, 0, 0, 0.8); }
.readout { margin-top: 8px; min-height: 1.45em; color: var(--muted); white-space: pre-wrap; overflow-wrap: anywhere; }
.readout b { color: var(--text); font-weight: 600; }
.paths { margin: 10px 0 0; display: grid; grid-template-columns: max-content minmax(0, 1fr); gap: 2px 10px;
  color: var(--muted); font-size: 12px; }
.paths dt { font-weight: 600; }
.paths dd { margin: 0; overflow-wrap: anywhere; }
`;

const SCRIPT = String.raw`
'use strict';
const run = JSON.parse(document.getElementById('run').textContent);
const images = JSON.parse(document.getElementById('images').textContent);

function el(tag, props, ...children) {
  const node = document.createElement(tag);
  for (const key in props || {}) {
    if (key === 'class') node.className = props[key];
    else if (key === 'text') node.textContent = props[key];
    else if (key.startsWith('on')) node.addEventListener(key.slice(2), props[key]);
    else node.setAttribute(key, props[key]);
  }
  for (const child of children) if (child != null) node.append(child);
  return node;
}

const plural = (n, one, many) => n + ' ' + (n === 1 ? one : many);
const size = (w, h) => (w == null ? '?' : w) + '×' + (h == null ? '?' : h);

// odiff rounds the percentage to two decimals, so a few pixels of a large image read as 0.
const percent = (c) => (c.diffPixels > 0 && c.diffPercent < 0.01 ? '<0.01%' : c.diffPercent + '%');

function facts(c) {
  switch (c.reason) {
    case 'identical': return size(c.width, c.height) + ' · identical at threshold ' + run.threshold;
    case 'within':
    case 'pixels':
      return size(c.width, c.height) + ' · ' + (c.diffPixels == null ? 'pixels differ'
        : plural(c.diffPixels, 'pixel differs', 'pixels differ') + ' (' + percent(c) + ')') +
        ', at most ' + run.maxDiffPercent + '% allowed';
    case 'layout':
      return 'size differs: baseline ' + size(c.width, c.height) + ', actual ' + size(c.actualWidth, c.actualHeight);
    case 'missing': return 'no capture at ' + c.missing;
  }
  return '';
}

// ---- Header ----

const header = document.getElementById('header');
const total = run.cases.length;
if (run.error) header.append(el('div', { class: 'banner', text: 'The comparison stopped: ' + run.error }));
header.append(el('h1', {
  class: run.failed > 0 || run.error ? 'fail' : 'pass',
  text: run.error ? plural(run.failed, 'case differs', 'cases differ') + ' before the run stopped'
    : run.failed > 0 ? run.failed + ' of ' + plural(total, 'case differs', 'cases differ')
    : 'All ' + plural(total, 'case passes', 'cases pass'),
}));
const meta = el('p', { class: 'meta' });
const metaItems = [new Date(run.time).toLocaleString()];
if (run.commit) metaItems.push(run.commit + (run.subject ? ' ' + run.subject : ''));
metaItems.push('odiff ' + (run.odiff || '?'), 'threshold ' + run.threshold,
  'at most ' + run.maxDiffPercent + '% of pixels may differ');
for (const item of metaItems) meta.append(el('span', { text: item }));
header.append(meta);
if (run.failed > 0) {
  const copy = el('button', { class: 'copy', title: 'Copy the command', text: 'just visual-update-ref' });
  copy.addEventListener('click', () => {
    const done = () => { copy.textContent = 'Copied'; setTimeout(() => { copy.textContent = 'just visual-update-ref'; }, 1500); };
    const select = () => { const range = document.createRange(); range.selectNodeContents(copy); getSelection().removeAllRanges(); getSelection().addRange(range); };
    if (navigator.clipboard) navigator.clipboard.writeText('just visual-update-ref').then(done, select);
    else select();
  });
  header.append(el('p', { class: 'hint' }, 'If the change is intended, run ', copy));
}

const filters = [['all', 'All', total], ['fail', 'Failed', run.failed], ['pass', 'Passed', run.passed]];
let filter = run.failed > 0 ? 'fail' : 'all';
const filterBar = el('div', { class: 'group filter', role: 'group', 'aria-label': 'Show cases' });
const filterButtons = filters.map(([key, label, count]) => {
  const button = el('button', { text: label + ' (' + count + ')', 'aria-pressed': String(key === filter) });
  button.addEventListener('click', () => { filter = key; applyFilter(); });
  filterBar.append(button);
  return button;
});
header.append(filterBar);

// ---- Cases ----

const main = document.getElementById('cases');
const cards = [];
const sorted = run.cases.map((c, i) => ({ c, i })).sort((a, b) =>
  (a.c.status === b.c.status ? 0 : a.c.status === 'fail' ? -1 : 1) || (a.c.name < b.c.name ? -1 : a.c.name > b.c.name ? 1 : 0));
for (const { c, i } of sorted) {
  const card = el('details', { class: 'case' },
    el('summary', {},
      el('span', { class: 'badge ' + c.status, text: c.status === 'pass' ? 'PASS' : 'FAIL' }),
      el('span', { class: 'name', text: c.name }),
      el('span', { class: 'facts', text: facts(c) })));
  let viewer = null;
  card.addEventListener('toggle', () => {
    if (card.open && !viewer) viewer = new Viewer(card, c, images[i]);
    if (viewer) viewer.visible(card.open);
  });
  card.open = true;
  cards.push({ card, c });
  main.append(card);
}
if (total === 0) main.append(el('p', { class: 'meta', text: 'No case was compared.' }));

function applyFilter() {
  filterButtons.forEach((button, k) => button.setAttribute('aria-pressed', String(filters[k][0] === filter)));
  for (const { card, c } of cards) card.hidden = filter !== 'all' && c.status !== filter;
}
applyFilter();

// ---- Viewer: one per opened card ----

const TABS = [['side', 'Side by side'], ['swipe', 'Swipe'], ['toggle', 'Toggle'], ['diff', 'Diff']];
const ZOOMS = [0, 1, 2, 4, 8]; // 0 is Fit; the others are device pixels per image pixel.
const viewers = [];
let activeViewer = null;

function loadPixels(src) {
  return new Promise((resolve) => {
    if (!src) { resolve(null); return; }
    const img = new Image();
    img.onload = () => {
      const canvas = document.createElement('canvas');
      canvas.width = img.naturalWidth;
      canvas.height = img.naturalHeight;
      const context = canvas.getContext('2d', { willReadFrequently: true });
      context.drawImage(img, 0, 0);
      try { resolve(context.getImageData(0, 0, canvas.width, canvas.height)); } catch (error) { resolve(null); }
    };
    img.onerror = () => resolve(null);
    img.src = src;
  });
}

// Compares the bytes of two images of one size: the rectangle around the pixels that differ, the
// largest difference, and the heatmap the Diff pane shows (white where the bytes match, red where they
// differ, redder for a larger difference). Every byte difference counts, also below odiff's threshold:
// this shows where and how much, it is no verdict and is never shown as a count of pixels. Null when
// either image is missing or the sizes differ.
function compareBytes(a, b) {
  if (!a || !b || a.width !== b.width || a.height !== b.height) return null;
  const w = a.width, h = a.height, n = w * h;
  const p = a.data, q = b.data;
  const delta = new Uint8Array(n);
  let max = 0, x0 = w, y0 = h, x1 = -1, y1 = -1;
  for (let y = 0, i = 0; y < h; ++y) {
    for (let x = 0; x < w; ++x, ++i) {
      const k = i * 4;
      const d = Math.max(Math.abs(p[k] - q[k]), Math.abs(p[k + 1] - q[k + 1]), Math.abs(p[k + 2] - q[k + 2]), Math.abs(p[k + 3] - q[k + 3]));
      if (d === 0) continue;
      delta[i] = d;
      if (d > max) max = d;
      if (x < x0) x0 = x;
      if (x > x1) x1 = x;
      if (y < y0) y0 = y;
      y1 = y;
    }
  }
  // The strength of the red follows the square root of the difference out of 255, from 0.15 up: one
  // scale for every case, so a small difference looks small, and a difference of 1 is still visible.
  const canvas = document.createElement('canvas');
  canvas.width = w;
  canvas.height = h;
  const context = canvas.getContext('2d');
  const heat = context.createImageData(w, h);
  for (let i = 0; i < n; ++i) {
    const d = delta[i];
    const g = d === 0 ? 255 : Math.round(255 * (1 - (0.15 + 0.85 * Math.sqrt(d / 255))));
    heat.data[i * 4] = 255;
    heat.data[i * 4 + 1] = g;
    heat.data[i * 4 + 2] = g;
    heat.data[i * 4 + 3] = 255;
  }
  context.putImageData(heat, 0, 0);
  return { max, box: x1 < 0 ? null : { x: x0, y: y0, w: x1 - x0 + 1, h: y1 - y0 + 1 }, src: canvas.toDataURL('image/png') };
}

class Viewer {
  constructor(card, c, srcs) {
    this.c = c;
    // The diff pane's image is the heatmap compareBytes makes once both images are decoded.
    this.srcs = Object.assign({}, srcs, { diff: null });
    this.diffLabel = 'Diff';
    this.diffWaiting = true;
    // Each image is drawn at its own size from the same top-left corner (a layout case has two sizes);
    // the view frames the larger.
    this.sizes = {
      baseline: { w: c.width || 1, h: c.height || 1 },
      actual: c.reason === 'layout' ? { w: c.actualWidth || 1, h: c.actualHeight || 1 } : { w: c.width || 1, h: c.height || 1 },
    };
    this.sizes.diff = this.sizes.baseline;
    this.w = Math.max(this.sizes.baseline.w, this.sizes.actual.w);
    this.h = Math.max(this.sizes.baseline.h, this.sizes.actual.h);
    this.view = { fit: true, s: 1, cx: this.w / 2, cy: this.h / 2 };
    this.tab = 'side';
    this.swipe = 0.5;
    this.showActual = false;
    this.mark = null;
    this.box = null;
    this.flickerTimer = 0;
    this.panes = [];

    this.tabButtons = TABS.map(([key, label]) => {
      const button = el('button', { text: label, 'aria-pressed': String(key === this.tab) });
      button.addEventListener('click', () => this.setTab(key));
      return button;
    });
    this.zoomButtons = ZOOMS.map((z) => {
      const button = el('button', { text: z === 0 ? 'Fit' : z + '×', title: z === 0 ? 'Fit the image to the view' : z + ' screen pixels per image pixel' });
      button.addEventListener('click', () => this.setZoom(z));
      return button;
    });
    this.gotoButton = el('button', { text: 'Go to difference', disabled: '', title: 'Comparing the pixels…' });
    this.gotoButton.addEventListener('click', () => this.gotoDifference());
    this.flicker = el('input', { type: 'checkbox' });
    this.flicker.addEventListener('change', () => this.setFlicker(this.flicker.checked));
    this.flickerLabel = el('label', { hidden: '' }, this.flicker, 'Flicker');
    this.stage = el('div', { class: 'stage' });
    this.stage.addEventListener('pointerenter', () => { activeViewer = this; });
    this.readout = el('div', { class: 'readout mono', text: 'Hover an image to read its pixels.' });
    const paths = el('dl', { class: 'paths mono' });
    for (const [label, value] of [['baseline', c.baseline], ['actual', c.actual || c.missing], ['diff', c.diff]])
      if (value) paths.append(el('dt', { text: label }), el('dd', { text: value }));

    card.append(el('div', { class: 'body' },
      el('div', { class: 'toolbar' },
        el('div', { class: 'group', role: 'group', 'aria-label': 'View' }, ...this.tabButtons),
        el('div', { class: 'group', role: 'group', 'aria-label': 'Zoom' }, ...this.zoomButtons),
        this.gotoButton, this.flickerLabel),
      this.stage, this.readout, paths));

    this.pixels = { baseline: null, actual: null };
    Promise.all([loadPixels(srcs.baseline), loadPixels(srcs.actual)]).then(([baseline, actual]) => {
      this.pixels = { baseline, actual };
      const bytes = compareBytes(baseline, actual);
      this.box = bytes && bytes.box;
      this.diffWaiting = false;
      if (bytes) {
        this.srcs.diff = bytes.src;
        this.diffLabel = bytes.max === 0 ? 'Diff: identical'
          : 'Diff: redder is larger, up to ' + bytes.max + ' of 255 here' + (c.reason === 'identical' ? ', all below the threshold' : '');
      }
      if (this.tab === 'side' || this.tab === 'diff') this.setTab(this.tab);
      if (this.box) {
        this.gotoButton.disabled = false;
        this.gotoButton.title = 'Zoom to the pixels whose values differ';
      } else {
        this.gotoButton.title = baseline && actual && baseline.width === actual.width && baseline.height === actual.height
          ? 'No pixel differs' : 'The images cannot be compared pixel by pixel';
      }
    });

    viewers.push(this);
    this.setTab('side');
  }

  visible(open) {
    if (!open) this.setFlicker(false);
    else requestAnimationFrame(() => this.layout());
  }

  setTab(tab) {
    this.tab = tab;
    this.tabButtons.forEach((button, k) => button.setAttribute('aria-pressed', String(TABS[k][0] === tab)));
    this.flickerLabel.hidden = tab !== 'toggle';
    if (tab !== 'toggle') this.setFlicker(false);
    this.stage.replaceChildren();
    this.stage.className = 'stage' + (tab === 'side' ? ' side' : '');
    this.panes = [];
    const c = this.c;
    const noActual = c.reason === 'missing' ? 'No capture' : 'No actual image';
    const noDiff = this.diffWaiting ? 'Comparing the pixels\u2026' : c.reason === 'layout' ? 'No diff image: the sizes differ'
      : c.reason === 'missing' ? 'No diff image: no capture' : 'No diff image';
    if (tab === 'side') {
      this.addPane([{ image: 'baseline', label: 'Baseline' }]);
      this.addPane([{ image: 'actual', label: 'Actual', empty: noActual }]);
      this.addPane([{ image: 'diff', label: this.diffLabel, empty: noDiff }]);
    } else if (tab === 'swipe') {
      this.addPane([{ image: 'baseline', label: 'Baseline' }, { image: 'actual', label: 'Actual', empty: noActual, clip: true }]);
    } else if (tab === 'toggle') {
      this.addPane([{ image: 'baseline', label: 'Baseline' }, { image: 'actual', label: 'Actual', empty: noActual, toggled: true }]);
    } else {
      this.addPane([{ image: 'diff', label: this.diffLabel, empty: noDiff }]);
    }
    requestAnimationFrame(() => this.layout());
  }

  addPane(layers) {
    const node = el('div', { class: 'pane' });
    node.style.aspectRatio = this.w + ' / ' + this.h;
    const pane = { node, layers: [], labels: [], clip: null, divider: null, markNode: el('div', { class: 'mark', hidden: '' }), toggles: [] };
    for (const layer of layers) {
      let parent = node;
      if (layer.clip) {
        pane.clip = el('div', { class: 'clip' });
        node.append(pane.clip);
        parent = pane.clip;
      }
      const src = this.srcs[layer.image];
      const empty = src ? null : el('div', { class: 'empty', text: layer.empty || 'No image' });
      const img = src ? el('img', { class: 'layer', src: src, alt: layer.label, draggable: 'false' }) : null;
      if (img) {
        img.style.width = this.sizes[layer.image].w + 'px';
        img.style.height = this.sizes[layer.image].h + 'px';
      }
      const label = el('div', { class: 'label' + (layer.clip ? ' right' : ''), text: layer.label });
      parent.append(img || empty);
      if (this.tab !== 'toggle') parent.append(label);
      if (img) pane.layers.push(img);
      if (this.tab === 'toggle') pane.toggles.push({ img, empty, label: layer.label, actual: !!layer.toggled });
      if (layer.clip && empty) pane.clip.style.clipPath = 'none';
    }
    if (this.tab === 'toggle') {
      pane.toggleLabel = el('div', { class: 'label' });
      node.append(pane.toggleLabel);
    }
    if (pane.clip) {
      pane.divider = el('div', { class: 'divider', title: 'Drag to move the boundary' });
      node.append(pane.divider);
      pane.divider.addEventListener('pointerdown', (event) => {
        event.stopPropagation();
        pane.divider.setPointerCapture(event.pointerId);
        const move = (e) => {
          const r = node.getBoundingClientRect();
          this.swipe = Math.min(1, Math.max(0, (e.clientX - r.left) / r.width));
          this.layout();
        };
        const up = () => { pane.divider.removeEventListener('pointermove', move); pane.divider.removeEventListener('pointerup', up); pane.divider.removeEventListener('pointercancel', up); };
        pane.divider.addEventListener('pointermove', move);
        pane.divider.addEventListener('pointerup', up);
        pane.divider.addEventListener('pointercancel', up);
      });
    }
    node.append(pane.markNode);
    this.attachInput(pane);
    this.stage.append(node);
    this.panes.push(pane);
  }

  fitScale(pane) {
    const r = pane.node.getBoundingClientRect();
    return Math.min(r.width / this.w, r.height / this.h) || 1;
  }

  // Scale (CSS pixels per image pixel) and the image point at the pane's center. The image never gets
  // smaller than the pane: a zoom at or below Fit is Fit, centered. The zoom is shared by panes of
  // different sizes (Side by side and the single views), so this holds for each pane on its own.
  transform(pane) {
    const fit = this.fitScale(pane);
    if (this.view.fit || this.view.s <= fit) return { s: fit, cx: this.w / 2, cy: this.h / 2 };
    return { s: this.view.s, cx: this.view.cx, cy: this.view.cy };
  }

  // Leaves Fit for an explicit zoom with the same picture, so a drag or the wheel starts from what is shown.
  unfit(pane) {
    if (!this.view.fit) return;
    this.view = { fit: false, s: this.fitScale(pane || this.panes[0]), cx: this.w / 2, cy: this.h / 2 };
  }

  layout() {
    for (const pane of this.panes) {
      const r = pane.node.getBoundingClientRect();
      const { s, cx, cy } = this.transform(pane);
      const tx = r.width / 2 - cx * s, ty = r.height / 2 - cy * s;
      pane.node.classList.toggle('pixelated', s * devicePixelRatio >= 1.5);
      for (const img of pane.layers) img.style.transform = 'translate(' + tx + 'px, ' + ty + 'px) scale(' + s + ')';
      if (pane.clip) {
        const x = this.swipe * r.width;
        if (pane.clip.style.clipPath !== 'none') pane.clip.style.clipPath = 'inset(0 0 0 ' + x + 'px)';
        pane.divider.style.left = x + 'px';
      }
      if (pane.toggleLabel) {
        for (const t of pane.toggles) {
          const shown = t.actual === this.showActual;
          if (t.img) t.img.hidden = !shown;
          if (t.empty) t.empty.hidden = !shown;
          if (shown) pane.toggleLabel.textContent = t.label + ' (click or Space to switch)';
        }
      }
      if (this.mark) {
        pane.markNode.hidden = false;
        pane.markNode.style.left = (tx + this.mark.x * s - 1) + 'px';
        pane.markNode.style.top = (ty + this.mark.y * s - 1) + 'px';
        pane.markNode.style.width = (this.mark.w * s + 2) + 'px';
        pane.markNode.style.height = (this.mark.h * s + 2) + 'px';
      } else {
        pane.markNode.hidden = true;
      }
    }
  }

  setZoom(z) {
    if (z === 0) {
      this.view.fit = true;
      this.mark = null;
    } else {
      this.unfit();
      this.view.s = z / devicePixelRatio;
    }
    this.layout();
  }

  gotoDifference() {
    if (!this.box || this.panes.length === 0) return;
    const r = this.panes[0].node.getBoundingClientRect();
    const b = this.box, pad = 8;
    const s = Math.min(r.width / (b.w + 2 * pad), r.height / (b.h + 2 * pad), 32 / devicePixelRatio);
    this.view = { fit: false, s: Math.max(s, this.fitScale(this.panes[0])), cx: b.x + b.w / 2, cy: b.y + b.h / 2 };
    this.mark = b;
    this.layout();
  }

  toggle() {
    this.showActual = !this.showActual;
    this.layout();
  }

  setFlicker(on) {
    clearInterval(this.flickerTimer);
    this.flickerTimer = 0;
    this.flicker.checked = on;
    if (on) this.flickerTimer = setInterval(() => this.toggle(), 500);
  }

  imagePoint(pane, event) {
    const r = pane.node.getBoundingClientRect();
    const { s, cx, cy } = this.transform(pane);
    return { x: cx + (event.clientX - r.left - r.width / 2) / s, y: cy + (event.clientY - r.top - r.height / 2) / s };
  }

  attachInput(pane) {
    const node = pane.node;
    let drag = null;
    node.addEventListener('pointerdown', (event) => {
      if (event.button !== 0) return;
      node.setPointerCapture(event.pointerId);
      drag = { x: event.clientX, y: event.clientY, moved: false, start: null };
    });
    node.addEventListener('pointermove', (event) => {
      this.read(pane, event);
      if (!drag) return;
      const dx = event.clientX - drag.x, dy = event.clientY - drag.y;
      if (!drag.moved && Math.hypot(dx, dy) < 4) return;
      if (!drag.moved) {
        drag.moved = true;
        this.unfit(pane);
        drag.start = { cx: this.view.cx, cy: this.view.cy };
        node.classList.add('dragging');
      }
      this.view.cx = drag.start.cx - dx / this.view.s;
      this.view.cy = drag.start.cy - dy / this.view.s;
      this.layout();
    });
    const end = (event) => {
      if (drag && !drag.moved && event.type === 'pointerup' && this.tab === 'toggle') this.toggle();
      drag = null;
      node.classList.remove('dragging');
    };
    node.addEventListener('pointerup', end);
    node.addEventListener('pointercancel', end);
    node.addEventListener('pointerleave', () => { if (!drag) this.readout.textContent = 'Hover an image to read its pixels.'; });
    node.addEventListener('wheel', (event) => {
      event.preventDefault();
      const r = node.getBoundingClientRect();
      const p = this.imagePoint(pane, event);
      this.unfit(pane);
      const lines = event.deltaMode === 1 ? 33 : event.deltaMode === 2 ? r.height : 1;
      const fit = this.fitScale(pane);
      const s = Math.min(64 / devicePixelRatio, Math.max(fit, this.view.s * Math.exp(-event.deltaY * lines * 0.0015)));
      if (s <= fit) {
        this.view.fit = true;
        this.mark = null;
        this.layout();
        this.read(pane, event);
        return;
      }
      this.view.s = s;
      this.view.cx = p.x - (event.clientX - r.left - r.width / 2) / s;
      this.view.cy = p.y - (event.clientY - r.top - r.height / 2) / s;
      this.layout();
      this.read(pane, event);
    }, { passive: false });
  }

  read(pane, event) {
    const p = this.imagePoint(pane, event);
    const x = Math.floor(p.x), y = Math.floor(p.y);
    const at = (image) => {
      if (!image || x < 0 || y < 0 || x >= image.width || y >= image.height) return null;
      const i = (y * image.width + x) * 4;
      return Array.from(image.data.subarray(i, i + 4));
    };
    const a = at(this.pixels.baseline), b = at(this.pixels.actual);
    if (!a && !b) {
      this.readout.textContent = 'x ' + x + '  y ' + y + '  outside the image';
      return;
    }
    const pad = (v) => String(v).padStart(3, ' ');
    const rgba = (v) => (v ? v.map(pad).join(' ') : '  -   -   -   -');
    const delta = a && b ? b.map((v, k) => v - a[k]) : null;
    this.readout.replaceChildren(
      'x ' + String(x).padStart(4) + '  y ' + String(y).padStart(4) + '   ',
      el('b', { text: 'baseline' }), ' ' + rgba(a) + '   ',
      el('b', { text: 'actual' }), ' ' + rgba(b) + '   ',
      el('b', { text: 'difference' }), ' ' + (delta ? delta.map((v) => (v > 0 ? '+' + v : String(v)).padStart(4)).join(' ') : ' -'));
  }
}

document.addEventListener('keydown', (event) => {
  if (event.key !== ' ' || !activeViewer || activeViewer.tab !== 'toggle') return;
  const target = event.target;
  if (target && (target.tagName === 'INPUT' || target.tagName === 'BUTTON' || target.tagName === 'SUMMARY')) return;
  event.preventDefault();
  activeViewer.toggle();
});
window.addEventListener('resize', () => { for (const viewer of viewers) viewer.layout(); });
`;

// JSON inside a <script> element: '<' is escaped so no string in it can close the element.
function scriptJson(value) {
    return JSON.stringify(value).replace(/</g, '\\u003c');
}

function dataUri(file) {
    if (!file || !fs.existsSync(file))
        return null;
    return `data:image/png;base64,${fs.readFileSync(file).toString('base64')}`;
}

// Writes report.json and report.html into dir and returns report.html's absolute path.
export function writeReport(dir, run) {
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, 'report.json'), `${JSON.stringify(run, null, 2)}\n`);
    // odiff's diff image stays on disk (its path is listed); the page draws its own heatmap instead.
    const images = run.cases.map((c) => ({ baseline: dataUri(c.baseline), actual: dataUri(c.actual) }));
    const html = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Visual Test Report</title>
<style>${STYLE}</style>
</head>
<body>
<header id="header"></header>
<main id="cases"></main>
<script type="application/json" id="run">${scriptJson(run)}</script>
<script type="application/json" id="images">${scriptJson(images)}</script>
<script>${SCRIPT}</script>
</body>
</html>
`;
    const file = path.join(dir, 'report.html');
    fs.writeFileSync(file, html);
    return file;
}

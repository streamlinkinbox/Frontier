import './ui/styles.css';
import { PRESETS, PRESET_GROUPS, TreeParams, cloneParams, SHAPE_NAMES, Shape, scatterFor, trunkBaseRadius, rootReach, isGrass, isDesert } from './tree/params';
import { GrassParams, Inflorescence, INFLORESCENCE_NAMES, HEAD_BRANCH_NAMES, grassHeight, grassHabit } from './plant/grassParams';
import { Viewer, DEFAULT_VIEW, ViewerSettings, DisplayMode } from './viewer/scene';
import { el, icon, button, section, slider, select, check, stepper, levelsHeader, levelRow, fmtInt, fmtCompact } from './ui/controls';
import type { WorkerRequest, WorkerResponse, GenerateResponse } from './worker/tree.worker';
import { LeafMesh } from './tree/mesh';
import { GpuBuffers } from './tree/export';
import { ObstacleKind, ObstacleSpec, makeObstacle, scatterObstacles } from './env/environment';
import { Random } from './core/random';

const APP_NAME = 'Frontier';

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------

let params: TreeParams = cloneParams(PRESETS[0]);
const view: ViewerSettings = { ...DEFAULT_VIEW };
let lastResult: GenerateResponse | null = null;
let busy = false;
let pending = false;
let reqId = 0;

const worker = new Worker(new URL('./worker/tree.worker.ts', import.meta.url), { type: 'module' });

// -----------------------------------------------------------------------------
// Layout: three floating cards – Library · Viewport · Inspector
// -----------------------------------------------------------------------------

const app = document.getElementById('app')!;
app.append(el('div', 'bg'));

// ---- Library (left) ---------------------------------------------------------

const library = el('aside', 'card library');
{
  const head = el('div', 'card-head');
  const h1 = el('h1');
  h1.append(el('span', 'dot green'), document.createTextNode('Library'));
  head.append(h1, el('span', 'badge', APP_NAME + ' · species'));
  library.append(head);
}
const libTiles = el('div', 'tiles');
const tileSpecies = el('div', 'tile');
const tileFaces = el('div', 'tile');
libTiles.append(tileSpecies, tileFaces);
library.append(libTiles);

const searchRow = el('div', 'search');
searchRow.append(icon('search'));
const searchInput = el('input');
searchInput.placeholder = 'Search species…';
searchInput.spellcheck = false;
searchRow.append(searchInput);
library.append(searchRow);

const libTree = el('div', 'tree');
library.append(libTree);

const libFoot = el('div', 'card-foot');
library.append(libFoot);

// ---- Viewport (centre) ------------------------------------------------------

const viewport = el('main', 'card viewport');
const canvas = el('canvas');
viewport.append(canvas);

const vtop = el('div', 'vtop');
const modeSeg = el('div', 'seg');
const MODES: { id: DisplayMode; label: string }[] = [
  { id: 'shaded', label: 'Shaded' },
  { id: 'matcap', label: 'Clay' },
  { id: 'wireframe', label: 'Wire' },
  { id: 'levels', label: 'Levels' },
  { id: 'junctions', label: 'Junctions' },
  { id: 'wind', label: 'Wind' },
];
const modeButtons = new Map<DisplayMode, HTMLButtonElement>();
for (const m of MODES) {
  const b = el('button', m.id === view.mode ? 'on' : '', m.label);
  b.addEventListener('click', () => {
    view.mode = m.id;
    syncView();
  });
  modeButtons.set(m.id, b);
  modeSeg.append(b);
}
const toggleSeg = el('div', 'seg');
const wireToggle = el('button');
wireToggle.append(icon('quad'), el('span', '', 'Quads'));
wireToggle.title = 'Quad overlay';
wireToggle.addEventListener('click', () => {
  view.showWire = !view.showWire;
  syncView();
});
const windToggle = el('button');
windToggle.append(icon('wind'), el('span', '', 'Wind'));
windToggle.title = 'Animate wind';
windToggle.addEventListener('click', () => {
  view.windEnabled = !view.windEnabled;
  syncView();
});
const leavesToggle = el('button');
leavesToggle.append(icon('leaf'), el('span', '', 'Leaves'));
leavesToggle.title = 'Leaf cards';
leavesToggle.addEventListener('click', () => {
  view.showLeaves = !view.showLeaves;
  syncView();
});
toggleSeg.append(wireToggle, leavesToggle, windToggle);
const actionSeg = el('div', 'seg');
const frameBtn = el('button');
frameBtn.append(icon('frame'), el('span', '', 'Frame'));
frameBtn.title = 'Frame the tree · F';
const shotBtn = el('button');
shotBtn.append(icon('camera'));
shotBtn.title = 'Save a PNG of the viewport';
actionSeg.append(frameBtn, shotBtn);
const exportSeg = el('div', 'seg');
const exportObj = el('button');
exportObj.append(icon('download'), el('span', '', 'OBJ'));
const exportGlb = el('button');
exportGlb.append(icon('download'), el('span', '', 'GLB'));
exportSeg.append(exportObj, exportGlb);
vtop.append(modeSeg, el('span', 'spacer'), toggleSeg, actionSeg, exportSeg);
viewport.append(vtop);

const axisHud = el('div', 'axis-hud');
const camPill = el('div', 'pill');
const sizePill = el('div', 'pill');
axisHud.append(sizePill, camPill);
viewport.append(axisHud);

const vtitle = el('div', 'vtitle');
const vtitleName = el('span', 'n');
const vtitleMeta = el('span', 'm');
vtitle.append(vtitleName, vtitleMeta);
viewport.append(vtitle);

const hud = el('div', 'hud');
const navPill = el('div', 'nav glass keysbar');
const keys = el('div', 'keys');
for (const [k, l] of [
  ['LMB', 'orbit'],
  ['RMB', 'pan'],
  ['F', 'frame'],
  ['B', 'base'],
  ['R', 'reseed'],
]) {
  keys.append(el('span', 'k', k), el('span', 'kl', l));
}
navPill.append(keys);
const status = el('div', 'nav glass status');
const statusDot = el('span', 'sdot');
const statusText = el('span', '', 'Idle');
status.append(statusDot, statusText);
hud.append(status, navPill);
viewport.append(hud);

const legend = el('div', 'legend');
viewport.append(legend);

const toast = el('div', 'toast');
viewport.append(toast);

const errBar = el('div', 'errbar');
viewport.append(errBar);

// ---- Inspector (right) ------------------------------------------------------

const panel = el('aside', 'card inspector');
const inspectorBadge = el('span', 'badge', 'tree · welded');
{
  const head = el('div', 'card-head');
  const h1 = el('h1');
  h1.append(el('span', 'dot orange'), document.createTextNode('Inspector'));
  head.append(h1, inspectorBadge);
  panel.append(head);
}
const hero = el('div', 'hero');
panel.append(hero);
const presence = el('div', 'presence');
panel.append(presence);

const tabs = el('div', 'tabs');
const tabBotany = el('button', 'on', 'Botany');
const tabGrass = el('button', '', 'Grass');
const tabRoots = el('button', '', 'Roots');
const tabMesh = el('button', '', 'Mesh');
const tabView = el('button', '', 'View');
const tabReport = el('button', '', 'Topology');
tabs.append(tabBotany, tabGrass, tabRoots, tabMesh, tabView, tabReport);
const scroll = el('div', 'insp');
panel.append(tabs, scroll);

const pages = {
  botany: el('div'),
  grass: el('div'),
  roots: el('div'),
  mesh: el('div'),
  view: el('div'),
  report: el('div'),
};
scroll.append(pages.botany, pages.grass, pages.roots, pages.mesh, pages.view, pages.report);
const tabMap: [HTMLButtonElement, HTMLElement][] = [
  [tabBotany, pages.botany],
  [tabGrass, pages.grass],
  [tabRoots, pages.roots],
  [tabMesh, pages.mesh],
  [tabView, pages.view],
  [tabReport, pages.report],
];
/** Tabs that only apply to one plant kind. */
const treeTabs = new Set<HTMLButtonElement>([tabBotany, tabRoots, tabMesh]);
const grassTabs = new Set<HTMLButtonElement>([tabGrass]);
function showTab(btn: HTMLButtonElement): void {
  for (const [b, p] of tabMap) {
    b.classList.toggle('on', b === btn);
    p.style.display = b === btn ? '' : 'none';
  }
  scroll.scrollTop = 0;
}
for (const [btn] of tabMap) btn.addEventListener('click', () => showTab(btn));
/** Show the tabs of the current plant kind; switch to its first tab if the active one does not apply. */
function syncTabs(): void {
  const grass = isGrass(params);
  for (const [b] of tabMap) {
    const hide = grass ? treeTabs.has(b) : grassTabs.has(b);
    b.style.display = hide ? 'none' : '';
  }
  const active = tabMap.find(([b]) => b.classList.contains('on'))?.[0];
  if (!active || active.style.display === 'none') showTab(grass ? tabGrass : tabBotany);
  inspectorBadge.textContent = grass ? 'grass · welded' : isDesert(params) ? 'desert · welded' : 'tree · welded';
}
showTab(tabBotany);

const cmdline = el('div', 'cmdline');
const cmdPrompt = el('span', 'prompt', '›');
const cmdInput = el('input');
cmdInput.placeholder = 'command · seed 42 · preset oak · mode wire · export glb · help';
cmdInput.spellcheck = false;
cmdline.append(cmdPrompt, cmdInput);
panel.append(cmdline);

app.append(library, viewport, panel);

// -----------------------------------------------------------------------------
// Viewer
// -----------------------------------------------------------------------------

const viewer = new Viewer(canvas);
const narrowObserver = new ResizeObserver(() => viewport.classList.toggle('narrow', viewport.clientWidth < 860));
narrowObserver.observe(viewport);
let camTick = 0;
viewer.onFrame(() => {
  if (++camTick % 6 !== 0) return;
  const az = ((viewer.controls.getAzimuthalAngle() * 180) / Math.PI + 360) % 360;
  const elv = 90 - (viewer.controls.getPolarAngle() * 180) / Math.PI;
  camPill.textContent = `persp · az ${az.toFixed(0)}° · el ${elv.toFixed(0)}° · d ${viewer.controls.getDistance().toFixed(1)} m · ${viewer.fps.toFixed(0)} fps`;
});
frameBtn.addEventListener('click', () => viewer.frame());
window.addEventListener('keydown', (e) => {
  if ((e.target as HTMLElement).tagName === 'INPUT' || (e.target as HTMLElement).tagName === 'SELECT') return;
  if (e.key === 'f' || e.key === 'F') viewer.frame();
  if (e.key === 'b' || e.key === 'B') viewer.frameBase(baseReach());
  if (e.key === 'r' || e.key === 'R') randomSeed();
  if (e.key === 'w' || e.key === 'W') {
    view.showWire = !view.showWire;
    syncView();
  }
  if (e.key === 'Escape' && viewer.isPlacing) setPlacing(false);
  if ((e.key === 'Delete' || e.key === 'Backspace') && selectedObstacle >= 0) removeObstacle(selectedObstacle);
});

// Object placement: drag existing objects on the ground, click to add in placement mode.
let selectedObstacle = -1;
let addKind: ObstacleKind = 'rock';
viewer.onPlacement((ev) => {
  const obs = params.environment.obstacles;
  if (ev.phase === 'add') {
    const rng = new Random((Date.now() ^ (obs.length * 7919)) >>> 0);
    const sc = params.environment.scatter;
    const size = rng.range(sc.minSize, sc.maxSize);
    obs.push(makeObstacle(addKind, ev.x, ev.z, size, rng, sc.burial, sc.roughness));
    selectedObstacle = obs.length - 1;
    setPlacing(false);
    buildRootsPage();
    scheduleGenerate(true);
    return;
  }
  const o = obs[ev.index];
  if (!o) return;
  if (ev.phase === 'move') {
    viewer.offsetObstacle(ev.index, ev.x - o.x, ev.z - o.z);
    return;
  }
  o.x = ev.x;
  o.z = ev.z;
  selectedObstacle = ev.index;
  buildRootsPage();
  scheduleGenerate(true);
});

/** Radius of interest around the base: root reach for trees, the crown for grasses. */
function baseReach(): number {
  return isGrass(params) && params.grass ? Math.max(0.2, params.grass.crownRadius * 3) : rootReach(params);
}

function setPlacing(on: boolean): void {
  viewer.setPlacing(on);
  for (const r of placementRefreshers) r();
  if (on) showToast(`Click on the ground to place a ${addKind === 'rock' ? 'rock' : 'block'} · Esc to cancel`);
}

function removeObstacle(i: number): void {
  params.environment.obstacles.splice(i, 1);
  selectedObstacle = -1;
  buildRootsPage();
  scheduleGenerate(true);
}
const placementRefreshers: (() => void)[] = [];
shotBtn.addEventListener('click', () => {
  const url = viewer.screenshot();
  download(url, `${slug(params.name)}_${params.seed}.png`);
});

function syncView(): void {
  viewer.applySettings(view);
  if (lastResult) refreshPresence();
  for (const [id, b] of modeButtons) b.classList.toggle('on', id === view.mode);
  wireToggle.classList.toggle('on', view.showWire);
  windToggle.classList.toggle('on', view.windEnabled);
  leavesToggle.classList.toggle('on', view.showLeaves);
  legend.style.display = view.mode === 'levels' || view.mode === 'junctions' || view.mode === 'wind' ? '' : 'none';
  legend.innerHTML = '';
  if (view.mode === 'levels') {
    const items: [string, string][] = isGrass(params)
      ? [
          ['Crown', '#8a5a3c'],
          ['Culms', '#c98b4b'],
          ['Blades', '#6fa16b'],
          ['Heads', '#5aa1c9'],
        ]
      : [
          ['Trunk', '#8a5a3c'],
          ['Limbs', '#c98b4b'],
          ['Branches', '#6fa16b'],
          ['Twigs', '#5aa1c9'],
          ['Roots', '#b86b5c'],
        ];
    for (const [n, c] of items) {
      const s = el('span');
      const i = el('i');
      i.style.background = c;
      s.append(i, document.createTextNode(n));
      legend.append(s);
    }
  } else if (view.mode === 'junctions') {
    const s = el('span');
    const i = el('i');
    i.style.background = '#f26b2e';
    s.append(i, document.createTextNode('Welded junction loops (collars & crotches)'));
    legend.append(s);
  } else if (view.mode === 'wind') {
    const s = el('span');
    s.append(document.createTextNode('Limb bend weight: '));
    const i0 = el('i');
    i0.style.background = '#1a338c';
    const i1 = el('i');
    i1.style.background = '#33bf8c';
    const i2 = el('i');
    i2.style.background = '#fad940';
    s.append(i0, document.createTextNode('rigid '), i1, document.createTextNode('mid '), i2, document.createTextNode('flexible'));
    legend.append(s);
  }
  for (const r of viewRefreshers) r();
}

// -----------------------------------------------------------------------------
// Generation
// -----------------------------------------------------------------------------

let debounceTimer = 0;
/** Re-frame the camera once the next tree arrives (species change: sizes differ wildly). */
let frameOnNext = false;

function scheduleGenerate(immediate = false): void {
  window.clearTimeout(debounceTimer);
  debounceTimer = window.setTimeout(() => generate(), immediate ? 0 : 140);
}

function generate(): void {
  if (busy) {
    pending = true;
    return;
  }
  busy = true;
  setStatus('busy', 'Generating…');
  const id = ++reqId;
  const msg: WorkerRequest = { type: 'generate', id, params: cloneParams(params) };
  worker.postMessage(msg);
}

worker.onmessage = (ev: MessageEvent<WorkerResponse>) => {
  const msg = ev.data;
  if (msg.type === 'error') {
    busy = false;
    setStatus('err', 'Generation failed');
    errBar.textContent = msg.message;
    errBar.classList.add('show');
    console.error(msg.message);
    return;
  }
  if (msg.type === 'exported') {
    const blob = new Blob([msg.data], { type: msg.format === 'obj' ? 'text/plain' : 'model/gltf-binary' });
    download(URL.createObjectURL(blob), `${slug(params.name)}_${params.seed}.${msg.format}`);
    showToast(`${msg.format.toUpperCase()} exported`);
    exportObj.disabled = exportGlb.disabled = false;
    return;
  }
  if (msg.type === 'generated') {
    busy = false;
    if (msg.id !== reqId) {
      if (pending) {
        pending = false;
        generate();
      }
      return;
    }
    lastResult = msg;
    const buffers: GpuBuffers = msg.buffers;
    const leaves = new LeafMesh();
    leaves.positions = Array.from(msg.leaves.positions);
    leaves.normals = Array.from(msg.leaves.normals);
    leaves.uvs = Array.from(msg.leaves.uvs);
    leaves.wind = Array.from(msg.leaves.wind);
    leaves.pivots = Array.from(msg.leaves.pivots);
    leaves.indices = Array.from(msg.leaves.indices);
    const firstTree = !hasTree;
    viewer.setTree(buffers, leaves, msg.summary.height, params.name);
    viewer.setObstacles(msg.obstacles);
    if (firstTree || frameOnNext) viewer.frame();
    frameOnNext = false;
    hasTree = true;
    updateReport(msg);
    if (pending) {
      pending = false;
      generate();
    }
  }
};
let hasTree = false;

function setStatus(kind: 'ok' | 'warn' | 'err' | 'busy' | '', text: string): void {
  statusDot.className = 'sdot ' + kind;
  statusText.textContent = text;
  if (kind !== 'err') errBar.classList.remove('show');
}

// -----------------------------------------------------------------------------
// Report
// -----------------------------------------------------------------------------

function kvSpan(ico: string, label: string, value: string, cls = ''): HTMLElement {
  const sp = el('span', 'kv' + (cls ? ' ' + cls : ''));
  sp.append(icon(ico), document.createTextNode(label + (value ? ' ' : '')));
  if (value) sp.append(el('b', '', value));
  return sp;
}

function refreshPresence(): void {
  const cells = presence.querySelectorAll<HTMLElement>('.pcell');
  const states = [view.showLeaves, view.showObstacles, view.showGrid, view.windEnabled];
  cells.forEach((c, i) => c.classList.toggle('on', states[i]));
}

function groupOf(name: string): string {
  return PRESET_GROUPS.find((g) => g.names.includes(name))?.label ?? 'Custom';
}

function groupIcon(name: string): string {
  const g = groupOf(name);
  if (g.startsWith('Grasses')) return GROUP_ICON[g] ?? 'grass';
  if (g.includes('conifer') || g.includes('Rocky')) return 'conifer';
  if (g === 'Jungle') return 'palm';
  if (g === 'Savanna') return 'acacia';
  if (g === 'Desert') return 'yucca';
  return 'tree';
}

/** Tiny bar chart of stems per level (trunk → twigs → roots) in the hero card. */
function drawSpark(cv: HTMLCanvasElement, levels: number[], roots: number, labels = ['L0', 'L1', 'L2', 'L3', 'R']): void {
  const w = 96;
  const h = 56;
  const dpr = Math.min(2, window.devicePixelRatio || 1);
  cv.width = w * dpr;
  cv.height = h * dpr;
  const ctx = cv.getContext('2d');
  if (!ctx) return;
  ctx.scale(dpr, dpr);
  const vals = labels.length > levels.length ? [...levels, roots] : levels;
  const max = Math.max(1, ...vals);
  const colours = ['#8a5a3c', '#c98b4b', '#6fa16b', '#5aa1c9', '#b86b5c'];
  const bw = 12;
  const gap = (w - bw * vals.length) / (vals.length - 1);
  vals.forEach((v, i) => {
    const bh = Math.max(2, (Math.log1p(v) / Math.log1p(max)) * (h - 14));
    const x = i * (bw + gap);
    ctx.fillStyle = colours[i];
    ctx.globalAlpha = 0.9;
    roundRect(ctx, x, h - 12 - bh, bw, bh, 3);
    ctx.fill();
    ctx.globalAlpha = 0.5;
    ctx.fillStyle = '#fff';
    ctx.font = '8px JetBrains Mono Variable, monospace';
    ctx.textAlign = 'center';
    ctx.fillText(labels[i], x + bw / 2, h - 2);
  });
  ctx.globalAlpha = 1;
}

function roundRect(ctx: CanvasRenderingContext2D, x: number, y: number, w: number, h: number, r: number): void {
  const rr = Math.min(r, w / 2, h / 2);
  ctx.beginPath();
  ctx.moveTo(x + rr, y);
  ctx.arcTo(x + w, y, x + w, y + h, rr);
  ctx.arcTo(x + w, y + h, x, y + h, rr);
  ctx.arcTo(x, y + h, x, y, rr);
  ctx.arcTo(x, y, x + w, y, rr);
  ctx.closePath();
}

function updateReport(r: GenerateResponse): void {
  const rep = r.report;
  const ok = rep.closed && rep.manifold && rep.components === 1 && rep.genus === 0;
  setStatus(ok ? 'ok' : 'warn', ok ? 'Closed manifold · genus 0' : 'Topology issues – see report');

  // Hero card: species, height, mesh census.
  hero.innerHTML = '';
  const cTop = el('div', 'c-top');
  const ico = el('span', 'ico');
  ico.append(icon(groupIcon(params.name)));
  const heroText = el('div', 't');
  heroText.append(el('span', 'n', params.name), el('span', 'm', `${groupOf(params.name).toLowerCase()} · seed ${params.seed} · ${r.timings.total.toFixed(0)} ms`));
  cTop.append(ico, heroText);
  const grass = isGrass(params);
  const big = el('div', 'big');
  const hSmall = r.summary.height < 1;
  big.append(el('span', 'v', hSmall ? (r.summary.height * 100).toFixed(0) : r.summary.height.toFixed(grass ? 2 : 1)), el('span', 'u', hSmall ? 'cm' : 'm'), el('span', 'lab', 'height'));
  const side = el('div', 'side');
  const cell = (k: string, v: string, cls = ''): void => {
    const d = el('div');
    d.append(el('span', 'k', k), el('span', 'v2' + (cls ? ' ' + cls : ''), v));
    side.append(d);
  };
  cell('faces', fmtCompact(rep.faces));
  cell('quads', `${(rep.quadRatio * 100).toFixed(rep.quadRatio > 0.9995 ? 0 : 1)} %`);
  if (grass && r.grass) {
    cell('blades', fmtInt(r.grass.blades));
    cell('culms', fmtInt(r.grass.culms));
    cell('organs', fmtCompact(r.grass.organs));
  } else {
    cell('stems', fmtInt(r.summary.stems));
    cell('roots', `${r.summary.primaryRoots} / ${r.summary.rootStems}`);
    cell('leaves', fmtCompact(r.summary.leaves));
  }
  cell('genus', ok ? '0 · closed' : `${rep.genus} · ${rep.components} part${rep.components === 1 ? '' : 's'}`, ok ? 'ok' : 'warn');
  const spark = el('canvas', 'spark');
  hero.append(cTop, big, side, spark);
  if (grass) drawSpark(spark, r.summary.stemsPerLevel.slice(0, 4), 0, ['crown', 'culm', 'leaf', 'head']);
  else drawSpark(spark, r.summary.stemsPerLevel.slice(0, 4), r.summary.rootStems);

  // Presence grid: the four toggles that decide what the viewport shows.
  presence.innerHTML = '';
  const pcell = (name: string, label: string, on: boolean, click: () => void): void => {
    const c = el('button', 'pcell' + (on ? ' on' : ''));
    c.append(icon(name), el('span', '', label));
    c.addEventListener('click', click);
    presence.append(c);
  };
  pcell('leaf', 'Leaves', view.showLeaves, () => {
    view.showLeaves = !view.showLeaves;
    syncView();
  });
  pcell('ground', 'Objects', view.showObstacles, () => {
    view.showObstacles = !view.showObstacles;
    syncView();
  });
  pcell('grid', 'Grid', view.showGrid, () => {
    view.showGrid = !view.showGrid;
    syncView();
  });
  pcell('wind', 'Wind', view.windEnabled, () => {
    view.windEnabled = !view.windEnabled;
    syncView();
  });

  vtitleName.textContent = params.name;
  vtitleMeta.textContent = grass
    ? `seed ${params.seed} · ${fmtInt(r.summary.stems)} organs welded${r.stats.droppedStems ? ` · ${r.stats.droppedStems} dropped` : ''}`
    : `seed ${params.seed} · ${fmtInt(r.summary.stems)} stems · ${r.summary.obstacles} object${r.summary.obstacles === 1 ? '' : 's'}${r.stats.droppedStems ? ` · ${r.stats.droppedStems} dropped` : ''}`;
  sizePill.textContent = `${fmtInt(rep.faces)} F · ${fmtInt(rep.vertices)} V · ${fmtInt(r.buffers.index.length / 3)} tris`;
  tileFaces.innerHTML = '';
  tileFaces.className = 'tile ' + (ok ? 'ok' : 'warn');
  tileFaces.append(el('span', 'l', 'Faces'), el('span', 's', ok ? 'genus 0' : `${rep.boundaryEdges + rep.nonManifoldEdges} bad edges`), el('b', '', fmtCompact(rep.faces)));
  libFoot.innerHTML = '';
  const census = el('div', 'census');
  const parts = grass ? r.summary.stemsPerLevel.slice(0, 4) : [...r.summary.stemsPerLevel.slice(0, 4), r.summary.rootStems];
  const total = Math.max(1, parts.reduce((a, b) => a + b, 0));
  const colours = ['#8a5a3c', '#c98b4b', '#6fa16b', '#5aa1c9', '#b86b5c'];
  parts.forEach((n, i) => {
    const bar = el('i');
    bar.style.width = `${(n / total) * 100}%`;
    bar.style.background = colours[i];
    census.append(bar);
  });
  const footRow = el('div', 'foot-row');
  if (grass) footRow.append(kvSpan('grass', 'organs', fmtInt(r.summary.stems)), kvSpan('layers', 'junctions', fmtInt(r.stats.junctions)), el('span', 'sp'), kvSpan('check', ok ? 'clean' : 'issues', '', ok ? 'ok' : 'warn'));
  else footRow.append(kvSpan('tree', 'stems', fmtInt(r.summary.stems)), kvSpan('layers', 'levels', String(params.botany.levels)), el('span', 'sp'), kvSpan('check', ok ? 'clean' : 'issues', '', ok ? 'ok' : 'warn'));
  libFoot.append(census, footRow);

  const page = pages.report;
  page.innerHTML = '';
  const s1 = section('Manifold checks', page, { hint: ok ? 'all pass' : 'issues' });
  const list = el('div', 'list');
  const item = (label: string, pass: boolean, detail: string): void => {
    const it = el('div', 'li' + (pass ? '' : ' warn'));
    it.append(el('span', 'dot'), el('span', 't', label), el('span', 'm', detail));
    list.append(it);
  };
  item('Closed surface (no boundary edges)', rep.boundaryEdges === 0, `${rep.boundaryEdges} open`);
  item('2-manifold (every edge has 2 faces)', rep.nonManifoldEdges === 0, `${rep.nonManifoldEdges} bad`);
  item('Consistent winding', rep.inconsistentEdges === 0, `${rep.inconsistentEdges} flipped`);
  item('Single connected piece', rep.components === 1, `${rep.components} part${rep.components === 1 ? '' : 's'}`);
  item('Genus 0  (V − E + F = 2)', rep.eulerCharacteristic === 2, `χ = ${rep.eulerCharacteristic}`);
  item('No degenerate faces', rep.degenerateFaces === 0, `${rep.degenerateFaces}`);
  item('No isolated vertices', rep.isolatedVertices === 0, `${rep.isolatedVertices}`);
  s1.append(list);

  const s2 = section('Mesh statistics', page, { hint: `${fmtCompact(rep.faces)} faces` });
  const grid = el('div', 'kvgrid');
  const add = (k: string, v: string, cls = ''): void => {
    grid.append(el('span', '', k), el('b', cls, v));
  };
  add('Vertices', fmtInt(rep.vertices));
  add('Edges', fmtInt(rep.edges));
  add('Faces', fmtInt(rep.faces));
  add('Quads', `${fmtInt(rep.quads)}  (${(rep.quadRatio * 100).toFixed(2)} %)`, rep.quadRatio > 0.99 ? 'ok' : 'warn');
  add('Triangles', fmtInt(rep.tris), rep.tris === 0 ? 'ok' : '');
  add('Triangulated', fmtInt(r.buffers.index.length / 3));
  add('Poles (valence ≠ 4)', `${fmtInt(rep.poles)}  (${((rep.poles / Math.max(1, rep.vertices)) * 100).toFixed(1)} %)`);
  add('Min edge length', `${(rep.minEdgeLength * 1000).toFixed(1)} mm`);
  if (grass && r.grass) {
    add('Organs welded', `${fmtInt(r.grass.organs)}  (${r.grass.junctions} windows)`);
    add('Blades · culms', `${fmtInt(r.grass.blades)} · ${fmtInt(r.grass.culms)}`);
    add('Culm leaves', fmtInt(Math.max(0, r.grass.perLevel[2] - r.grass.blades)));
    add('Head parts', fmtInt(r.grass.perLevel[3]));
    add('Organs dropped', fmtInt(r.grass.dropped), r.grass.dropped ? 'warn' : 'ok');
  } else {
    add('Junctions welded', fmtInt(r.stats.junctions));
    add('Forks (Y crotches)', fmtInt(r.stats.forks));
    add('Root stems welded', `${fmtInt(r.stats.rootStems)}  (${r.summary.primaryRoots} primary)`);
    add('Stems dropped', fmtInt(r.stats.droppedStems), r.stats.droppedStems ? 'warn' : 'ok');
    add('Roots dropped', fmtInt(r.stats.droppedRoots), r.stats.droppedRoots ? 'warn' : 'ok');
  }
  s2.append(grid);

  const s3 = section('Valence histogram', page, { collapsed: true });
  const bars = el('div', 'bars');
  const entries = Object.entries(rep.valenceHistogram)
    .map(([k, v]) => [Number(k), v] as [number, number])
    .sort((a, b) => a[0] - b[0]);
  const maxV = Math.max(1, ...entries.map((e) => e[1]));
  for (const [val, count] of entries) {
    bars.append(el('span', '', `v${val}`));
    const bar = el('div', 'bar');
    const fill = el('i');
    fill.style.width = `${(count / maxV) * 100}%`;
    bar.append(fill);
    bars.append(bar, el('span', '', fmtInt(count)));
  }
  s3.append(bars);

  const t = r.timings;
  const s4 = section('Timings', page, { collapsed: true, hint: `${t.total.toFixed(0)} ms` });
  const tg = el('div', 'kvgrid');
  const addT = (k: string, v: number): void => {
    tg.append(el('span', '', k), el('b', '', `${v.toFixed(1)} ms`));
  };
  if (!grass) {
    addT('Skeleton', t.skeleton);
    addT('Roots', t.roots);
  }
  addT('Welded mesh', t.mesh);
  addT('Validation', t.validate);
  addT('GPU buffers', t.buffers);
  addT('Total', t.total);
  s4.append(tg);

  if (r.stats.droppedStems > 0) {
    const s5 = section('Dropped stems', page, { collapsed: true, hint: String(r.stats.droppedStems) });
    const dg = el('div', 'kvgrid');
    for (const [k, v] of Object.entries(r.stats.dropReasons)) dg.append(el('span', '', k), el('b', '', fmtInt(v)));
    s5.append(dg, el('p', 'note', grass ? 'An organ is dropped when no window can be opened for it on its parent without overlapping another one: a shorter head or culm, or more sides, makes room.' : 'A stem is dropped when no window can be opened for it on the parent without overlapping another junction. Increase trunk radial segments or rings per segment to make room.'));
  }
}

// -----------------------------------------------------------------------------
// Library (species browser)
// -----------------------------------------------------------------------------

const GROUP_ICON: Record<string, string> = {
  Oaks: 'tree',
  'Forest · broadleaf': 'tree',
  'Forest · conifers': 'conifer',
  Jungle: 'palm',
  Savanna: 'acacia',
  Desert: 'yucca',
  'Rocky terrain': 'mountain',
  'Grasses · turf & meadow': 'grass',
  'Grasses · tussock': 'tussock',
  'Grasses · cereals & reeds': 'wheat',
};
const GROUP_ACCENT: Record<string, string> = {
  Oaks: '#c98b4b',
  'Forest · broadleaf': '#6fa16b',
  'Forest · conifers': '#4fd8e0',
  Jungle: '#34c759',
  Savanna: '#e5d33a',
  Desert: '#ffb454',
  'Rocky terrain': '#b48cff',
  'Grasses · turf & meadow': '#8fd15a',
  'Grasses · tussock': '#c9d36a',
  'Grasses · cereals & reeds': '#e2b95b',
};
const closedGroups = new Set<string>();

function speciesMeta(name: string): string {
  const p = PRESETS.find((x) => x.name === name);
  if (!p) return '';
  if (isGrass(p) && p.grass) {
    const g = p.grass;
    const h = grassHeight(g);
    return `${grassHabit(g)} · ${h < 1 ? `${Math.round(h * 100)} cm` : `${h.toFixed(1)} m`} · ${g.head === 'none' ? 'vegetative' : g.head}`;
  }
  const b = p.botany;
  return `${SHAPE_NAMES[b.shape].toLowerCase()} · ${b.scale} m · L${b.levels}`;
}

function buildLibrary(): void {
  libTree.innerHTML = '';
  const q = searchInput.value.trim().toLowerCase();
  const grouped = new Set<string>();
  const groups = [...PRESET_GROUPS.map((g) => ({ label: g.label, names: g.names.filter((n) => PRESETS.some((p) => p.name === n)) }))];
  for (const g of groups) g.names.forEach((n) => grouped.add(n));
  const rest = PRESETS.map((p) => p.name).filter((n) => !grouped.has(n));
  if (rest.length) groups.push({ label: 'Other', names: rest });
  let shown = 0;
  for (const g of groups) {
    const names = g.names.filter((n) => !q || n.toLowerCase().includes(q) || g.label.toLowerCase().includes(q));
    if (names.length === 0) continue;
    const grp = el('div', 'grp' + (closedGroups.has(g.label) && !q ? ' closed' : ''));
    const gh = el('div', 'grp-h');
    gh.append(icon('caret', 'i car'), el('span', '', g.label), el('span', 'cnt', String(names.length)));
    gh.addEventListener('click', () => {
      if (closedGroups.has(g.label)) closedGroups.delete(g.label);
      else closedGroups.add(g.label);
      grp.classList.toggle('closed');
    });
    const kids = el('div', 'kids');
    for (const name of names) {
      const row = el('div', 'row' + (name === params.name ? ' sel' : ''));
      row.style.setProperty('--acc', GROUP_ACCENT[g.label] ?? '#9aa4b2');
      const ico = el('span', 'ico');
      ico.append(icon(GROUP_ICON[g.label] ?? 'tree'));
      const txt = el('div', 'txt');
      txt.append(el('span', 'name', name), el('span', 'meta', speciesMeta(name)));
      row.append(ico, txt);
      if (name === params.name) {
        const st = el('span', 'st ok');
        st.append(icon('check'));
        row.append(st);
      }
      row.addEventListener('click', () => {
        if (name !== params.name) selectPreset(name);
      });
      kids.append(row);
      shown++;
    }
    grp.append(gh, kids);
    libTree.append(grp);
  }
  if (shown === 0) {
    const empty = el('div', 'empty');
    empty.append(el('b', '', 'No species match'), document.createTextNode('Try a family name such as “oak”, “conifer”, “savanna” or “grass”.'));
    libTree.append(empty);
  }
  tileSpecies.innerHTML = '';
  tileSpecies.className = 'tile ok';
  tileSpecies.append(el('span', 'l', 'Species'), el('span', 's', `${groups.length} families`), el('b', '', String(PRESETS.length)));
}
searchInput.addEventListener('input', () => buildLibrary());
searchInput.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') {
    searchInput.value = '';
    buildLibrary();
    searchInput.blur();
  }
});

// -----------------------------------------------------------------------------
// Botany page
// -----------------------------------------------------------------------------

const refreshers: (() => void)[] = [];
const viewRefreshers: (() => void)[] = [];
const onChange = (): void => {
  scheduleGenerate();
  for (const r of refreshers) r();
};

function buildBotanyPage(): void {
  const page = pages.botany;
  page.innerHTML = '';
  const b = params.botany;

  const sSeed = section('Seed', page, { hint: groupOf(params.name).toLowerCase() });
  const seedRow = el('div', 'ctl rowctl seed');
  seedRow.append(el('span', 'lab', 'Seed'));
  const stp = el('div', 'stp');
  const seedInput = el('input', 'n');
  seedInput.type = 'text';
  seedInput.spellcheck = false;
  seedInput.value = String(params.seed);
  seedInput.addEventListener('change', () => {
    const v = parseInt(seedInput.value, 10);
    if (!Number.isNaN(v)) {
      params.seed = v;
      scheduleGenerate(true);
    }
  });
  seedInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') seedInput.blur();
  });
  const seedDec = el('button', 'b', '−');
  seedDec.addEventListener('click', () => setSeed(params.seed - 1));
  const seedInc = el('button', 'b', '+');
  seedInc.addEventListener('click', () => setSeed(params.seed + 1));
  stp.append(seedDec, seedInput, seedInc);
  const rnd = button('Randomise', () => randomSeed(), { icon: 'dice', kbd: 'R', cls: 'slim' });
  seedRow.append(stp, rnd);
  sSeed.append(seedRow);
  const resetRow = el('div', 'actions');
  resetRow.append(
    button('Reset species', () => {
      const seed = params.seed;
      params = cloneParams(PRESETS.find((p) => p.name === params.name) ?? PRESETS[0]);
      params.seed = seed;
      selectedObstacle = -1;
      rebuildPages();
      scheduleGenerate(true);
    }, { icon: 'reset', title: 'Restore the preset values of the current species' }),
  );
  sSeed.append(resetRow);
  refreshers.push(() => {
    seedInput.value = String(params.seed);
  });

  const sGlobal = section('Form', page);
  select(
    sGlobal,
    'Crown shape',
    (Object.keys(SHAPE_NAMES) as unknown as Shape[]).map((k) => ({ value: Number(k) as Shape, label: SHAPE_NAMES[Number(k) as Shape] })),
    () => params.botany.shape,
    (v) => (params.botany.shape = v),
    onChange,
  );
  slider(sGlobal, 'Height', () => params.botany.scale, (v) => (params.botany.scale = v), { min: 1, max: 80, step: 0.5, unit: 'm' }, onChange);
  slider(sGlobal, 'Height variation', () => params.botany.scaleV, (v) => (params.botany.scaleV = v), { min: 0, max: 20, step: 0.5, unit: 'm' }, onChange);
  stepper(sGlobal, 'Levels', () => params.botany.levels, (v) => (params.botany.levels = v), { min: 1, max: 4, integer: true, title: 'Recursion depth: trunk, limbs, branches, twigs' }, () => {
    onChange();
    for (const r of refreshers) r();
  });
  slider(sGlobal, 'Trunk ratio', () => params.botany.ratio, (v) => (params.botany.ratio = v), { min: 0.005, max: 0.16, step: 0.001, title: 'Trunk radius as a fraction of trunk length' }, onChange);
  slider(sGlobal, 'Ratio power', () => params.botany.ratioPower, (v) => (params.botany.ratioPower = v), { min: 0.5, max: 3, step: 0.05, title: 'How fast child radius falls off with relative length' }, onChange);
  slider(sGlobal, 'Root flare', () => params.botany.flare, (v) => (params.botany.flare = v), { min: 0, max: 3, step: 0.05 }, onChange);
  stepper(sGlobal, 'Base splits', () => params.botany.baseSplits, (v) => (params.botany.baseSplits = v), { min: -3, max: 4, integer: true, title: 'Clones at the base of the trunk (negative = up to N, random)' }, onChange);
  slider(sGlobal, 'Attraction up', () => params.botany.attractionUp, (v) => (params.botany.attractionUp = v), { min: -4, max: 4, step: 0.1, title: 'Vertical tropism. Negative droops (willow), positive reaches up.' }, onChange);
  slider(sGlobal, 'Canopy flattening', () => params.botany.flatten, (v) => (params.botany.flatten = v), { min: 0, max: 1, step: 0.05, title: 'Pulls the fine growth towards the horizontal: flat-topped canopies (acacia).' }, onChange);
  slider(sGlobal, 'Culm nodes', () => params.botany.nodeSwell, (v) => (params.botany.nodeSwell = v), { min: 0, max: 0.4, step: 0.01, title: 'Periodic swelling of the trunk (bamboo). 0 = none.' }, onChange);
  slider(sGlobal, 'Node spacing', () => params.botany.nodeSpacing, (v) => (params.botany.nodeSpacing = v), { min: 0.02, max: 0.2, step: 0.005, title: 'Distance between culm nodes as a fraction of the trunk length.' }, onChange);

  const enabled = (): number => params.botany.levels;
  const lvl = (key: keyof typeof b): (() => number[]) => () => params.botany[key] as number[];
  const setLvl = (key: keyof typeof b) => (i: number, v: number) => {
    (params.botany[key] as number[])[i] = v;
  };

  const sLen = section('Length & shape per level', page);
  levelsHeader(sLen);
  levelRow(sLen, 'Length', lvl('length'), setLvl('length'), { step: 0.01, min: 0, enabled, title: 'Relative length (trunk: fraction of height)' }, onChange);
  levelRow(sLen, 'Length var.', lvl('lengthV'), setLvl('lengthV'), { step: 0.01, min: 0, enabled }, onChange);
  levelRow(sLen, 'Base size', lvl('baseSize'), setLvl('baseSize'), { step: 0.01, min: 0, max: 0.95, enabled, title: 'Bare fraction at the base of each stem' }, onChange);
  levelRow(sLen, 'Taper', lvl('taper'), setLvl('taper'), { step: 0.01, min: 0, max: 3, enabled, title: '0–1 linear taper, 1–2 spherical end, 2–3 periodic' }, onChange);
  levelRow(sLen, 'Radius mod.', lvl('radiusMod'), setLvl('radiusMod'), { step: 0.01, min: 0.05, max: 2, enabled }, onChange);

  const sCurve = section('Curvature per level', page);
  levelsHeader(sCurve);
  levelRow(sCurve, 'Segments', lvl('curveRes'), setLvl('curveRes'), { step: 1, min: 1, max: 24, integer: true, enabled }, onChange);
  levelRow(sCurve, 'Curve °', lvl('curve'), setLvl('curve'), { step: 1, enabled }, onChange);
  levelRow(sCurve, 'Curve back °', lvl('curveBack'), setLvl('curveBack'), { step: 1, enabled, title: 'S-curve: second half bends by this instead' }, onChange);
  levelRow(sCurve, 'Curve var. °', lvl('curveV'), setLvl('curveV'), { step: 1, min: 0, enabled }, onChange);
  levelRow(sCurve, 'Bend var. °', lvl('bendV'), setLvl('bendV'), { step: 1, min: 0, enabled, title: 'Random side-to-side wobble' }, onChange);

  const sSplit = section('Splitting (forks)', page);
  levelsHeader(sSplit);
  levelRow(sSplit, 'Splits / seg', lvl('segSplits'), setLvl('segSplits'), { step: 0.05, min: 0, max: 3, enabled, title: 'Clones per segment; fractional values are distributed' }, onChange);
  levelRow(sSplit, 'Split angle °', lvl('splitAngle'), setLvl('splitAngle'), { step: 1, min: 0, max: 120, enabled }, onChange);
  levelRow(sSplit, 'Split var. °', lvl('splitAngleV'), setLvl('splitAngleV'), { step: 1, min: 0, enabled }, onChange);

  const sBranch = section('Branching', page);
  levelsHeader(sBranch, ['—', 'Limbs', 'Branches', 'Twigs']);
  levelRow(sBranch, 'Count', lvl('branches'), setLvl('branches'), { step: 1, min: 0, max: 400, integer: true, enabled, title: 'Maximum children per parent stem' }, onChange);
  levelRow(sBranch, 'Down angle °', lvl('downAngle'), setLvl('downAngle'), { step: 1, min: 0, max: 170, enabled, title: 'Angle from the parent axis at emergence' }, onChange);
  levelRow(sBranch, 'Down var. °', lvl('downAngleV'), setLvl('downAngleV'), { step: 1, enabled, title: 'Negative: varies with position along the parent' }, onChange);
  levelRow(sBranch, 'Rotate °', lvl('rotate'), setLvl('rotate'), { step: 1, enabled, title: 'Phyllotactic angle between successive children (negative = alternate)' }, onChange);
  levelRow(sBranch, 'Rotate var. °', lvl('rotateV'), setLvl('rotateV'), { step: 1, min: 0, enabled }, onChange);
  levelRow(sBranch, 'Distribution', lvl('branchDist'), setLvl('branchDist'), { step: 0.5, min: 0, max: 8, enabled, title: '0 alternate · 1 opposite · >1 whorled' }, onChange);

  const sLeaf = section('Leaves', page, { collapsed: true, hint: 'proxy cards' });
  slider(sLeaf, 'Leaves per twig', () => params.botany.leaves, (v) => (params.botany.leaves = Math.round(v)), { min: 0, max: 300, step: 1 }, onChange);
  slider(sLeaf, 'Leaf length', () => params.botany.leafScale, (v) => (params.botany.leafScale = v), { min: 0.02, max: 0.6, step: 0.01, unit: 'm' }, onChange);
  slider(sLeaf, 'Leaf width', () => params.botany.leafScaleX, (v) => (params.botany.leafScaleX = v), { min: 0.1, max: 2, step: 0.05, unit: '×' }, onChange);
  slider(sLeaf, 'Tip tuft', () => params.botany.leafTuft, (v) => (params.botany.leafTuft = v), { min: 0.05, max: 1, step: 0.05, title: 'Fraction of each terminal stem, from the tip, that carries leaves. 1 = the whole stem; small values gather the leaves into a tip rosette (yucca) or brush (foxtail pine).' }, onChange);
}


// -----------------------------------------------------------------------------
// Grass page
// -----------------------------------------------------------------------------

function buildGrassPage(): void {
  const page = pages.grass;
  page.innerHTML = '';
  if (!isGrass(params) || !params.grass) return;
  const G = (): GrassParams => params.grass!;
  const g = G();

  const sSeed = section('Seed', page, { hint: groupOf(params.name).toLowerCase() });
  const seedRow = el('div', 'ctl rowctl seed');
  seedRow.append(el('span', 'lab', 'Seed'));
  const stp = el('div', 'stp');
  const seedInput = el('input', 'n');
  seedInput.type = 'text';
  seedInput.spellcheck = false;
  seedInput.value = String(params.seed);
  seedInput.addEventListener('change', () => {
    const v = parseInt(seedInput.value, 10);
    if (!Number.isNaN(v)) {
      params.seed = v;
      scheduleGenerate(true);
    }
  });
  seedInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') seedInput.blur();
  });
  const seedDec = el('button', 'b', '−');
  seedDec.addEventListener('click', () => setSeed(params.seed - 1));
  const seedInc = el('button', 'b', '+');
  seedInc.addEventListener('click', () => setSeed(params.seed + 1));
  stp.append(seedDec, seedInput, seedInc);
  seedRow.append(stp, button('Randomise', () => randomSeed(), { icon: 'dice', kbd: 'R', cls: 'slim' }));
  sSeed.append(seedRow);
  const resetRow = el('div', 'actions');
  resetRow.append(
    button('Reset species', () => {
      const seed = params.seed;
      params = cloneParams(PRESETS.find((p) => p.name === params.name) ?? PRESETS[0]);
      params.seed = seed;
      rebuildPages();
      scheduleGenerate(true);
    }, { icon: 'reset', title: 'Restore the preset values of the current species' }),
  );
  sSeed.append(resetRow);
  refreshers.push(() => {
    seedInput.value = String(params.seed);
  });

  const cm = { format: (v: number) => (v * 100).toFixed(v < 0.1 ? 1 : 0), unit: 'cm' };
  const mm = { format: (v: number) => (v * 1000).toFixed(v < 0.01 ? 1 : 0), unit: 'mm' };

  const sCrown = section('Crown & habit', page);
  slider(sCrown, 'Crown radius', () => G().crownRadius, (v) => (G().crownRadius = v), { min: 0.02, max: 0.8, step: 0.005, ...cm, title: 'Radius of the tussock base at ground level' }, onChange);
  slider(sCrown, 'Crown height', () => G().crownHeight, (v) => (G().crownHeight = v), { min: 0, max: 0.4, step: 0.005, ...cm, title: 'Dome of the tussock above the soil' }, onChange);
  slider(sCrown, 'Sunk into soil', () => G().crownSink, (v) => (G().crownSink = v), { min: 0, max: 0.1, step: 0.002, ...cm, title: 'Buries the crown so the blades rise straight out of the ground' }, onChange);
  slider(sCrown, 'Dome profile', () => G().crownProfile, (v) => (G().crownProfile = v), { min: 1.2, max: 6, step: 0.1, title: '2 = rounded · higher = flat top with a rounded shoulder' }, onChange);
  slider(sCrown, 'Centre bias', () => G().centreBias, (v) => (G().centreBias = v), { min: -1, max: 1, step: 0.05, title: '−1 tillers in a ring at the rim · 0 even · 1 crowded at the centre' }, onChange);

  const sBlade = section('Leaf blades', page, { hint: `${g.blades} blades` });
  slider(sBlade, 'Blades', () => G().blades, (v) => (G().blades = Math.round(v)), { min: 0, max: 800, step: 1 }, onChange);
  slider(sBlade, 'Length', () => G().bladeLength, (v) => (G().bladeLength = v), { min: 0.02, max: 2.5, step: 0.01, ...cm }, onChange);
  slider(sBlade, 'Length variation', () => G().bladeLengthV, (v) => (G().bladeLengthV = v), { min: 0, max: 0.8, step: 0.02, unit: '±' }, onChange);
  slider(sBlade, 'Width', () => G().bladeWidth, (v) => (G().bladeWidth = v), { min: 0.001, max: 0.06, step: 0.0005, ...mm }, onChange);
  slider(sBlade, 'Thickness', () => G().bladeThickness, (v) => (G().bladeThickness = v), { min: 0.02, max: 0.6, step: 0.01, unit: '× w', title: 'Blade thickness as a fraction of its width' }, onChange);
  slider(sBlade, 'Taper', () => G().bladeTaper, (v) => (G().bladeTaper = v), { min: 0, max: 1, step: 0.05, title: '0 narrows evenly from the base · 1 keeps its width and ends in a fine point' }, onChange);
  slider(sBlade, 'Keel', () => G().keel, (v) => (G().keel = v), { min: 0, max: 1, step: 0.05, title: 'V-fold along the midrib' }, onChange);
  slider(sBlade, 'Rolled', () => G().rolled, (v) => (G().rolled = v), { min: 0, max: 1, step: 0.05, title: '0 flat · 1 rolled into a bristle (fescue, marram)' }, onChange);
  slider(sBlade, 'Twist', () => G().twist, (v) => (G().twist = v), { min: 0, max: 360, step: 5, unit: '°' }, onChange);
  slider(sBlade, 'Twist variation', () => G().twistV, (v) => (G().twistV = v), { min: 0, max: 180, step: 5, unit: '°' }, onChange);
  slider(sBlade, 'Lean', () => G().lean, (v) => (G().lean = v), { min: 0, max: 80, step: 1, unit: '°', title: 'Tilt from the vertical at the rim of the crown; the centre stays upright' }, onChange);
  slider(sBlade, 'Lean variation', () => G().leanV, (v) => (G().leanV = v), { min: 0, max: 40, step: 1, unit: '°' }, onChange);
  slider(sBlade, 'Droop', () => G().droop, (v) => (G().droop = v), { min: 0, max: 180, step: 2, unit: '°', title: 'Arch from base to tip' }, onChange);
  slider(sBlade, 'Droop variation', () => G().droopV, (v) => (G().droopV = v), { min: 0, max: 90, step: 2, unit: '°' }, onChange);
  slider(sBlade, 'Droop profile', () => G().droopPower, (v) => (G().droopPower = v), { min: 1, max: 4, step: 0.1, title: '1 bends evenly · higher keeps the base stiff and lets the tip hang' }, onChange);
  slider(sBlade, 'Waviness', () => G().wave, (v) => (G().wave = v), { min: 0, max: 30, step: 1, unit: '°' }, onChange);

  const sCulm = section('Culms', page, { hint: g.culms ? `${g.culms} culms` : 'none' });
  slider(sCulm, 'Culms', () => G().culms, (v) => (G().culms = Math.round(v)), { min: 0, max: 80, step: 1, title: 'Flowering stems' }, onChange);
  slider(sCulm, 'Height', () => G().culmHeight, (v) => (G().culmHeight = v), { min: 0.05, max: 4, step: 0.01, ...cm }, onChange);
  slider(sCulm, 'Height variation', () => G().culmHeightV, (v) => (G().culmHeightV = v), { min: 0, max: 0.5, step: 0.01, unit: '±' }, onChange);
  slider(sCulm, 'Radius', () => G().culmRadius, (v) => (G().culmRadius = v), { min: 0.0005, max: 0.02, step: 0.0001, ...mm }, onChange);
  slider(sCulm, 'Lean', () => G().culmLean, (v) => (G().culmLean = v), { min: 0, max: 60, step: 1, unit: '°' }, onChange);
  slider(sCulm, 'Lean variation', () => G().culmLeanV, (v) => (G().culmLeanV = v), { min: 0, max: 30, step: 1, unit: '°' }, onChange);
  slider(sCulm, 'Curve', () => G().culmCurve, (v) => (G().culmCurve = v), { min: 0, max: 90, step: 1, unit: '°', title: 'Bend along the culm' }, onChange);
  stepper(sCulm, 'Leaf nodes', () => G().nodes, (v) => (G().nodes = v), { min: 0, max: 12, integer: true }, onChange);
  slider(sCulm, 'Node swelling', () => G().nodeSwell, (v) => (G().nodeSwell = v), { min: 0, max: 0.6, step: 0.01 }, onChange);
  slider(sCulm, 'Culm leaf length', () => G().culmLeafLength, (v) => (G().culmLeafLength = v), { min: 0.1, max: 1.5, step: 0.05, unit: '×', title: 'Relative to the basal blades' }, onChange);
  slider(sCulm, 'Culm leaf width', () => G().culmLeafWidth, (v) => (G().culmLeafWidth = v), { min: 0.3, max: 2, step: 0.05, unit: '×' }, onChange);
  slider(sCulm, 'Culm leaf angle', () => G().culmLeafAngle, (v) => (G().culmLeafAngle = v), { min: 5, max: 90, step: 1, unit: '°' }, onChange);

  const sHead = section('Inflorescence', page, { hint: g.head === 'none' ? 'none' : g.head });
  select(
    sHead,
    'Type',
    (Object.keys(INFLORESCENCE_NAMES) as Inflorescence[]).map((k) => ({ value: k, label: INFLORESCENCE_NAMES[k] })),
    () => G().head,
    (v) => (G().head = v),
    () => {
      onChange();
      buildGrassPage();
    },
  );
  if (g.head !== 'none') {
    const branchName = HEAD_BRANCH_NAMES[g.head];
    const spread = g.head === 'panicle' || g.head === 'plume';
    slider(sHead, 'Head length', () => G().headLength, (v) => (G().headLength = v), { min: 0.01, max: 0.8, step: 0.005, ...cm }, onChange);
    if (!spread) slider(sHead, 'Head width', () => G().headWidth, (v) => (G().headWidth = v), { min: 0.004, max: 0.08, step: 0.001, ...mm }, onChange);
    slider(sHead, 'Nod', () => G().headDroop, (v) => (G().headDroop = v), { min: 0, max: 120, step: 1, unit: '°' }, onChange);
    slider(sHead, branchName, () => G().headBranches, (v) => (G().headBranches = Math.round(v)), { min: 0, max: 400, step: 1 }, onChange);
    if (spread) {
      slider(sHead, 'Branchlet length', () => G().headBranchLength, (v) => (G().headBranchLength = v), { min: 0.05, max: 1.2, step: 0.05, unit: '× head' }, onChange);
      slider(sHead, 'Attach over', () => G().headBranchSpread, (v) => (G().headBranchSpread = v), { min: 0.1, max: 1, step: 0.05, title: 'Fraction of the head, from its base, over which branchlets attach' }, onChange);
      stepper(sHead, 'Secondaries', () => G().headSecondary, (v) => (G().headSecondary = v), { min: 0, max: 8, integer: true, title: 'Secondary branchlets per branchlet' }, onChange);
    }
    slider(sHead, `${branchName.slice(0, -1)} angle`, () => G().headBranchAngle, (v) => (G().headBranchAngle = v), { min: 5, max: 90, step: 1, unit: '°' }, onChange);
    slider(sHead, `${branchName.slice(0, -1)} droop`, () => G().headBranchDroop, (v) => (G().headBranchDroop = v), { min: 0, max: 120, step: 2, unit: '°' }, onChange);
    slider(sHead, `${branchName.slice(0, -1)} radius`, () => G().headBranchRadius, (v) => (G().headBranchRadius = v), { min: 0.0001, max: 0.003, step: 0.00005, format: (v) => (v * 1000).toFixed(2), unit: 'mm' }, onChange);
    if (g.head === 'spike' || g.head === 'panicle') {
      slider(sHead, 'Spikelet', () => G().spikelet, (v) => (G().spikelet = v), { min: 0, max: 0.01, step: 0.0002, format: (v) => (v * 1000).toFixed(1), unit: 'mm' }, onChange);
      slider(sHead, 'Awn', () => G().awn, (v) => (G().awn = v), { min: 0, max: 0.2, step: 0.002, ...cm }, onChange);
    }
  }

  const sRes = section('Resolution', page, { collapsed: true });
  stepper(sRes, 'Blade sides', () => G().bladeSides, (v) => (G().bladeSides = v), { min: 4, max: 8, step: 2, integer: true, title: '4 flat strip · 6 keeled · 8 keeled and wide' }, onChange);
  stepper(sRes, 'Blade rings', () => G().bladeSegments, (v) => (G().bladeSegments = v), { min: 3, max: 24, integer: true }, onChange);
  stepper(sRes, 'Culm sides', () => G().culmSides, (v) => (G().culmSides = v), { min: 6, max: 12, step: 2, integer: true }, onChange);
  stepper(sRes, 'Culm rings', () => G().culmSegments, (v) => (G().culmSegments = v), { min: 4, max: 40, integer: true }, onChange);
  stepper(sRes, 'Branchlet rings', () => G().branchletSegments, (v) => (G().branchletSegments = v), { min: 1, max: 8, integer: true }, onChange);
  stepper(sRes, 'Collar rings', () => G().collarRings, (v) => (G().collarRings = v), { min: 0, max: 3, integer: true, title: 'Intermediate loops between a window and the first ring of the tiller' }, onChange);

  page.append(
    Object.assign(el('p', 'note'), {
      textContent:
        'A grass plant is one closed quad surface, like the trees. The crown is a grid dome; every blade, culm, culm leaf, branchlet, spikelet and awn leaves its parent through a window cut into the parent\'s ring grid and is welded to it with collar loops, so the wind deformation runs continuously from the soil to the tip of every awn.',
    }),
  );
}

// -----------------------------------------------------------------------------
// Roots & site page
// -----------------------------------------------------------------------------

function buildRootsPage(): void {
  const page = pages.roots;
  page.innerHTML = '';
  placementRefreshers.length = 0;
  const R = (): TreeParams['roots'] => params.roots;
  const E = (): TreeParams['environment'] => params.environment;

  const s1 = section('Root system', page, { hint: R().enabled ? `${R().count} primary` : 'off' });
  check(s1, 'Grow roots', () => R().enabled, (v) => (R().enabled = v), onChange);
  stepper(s1, 'Primary roots', () => R().count, (v) => (R().count = v), { min: 1, max: 12, integer: true, title: 'Roots leaving the trunk base; the trunk gets one buttress lobe per root' }, onChange);
  slider(s1, 'Length', () => R().length, (v) => (R().length = v), { min: 0.04, max: 0.6, step: 0.01, title: 'Fraction of the tree height', format: (v) => `${(v * params.botany.scale).toFixed(1)}`, unit: 'm' }, onChange);
  slider(s1, 'Length variation', () => R().lengthV, (v) => (R().lengthV = v), { min: 0, max: 0.3, step: 0.01 }, onChange);
  slider(s1, 'Radius', () => R().radius, (v) => (R().radius = v), { min: 0.1, max: 0.68, step: 0.01, title: 'Fraction of the trunk radius' }, onChange);
  slider(s1, 'Radius variation', () => R().radiusV, (v) => (R().radiusV = v), { min: 0, max: 0.5, step: 0.01 }, onChange);
  slider(s1, 'Taper', () => R().taper, (v) => (R().taper = v), { min: 0.3, max: 2, step: 0.05, title: '<1 stays thick for longer, >1 thins out quickly' }, onChange);
  slider(s1, 'Exit height', () => R().emergeHeight, (v) => (R().emergeHeight = v), { min: 0, max: 2.5, step: 0.05, title: 'Height of the root above the ground where it leaves the trunk, in root radii' }, onChange);
  slider(s1, 'Descent', () => R().descent, (v) => (R().descent = v), { min: 0, max: 60, step: 1, title: 'Initial pitch below the horizontal', unit: '°' }, onChange);
  slider(s1, 'Exposure', () => R().exposure, (v) => (R().exposure = v), { min: 0, max: 1, step: 0.02, title: '0 = flush with the soil · 0.5 = half buried · 1 = lying on top' }, onChange);
  slider(s1, 'Meander', () => R().wander, (v) => (R().wander = v), { min: 0, max: 1.5, step: 0.05 }, onChange);
  slider(s1, 'Dive at', () => R().dive, (v) => (R().dive = v), { min: 0.3, max: 1, step: 0.02, title: 'Fraction of the length after which the tip dives underground' }, onChange);

  const s2 = section('Branching', page);
  slider(s2, 'Fork chance', () => R().forks, (v) => (R().forks = v), { min: 0, max: 1, step: 0.05 }, onChange);
  stepper(s2, 'Laterals per root', () => R().laterals, (v) => (R().laterals = v), { min: 0, max: 6, integer: true }, onChange);
  slider(s2, 'Lateral radius', () => R().lateralRadius, (v) => (R().lateralRadius = v), { min: 0.2, max: 0.68, step: 0.01, title: 'Relative to the parent root' }, onChange);
  slider(s2, 'Lateral length', () => R().lateralLength, (v) => (R().lateralLength = v), { min: 0.1, max: 1.2, step: 0.05, title: 'Relative to the remaining parent length' }, onChange);

  const s3 = section('Response to objects', page);
  slider(s3, 'Attraction', () => R().attraction, (v) => (R().attraction = v), { min: 0, max: 1, step: 0.05, title: 'Pull towards nearby objects (0 = indifferent)' }, onChange);
  slider(s3, 'Grip', () => R().grip, (v) => (R().grip = v), { min: 0, max: 1, step: 0.05, title: 'How deeply the root presses into the object it follows' }, onChange);
  slider(s3, 'Climb height', () => R().climb, (v) => (R().climb = v), { min: 0, max: 12, step: 0.5, title: 'Objects up to this many root diameters tall are climbed over; taller ones are skirted', unit: '⌀' }, onChange);

  const s4 = section('Objects on site', page, { hint: `${E().obstacles.length} placed` });
  const tools = el('div', 'actions');
  const kindSel = el('select', 'dd');
  for (const [v, l] of [
    ['rock', 'Rock'],
    ['block', 'Block'],
  ] as [ObstacleKind, string][]) {
    const o = el('option', '', l);
    o.value = v;
    kindSel.append(o);
  }
  kindSel.value = addKind;
  kindSel.addEventListener('change', () => (addKind = kindSel.value as ObstacleKind));
  const placeBtn = button('Place', () => setPlacing(!viewer.isPlacing), { icon: 'place', title: 'Click on the ground in the viewport to add an object' });
  placementRefreshers.push(() => placeBtn.classList.toggle('primary', viewer.isPlacing));
  const scatterBtn = button('Scatter', () => {
    const sc = E().scatter;
    const next = scatterFor(params, Math.floor(Math.random() * 99999) + 1, sc.count);
    next.blocks = sc.blocks;
    next.burial = sc.burial;
    next.roughness = sc.roughness;
    E().scatter = next;
    E().obstacles = scatterObstacles(next, trunkBaseRadius(params.botany));
    selectedObstacle = -1;
    buildRootsPage();
    scheduleGenerate(true);
  }, { icon: 'scatter', title: 'Replace the objects with a random layout within root reach' });
  const clearBtn = button('Clear', () => {
    E().obstacles = [];
    selectedObstacle = -1;
    buildRootsPage();
    scheduleGenerate(true);
  }, { icon: 'trash', cls: 'danger' });
  tools.append(kindSel, placeBtn, scatterBtn, clearBtn);
  s4.append(tools);
  stepper(s4, 'Scatter count', () => E().scatter.count, (v) => (E().scatter.count = v), { min: 0, max: 12, integer: true }, () => undefined);
  slider(s4, 'Block share', () => E().scatter.blocks, (v) => (E().scatter.blocks = v), { min: 0, max: 1, step: 0.05, title: 'Fraction of scattered objects that are blocks' }, () => undefined);

  const list = el('div', 'obj-list');
  const obs = E().obstacles;
  if (obs.length === 0) list.append(el('p', 'note', 'No objects. Place one in the viewport, or scatter a few – roots grow onto whatever stands within their reach and can be dragged around afterwards.'));
  obs.forEach((o, i) => {
    const row = el('div', 'obj-row' + (i === selectedObstacle ? ' selected' : ''));
    const head = el('div', 'li obj-head');
    const title = el('span', 't', `${o.kind === 'rock' ? 'Rock' : 'Block'} ${i + 1}`);
    const pos = el('span', 'm', `${o.x.toFixed(2)}, ${o.z.toFixed(2)} m`);
    const del = el('button', 'ib');
    del.title = 'Remove';
    del.append(icon('x'));
    del.addEventListener('click', (ev) => {
      ev.stopPropagation();
      removeObstacle(i);
    });
    head.append(el('span', 'dot'), title, pos, del);
    head.addEventListener('click', () => {
      selectedObstacle = selectedObstacle === i ? -1 : i;
      buildRootsPage();
    });
    row.append(head);
    if (i === selectedObstacle) {
      const body = el('div', 'obj-body');
      const set = (fn: (o: ObstacleSpec) => void) => (): void => {
        fn(o);
        pos.textContent = `${o.x.toFixed(2)}, ${o.z.toFixed(2)} m`;
        onChange();
      };
      select(body, 'Type', [{ value: 'rock', label: 'Rock' }, { value: 'block', label: 'Block' }], () => o.kind, (v) => (o.kind = v as ObstacleKind), set(() => undefined));
      slider(body, 'Size', () => o.size, (v) => (o.size = v), { min: 0.1, max: 4, step: 0.05, unit: 'm' }, set(() => undefined));
      slider(body, 'X', () => o.x, (v) => (o.x = v), { min: -15, max: 15, step: 0.05, unit: 'm' }, set(() => undefined));
      slider(body, 'Z', () => o.z, (v) => (o.z = v), { min: -15, max: 15, step: 0.05, unit: 'm' }, set(() => undefined));
      slider(body, 'Rotation', () => (o.yaw * 180) / Math.PI, (v) => (o.yaw = (v * Math.PI) / 180), { min: 0, max: 360, step: 1, unit: '°' }, set(() => undefined));
      slider(body, 'Burial', () => o.burial, (v) => (o.burial = v), { min: 0, max: 0.9, step: 0.02, title: '0 = resting on the ground · 0.5 = half buried' }, set(() => undefined));
      slider(body, 'Roughness', () => o.roughness, (v) => (o.roughness = v), { min: 0, max: 0.5, step: 0.01 }, set(() => undefined));
      slider(body, 'Stretch X', () => o.aspect[0], (v) => (o.aspect[0] = v), { min: 0.4, max: 3, step: 0.05 }, set(() => undefined));
      slider(body, 'Height', () => o.aspect[1], (v) => (o.aspect[1] = v), { min: 0.2, max: 3, step: 0.05 }, set(() => undefined));
      slider(body, 'Stretch Z', () => o.aspect[2], (v) => (o.aspect[2] = v), { min: 0.4, max: 3, step: 0.05 }, set(() => undefined));
      row.append(body);
    }
    list.append(row);
  });
  s4.append(list);

  page.append(
    Object.assign(el('p', 'note'), {
      textContent:
        'Roots are grown by a turtle that senses the ground and the objects around the tree: it climbs onto whatever it can, presses into the surface it follows, wraps around what it cannot climb and dives underground at the tip. Every root is welded into the trunk through a junction window like any branch, so the whole plant stays one closed quad surface. Drag objects in the viewport to move them; press B to frame the base.',
    }),
  );
}

function buildMeshPage(): void {
  const page = pages.mesh;
  page.innerHTML = '';
  const m = (): TreeParams['mesh'] => params.mesh;
  const s1 = section('Resolution', page);
  slider(s1, 'Trunk ring segments', () => m().trunkRadialSegments, (v) => (m().trunkRadialSegments = Math.round(v / 2) * 2), { min: 8, max: 48, step: 2, title: 'Vertices around the trunk. Children derive their ring size from the window they grow out of.', unit: 'v' }, onChange);
  levelsHeader(s1);
  levelRow(s1, 'Rings / segment', () => m().ringsPerSegment, (i, v) => (m().ringsPerSegment[i] = v), { step: 1, min: 1, max: 8, integer: true, enabled: () => params.botany.levels }, onChange);
  slider(s1, 'Root ring segments', () => m().rootRadialSegments, (v) => (m().rootRadialSegments = Math.round(v / 2) * 2), { min: 6, max: 24, step: 2, title: 'Minimum vertices around a primary root where it leaves the trunk', unit: 'v' }, onChange);
  slider(s1, 'Min radius', () => m().minRadius * 1000, (v) => (m().minRadius = v / 1000), { min: 1, max: 30, step: 0.5, unit: 'mm' }, onChange);
  slider(s1, 'Cull below', () => m().cullRadius * 1000, (v) => (m().cullRadius = v / 1000), { min: 0, max: 40, step: 0.5, title: 'Skip stems thinner than this (LOD)', unit: 'mm' }, onChange);
  check(s1, 'Cap stem tips', () => m().capTips, (v) => (m().capTips = v), onChange);

  const s2 = section('Junctions', page);
  slider(s2, 'Collar width', () => m().collarScale, (v) => (m().collarScale = v), { min: 1.0, max: 2.2, step: 0.05, title: 'Window size relative to the child diameter' }, onChange);
  slider(s2, 'Collar length', () => m().collarLength, (v) => (m().collarLength = v), { min: 0.2, max: 2.5, step: 0.05, title: 'Distance of the first full child ring from the parent surface, in child radii' }, onChange);
  stepper(s2, 'Collar rings', () => m().collarRings, (v) => (m().collarRings = v), { min: 0, max: 3, integer: true, title: 'Intermediate edge loops between the window and the first child ring' }, onChange);
  slider(s2, 'Collar fillet', () => m().collarFillet, (v) => (m().collarFillet = v), { min: 0, max: 1, step: 0.05, title: '0 = straight chamfer, 1 = tangent-continuous fillet' }, onChange);
  slider(s2, 'Collar mitre', () => m().collarMitre, (v) => (m().collarMitre = v), { min: 0, max: 0.9, step: 0.05, title: 'Tilt of the first child ring towards the parent surface' }, onChange);
  slider(s2, 'Fork area conservation', () => m().forkRadiusConservation, (v) => (m().forkRadiusConservation = v), { min: 0, max: 1, step: 0.05, title: '0: forks keep the parent radius (Weber–Penn) · 1: cross-section area is conserved (da Vinci rule)' }, onChange);

  const s3 = section('Root buttresses', page);
  stepper(s3, 'Lobes', () => m().rootLobes, (v) => (m().rootLobes = v), { min: 0, max: 9, integer: true, title: 'Used when the root system is disabled; otherwise one lobe per primary root' }, onChange);
  slider(s3, 'Amplitude', () => m().rootLobeAmplitude, (v) => (m().rootLobeAmplitude = v), { min: 0, max: 1, step: 0.02 }, onChange);
  slider(s3, 'Height', () => m().rootLobeHeight, (v) => (m().rootLobeHeight = v), { min: 0.02, max: 0.3, step: 0.01, title: 'Fraction of trunk length over which the lobes fade' }, onChange);

  page.append(
    Object.assign(el('p', 'note'), {
      textContent:
        'The branch system is a single closed quad surface. Side branches grow out of a rectangular window cut into the parent ring grid; forks split the parent ring into arcs joined by a crotch bridge. Nothing is intersected or merged.',
    }),
  );
}

function buildViewPage(): void {
  const page = pages.view;
  page.innerHTML = '';
  const s1 = section('Wind', page);
  viewRefreshers.push(check(s1, 'Enabled', () => view.windEnabled, (v) => (view.windEnabled = v), syncView).refresh);
  viewRefreshers.push(slider(s1, 'Strength', () => view.windStrength, (v) => (view.windStrength = v), { min: 0, max: 3, step: 0.05 }, syncView).refresh);
  viewRefreshers.push(slider(s1, 'Gustiness', () => view.windGust, (v) => (view.windGust = v), { min: 0, max: 1.5, step: 0.05 }, syncView).refresh);
  viewRefreshers.push(slider(s1, 'Direction', () => view.windDirection, (v) => (view.windDirection = v), { min: 0, max: 360, step: 1, unit: '°' }, syncView).refresh);
  viewRefreshers.push(slider(s1, 'Trunk flex', () => view.trunkFlex, (v) => (view.trunkFlex = v), { min: 0, max: 3, step: 0.05 }, syncView).refresh);
  viewRefreshers.push(slider(s1, 'Limb flex', () => view.limbFlex, (v) => (view.limbFlex = v), { min: 0, max: 3, step: 0.05 }, syncView).refresh);
  viewRefreshers.push(slider(s1, 'Detail flutter', () => view.detailFlex, (v) => (view.detailFlex = v), { min: 0, max: 3, step: 0.05 }, syncView).refresh);

  const s2 = section('Display', page);
  viewRefreshers.push(check(s2, 'Quad overlay', () => view.showWire, (v) => (view.showWire = v), syncView).refresh);
  viewRefreshers.push(check(s2, 'Leaf cards', () => view.showLeaves, (v) => (view.showLeaves = v), syncView).refresh);
  viewRefreshers.push(check(s2, 'Grid', () => view.showGrid, (v) => (view.showGrid = v), syncView).refresh);
  viewRefreshers.push(check(s2, 'Site objects', () => view.showObstacles, (v) => (view.showObstacles = v), syncView).refresh);
  viewRefreshers.push(check(s2, 'See-through ground', () => view.xrayGround, (v) => (view.xrayGround = v), syncView).refresh);
  viewRefreshers.push(check(s2, 'Turntable', () => view.autoRotate, (v) => (view.autoRotate = v), syncView).refresh);
  page.append(
    Object.assign(el('p', 'note'), {
      textContent: 'Wind is evaluated in the vertex shader from per-vertex data baked by the generator: normalised height (trunk sway), limb weight + pivot (limb bending) and a detail weight (twig flutter). Because branches and twigs share one welded surface, the deformation is continuous through every junction.',
    }),
  );
}

function setSeed(seed: number): void {
  params.seed = Math.max(0, Math.round(seed));
  for (const r of refreshers) r();
  scheduleGenerate(true);
}

function rebuildPages(): void {
  buildBotanyPage();
  buildGrassPage();
  buildRootsPage();
  buildMeshPage();
  buildLibrary();
  syncTabs();
}

function selectPreset(name: string): void {
  const seed = params.seed;
  params = cloneParams(PRESETS.find((p) => p.name === name) ?? PRESETS[0]);
  params.seed = seed;
  selectedObstacle = -1;
  frameOnNext = true;
  rebuildPages();
  scheduleGenerate(true);
}

function randomSeed(): void {
  params.seed = Math.floor(Math.random() * 99999) + 1;
  for (const r of refreshers) r();
  scheduleGenerate(true);
}

// -----------------------------------------------------------------------------
// Export
// -----------------------------------------------------------------------------

function requestExport(format: 'obj' | 'glb'): void {
  exportObj.disabled = exportGlb.disabled = true;
  showToast(`Building ${format.toUpperCase()}…`);
  const msg: WorkerRequest = { type: 'export', id: ++reqId, params: cloneParams(params), format, includeLeaves: view.showLeaves };
  worker.postMessage(msg);
}
exportObj.addEventListener('click', () => requestExport('obj'));
exportGlb.addEventListener('click', () => requestExport('glb'));

function download(url: string, filename: string): void {
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.append(a);
  a.click();
  a.remove();
  if (url.startsWith('blob:')) setTimeout(() => URL.revokeObjectURL(url), 5000);
}

let toastTimer = 0;
function showToast(text: string, ms = 1800): void {
  toast.textContent = text;
  toast.classList.add('show');
  window.clearTimeout(toastTimer);
  toastTimer = window.setTimeout(() => toast.classList.remove('show'), ms);
}

function slug(s: string): string {
  return s.toLowerCase().replace(/[^a-z0-9]+/g, '_');
}

// -----------------------------------------------------------------------------
// Boot
// -----------------------------------------------------------------------------

buildBotanyPage();
buildGrassPage();
buildRootsPage();
buildMeshPage();
buildViewPage();
buildLibrary();
syncTabs();
syncView();
generate();

// -----------------------------------------------------------------------------
// Command line (Inspector footer)
// -----------------------------------------------------------------------------

function runCommand(line: string): void {
  const [verb, ...rest] = line.trim().split(/\s+/);
  const arg = rest.join(' ');
  const v = verb.toLowerCase();
  if (!v) return;
  if (v === 'seed') {
    const n = parseInt(arg, 10);
    if (Number.isNaN(n)) randomSeed();
    else setSeed(n);
  } else if (v === 'preset' || v === 'species') {
    const q = arg.toLowerCase();
    const hit = PRESETS.find((p) => p.name.toLowerCase() === q) ?? PRESETS.find((p) => p.name.toLowerCase().includes(q));
    if (hit) selectPreset(hit.name);
    else showToast(`No species matches “${arg}”`);
  } else if (v === 'mode') {
    const hit = MODES.find((m) => m.id === arg.toLowerCase() || m.label.toLowerCase() === arg.toLowerCase());
    if (hit) {
      view.mode = hit.id;
      syncView();
    } else showToast('Modes: ' + MODES.map((m) => m.id).join(' · '));
  } else if (v === 'export') {
    const f = arg.toLowerCase();
    if (f === 'obj' || f === 'glb') requestExport(f);
    else showToast('export obj | export glb');
  } else if (v === 'frame') {
    if (arg === 'base') viewer.frameBase(baseReach());
    else viewer.frame();
  } else if (v === 'wind') {
    view.windEnabled = arg ? arg === 'on' : !view.windEnabled;
    syncView();
  } else if (v === 'leaves') {
    view.showLeaves = arg ? arg === 'on' : !view.showLeaves;
    syncView();
  } else if (v === 'quads' || v === 'wire') {
    view.showWire = arg ? arg === 'on' : !view.showWire;
    syncView();
  } else if (v === 'scatter') {
    const n = parseInt(arg, 10);
    const sc = params.environment.scatter;
    const next = scatterFor(params, Math.floor(Math.random() * 99999) + 1, Number.isNaN(n) ? sc.count : n);
    next.blocks = sc.blocks;
    next.burial = sc.burial;
    next.roughness = sc.roughness;
    params.environment.scatter = next;
    params.environment.obstacles = scatterObstacles(next, trunkBaseRadius(params.botany));
    selectedObstacle = -1;
    buildRootsPage();
    scheduleGenerate(true);
  } else if (v === 'clear') {
    params.environment.obstacles = [];
    selectedObstacle = -1;
    buildRootsPage();
    scheduleGenerate(true);
  } else if (v === 'shot' || v === 'screenshot') {
    download(viewer.screenshot(), `${slug(params.name)}_${params.seed}.png`);
  } else if (v === 'help' || v === '?') {
    showToast('seed [n] · preset <name> · mode <shaded|matcap|wireframe|levels|junctions|wind> · export obj|glb · frame [base] · wind|leaves|quads [on|off] · scatter [n] · clear · shot', 5000);
  } else {
    showToast(`Unknown command “${verb}” · type help`);
  }
}
cmdInput.addEventListener('keydown', (e) => {
  if (e.key === 'Enter') {
    runCommand(cmdInput.value);
    cmdInput.value = '';
  } else if (e.key === 'Escape') {
    cmdInput.value = '';
    cmdInput.blur();
  }
  e.stopPropagation();
});

// Expose for debugging / automation.
(window as unknown as { frontier: unknown }).frontier = {
  get params() {
    return params;
  },
  get result() {
    return lastResult;
  },
  view,
  viewer,
  setPreset(name: string) {
    params = cloneParams(PRESETS.find((p) => p.name === name) ?? PRESETS[0]);
    selectedObstacle = -1;
    frameOnNext = true;
    rebuildPages();
    scheduleGenerate(true);
  },
  setSeed(seed: number) {
    params.seed = seed;
    scheduleGenerate(true);
  },
  /** Deep-merge arbitrary parameter overrides (debug / automation). */
  setParams(partial: {
    botany?: Partial<TreeParams['botany']>;
    mesh?: Partial<TreeParams['mesh']>;
    roots?: Partial<TreeParams['roots']>;
    environment?: Partial<TreeParams['environment']>;
    grass?: Partial<GrassParams>;
    seed?: number;
    name?: string;
  }) {
    if (partial.botany) Object.assign(params.botany, partial.botany);
    if (partial.grass && params.grass) Object.assign(params.grass, partial.grass);
    if (partial.mesh) Object.assign(params.mesh, partial.mesh);
    if (partial.roots) Object.assign(params.roots, partial.roots);
    if (partial.environment) Object.assign(params.environment, partial.environment);
    if (partial.seed !== undefined) params.seed = partial.seed;
    if (partial.name) params.name = partial.name;
    rebuildPages();
    scheduleGenerate(true);
  },
  frameBase: () => viewer.frameBase(baseReach()),
  /** Replace the site objects (debug / automation). */
  setObstacles(obstacles: ObstacleSpec[]) {
    params.environment.obstacles = obstacles;
    selectedObstacle = -1;
    buildRootsPage();
    scheduleGenerate(true);
  },
  setMode(mode: DisplayMode) {
    view.mode = mode;
    syncView();
  },
  setView(partial: Partial<ViewerSettings>) {
    Object.assign(view, partial);
    syncView();
  },
  regenerate: () => scheduleGenerate(true),
  frame: () => viewer.frame(),
  ready: () => !busy && hasTree,
  setCamera(pos: [number, number, number], target: [number, number, number]) {
    viewer.camera.position.set(pos[0], pos[1], pos[2]);
    viewer.controls.target.set(target[0], target[1], target[2]);
    viewer.controls.update();
  },
  /** Find a junction (side attachment) of a given level for close-up inspection. */
  junctionSamples(level: number, count = 3, kind?: string) {
    const r = lastResult;
    if (!r) return [];
    return r.samples.filter((s) => s.level === level && (!kind || s.kind === kind)).slice(0, count);
  },
};

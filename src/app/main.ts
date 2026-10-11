/**
 * Frontier Road Studio — browser entry.
 *
 * Loads a scene preset, builds the geometry, and renders it with WebGPU. The
 * right-hand panel is the project outliner: a layerstack of the surface groups
 * that actually exist in the mesh (each is a separate draw batch), plus an
 * inspector for the current settings and the build diagnostics.
 */

import { MeshBuilder, MAT_ORDER, type MeshVertex } from '../core/mesh';
import { buildScene, type BuildResult } from '../core/build';
import { makeRng, type V3 } from '../core/math';
import type { QualityProfile, SurfaceKind } from '../core/model';
import { RoadRenderer, type Batch, type FrameUniforms } from '../gpu/renderer';
import { OrbitCamera, SCENES } from './scenes';

type LayerId = 'carriageway' | 'markings' | 'footway' | 'kerbs' | 'planting' | 'structures';

interface LayerDef {
  id: LayerId;
  name: string;
  hint: string;
  materials: SurfaceKind[];
  visible: boolean;
  batch: Batch | null;
  quads: number;
}

const LAYERS: LayerDef[] = [
  {
    id: 'carriageway',
    name: 'Carriageway',
    hint: 'asphalt · channels · shoulders',
    materials: ['asphalt', 'asphalt-worn', 'asphalt-fresh', 'road-plate', 'water'],
    visible: true,
    batch: null,
    quads: 0,
  },
  {
    id: 'markings',
    name: 'Markings',
    hint: 'lane lines · crossing paint',
    materials: ['paint'],
    visible: true,
    batch: null,
    quads: 0,
  },
  {
    id: 'footway',
    name: 'Footways & paving',
    hint: 'slabs · pavers · tactile',
    materials: ['concrete', 'concrete-panel', 'slabs', 'pavers', 'pavers-mixed', 'cobble', 'tactile'],
    visible: true,
    batch: null,
    quads: 0,
  },
  {
    id: 'kerbs',
    name: 'Kerbs',
    hint: 'kerb runs · islands',
    materials: ['kerb'],
    visible: true,
    batch: null,
    quads: 0,
  },
  {
    id: 'planting',
    name: 'Planting & verges',
    hint: 'grass · soil · gravel · ballast',
    materials: ['grass', 'soil', 'gravel', 'ballast'],
    visible: true,
    batch: null,
    quads: 0,
  },
  {
    id: 'structures',
    name: 'Structures',
    hint: 'steel · timber · rubber',
    materials: ['steel', 'wood', 'rubber'],
    visible: true,
    batch: null,
    quads: 0,
  },
];

const QUALITIES: Array<{ id: QualityProfile; name: string; hint: string }> = [
  { id: 'draft', name: 'Draft', hint: 'fast, coarse' },
  { id: 'standard', name: 'Standard', hint: 'balanced' },
  { id: 'high', name: 'High', hint: 'default detail' },
  { id: 'cinematic', name: 'Cinematic', hint: '0.3 m arcs' },
];

const state = {
  sceneId: SCENES[0].id,
  quality: 'high' as QualityProfile,
  showWire: false,
  turnTable: false,
  last: null as BuildResult | null,
  buildMs: 0,
  camera: new OrbitCamera(),
};

let renderer: RoadRenderer | null = null;
let context: GPUCanvasContext | null = null;
let canvas: HTMLCanvasElement;
let statusEl: HTMLElement;
let statsEl: HTMLElement;
let layerListEl: HTMLElement;
let diagEl: HTMLElement;
let wireBatch: Batch | null = null;
let groundBatch: Batch | null = null;

function el<T extends HTMLElement>(id: string): T {
  const found = document.getElementById(id);
  if (!found) throw new Error(`missing element #${id}`);
  return found as T;
}

// ---------------------------------------------------------------- mesh split

function splitByLayer(builder: MeshBuilder): { layer: LayerDef; builder: MeshBuilder }[] {
  const out = new Map<LayerId, MeshBuilder>();
  for (const l of LAYERS) out.set(l.id, new MeshBuilder());
  const verts = builder.verts;
  const quads = builder.quads;
  const lookup = new Map<number, LayerDef>();
  LAYERS.forEach((l) => l.materials.forEach((m) => lookup.set(MAT_ORDER.indexOf(m), l)));
  const remap = new Map<string, number>();
  for (let q = 0; q + 3 < quads.length; q += 4) {
    const idx = [quads[q], quads[q + 1], quads[q + 2], quads[q + 3]];
    const kind = MAT_ORDER[verts[idx[0]].mat] ?? 'asphalt';
    const layer = lookup.get(verts[idx[0]].mat);
    if (!layer) continue;
    const target = out.get(layer.id)!;
    layer.quads += 1;
    const newIdx: number[] = [];
    for (const i of idx) {
      const v = verts[i];
      const key = `${layer.id}|${i}`;
      let mapped = remap.get(key);
      if (mapped === undefined) {
        const clone: MeshVertex = { p: [...v.p], n: [...v.n], uv: [...v.uv], uv2: [...v.uv2], mat: v.mat, tint: [...v.tint] as [number, number, number, number] };
        mapped = target.addVertex(clone);
        remap.set(key, mapped);
      }
      newIdx.push(mapped);
    }
    target.addQuad(newIdx[0], newIdx[1], newIdx[2], newIdx[3]);
    void kind;
  }
  return LAYERS.map((layer) => ({ layer, builder: out.get(layer.id)! }));
}

function buildWireIndex(builder: MeshBuilder): Uint32Array {
  const seen = new Set<number>();
  const out: number[] = [];
  const quads = builder.quads;
  const key = (a: number, b: number) => (a < b ? a * 4294967296 + b : b * 4294967296 + a);
  for (let q = 0; q + 3 < quads.length; q += 4) {
    for (let e = 0; e < 4; e++) {
      const a = quads[q + e];
      const b = quads[q + ((e + 1) % 4)];
      const k = key(a, b);
      if (seen.has(k)) continue;
      seen.add(k);
      out.push(a, b);
    }
  }
  return new Uint32Array(out);
}

function groundMesh(radius: number): MeshBuilder {
  const b = new MeshBuilder();
  const mat = MAT_ORDER.indexOf('soil');
  const n = 24;
  const uvs: number[] = [];
  void uvs;
  const step = (radius * 2) / n;
  const idx: number[][] = [];
  for (let j = 0; j <= n; j++) {
    const row: number[] = [];
    for (let i = 0; i <= n; i++) {
      const x = -radius + i * step;
      const z = -radius + j * step;
      const y = -0.42 + Math.sin(x * 0.011) * 0.18 + Math.cos(z * 0.014) * 0.18;
      row.push(b.addVertex({ p: [x, y, z], n: [0, 1, 0], uv: [x, z], uv2: [x, z], mat, tint: [1, 1, 1, 1] }));
    }
    idx.push(row);
  }
  for (let j = 0; j < n; j++) {
    for (let i = 0; i < n; i++) {
      b.addQuad(idx[j][i], idx[j][i + 1], idx[j + 1][i + 1], idx[j + 1][i]);
    }
  }
  return b;
}

// ---------------------------------------------------------------- build

function rebuild(): void {
  if (!renderer) return;
  const preset = SCENES.find((s) => s.id === state.sceneId) ?? SCENES[0];
  const doc = preset.build();
  const t0 = performance.now();
  const result = buildScene(doc, state.quality);
  state.buildMs = performance.now() - t0;
  state.last = result;

  renderer.clearBatches();
  for (const { layer, builder } of splitByLayer(result.builder)) {
    if (layer.batch) {
      layer.batch.vbo.destroy();
      layer.batch.ibo.destroy();
    }
    layer.batch = builder.triCount ? renderer.upload(builder) : null;
  }
  groundBatch?.vbo.destroy();
  groundBatch?.ibo.destroy();
  groundBatch = renderer.upload(groundMesh(1400));
  wireBatch?.vbo.destroy();
  wireBatch?.ibo.destroy();
  wireBatch = renderer.uploadWire(result.builder, buildWireIndex(result.builder));

  const bounds = sceneBounds(result);
  state.camera.target = [bounds.cx, 0, bounds.cz];
  state.camera.distance = Math.max(60, bounds.radius * 2.4);
  renderLayers();
  renderDiagnostics();
  updateStats();
}

function sceneBounds(result: BuildResult): { cx: number; cz: number; radius: number } {
  let minX = Infinity;
  let maxX = -Infinity;
  let minZ = Infinity;
  let maxZ = -Infinity;
  for (const corridor of result.corridors) {
    const p0 = corridor.path.positionAt(0);
    const p1 = corridor.path.positionAt(corridor.length);
    const mid = corridor.path.positionAt(corridor.length / 2);
    for (const p of [p0, p1, mid]) {
      minX = Math.min(minX, p[0]);
      maxX = Math.max(maxX, p[0]);
      minZ = Math.min(minZ, p[2]);
      maxZ = Math.max(maxZ, p[2]);
    }
  }
  if (!Number.isFinite(minX)) return { cx: 0, cz: 0, radius: 100 };
  return { cx: (minX + maxX) / 2, cz: (minZ + maxZ) / 2, radius: Math.max(40, Math.hypot(maxX - minX, maxZ - minZ) / 2) };
}

// ---------------------------------------------------------------- UI

function renderLayers(): void {
  layerListEl.innerHTML = '';
  for (const layer of LAYERS) {
    const row = document.createElement('button');
    row.className = `layer${layer.visible ? '' : ' off'}`;
    row.type = 'button';
    row.innerHTML = `
      <span class="swatch" data-layer="${layer.id}"></span>
      <span class="layer-name">${layer.name}</span>
      <span class="layer-count">${layer.batch ? layer.quads.toLocaleString() : '–'}</span>
      <span class="eye">${layer.visible ? '◉' : '○'}</span>`;
    row.addEventListener('click', () => {
      layer.visible = !layer.visible;
      renderLayers();
    });
    layerListEl.appendChild(row);
    const sub = document.createElement('div');
    sub.className = 'layer-hint';
    sub.textContent = layer.hint;
    layerListEl.appendChild(sub);
  }
}

function renderDiagnostics(): void {
  const result = state.last;
  diagEl.innerHTML = '';
  if (!result) return;
  const warns = [...new Set(result.diagnostics.filter((d) => d.level === 'warn').map((d) => d.message))];
  if (!warns.length) {
    diagEl.innerHTML = `<div class="ok">Topology clean — every junction solved within tolerance.</div>`;
    return;
  }
  for (const w of warns.slice(0, 8)) {
    const div = document.createElement('div');
    div.className = 'warn';
    div.textContent = w;
    diagEl.appendChild(div);
  }
}

function updateStats(): void {
  const result = state.last;
  if (!result) return;
  const s = result.stats;
  statsEl.innerHTML = `
    <div class="stat"><span>Build</span><b>${state.buildMs.toFixed(0)} ms</b></div>
    <div class="stat"><span>Vertices</span><b>${s.vertices.toLocaleString()}</b></div>
    <div class="stat"><span>Quads</span><b>${s.quads.toLocaleString()}</b></div>
    <div class="stat"><span>Triangles</span><b>${s.triangles.toLocaleString()}</b></div>
    <div class="stat"><span>Junctions</span><b>${s.junctions}</b></div>
    <div class="stat"><span>Decal sheet</span><b>${s.sheetQuads.toLocaleString()}</b></div>`;
}

// ---------------------------------------------------------------- frame loop

const SUN: [number, number, number, number] = [0.42, 0.78, 0.46, 1.15];

function frame(renderer: RoadRenderer, ctx: GPUCanvasContext, canvas: HTMLCanvasElement, dt: number): void {
  if (state.turnTable) state.camera.yaw += dt * 0.00012;
  const size = [canvas.width, canvas.height];
  const uniforms: FrameUniforms = {
    viewProj: state.camera.viewProj(size[0] / Math.max(1, size[1])),
    cameraPos: state.camera.eye(),
    sunDir: SUN,
    skyTop: [0.24, 0.42, 0.72],
    skyBottom: [0.66, 0.74, 0.8],
    ground: [0.1, 0.1, 0.09],
    params: [0.0, 0, 0, 0],
    fog: [0.68, 0.74, 0.8, 1 / 2400],
  };
  renderer.batches = [];
  if (groundBatch) renderer.batches.push(groundBatch);
  renderer.batches.push(...LAYERS.filter((l) => l.visible && l.batch).map((l) => l.batch!));
  const encoder = renderer.device.createCommandEncoder();
  const view = ctx.getCurrentTexture().createView();
  renderer.render(encoder, view, size[0], size[1], uniforms);
  if (state.showWire && wireBatch) renderer.renderWire(encoder, view, size[0], size[1], wireBatch);
  renderer.device.queue.submit([encoder.finish()]);
}

// ---------------------------------------------------------------- boot

async function boot(): Promise<void> {
  canvas = el<HTMLCanvasElement>('view');
  statusEl = el('status');
  statsEl = el('stats');
  layerListEl = el('layers');
  diagEl = el('diag');

  if (!navigator.gpu) {
    statusEl.textContent = 'WebGPU is not available in this browser. Use Chrome 113+, Edge 113+ or Safari 18+.';
    statusEl.classList.add('error');
    return;
  }
  const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
  if (!adapter) {
    statusEl.textContent = 'No WebGPU adapter found (a discrete or integrated GPU is required).';
    statusEl.classList.add('error');
    return;
  }
  const device = await adapter.requestDevice();
  const info = adapter.info ?? {};
  context = canvas.getContext('webgpu');
  if (!context) {
    statusEl.textContent = 'Could not create a WebGPU canvas context.';
    statusEl.classList.add('error');
    return;
  }
  const format = navigator.gpu.getPreferredCanvasFormat();
  context.configure({ device, format, alphaMode: 'opaque' });
  renderer = await RoadRenderer.create(device, format);
  statusEl.textContent = `${info.vendor ?? 'GPU'} · ${info.architecture ?? ''} · ${format}`;

  const resize = () => {
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    const rect = canvas.getBoundingClientRect();
    canvas.width = Math.max(320, Math.floor(rect.width * dpr));
    canvas.height = Math.max(240, Math.floor(rect.height * dpr));
  };
  new ResizeObserver(resize).observe(canvas);
  resize();

  // controls
  let dragging: 0 | 1 | 2 = 0;
  let lastX = 0;
  let lastY = 0;
  canvas.addEventListener('pointerdown', (e) => {
    dragging = e.button === 0 && !e.shiftKey ? 1 : 2;
    lastX = e.clientX;
    lastY = e.clientY;
    canvas.setPointerCapture(e.pointerId);
  });
  canvas.addEventListener('pointerup', (e) => {
    dragging = 0;
    canvas.releasePointerCapture(e.pointerId);
  });
  canvas.addEventListener('pointermove', (e) => {
    if (!dragging) return;
    const dx = e.clientX - lastX;
    const dy = e.clientY - lastY;
    lastX = e.clientX;
    lastY = e.clientY;
    if (dragging === 1) state.camera.orbit(dx, dy);
    else state.camera.pan(dx, dy);
  });
  canvas.addEventListener('wheel', (e) => {
    e.preventDefault();
    state.camera.zoom(e.deltaY);
  }, { passive: false });

  // scene + quality selectors
  const sceneSel = el<HTMLSelectElement>('scene');
  sceneSel.innerHTML = SCENES.map((s) => `<option value="${s.id}">${s.name}</option>`).join('');
  sceneSel.value = state.sceneId;
  sceneSel.addEventListener('change', () => {
    state.sceneId = sceneSel.value;
    rebuild();
  });

  const qualitySel = el<HTMLSelectElement>('quality');
  qualitySel.innerHTML = QUALITIES.map((q) => `<option value="${q.id}">${q.name} — ${q.hint}</option>`).join('');
  qualitySel.value = state.quality;
  qualitySel.addEventListener('change', () => {
    state.quality = qualitySel.value as QualityProfile;
    rebuild();
  });

  el('rebuild').addEventListener('click', () => rebuild());
  const wireToggle = el<HTMLInputElement>('wire');
  wireToggle.addEventListener('change', () => {
    state.showWire = wireToggle.checked;
  });
  const turnToggle = el<HTMLInputElement>('turntable');
  turnToggle.addEventListener('change', () => {
    state.turnTable = turnToggle.checked;
  });

  rebuild();

  let prev = performance.now();
  const loop = () => {
    const now = performance.now();
    const dt = now - prev;
    prev = now;
    if (renderer && context) frame(renderer, context, canvas, dt);
    const fps = dt > 0 ? Math.round(1000 / dt) : 0;
    const el2 = document.getElementById('fps');
    if (el2) el2.textContent = `${fps} fps`;
    requestAnimationFrame(loop);
  };
  requestAnimationFrame(loop);
}

boot().catch((err) => {
  const status = document.getElementById('status');
  if (status) {
    status.textContent = `Failed to start: ${err instanceof Error ? err.message : String(err)}`;
    status.classList.add('error');
  }
  console.error(err);
});

export { makeRng, buildScene, type V3 };

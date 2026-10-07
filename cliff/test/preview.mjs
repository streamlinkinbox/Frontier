/*
 * Headless preview: software-rasterises the generated cliff to ASCII so the
 * geometry can be eyeballed without a browser (also handy in CI).
 *
 *   node test/preview.mjs [seed] [preset] [view]
 *   view: iso | top | low | road
 */
import * as THREE from '../vendor/three.module.js';
import { generateCliff } from '../src/build.js';
import { DEFAULT_PARAMS, PRESETS } from '../src/palette.js';

const seed = process.argv[2] || 'quarry-01';
const presetId = process.argv[3] || 'limestone';
const view = process.argv[4] || 'iso';

const params = { ...DEFAULT_PARAMS, ...(PRESETS[presetId] || PRESETS.limestone).params, preset: presetId, seed };
const result = generateCliff(params);
const { pit } = result;
console.log(`seed=${seed} preset=${presetId} benches=${result.stats.benches} rocks=${result.stats.rocks} tris=${result.stats.totalTris} (${result.stats.ms}ms)`);

const RAMP = ' .:-=+*#%@';
const W = 148, H = 52;

const views = {
  iso: {
    eye: [pit.radius * 2.1, pit.yTop + pit.nBench * pit.bh * 1.15, pit.radius * 2.4],
    target: [0, pit.yTop - pit.nBench * pit.bh * 0.35, 0],
    light: [0.45, 0.72, 0.32], fov: 38,
  },
  low: {
    eye: [pit.radius * 1.5, pit.yTop - pit.nBench * pit.bh * 0.15, pit.radius * 2.0],
    target: [0, pit.yTop - pit.nBench * pit.bh * 0.55, 0],
    light: [0.45, 0.72, 0.32], fov: 42,
  },
  top: {
    eye: [0.01, pit.radius * 2.6, 0],
    target: [0, pit.yTop - pit.nBench * pit.bh * 0.5, 0],
    light: [0.45, 0.8, 0.32], fov: 40,
  },
  road: {
    eye: [Math.cos(pit.road ? pit.road.az : 0) * pit.radius * 1.2, pit.yTop - pit.nBench * pit.bh * 0.1,
      Math.sin(pit.road ? pit.road.az : 0) * pit.radius * 1.2],
    target: [0, pit.yTop - pit.nBench * pit.bh * 0.6, 0],
    light: [0.45, 0.72, 0.32], fov: 45,
  },
};
const vp = views[view] || views.iso;

const eye = new THREE.Vector3(...vp.eye);
const fwd = new THREE.Vector3(...vp.target).sub(eye).normalize();
const right = new THREE.Vector3().crossVectors(fwd, new THREE.Vector3(0, 1, 0)).normalize();
const up = new THREE.Vector3().crossVectors(right, fwd).normalize();
const fl = (W / 2) / Math.tan((vp.fov * Math.PI) / 360);
const L = new THREE.Vector3(...vp.light).normalize();

const zbuf = new Float32Array(W * H).fill(Infinity);
const cbuf = new Float32Array(W * H * 3);

const tmp = new THREE.Vector3();
const project = (p, out) => {
  tmp.subVectors(p, eye);
  const z = tmp.dot(fwd);
  if (z < 0.5) return false;
  const x = tmp.dot(right), y = tmp.dot(up);
  out.x = W / 2 + (x * fl) / z;
  out.y = H / 2 - (y * fl) / z;
  out.z = z;
  return out.x >= -2 && out.x < W + 2 && out.y >= -2 && out.y < H + 2;
};

const geos = [result.terrainGeo, result.cliffGeo];
const A = {}, B = {}, C = {};
const a = new THREE.Vector3(), b = new THREE.Vector3(), c = new THREE.Vector3();
const n = new THREE.Vector3(), e1 = new THREE.Vector3(), e2 = new THREE.Vector3();
const col = new THREE.Color();

for (const geo of geos) {
  const pos = geo.attributes.position;
  const colAttr = geo.attributes.color;
  const index = geo.index ? geo.index.array : null;
  const triCount = pos.count / 3;
  for (let t = 0; t < triCount; t++) {
    const i0 = index ? index[t * 3] : t * 3;
    const i1 = index ? index[t * 3 + 1] : t * 3 + 1;
    const i2 = index ? index[t * 3 + 2] : t * 3 + 2;
    a.fromBufferAttribute(pos, i0); b.fromBufferAttribute(pos, i1); c.fromBufferAttribute(pos, i2);
    if (!project(a, A) || !project(b, B) || !project(c, C)) continue;
    e1.subVectors(b, a); e2.subVectors(c, a);
    n.crossVectors(e1, e2);
    if (n.lengthSq() < 1e-12) continue;
    n.normalize();
    if (n.dot(tmp.subVectors(eye, a)) <= 0) continue; // backface

    const ndl = Math.max(0, n.dot(L));
    const hemi = 0.5 + 0.5 * n.y;
    const lum = 0.16 + 0.78 * ndl + 0.16 * hemi;

    col.setRGB(
      (colAttr.getX(i0) + colAttr.getX(i1) + colAttr.getX(i2)) / 3,
      (colAttr.getY(i0) + colAttr.getY(i1) + colAttr.getY(i2)) / 3,
      (colAttr.getZ(i0) + colAttr.getZ(i1) + colAttr.getZ(i2)) / 3,
      THREE.LinearSRGBColorSpace
    );
    // to sRGB for display
    const sr = Math.pow(Math.max(0, col.r * lum), 1 / 2.2);
    const sg = Math.pow(Math.max(0, col.g * lum), 1 / 2.2);
    const sb = Math.pow(Math.max(0, col.b * lum), 1 / 2.2);

    const minX = Math.max(0, Math.floor(Math.min(A.x, B.x, C.x)));
    const maxX = Math.min(W - 1, Math.ceil(Math.max(A.x, B.x, C.x)));
    const minY = Math.max(0, Math.floor(Math.min(A.y, B.y, C.y)));
    const maxY = Math.min(H - 1, Math.ceil(Math.max(A.y, B.y, C.y)));
    const d = (B.y - C.y) * (A.x - C.x) + (C.x - B.x) * (A.y - C.y);
    if (Math.abs(d) < 1e-9) continue;
    for (let y = minY; y <= maxY; y++) {
      for (let x = minX; x <= maxX; x++) {
        const l1 = ((B.y - C.y) * (x - C.x) + (C.x - B.x) * (y - C.y)) / d;
        const l2 = ((C.y - A.y) * (x - C.x) + (A.x - C.x) * (y - C.y)) / d;
        const l3 = 1 - l1 - l2;
        if (l1 < -0.001 || l2 < -0.001 || l3 < -0.001) continue;
        const z = l1 * A.z + l2 * B.z + l3 * C.z;
        const id = y * W + x;
        if (z < zbuf[id]) {
          zbuf[id] = z;
          cbuf[id * 3] = sr; cbuf[id * 3 + 1] = sg; cbuf[id * 3 + 2] = sb;
        }
      }
    }
  }
}

let out = '';
for (let y = 0; y < H; y++) {
  let line = '';
  for (let x = 0; x < W; x++) {
    const id = y * W + x;
    if (zbuf[id] === Infinity) { line += ' '; continue; }
    const l = 0.299 * cbuf[id * 3] + 0.587 * cbuf[id * 3 + 1] + 0.114 * cbuf[id * 3 + 2];
    line += RAMP[Math.min(RAMP.length - 1, Math.max(0, Math.round(l * (RAMP.length - 1))))];
  }
  out += line.replace(/\s+$/, '') + '\n';
}
console.log(out);

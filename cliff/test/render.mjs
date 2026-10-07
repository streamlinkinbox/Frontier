/*
 * Headless software renderer - produces a real PNG preview of a generated
 * cliff without needing a browser or a GPU.  Useful for eyeballing the
 * geometry and for CI snapshots.
 *
 *   node test/render.mjs [seed] [preset] [out.png] [flags]
 *   flags: --fast  (lower terrain resolution)
 *          --noSky
 *
 * Lighting is a cheap approximation of what main.js sets up in three.js
 * (directional sun + sky/ground hemisphere + linear fog + gamma).
 */
import * as fs from 'node:fs';
import * as THREE from '../vendor/three.module.js';
import { generateCliff } from '../src/build.js';
import { DEFAULT_PARAMS, PRESETS } from '../src/palette.js';
import { encodePNG } from './png.js';

const args = process.argv.slice(2);
const seed = args[0] || 'quarry-01';
const presetId = args[1] || 'limestone';
const out = args[2] || '/tmp/cliff.png';
const fast = args.includes('--fast');

const preset = PRESETS[presetId] || PRESETS.limestone;
const params = {
  ...DEFAULT_PARAMS, ...preset.params, preset: presetId, seed,
  ...(fast ? { terrainRes: 1.3, rockDensity: 0.7 } : {}),
};

const t0 = Date.now();
const result = generateCliff(params);
const { pit } = result;
console.log(`generated ${result.stats.totalTris} tris in ${Date.now() - t0}ms`);

const W = 1100, H = 700;
const fb = new Float32Array(W * H * 3);
const zbuf = new Float32Array(W * H).fill(Infinity);

/* ---------------- sky ---------------- */
const skyTop = new THREE.Color(preset.sky.top);
const skyHor = new THREE.Color(preset.sky.horizon);
const fogC = new THREE.Color(preset.fog);

function skyColor(nx, ny, nz) {
  // ny in [-1,1] along the view ray
  const t = Math.pow(Math.max(0, ny), 0.65);
  return skyHor.clone().lerp(skyTop, t);
}

/* ---------------- camera ---------------- */
const az = (preset.sun.azimuth * Math.PI) / 180;
const el = (preset.sun.elevation * Math.PI) / 180;
const sunDir = new THREE.Vector3(
  Math.cos(el) * Math.sin(az), Math.sin(el), Math.cos(el) * Math.cos(az)
).normalize();
// camera on the opposite side of the sun so the pit is lit
const viewMode = args.includes('--side') ? 'side' : (args.includes('--front') ? 'front' : 'iso');
let camAz, camEl, camDist;
if (viewMode === 'side') { camAz = 0.0; camEl = 0.02; camDist = pit.radius * 2.3; }
else if (viewMode === 'front') { camAz = az + Math.PI; camEl = 0.10; camDist = pit.radius * 1.9; }
else { camAz = az + Math.PI * 0.78; camEl = 0.155; camDist = pit.radius * 1.55 + pit.nBench * pit.bh * 0.55; }
const _caz = camAz;
const camElV = viewMode === 'side' ? 0.02 : (viewMode === 'front' ? 0.10 : 0.155);
const dist = camDist;
const eyeY = viewMode === 'side'
  ? pit.yTop - pit.nBench * pit.bh * 0.45
  : pit.yTop + pit.nBench * pit.bh * 0.30;
const eye = new THREE.Vector3(
  Math.cos(camElV) * Math.sin(camAz) * dist,
  eyeY + Math.sin(camElV) * dist * 0.42,
  Math.cos(camElV) * Math.cos(camAz) * dist
);
const target = new THREE.Vector3(0, viewMode === 'side' ? pit.yTop - pit.nBench * pit.bh * 0.45 : pit.yTop - pit.nBench * pit.bh * 0.38, 0);

const fwd = target.clone().sub(eye).normalize();
const right = new THREE.Vector3().crossVectors(fwd, new THREE.Vector3(0, 1, 0)).normalize();
const up = new THREE.Vector3().crossVectors(right, fwd).normalize();
const fl = (W / 2) / Math.tan(0.42); // ~24deg half-fov

const tmp = new THREE.Vector3();
const project = (p, out) => {
  tmp.subVectors(p, eye);
  const z = tmp.dot(fwd);
  if (z < 0.5) return false; // behind the eye - no near-plane clipping needed
  const x = tmp.dot(right), y = tmp.dot(up);
  out.x = W / 2 + (x * fl) / z;
  out.y = H / 2 - (y * fl) / z;
  out.z = z;
  // NOTE: never reject on screen bounds here - big triangles (terrain) can
  // have all three vertices off-screen while still covering the viewport.
  return true;
};

/* ---------------- rasterise ---------------- */
const geos = [result.terrainGeo, result.cliffGeo];
const A = {}, B = {}, C = {};
const a = new THREE.Vector3(), b = new THREE.Vector3(), c = new THREE.Vector3();
const n = new THREE.Vector3(), e1 = new THREE.Vector3(), e2 = new THREE.Vector3();

for (const geo of geos) {
  const pos = geo.attributes.position;
  const colA = geo.attributes.color;
  const index = geo.index ? geo.index.array : null;
  const triCount = index ? index.length / 3 : pos.count / 3;
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
    if (n.dot(tmp.subVectors(eye, a)) <= 0) continue;

    const ndl = Math.max(0, n.dot(sunDir));
    const hemi = 0.5 + 0.5 * n.y;
    // sun (warm) + sky hemisphere + bounce
    const sr = (0.94 + 0.10 * ndl) * ndl * 1.0 + 0.22 * hemi + 0.06;
    const sg = (0.90 + 0.12 * ndl) * ndl * 1.0 + 0.22 * hemi + 0.06;
    const sb = (0.82 + 0.22 * ndl) * ndl * 1.0 + 0.24 * hemi + 0.08;

    const minX = Math.max(0, Math.floor(Math.min(A.x, B.x, C.x)));
    const maxX = Math.min(W - 1, Math.ceil(Math.max(A.x, B.x, C.x)));
    const minY = Math.max(0, Math.floor(Math.min(A.y, B.y, C.y)));
    const maxY = Math.min(H - 1, Math.ceil(Math.max(A.y, B.y, C.y)));
    const den = (B.y - C.y) * (A.x - C.x) + (C.x - B.x) * (A.y - C.y);
    if (Math.abs(den) < 1e-9) continue;
    for (let y = minY; y <= maxY; y++) {
      for (let x = minX; x <= maxX; x++) {
        const l1 = ((B.y - C.y) * (x - C.x) + (C.x - B.x) * (y - C.y)) / den;
        const l2 = ((C.y - A.y) * (x - C.x) + (A.x - C.x) * (y - C.y)) / den;
        const l3 = 1 - l1 - l2;
        if (l1 < -0.001 || l2 < -0.001 || l3 < -0.001) continue;
        const z = l1 * A.z + l2 * B.z + l3 * C.z;
        const id = y * W + x;
        if (z >= zbuf[id]) continue;
        zbuf[id] = z;

        let r = (colA.getX(i0) * l1 + colA.getX(i1) * l2 + colA.getX(i2) * l3) * sr;
        let g = (colA.getY(i0) * l1 + colA.getY(i1) * l2 + colA.getY(i2) * l3) * sg;
        let bl = (colA.getZ(i0) * l1 + colA.getZ(i1) * l2 + colA.getZ(i2) * l3) * sb;

        // fog
        const fg = Math.min(1, Math.max(0, (z - pit.radius * 1.7) / (dist * 4.5)));
        r = r * (1 - fg) + fogC.r * fg;
        g = g * (1 - fg) + fogC.g * fg;
        bl = bl * (1 - fg) + fogC.b * fg;

        fb[id * 3] = r; fb[id * 3 + 1] = g; fb[id * 3 + 2] = bl;
      }
    }
  }
}

/* ---------------- tonemap + sky fill + encode ---------------- */
const rgba = Buffer.alloc(W * H * 4);
const tonemap = (x) => {
  const v = x / (x + 0.86);          // Reinhard-ish
  return Math.pow(Math.max(0, Math.min(1, v * 1.06)), 1 / 2.2);
};
for (let y = 0; y < H; y++) {
  for (let x = 0; x < W; x++) {
    const id = y * W + x;
    const o = id * 4;
    if (zbuf[id] === Infinity) {
      const dx = (x - W / 2) / (W / 2), dy = (y - H / 2) / (H / 2);
      const len = Math.hypot(dx, dy) || 1;
      const c = skyColor(0, dy / len, 0);
      rgba[o] = tonemap(c.r) * 255; rgba[o + 1] = tonemap(c.g) * 255; rgba[o + 2] = tonemap(c.b) * 255;
    } else {
      rgba[o] = tonemap(fb[id * 3]) * 255;
      rgba[o + 1] = tonemap(fb[id * 3 + 1]) * 255;
      rgba[o + 2] = tonemap(fb[id * 3 + 2]) * 255;
    }
    rgba[o + 3] = 255;
  }
}
fs.writeFileSync(out, encodePNG(W, H, rgba));
console.log(`wrote ${out} (${W}x${H})`);

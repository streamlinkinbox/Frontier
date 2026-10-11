// Software perspective renderer (z-buffer, flat Lambert shading) for headless 3D QA of generated meshes.
// Usage: renderView([{ mb }, { mb, matrices: [mat4,...] }], 'out.png', { eye, target, fov, width, height, sun })
import { encodePNG } from "./wire-png.mjs";
import { mat4 } from "../RoadGenerator/src/core/math.js";

const MAT_COLORS = {
  asphalt: [58, 58, 62], asphaltBike: [130, 60, 55], curb: [165, 165, 158], paving: [150, 138, 118], concrete: [160, 158, 150], grass: [70, 105, 50], soil: [95, 70, 45],
  gravel: [130, 125, 112], paint: [235, 235, 225], paintYellow: [230, 190, 70], metal: [120, 125, 130], metalPainted: [60, 65, 70], steelGalv: [160, 165, 170], rubber: [30, 30, 30],
  tactile: [200, 80, 60], wood: [120, 85, 50], ballast: [110, 105, 100], tiles: [150, 150, 160], glass: [120, 160, 190], carPaint: [120, 40, 40], billboard: [220, 200, 180], sign: [200, 40, 40],
  plastic: [240, 120, 30], lamp: [255, 240, 200], lampOff: [40, 40, 40], lampRed: [255, 40, 40], lampGreen: [40, 230, 80], lampAmber: [255, 170, 40],
};

export function renderView(items, path, opts = {}) {
  const W = opts.width || 1200, H = opts.height || 800;
  const eye = opts.eye || [60, 40, 60], target = opts.target || [0, 0, 0], fov = (opts.fov || 50) * Math.PI / 180;
  const sun = norm(opts.sun || [0.45, 0.7, 0.35]);
  const view = mat4.lookAt(eye, target, [0, 1, 0]);
  const proj = mat4.perspective(fov, W / H, 0.5, 5000);
  const vp = mat4.multiply(proj, view);
  const buf = Buffer.alloc(W * H * 4);
  const zb = new Float32Array(W * H).fill(Infinity);
  // sky gradient background
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) { const t = y / H; const i = (y * W + x) * 4; buf[i] = 120 + 60 * t; buf[i + 1] = 150 + 50 * t; buf[i + 2] = 200 + 30 * t; buf[i + 3] = 255; }
  const project = (p) => { const c = mat4.transformPoint4 ? mat4.transformPoint4(vp, p) : tp4(vp, p); if (c[3] <= 0.01) return null; return [(c[0] / c[3] * 0.5 + 0.5) * W, (1 - (c[1] / c[3] * 0.5 + 0.5)) * H, c[2] / c[3]]; };
  let tris = 0;
  const drawMesh = (mb, xf) => {
    const pos = (i) => { const p = mb.position(i); return xf ? mat4.transformPoint(xf, p) : p; };
    for (const f of mb.faces) {
      const pts = f.i.map((i) => pos(i));
      // flat normal (Newell)
      let nx = 0, ny = 0, nz = 0;
      for (let k = 0; k < pts.length; k++) { const a = pts[k], b = pts[(k + 1) % pts.length]; nx += (a[1] - b[1]) * (a[2] + b[2]); ny += (a[2] - b[2]) * (a[0] + b[0]); nz += (a[0] - b[0]) * (a[1] + b[1]); }
      const l = Math.hypot(nx, ny, nz); if (l < 1e-12) continue; nx /= l; ny /= l; nz /= l;
      const base = MAT_COLORS[mb.materials[f.m]] || [200, 0, 200];
      const lam = Math.max(0, nx * sun[0] + ny * sun[1] + nz * sun[2]);
      const sh = 0.35 + 0.75 * lam;
      const col = [Math.min(255, base[0] * sh), Math.min(255, base[1] * sh), Math.min(255, base[2] * sh)];
      const sp = pts.map(project); if (sp.some((p) => !p)) continue;
      for (let k = 1; k + 1 < sp.length; k++) { fillTri(sp[0], sp[k], sp[k + 1], col); tris++; }
    }
  };
  function fillTri(a, b, c, col) {
    const minX = Math.max(0, Math.floor(Math.min(a[0], b[0], c[0]))), maxX = Math.min(W - 1, Math.ceil(Math.max(a[0], b[0], c[0])));
    const minY = Math.max(0, Math.floor(Math.min(a[1], b[1], c[1]))), maxY = Math.min(H - 1, Math.ceil(Math.max(a[1], b[1], c[1])));
    if (minX > maxX || minY > maxY) return;
    const e = (p, q, r) => (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]);
    const area = e(a, b, c); if (Math.abs(area) < 1e-9) return;
    for (let y = minY; y <= maxY; y++) for (let x = minX; x <= maxX; x++) {
      const p = [x + 0.5, y + 0.5];
      const w0 = e(b, c, p) / area, w1 = e(c, a, p) / area, w2 = e(a, b, p) / area;
      if (w0 < 0 || w1 < 0 || w2 < 0) continue;
      const z = w0 * a[2] + w1 * b[2] + w2 * c[2];
      const idx = y * W + x;
      if (z >= zb[idx]) continue;
      zb[idx] = z;
      const i = idx * 4; buf[i] = col[0]; buf[i + 1] = col[1]; buf[i + 2] = col[2]; buf[i + 3] = 255;
    }
  }
  for (const it of items) {
    if (!it || !it.mb) continue;
    if (it.matrices && it.matrices.length) for (const m of it.matrices) drawMesh(it.mb, m);
    else drawMesh(it.mb, it.transform || null);
  }
  const { writeFileSync } = awaitFs();
  writeFileSync(path, encodePNG(W, H, buf));
  return { tris };
}
function tp4(m, p) { const x = p[0], y = p[1], z = p[2]; return [m[0] * x + m[4] * y + m[8] * z + m[12], m[1] * x + m[5] * y + m[9] * z + m[13], m[2] * x + m[6] * y + m[10] * z + m[14], m[3] * x + m[7] * y + m[11] * z + m[15]]; }
function norm(v) { const l = Math.hypot(v[0], v[1], v[2]) || 1; return [v[0] / l, v[1] / l, v[2] / l]; }
import * as fs from "node:fs";
function awaitFs() { return fs; }

// Software rasteriser for headless visual verification of the mesh core.
// Renders preset scenes to PNG (z-buffer, flat shading, optional quad wire).
// Run: node tools/render.mjs

import { deflateSync, crc32 } from 'zlib';
import { mkdirSync, writeFileSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';
import { buildNetworkMesh } from '../src/core/generate.js';
import { sampleDense } from '../src/core/spline.js';

const __dir = dirname(fileURLToPath(import.meta.url));
const OUT = join(__dir, '..', 'docs');
mkdirSync(OUT, { recursive: true });

// --- tiny PNG writer ---------------------------------------------------------

function writePNG(path, w, h, rgb) {
  const stride = w * 3 + 1;
  const raw = Buffer.alloc(stride * h);
  for (let y = 0; y < h; y++) {
    raw[y * stride] = 0;
    Buffer.from(rgb.buffer, rgb.byteOffset + y * w * 3, w * 3).copy(raw, y * stride + 1);
  }
  const chunk = (type, data) => {
    const len = Buffer.alloc(4);
    len.writeUInt32BE(data.length);
    const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(td) >>> 0);
    return Buffer.concat([len, td, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; ihdr[9] = 2; // 8-bit RGB
  const png = Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(raw, { level: 6 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
  writeFileSync(path, png);
}

// --- math --------------------------------------------------------------------

const sub3 = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const cross3 = (a, b) => [
  a[1] * b[2] - a[2] * b[1],
  a[2] * b[0] - a[0] * b[2],
  a[0] * b[1] - a[1] * b[0],
];
const dot3 = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const norm3 = (a) => {
  const l = Math.hypot(a[0], a[1], a[2]) || 1;
  return [a[0] / l, a[1] / l, a[2] / l];
};

function lookAt(eye, target, up = [0, 1, 0]) {
  const f = norm3(sub3(target, eye));
  const r = norm3(cross3(f, up));
  const u = cross3(r, f);
  return {
    r, u, f,
    toView(p) {
      const d = sub3(p, eye);
      return [dot3(d, r), dot3(d, u), dot3(d, f)]; // x right, y up, z forward
    },
  };
}

function project(viewPt, fovY, W, H) {
  const z = Math.max(viewPt[2], 0.05);
  const ty = Math.tan(fovY / 2);
  const ndcX = viewPt[0] / (z * ty * (W / H));
  const ndcY = viewPt[1] / (z * ty);
  return [(ndcX * 0.5 + 0.5) * W, (0.5 - ndcY * 0.5) * H, z];
}

// --- rasteriser --------------------------------------------------------------

function render(mesh, cam, W, H, opts = {}) {
  const bg = opts.bg ?? [13, 16, 20];
  const img = new Uint8Array(W * H * 3);
  for (let i = 0; i < W * H; i++) {
    img[i * 3] = bg[0]; img[i * 3 + 1] = bg[1]; img[i * 3 + 2] = bg[2];
  }
  const zbuf = new Float32Array(W * H).fill(Infinity);
  const view = lookAt(cam.eye, cam.target, cam.up ?? [0, 1, 0]);
  const fovY = (cam.fov ?? 52) * Math.PI / 180;

  const P = mesh.positions;
  const nVerts = P.length / 3;
  const projPts = new Array(nVerts);
  const viewPts = new Array(nVerts);
  for (let i = 0; i < nVerts; i++) {
    const v = view.toView([P[i * 3], P[i * 3 + 1], P[i * 3 + 2]]);
    viewPts[i] = v;
    projPts[i] = project(v, fovY, W, H);
  }

  const light = norm3(opts.light ?? [0.55, 0.85, 0.35]);
  const light2 = norm3(opts.light2 ?? [-0.5, 0.35, -0.6]);
  const ambient = opts.ambient ?? 0.32;

  const tri = (a, b, c, rgb) => {
    const va = viewPts[a], vb = viewPts[b], vc = viewPts[c];
    if (va[2] < 0.05 || vb[2] < 0.05 || vc[2] < 0.05) return;
    const pa = projPts[a], pb = projPts[b], pc = projPts[c];
    const n = norm3(cross3(sub3([P[b * 3], P[b * 3 + 1], P[b * 3 + 2]], [P[a * 3], P[a * 3 + 1], P[a * 3 + 2]]),
      sub3([P[c * 3], P[c * 3 + 1], P[c * 3 + 2]], [P[a * 3], P[a * 3 + 1], P[a * 3 + 2]])));
    // view-space normal for shading
    const nv = norm3(cross3(sub3(vb, va), sub3(vc, va)));
    const shade = (nn) => {
      const d1 = Math.max(dot3(nn, light), 0);
      const d2 = Math.max(dot3(nn, light2), 0);
      return ambient + 0.72 * d1 + 0.28 * d2;
    };
    let s = shade(nv);
    if (s < ambient) {
      nv[0] = -nv[0]; nv[1] = -nv[1]; nv[2] = -nv[2];
      s = shade(nv); // double-sided
    }
    const minX = Math.max(0, Math.floor(Math.min(pa[0], pb[0], pc[0])));
    const maxX = Math.min(W - 1, Math.ceil(Math.max(pa[0], pb[0], pc[0])));
    const minY = Math.max(0, Math.floor(Math.min(pa[1], pb[1], pc[1])));
    const maxY = Math.min(H - 1, Math.ceil(Math.max(pa[1], pb[1], pc[1])));
    const area = (pb[0] - pa[0]) * (pc[1] - pa[1]) - (pb[1] - pa[1]) * (pc[0] - pa[0]);
    if (Math.abs(area) < 1e-9) return;
    for (let y = minY; y <= maxY; y++) {
      for (let x = minX; x <= maxX; x++) {
        const w0 = ((pb[0] - pa[0]) * (y + 0.5 - pa[1]) - (pb[1] - pa[1]) * (x + 0.5 - pa[0])) / area;
        const w1 = ((pc[0] - pb[0]) * (y + 0.5 - pb[1]) - (pc[1] - pb[1]) * (x + 0.5 - pb[0])) / area;
        const w2 = 1 - w0 - w1;
        if (w0 < -1e-5 || w1 < -1e-5 || w2 < -1e-5) continue;
        const z = w0 * pc[2] + w1 * pa[2] + w2 * pb[2];
        const idx = y * W + x;
        if (z >= zbuf[idx]) continue;
        zbuf[idx] = z;
        const fog = Math.min(Math.max((z - 120) / 380, 0), 0.82) * (opts.fogAmt ?? 1);
        const r = rgb[0] * s * (1 - fog) + bg[0] * fog;
        const g = rgb[1] * s * (1 - fog) + bg[1] * fog;
        const b2 = rgb[2] * s * (1 - fog) + bg[2] * fog;
        img[idx * 3] = Math.min(255, r);
        img[idx * 3 + 1] = Math.min(255, g);
        img[idx * 3 + 2] = Math.min(255, b2);
      }
    }
  };

  for (const q of mesh.quads) {
    const c = mesh.colors;
    const r = (c[q[0] * 3] + c[q[1] * 3] + c[q[2] * 3] + c[q[3] * 3]) / 4 * 255;
    const g = (c[q[0] * 3 + 1] + c[q[1] * 3 + 1] + c[q[2] * 3 + 1] + c[q[3] * 3 + 1]) / 4 * 255;
    const b = (c[q[0] * 3 + 2] + c[q[1] * 3 + 2] + c[q[2] * 3 + 2] + c[q[3] * 3 + 2]) / 4 * 255;
    tri(q[0], q[1], q[2], [r, g, b]);
    tri(q[0], q[2], q[3], [r, g, b]);
  }

  // quad wireframe overlay
  if (opts.wire) {
    const lineCol = opts.wireColor ?? [255, 176, 72];
    const drawLine = (i, j) => {
      const a = projPts[i], b = projPts[j];
      if (a[2] < 0.05 || b[2] < 0.05) return;
      const steps = Math.ceil(Math.hypot(b[0] - a[0], b[1] - a[1]));
      for (let t = 0; t <= steps; t++) {
        const u = t / steps;
        const x = Math.round(a[0] + (b[0] - a[0]) * u);
        const y = Math.round(a[1] + (b[1] - a[1]) * u);
        const z = a[2] + (b[2] - a[2]) * u;
        if (x < 0 || y < 0 || x >= W || y >= H) continue;
        const idx = y * W + x;
        if (z > zbuf[idx] + 0.12) continue;
        img[idx * 3] = lineCol[0];
        img[idx * 3 + 1] = lineCol[1];
        img[idx * 3 + 2] = lineCol[2];
      }
    };
    for (const q of mesh.quads) {
      drawLine(q[0], q[1]); drawLine(q[1], q[2]); drawLine(q[2], q[3]); drawLine(q[3], q[0]);
    }
  }

  return img;
}

// --- scenes ------------------------------------------------------------------

const base = {
  stationSpacing: 3, halfW: 4.2, tiers: 3, tierH: 2.3, ledgeD: 1.15,
  tierHVar: 0.4, ledgeDVar: 0.4, archRise: 3.4, archRiseVar: 0.5,
  roadPts: 5, seed: 1, junctionDist: 11, detectRadius: 5.5,
  minArmLen: 26, junctionMergeDist: 9, heightTolerance: 7,
};

const full = [
  { points: [{ x: -95, y: 0, z: -28 }, { x: -52, y: 10, z: 8 }, { x: -6, y: -4, z: -14 }, { x: 38, y: 8, z: 14 }, { x: 88, y: 0, z: -16 }] },
  { points: [{ x: 8, y: 0, z: -5 }, { x: 16, y: 6, z: 22 }, { x: 26, y: 12, z: 50 }, { x: 24, y: 16, z: 82 }] },
  { points: [{ x: -80, y: 11, z: 45 }, { x: -58, y: 11, z: 18 }, { x: -46, y: 11, z: -6 }, { x: -20, y: 13, z: -50 }] },
  { points: [{ x: 48, y: 24, z: -60 }, { x: 58, y: 24, z: -12 }, { x: 66, y: 24, z: 40 }, { x: 74, y: 24, z: 75 }] },
];

const xCross = [
  { points: [{ x: -70, y: 0, z: -14 }, { x: -28, y: 3, z: -6 }, { x: 14, y: 1, z: 6 }, { x: 64, y: 5, z: 16 }] },
  { points: [{ x: -16, y: 2, z: -58 }, { x: -6, y: 1, z: -16 }, { x: 2, y: 3, z: 18 }, { x: 12, y: 2, z: 58 }] },
];

const hills = [
  { points: [{ x: -90, y: 0, z: -30 }, { x: -50, y: 14, z: 10 }, { x: -8, y: -6, z: -16 }, { x: 30, y: 12, z: 16 }, { x: 72, y: -2, z: -12 }] },
];

const W = 1100, H = 720;
const beauty = { ambient: 0.52, light: [0.62, 0.78, 0.42], light2: [-0.55, 0.4, -0.55], fogAmt: 0.22 };

function shoot(name, mesh, cam, opts = {}) {
  const img = render(mesh, cam, W, H, { ...beauty, ...opts });
  writePNG(join(OUT, `${name}.png`), W, H, img);
  console.log(`wrote docs/${name}.png  (verts=${mesh.stats.vertices} quads=${mesh.stats.quads} junctions=${mesh.stats.junctions})`);
}

// camera on a spline centreline at road level + 2.2, looking along the path
function insideCam(points, atS, lookS, h = 2.2) {
  const dense = sampleDense(points, 20);
  const at = (s) => {
    let i = 0;
    while (i < dense.length - 2 && dense[i + 1].s < s) i++;
    const a = dense[i], b = dense[i + 1];
    const u = (s - a.s) / Math.max(b.s - a.s, 1e-6);
    return {
      pos: [
        a.pos.x + (b.pos.x - a.pos.x) * u,
        a.pos.y + (b.pos.y - a.pos.y) * u,
        a.pos.z + (b.pos.z - a.pos.z) * u,
      ],
    };
  };
  const p = at(atS).pos;
  const q = at(lookS).pos;
  return { eye: [p[0], p[1] + h, p[2]], target: [q[0], q[1] + h, q[2]], fov: 72 };
}

const meshFull = buildNetworkMesh(full, base);
shoot('overview', meshFull, {
  eye: [135, 115, 165], target: [-2, 8, 8], fov: 48,
});
shoot('overview_wire', meshFull, {
  eye: [135, 115, 165], target: [-2, 8, 8], fov: 48,
}, { wire: true, wireColor: [255, 170, 60] });

const meshX = buildNetworkMesh(xCross, base);
shoot('x_junction', meshX, {
  eye: [20, 55, 62], target: [-2, 3, 2], fov: 42,
});
shoot('x_junction_wire', meshX, {
  eye: [20, 55, 62], target: [-2, 3, 2], fov: 42,
}, { wire: true, wireColor: [255, 170, 60] });

// inside the tunnel, riding the centreline toward the junction
shoot('inside', meshX, insideCam(xCross[0].points, 18, 62, 2.3), {
  ambient: 0.52, fogAmt: 0.35,
});

// interior of the junction chamber: approach along spline 1
shoot('inside_junction', meshX, insideCam(xCross[1].points, 34, 66, 2.3), {
  ambient: 0.55, fogAmt: 0.3,
});

const meshHills = buildNetworkMesh(hills, base);
shoot('hills', meshHills, {
  eye: [-55, 48, 118], target: [-8, 4, -4], fov: 48,
});
shoot('hills_wire', meshHills, {
  eye: [-55, 48, 118], target: [-8, 4, -4], fov: 48,
}, { wire: true, wireColor: [255, 170, 60] });

console.log('done');

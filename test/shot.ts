/**
 * A tiny software rasteriser. No GPU, no browser: this exists so the roof can
 * be LOOKED at from a headless test run — the geometry checks prove it is
 * right, this proves it is a roof.
 *
 *   npx tsx test/shot.ts out.bmp
 */
import { writeFileSync } from 'node:fs';
import { BufferGeometry, Matrix4, Vector3 } from 'three';
import { generateRoof, type RoofRequest } from '../src/gen';

interface Tri { a: Vector3; b: Vector3; c: Vector3; col: [number, number, number] }

function trisOf(geom: BufferGeometry, m: Matrix4, col: [number, number, number], out: Tri[]) {
  const pos = geom.getAttribute('position');
  const idx = geom.getIndex();
  const n = idx ? idx.count : pos.count;
  const va = new Vector3();
  const vb = new Vector3();
  const vc = new Vector3();
  for (let i = 0; i < n; i += 3) {
    const i0 = idx ? idx.getX(i) : i;
    const i1 = idx ? idx.getX(i + 1) : i + 1;
    const i2 = idx ? idx.getX(i + 2) : i + 2;
    va.fromBufferAttribute(pos, i0).applyMatrix4(m);
    vb.fromBufferAttribute(pos, i1).applyMatrix4(m);
    vc.fromBufferAttribute(pos, i2).applyMatrix4(m);
    out.push({ a: va.clone(), b: vb.clone(), c: vc.clone(), col });
  }
}

function render(tri: Tri[], W: number, H: number, opt: { yaw: number; pitch: number; zoom: number }): Uint8Array {
  const img = new Uint8Array(W * H * 3);
  const depth = new Float32Array(W * H).fill(Infinity);
  // background
  for (let i = 0; i < W * H; i++) {
    img[i * 3] = 22; img[i * 3 + 1] = 26; img[i * 3 + 2] = 33;
  }
  let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
  for (const t of tri) {
    for (const v of [t.a, t.b, t.c]) {
      minX = Math.min(minX, v.x); maxX = Math.max(maxX, v.x);
      minY = Math.min(minY, v.y); maxY = Math.max(maxY, v.y);
    }
  }
  const cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;
  const span = Math.max(maxX - minX, maxY - minY) * 1.02;
  const s = (Math.min(W, H) / span) * opt.zoom;
  const cy2 = Math.cos(opt.yaw), sy2 = Math.sin(opt.yaw);
  const cp = Math.cos(opt.pitch), sp = Math.sin(opt.pitch);
  const toScreen = (v: Vector3) => {
    const x0 = v.x - cx, y0 = v.y - cy, z0 = v.z;
    const x1 = x0 * cy2 - z0 * sy2;
    const z1 = x0 * sy2 + z0 * cy2;
    const y1 = y0 * cp - z1 * sp;
    // Camera sits ABOVE the scene looking down, so depth must grow as y falls.
    // (Negating this is the difference between seeing the roof and seeing the
    // inside of the roof.)
    const depthV = -y0 * sp + z1 * cp;
    return { x: W / 2 + x1 * s, y: H / 2 - y1 * s, z: depthV };
  };
  const L = new Vector3(0.45, 0.8, 0.4).normalize();
  for (const t of tri) {
    let A = toScreen(t.a);
    let B = toScreen(t.b);
    let C = toScreen(t.c);
    // Normalise the winding by SWAPPING vertices, never by negating weights:
    // negated barycentrics still sum to 1 but no longer index the right vertex,
    // which silently corrupts the interpolated depth.
    let area = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
    if (area < 0) {
      const tb = B;
      B = C;
      C = tb;
      area = -area;
    }
    if (area < 1e-9) continue;
    const nrm = new Vector3()
      .subVectors(t.b, t.a)
      .cross(new Vector3().subVectors(t.c, t.a))
      .normalize();
    // two-sided shading: always light the side the camera can see
    if (nrm.dot(new Vector3(Math.sin(opt.yaw) * cp, -sp, Math.cos(opt.yaw) * cp)) > 0) nrm.negate();
    const lam = Math.max(0.18, nrm.dot(L)) * 0.85 + 0.15;
    const col: [number, number, number] = [
      Math.min(255, t.col[0] * lam) | 0,
      Math.min(255, t.col[1] * lam) | 0,
      Math.min(255, t.col[2] * lam) | 0,
    ];
    const minPx = Math.max(0, Math.floor(Math.min(A.x, B.x, C.x)));
    const maxPx = Math.min(W - 1, Math.ceil(Math.max(A.x, B.x, C.x)));
    const minPy = Math.max(0, Math.floor(Math.min(A.y, B.y, C.y)));
    const maxPy = Math.min(H - 1, Math.ceil(Math.max(A.y, B.y, C.y)));
    const inv = 1 / area;
    for (let py = minPy; py <= maxPy; py++) {
      for (let px = minPx; px <= maxPx; px++) {
        const x = px + 0.5, y = py + 0.5;
        const w0 = ((B.x - A.x) * (y - A.y) - (B.y - A.y) * (x - A.x)) * inv;
        const w1 = ((C.x - B.x) * (y - B.y) - (C.y - B.y) * (x - B.x)) * inv;
        const w2 = ((A.x - C.x) * (y - C.y) - (A.y - C.y) * (x - C.x)) * inv;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        const z = w1 * A.z + w2 * B.z + w0 * C.z;
        const o = py * W + px;
        if (z >= depth[o]) continue;
        depth[o] = z;
        img[o * 3] = col[0]; img[o * 3 + 1] = col[1]; img[o * 3 + 2] = col[2];
      }
    }
  }
  return img;
}

const TILE_COL: [number, number, number] = [96, 112, 122];
const WOOD: [number, number, number] = [150, 96, 52];
const SHEATH: [number, number, number] = [92, 74, 58];
const RIDGE: [number, number, number] = [74, 84, 96];

const ONLY = process.argv[3] ?? 'all';

function buildTris(req: Partial<RoofRequest>): Tri[] {
  const b = generateRoof(req);
  const out: Tri[] = [];
  if (ONLY === 'all' || ONLY === 'tiles')
    for (const inst of b.tiles.instances) trisOf(b.geometries.tile[inst.kind], inst.m, TILE_COL, out);
  if (ONLY === 'all' || ONLY === 'frame')
    for (const inst of b.frame.instances)
      trisOf(b.geometries.frame[inst.kind], inst.m, inst.kind === 'column' ? WOOD : SHEATH, out);
  if (ONLY === 'all' || ONLY === 'ridges')
    for (const inst of b.ridges) trisOf(b.geometries.ridge[inst.kind], inst.m, RIDGE, out);
  return out;
}

const W = 880;
const H = 620;
const views: { req: Partial<RoofRequest>; yaw: number; pitch: number; zoom: number; label: string }[] = [
  { req: { type: 'hipGable' }, yaw: 0.62, pitch: 0.85, zoom: 1, label: 'hipGable' },
  { req: { type: 'hip', width: 11, depth: 8, tile: 'temple', cornerUpturn: 0.3 }, yaw: -0.7, pitch: 0.8, zoom: 1, label: 'hip' },
  { req: { type: 'gable', width: 7.4, depth: 5.6, tile: 'qing', hasBarrels: false }, yaw: 1.1, pitch: 0.8, zoom: 1, label: 'gable' },
  { req: { type: 'pyramid', width: 4.6, depth: 4.6, tile: 'temple', cornerUpturn: 0.34 }, yaw: 0.5, pitch: 0.9, zoom: 1, label: 'pyramid' },
];
const panels: Uint8Array[] = [];
for (const v of views) {
  const tris = buildTris(v.req);
  console.error(`${v.label}: ${tris.length} triangles`);
  panels.push(render(tris, W, H, { yaw: v.yaw, pitch: v.pitch, zoom: v.zoom }));
}

// 2 × 2 sheet
const SW = W * 2;
const SH = H * 2;
const sheet = new Uint8Array(SW * SH * 3);
for (let i = 0; i < SW * SH; i++) { sheet[i * 3] = 12; sheet[i * 3 + 1] = 14; sheet[i * 3 + 2] = 18; }
panels.forEach((p, k) => {
  const ox = (k % 2) * W;
  const oy = Math.floor(k / 2) * H;
  for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
      const src = (y * W + x) * 3;
      const dst = ((oy + y) * SW + ox + x) * 3;
      sheet[dst] = p[src]; sheet[dst + 1] = p[src + 1]; sheet[dst + 2] = p[src + 2];
    }
  }
});

// 24-bit BMP, bottom-up
const rowBytes = SW * 3;
const pad = (4 - (rowBytes % 4)) % 4;
const stride = rowBytes + pad;
const dataSize = stride * SH;
const buf = Buffer.alloc(54 + dataSize);
buf.write('BM', 0, 'ascii');
buf.writeUInt32LE(54 + dataSize, 2);
buf.writeUInt32LE(54, 10);
buf.writeUInt32LE(40, 14);
buf.writeInt32LE(SW, 18);
buf.writeInt32LE(SH, 22);
buf.writeUInt16LE(1, 26);
buf.writeUInt16LE(24, 28);
buf.writeUInt32LE(dataSize, 34);
for (let y = 0; y < SH; y++) {
  const srcY = SH - 1 - y;
  for (let x = 0; x < SW; x++) {
    const s = (srcY * SW + x) * 3;
    const d = 54 + y * stride + x * 3;
    buf[d] = sheet[s + 2]; buf[d + 1] = sheet[s + 1]; buf[d + 2] = sheet[s];
  }
}
const out = process.argv[2] ?? 'roof.bmp';
writeFileSync(out, buf);
console.error(`wrote ${out} ${SW}×${SH}`);

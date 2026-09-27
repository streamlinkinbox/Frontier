/**
 * Minimal CPU rasteriser — renders the mosquito rig to a PNG without a
 * browser, so the asset can actually be looked at. Flat-shaded, z-buffered,
 * one directional + one ambient + one fill light.
 */
import { deflateSync } from 'node:zlib';
import { writeFileSync } from 'node:fs';

const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const W = Number(process.env.W || 900);
const H = Number(process.env.H || 620);

export function makeCamera({ eye, target, up, fov = 26 }) {
  const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
  const norm = (a) => { const l = Math.hypot(...a); return [a[0] / l, a[1] / l, a[2] / l]; };
  const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  const fwd = norm(sub(target, eye));
  const right = norm(cross(fwd, up));
  const realUp = cross(right, fwd);
  const f = 1 / Math.tan((fov * Math.PI) / 180 / 2);
  return { eye, fwd, right, realUp, f, aspect: W / H };
}

/** Returns an RGBA Uint8Array. */
export function render(root, cam, opts = {}) {
  const bg = opts.bg || [22, 24, 28];
  const buf = new Uint8Array(W * H * 4);
  for (let i = 0; i < W * H; i++) {
    // subtle vertical gradient so the silhouette reads
    const t = i / (W * H);
    buf[i * 4] = bg[0] + t * 10; buf[i * 4 + 1] = bg[1] + t * 10; buf[i * 4 + 2] = bg[2] + t * 12; buf[i * 4 + 3] = 255;
  }
  const zbuf = new Float32Array(W * H).fill(Infinity);
  root.updateWorldMatrix(true, true);

  const L1 = normv([0.55, 0.78, 0.32]);      // key
  const L2 = normv([-0.6, 0.15, -0.5]);      // fill
  const base = opts.base || [150, 128, 96];

  // Geometry is authored in each MESH's own local frame, so every vertex must
  // go through that mesh's world matrix — not the root's.
  let m = null;
  const xf = (p) => [
    m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12],
    m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
    m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14],
  ];

  const project = (w) => {
    const d = [w[0] - cam.eye[0], w[1] - cam.eye[1], w[2] - cam.eye[2]];
    const z = dot(d, cam.fwd);
    if (z < 0.01) return null;
    const x = dot(d, cam.right), y = dot(d, cam.realUp);
    return [(x * cam.f / (z * cam.aspect) * 0.5 + 0.5) * W, (0.5 - y * cam.f / z * 0.5) * H, z];
  };

  const meshes = [];
  root.traverse((o) => { if (o.isMesh && o.geometry?.attributes?.position) meshes.push(o); });

  for (const mesh of meshes) {
    mesh.updateWorldMatrix(true, false);
    if (opts.only && !opts.only(mesh)) continue;
    const g = mesh.geometry;
    const pos = g.attributes.position, nor = g.attributes.normal;
    const idx = g.index ? g.index.array : null;
    const count = idx ? idx.length : pos.count;
    m = mesh.matrixWorld.elements;
    const col = opts.colorOf ? opts.colorOf(mesh) : base;
    for (let t = 0; t < count; t += 3) {
      const i0 = idx ? idx[t] : t, i1 = idx ? idx[t + 1] : t + 1, i2 = idx ? idx[t + 2] : t + 2;
      const a = project(xf([pos.getX(i0), pos.getY(i0), pos.getZ(i0)]));
      const b = project(xf([pos.getX(i1), pos.getY(i1), pos.getZ(i1)]));
      const c = project(xf([pos.getX(i2), pos.getY(i2), pos.getZ(i2)]));
      if (!a || !b || !c) continue;
      // face normal in world space (flat shading)
      const w0 = xf([pos.getX(i0), pos.getY(i0), pos.getZ(i0)]);
      const w1 = xf([pos.getX(i1), pos.getY(i1), pos.getZ(i1)]);
      const w2 = xf([pos.getX(i2), pos.getY(i2), pos.getZ(i2)]);
      let fn = normv(crossv(sub3(w1, w0), sub3(w2, w0)));
      if (!isFinite(fn[0])) continue;
      // backface: flip normal toward the eye
      const view = normv(sub3(cam.eye, w0));
      if (dot(fn, view) < 0) fn = [-fn[0], -fn[1], -fn[2]];
      const sh = 0.20 + 0.80 * Math.max(0, dot(fn, L1)) + 0.28 * Math.max(0, dot(fn, L2));
      const shade = Math.min(1, sh);
      const r = Math.min(255, col[0] * shade), g2 = Math.min(255, col[1] * shade), b2 = Math.min(255, col[2] * shade);
      tri(a, b, c, r, g2, b2);
    }
  }

  function tri(a, b, c, r, g, bl) {
    const minX = Math.max(0, Math.floor(Math.min(a[0], b[0], c[0])));
    const maxX = Math.min(W - 1, Math.ceil(Math.max(a[0], b[0], c[0])));
    const minY = Math.max(0, Math.floor(Math.min(a[1], b[1], c[1])));
    const maxY = Math.min(H - 1, Math.ceil(Math.max(a[1], b[1], c[1])));
    const area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    if (Math.abs(area) < 1e-9) return;
    for (let y = minY; y <= maxY; y++) {
      for (let x = minX; x <= maxX; x++) {
        const px = x + 0.5, py = y + 0.5;
        const w0 = ((b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0])) / area;
        const w1 = ((px - a[0]) * (c[1] - a[1]) - (py - a[1]) * (c[0] - a[0])) / area;
        if (w0 < 0 || w1 < 0 || w0 + w1 > 1) continue;
        const z = a[2] + (b[2] - a[2]) * w1 + (c[2] - a[2]) * w0;
        const o = y * W + x;
        if (z >= zbuf[o]) continue;
        zbuf[o] = z;
        buf[o * 4] = r; buf[o * 4 + 1] = g; buf[o * 4 + 2] = bl; buf[o * 4 + 3] = 255;
      }
    }
  }
  return buf;
}

const sub3 = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const crossv = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const normv = (a) => { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };

/** Minimal PNG writer (RGB, no filter). */
export function writePNG(path, rgba) {
  const raw = Buffer.alloc((W * 3 + 1) * H);
  let p = 0;
  for (let y = 0; y < H; y++) {
    raw[p++] = 0;
    for (let x = 0; x < W; x++) {
      const o = (y * W + x) * 4;
      raw[p++] = rgba[o]; raw[p++] = rgba[o + 1]; raw[p++] = rgba[o + 2];
    }
  }
  const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td) >>> 0);
    return Buffer.concat([len, td, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(W, 0); ihdr.writeUInt32BE(H, 4);
  ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  writeFileSync(path, Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw, { level: 6 })), chunk('IEND', Buffer.alloc(0)),
  ]));
}

let TBL = null;
function crc32(buf) {
  if (!TBL) {
    TBL = new Int32Array(256);
    for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; TBL[n] = c; }
  }
  let c = -1;
  for (let i = 0; i < buf.length; i++) c = TBL[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return c ^ -1;
}

export { W, H };

/**
 * Headless software rasteriser + PNG writer.
 *
 * Used by the probe and by tools/render-check.mjs to look at a build without a
 * GPU: orthographic or perspective camera, z-buffered triangles, flat or
 * material-coloured shading, optional wireframe overlay of the quad topology and
 * a UV checker so texture-scale problems are visible.
 */

import { deflateSync } from 'node:zlib';
import { writeFileSync } from 'node:fs';
import { clamp, type V2, type V3 } from '../core/math';
import { MeshBuilder, MAT_ORDER } from '../core/mesh';

export interface Camera {
  /** eye position */
  eye: V3;
  /** look-at target */
  target: V3;
  /** up vector */
  up: V3;
  /** vertical field of view (radians) for perspective; ignored in ortho */
  fov: number;
  /** world height of the view for ortho */
  orthoHeight: number;
  projection: 'perspective' | 'ortho';
  width: number;
  height: number;
}

export interface RasterOptions {
  /** fill mode */
  shade: 'flat' | 'material' | 'uv' | 'normal' | 'wire';
  /** draw quad wireframe on top */
  wireframe: boolean;
  /** 0..1 strength of the directional light */
  light: number;
  /** paint triangles whose geometric normal faces down */
  debugBackfaces?: boolean;
  /** only draw triangles whose material passes this test (layer isolation) */
  matFilter?: (kind: string) => boolean;
}

const MAT_COLOR: Record<string, [number, number, number]> = {
  asphalt: [58, 58, 61],
  'asphalt-worn': [74, 73, 70],
  'asphalt-fresh': [42, 42, 45],
  concrete: [148, 146, 141],
  'paver-brick': [150, 84, 70],
  'paver-stone': [166, 160, 150],
  slabs: [176, 172, 164],
  cobble: [122, 118, 110],
  tactile: [196, 176, 92],
  gravel: [140, 132, 118],
  soil: [92, 74, 58],
  grass: [86, 118, 62],
  ballast: [104, 100, 96],
  steel: [140, 146, 154],
  paint: [232, 232, 226],
  wood: [126, 96, 62],
  rubber: [46, 46, 48],
  water: [40, 70, 92],
  kerb: [172, 170, 164],
  'road-plate': [96, 98, 104],
};

export function matColor(kind: string): [number, number, number] {
  return MAT_COLOR[kind] ?? [120, 120, 120];
}

export function materialColors(): string[] {
  return MAT_ORDER.map((m) => '#' + matColor(m).map((c) => c.toString(16).padStart(2, '0')).join(''));
}

function v3sub(a: V3, b: V3): V3 {
  return [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
}
function v3cross(a: V3, b: V3): V3 {
  return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
}
function v3dot(a: V3, b: V3): number {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
function v3norm(a: V3): V3 {
  const l = Math.hypot(a[0], a[1], a[2]) || 1;
  return [a[0] / l, a[1] / l, a[2] / l];
}

export class Framebuffer {
  readonly w: number;
  readonly h: number;
  readonly color: Float32Array;
  readonly depth: Float32Array;

  constructor(w: number, h: number) {
    this.w = w;
    this.h = h;
    this.color = new Float32Array(w * h * 3);
    this.depth = new Float32Array(w * h);
    for (let i = 0; i < w * h; i++) this.depth[i] = Infinity;
    // graphite background, matching the app shell
    for (let i = 0; i < w * h; i++) {
      const g = 0.13;
      this.color[i * 3] = g;
      this.color[i * 3 + 1] = g;
      this.color[i * 3 + 2] = g + 0.01;
    }
  }

  clear(r: number, g: number, b: number): void {
    for (let i = 0; i < this.w * this.h; i++) {
      this.color[i * 3] = r;
      this.color[i * 3 + 1] = g;
      this.color[i * 3 + 2] = b;
      this.depth[i] = Infinity;
    }
  }

  blend(x: number, y: number, r: number, g: number, b: number): void {
    if (x < 0 || y < 0 || x >= this.w || y >= this.h) return;
    const i = (y * this.w + x) * 3;
    this.color[i] = r;
    this.color[i + 1] = g;
    this.color[i + 2] = b;
  }
}

export interface RasterStats {
  trisDrawn: number;
  trisCulled: number;
  pixels: number;
}

/** Draw a mesh with a z-buffered triangle rasteriser. */
export function rasterMesh(fb: Framebuffer, builder: MeshBuilder, cam: Camera, opts: RasterOptions): RasterStats {
  const fwd = v3norm(v3sub(cam.target, cam.eye));
  let right = v3norm(v3cross(fwd, cam.up));
  const up = v3cross(right, fwd);
  right = v3norm(right);
  const aspect = cam.width / cam.height;
  const tanHalf = Math.tan(cam.fov / 2);
  const stats: RasterStats = { trisDrawn: 0, trisCulled: 0, pixels: 0 };

  const project = (p: V3): { x: number; y: number; z: number } | null => {
    const rel = v3sub(p, cam.eye);
    const camZ = v3dot(rel, fwd);
    if (cam.projection === 'perspective') {
      if (camZ <= 0.05) return null;
      const ndcX = v3dot(rel, right) / (camZ * tanHalf * aspect);
      const ndcY = v3dot(rel, up) / (camZ * tanHalf);
      return { x: ((ndcX + 1) / 2) * fb.w, y: ((1 - ndcY) / 2) * fb.h, z: camZ };
    }
    const ndcX = v3dot(rel, right) / ((cam.orthoHeight * aspect) / 2);
    const ndcY = v3dot(rel, up) / (cam.orthoHeight / 2);
    return { x: ((ndcX + 1) / 2) * fb.w, y: ((1 - ndcY) / 2) * fb.h, z: v3dot(rel, fwd) };
  };

  const clip = { minX: 0, minY: 0, maxX: fb.w - 1, maxY: fb.h - 1 };
  const edges: Array<[number, number]> = [];
  const tris = builder.tris;

  for (let t = 0; t + 2 < tris.length; t += 3) {
    const ia = tris[t];
    const ib = tris[t + 1];
    const ic = tris[t + 2];
    const pa = builder.verts[ia];
    const pb = builder.verts[ib];
    const pc = builder.verts[ic];
    if (opts.matFilter && !opts.matFilter(MAT_ORDER[pa.mat] ?? 'asphalt')) continue;
    const sa = project(pa.p);
    const sb = project(pb.p);
    const sc = project(pc.p);
    if (!sa || !sb || !sc) {
      stats.trisCulled++;
      continue;
    }
    const area = (sb.x - sa.x) * (sc.y - sa.y) - (sc.x - sa.x) * (sb.y - sa.y);
    if (Math.abs(area) < 1e-7) {
      stats.trisCulled++;
      continue;
    }
    // face normal for shading
    const fn = v3norm(v3cross(v3sub(pb.p, pa.p), v3sub(pc.p, pa.p)));
    const lightDir = v3norm([-0.45, 0.82, 0.35]);
    const l = clamp(v3dot(fn, lightDir), 0, 1);
    const ambient = 0.42;
    const shade = ambient + (1 - ambient) * l * opts.light;

    const base = pa.mat;
    const kind = MAT_ORDER[base] ?? 'asphalt';
    let col = matColor(kind);
    if (opts.shade === 'material') col = matColor(kind);
    else if (opts.shade === 'normal') col = [(fn[0] + 1) / 2, (fn[1] + 1) / 2, (fn[2] + 1) / 2];
    else if (opts.shade === 'uv') {
      const checker = ((Math.floor(pa.uv[0]) + Math.floor(pa.uv[1])) & 1) === 0 ? 1 : 0.45;
      const checker2 = ((Math.floor(pa.uv[0] * 4) + Math.floor(pa.uv[1] * 4)) & 1) === 0 ? 1 : 0.75;
      col = [col[0] * checker * checker2, col[1] * checker * checker2, col[2] * checker * checker2];
    } else if (opts.shade === 'wire') col = [22, 22, 24];
    if (opts.debugBackfaces && fn[1] < -0.12) col = [255, 20, 90];

    const minX = Math.max(clip.minX, Math.floor(Math.min(sa.x, sb.x, sc.x) - 1));
    const maxX = Math.min(clip.maxX, Math.ceil(Math.max(sa.x, sb.x, sc.x) + 1));
    const minY = Math.max(clip.minY, Math.floor(Math.min(sa.y, sb.y, sc.y) - 1));
    const maxY = Math.min(clip.maxY, Math.ceil(Math.max(sa.y, sb.y, sc.y) + 1));
    if (maxX < minX || maxY < minY) {
      stats.trisCulled++;
      continue;
    }
    const inv = 1 / area;
    let drawn = false;
    for (let y = minY; y <= maxY; y++) {
      for (let x = minX; x <= maxX; x++) {
        const px = x + 0.5;
        const py = y + 0.5;
        // barycentric weights: lc is opposite AB, lb opposite AC, la at A
        const lc = ((sb.x - sa.x) * (py - sa.y) - (px - sa.x) * (sb.y - sa.y)) * inv;
        const lb = ((px - sa.x) * (sc.y - sa.y) - (sc.x - sa.x) * (py - sa.y)) * inv;
        const la = 1 - lb - lc;
        if (la < -1e-6 || lb < -1e-6 || lc < -1e-6) continue;
        const z = la * sa.z + lb * sb.z + lc * sc.z;
        const di = y * fb.w + x;
        if (z >= fb.depth[di]) continue;
        fb.depth[di] = z;
        fb.color[di * 3] = col[0] * shade * 0.00392;
        fb.color[di * 3 + 1] = col[1] * shade * 0.00392;
        fb.color[di * 3 + 2] = col[2] * shade * 0.00392;
        drawn = true;
        stats.pixels++;
      }
    }
    if (drawn) stats.trisDrawn++;
  }

  if (opts.wireframe) {
    for (let q = 0; q + 3 < builder.quads.length; q += 4) {
      const idx = [builder.quads[q], builder.quads[q + 1], builder.quads[q + 2], builder.quads[q + 3]];
      edges.length = 0;
      for (let i = 0; i < 4; i++) {
        const v0 = builder.verts[idx[i]];
        const v1 = builder.verts[idx[(i + 1) % 4]];
        const s0 = project(v0.p);
        const s1 = project(v1.p);
        if (!s0 || !s1) continue;
        drawLine(fb, s0, s1, v0, v1, opts.shade === 'wire' ? [0.8, 0.85, 0.9] : [0.05, 0.05, 0.06], 0.55);
      }
    }
  }
  return stats;
}

function drawLine(
  fb: Framebuffer,
  a: { x: number; y: number; z: number },
  b: { x: number; y: number; z: number },
  va: { p: V3 },
  vb: { p: V3 },
  col: [number, number, number],
  alpha: number,
): void {
  const steps = Math.max(1, Math.ceil(Math.max(Math.abs(b.x - a.x), Math.abs(b.y - a.y))));
  for (let i = 0; i <= steps; i++) {
    const t = i / steps;
    const x = Math.round(a.x + (b.x - a.x) * t);
    const y = Math.round(a.y + (b.y - a.y) * t);
    if (x < 0 || y < 0 || x >= fb.w || y >= fb.h) continue;
    const z = a.z + (b.z - a.z) * t;
    const di = y * fb.w + x;
    if (z > fb.depth[di] + 0.06) continue;
    const i3 = di * 3;
    fb.color[i3] = fb.color[i3] * (1 - alpha) + col[0] * alpha;
    fb.color[i3 + 1] = fb.color[i3 + 1] * (1 - alpha) + col[1] * alpha;
    fb.color[i3 + 2] = fb.color[i3 + 2] * (1 - alpha) + col[2] * alpha;
    void va;
    void vb;
  }
}

/** Overlay markers (used to show nodes, junction centres, mouth points...). */
export function drawMarker(fb: Framebuffer, cam: Camera, world: V3, col: [number, number, number], radius = 3, text?: string): void {
  const fwd = v3norm(v3sub(cam.target, cam.eye));
  const right = v3norm(v3cross(fwd, cam.up));
  const up = v3cross(right, fwd);
  const rel = v3sub(world, cam.eye);
  const aspect = cam.width / cam.height;
  let x: number;
  let y: number;
  if (cam.projection === 'perspective') {
    const camZ = v3dot(rel, fwd);
    if (camZ <= 0.05) return;
    const tanHalf = Math.tan(cam.fov / 2);
    x = (((v3dot(rel, right) / (camZ * tanHalf * aspect)) + 1) / 2) * fb.w;
    y = ((1 - v3dot(rel, up) / (camZ * tanHalf)) / 2) * fb.h;
  } else {
    const oh = cam.orthoHeight;
    x = (((v3dot(rel, right) / ((oh * aspect) / 2)) + 1) / 2) * fb.w;
    y = ((1 - v3dot(rel, up) / (oh / 2)) / 2) * fb.h;
  }
  for (let dy = -radius; dy <= radius; dy++) {
    for (let dx = -radius; dx <= radius; dx++) {
      if (dx * dx + dy * dy > radius * radius) continue;
      const px = Math.round(x) + dx;
      const py = Math.round(y) + dy;
      if (px < 0 || py < 0 || px >= fb.w || py >= fb.h) continue;
      const i = (py * fb.w + px) * 3;
      fb.color[i] = col[0];
      fb.color[i + 1] = col[1];
      fb.color[i + 2] = col[2];
    }
  }
  if (text) drawText(fb, Math.round(x) + radius + 2, Math.round(y) - 4, text, col);
}

const FONT: Record<string, string[]> = {
  '0': ['111', '101', '101', '101', '111'],
  '1': ['010', '110', '010', '010', '111'],
  '2': ['111', '001', '111', '100', '111'],
  '3': ['111', '001', '111', '001', '111'],
  '4': ['101', '101', '111', '001', '001'],
  '5': ['111', '100', '111', '001', '111'],
  '6': ['111', '100', '111', '101', '111'],
  '7': ['111', '001', '001', '001', '001'],
  '8': ['111', '101', '111', '101', '111'],
  '9': ['111', '101', '111', '001', '111'],
  A: ['111', '101', '111', '101', '101'],
  B: ['110', '101', '110', '101', '110'],
  C: ['111', '100', '100', '100', '111'],
  D: ['110', '101', '101', '101', '110'],
  E: ['111', '100', '110', '100', '111'],
  F: ['111', '100', '110', '100', '100'],
  G: ['111', '100', '101', '101', '111'],
  H: ['101', '101', '111', '101', '101'],
  I: ['111', '010', '010', '010', '111'],
  J: ['001', '001', '001', '101', '111'],
  K: ['101', '101', '110', '101', '101'],
  L: ['100', '100', '100', '100', '111'],
  M: ['101', '111', '111', '101', '101'],
  N: ['101', '111', '111', '111', '101'],
  O: ['111', '101', '101', '101', '111'],
  P: ['111', '101', '111', '100', '100'],
  Q: ['111', '101', '101', '111', '001'],
  R: ['111', '101', '110', '101', '101'],
  S: ['111', '100', '111', '001', '111'],
  T: ['111', '010', '010', '010', '010'],
  U: ['101', '101', '101', '101', '111'],
  V: ['101', '101', '101', '101', '010'],
  W: ['101', '101', '111', '111', '101'],
  X: ['101', '101', '010', '101', '101'],
  Y: ['101', '101', '010', '010', '010'],
  Z: ['111', '001', '010', '100', '111'],
  '-': ['000', '000', '111', '000', '000'],
  '.': ['000', '000', '000', '000', '010'],
  ' ': ['000', '000', '000', '000', '000'],
  ':': ['000', '010', '000', '010', '000'],
};

export function drawText(fb: Framebuffer, x0: number, y0: number, text: string, col: [number, number, number]): void {
  let x = x0;
  for (const ch of text.toUpperCase()) {
    const glyph = FONT[ch] ?? FONT[' '];
    for (let r = 0; r < glyph.length; r++) {
      for (let c = 0; c < glyph[r].length; c++) {
        if (glyph[r][c] !== '1') continue;
        const px = x + c;
        const py = y0 + r;
        if (px < 0 || py < 0 || px >= fb.w || py >= fb.h) continue;
        const i = (py * fb.w + px) * 3;
        fb.color[i] = col[0];
        fb.color[i + 1] = col[1];
        fb.color[i + 2] = col[2];
      }
    }
    x += 4;
  }
}

/** Write an 8-bit RGB PNG. */
export function writePng(fb: Framebuffer, path: string): void {
  const w = fb.w;
  const h = fb.h;
  const raw = Buffer.alloc(h * (w * 3 + 1));
  for (let y = 0; y < h; y++) {
    raw[y * (w * 3 + 1)] = 0;
    for (let x = 0; x < w; x++) {
      const i = (y * w + x) * 3;
      const o = y * (w * 3 + 1) + 1 + x * 3;
      raw[o] = clamp(Math.round(fb.color[i] * 255), 0, 255);
      raw[o + 1] = clamp(Math.round(fb.color[i + 1] * 255), 0, 255);
      raw[o + 2] = clamp(Math.round(fb.color[i + 2] * 255), 0, 255);
    }
  }
  const chunks: Buffer[] = [Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])];
  const crcTable: number[] = [];
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    crcTable[n] = c >>> 0;
  }
  const crc = (buf: Buffer): number => {
    let c = 0xffffffff;
    for (const b of buf) c = crcTable[(c ^ b) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  };
  const chunk = (type: string, data: Buffer): Buffer => {
    const len = Buffer.alloc(4);
    len.writeUInt32BE(data.length);
    const t = Buffer.from(type, 'ascii');
    const body = Buffer.concat([t, data]);
    const c = Buffer.alloc(4);
    c.writeUInt32BE(crc(body));
    return Buffer.concat([len, body, c]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8;
  ihdr[9] = 2;
  chunks.push(chunk('IHDR', ihdr));
  chunks.push(chunk('IDAT', deflateSync(raw, { level: 9 })));
  chunks.push(chunk('IEND', Buffer.alloc(0)));
  writeFileSync(path, Buffer.concat(chunks));
}

export type { V2 };

/** Write an 8-bit RGBA buffer (e.g. from a GPU readback) as a PNG. */
export function writePngFromRgba(path: string, width: number, height: number, rgba: Uint8Array, flipY = false): void {
  const raw = Buffer.alloc(height * (width * 3 + 1));
  for (let y = 0; y < height; y++) {
    const sy = flipY ? height - 1 - y : y;
    raw[y * (width * 3 + 1)] = 0;
    for (let x = 0; x < width; x++) {
      const si = (sy * width + x) * 4;
      const o = y * (width * 3 + 1) + 1 + x * 3;
      raw[o] = Math.min(255, Math.max(0, rgba[si]));
      raw[o + 1] = Math.min(255, Math.max(0, rgba[si + 1]));
      raw[o + 2] = Math.min(255, Math.max(0, rgba[si + 2]));
    }
  }
  const chunks: Buffer[] = [Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])];
  const crcTable: number[] = [];
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    crcTable[n] = c >>> 0;
  }
  const crc = (buf: Buffer): number => {
    let c = 0xffffffff;
    for (const b of buf) c = crcTable[(c ^ b) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  };
  const chunk = (type: string, data: Buffer): Buffer => {
    const len = Buffer.alloc(4);
    len.writeUInt32BE(data.length);
    const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const c = Buffer.alloc(4);
    c.writeUInt32BE(crc(body));
    return Buffer.concat([len, body, c]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;
  ihdr[9] = 2;
  chunks.push(chunk('IHDR', ihdr));
  chunks.push(chunk('IDAT', deflateSync(raw, { level: 9 })));
  chunks.push(chunk('IEND', Buffer.alloc(0)));
  writeFileSync(path, Buffer.concat(chunks));
}

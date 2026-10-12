// Paint helpers: lines follow the shared centreline frames so markings hug every curve.
import { quad } from './out.js';
import { decalUV, DECALS } from '../decals.js';

export const PAINT_LIFT = 0.012;

// Solid strip along centreline from s0..s1 at lateral offset o (width w). h = level above frame.y.
export function line(out, mat, c, s0, s1, o, w, h = 0, hFn = null) {
  if (s1 - s0 < 0.05) return;
  const pc = out.piece(mat), base = pc.p.length / 3;
  const st = c.stations(s0, s1, [], 4, 0.02);
  const f = {};
  for (const s of st) {
    c.frameAt(s, f);
    const y = f.y + h + PAINT_LIFT + (hFn ? hFn(s) : 0);
    const a = o - w / 2, b = o + w / 2;
    out.vert(pc, f.x + f.nx * a, y, f.z + f.nz * a, 0, s);
    out.vert(pc, f.x + f.nx * b, y, f.z + f.nz * b, 1, s);
  }
  for (let k = 0; k < st.length - 1; k++) {
    const l0 = base + k * 2;
    pc.i.push(l0, l0 + 1, l0 + 2, l0 + 1, l0 + 3, l0 + 2);
  }
}

export function dashed(out, mat, c, s0, s1, o, w, dash, gap, h = 0) {
  const period = dash + gap;
  let s = Math.ceil(s0 / period) * period;
  if (s - s0 > gap) line(out, mat, c, s0, Math.min(s1, s - gap), o, w, h);
  for (; s < s1; s += period) line(out, mat, c, s, Math.min(s1, s + dash), o, w, h);
}

// Decal: oriented along travel. dir = +1 reads toward +s, -1 toward -s.
const tmp = {};
export function decal(out, name, c, s, o, dir, h = 0, scale = 1) {
  const d = DECALS[name];
  c.frameAt(s, tmp);
  const tx = tmp.tx * dir, tz = tmp.tz * dir;
  quad(out, 'decal', tmp.x + tmp.nx * o, tmp.y + h + PAINT_LIFT + 0.002, tmp.z + tmp.nz * o, tx, tz, d.l / 2 * scale, d.w / 2 * scale, decalUV(name));
}

// Rectangle bar perpendicular-ish: centre at (s, o), half sizes along/across.
export function bar(out, mat, c, s, o, halfL, halfW, h = 0) {
  c.frameAt(s, tmp);
  quad(out, mat, tmp.x + tmp.nx * o, tmp.y + h + PAINT_LIFT, tmp.z + tmp.nz * o, tmp.tx, tmp.tz, halfL, halfW);
}

// Triangle (shark tooth) pointing toward -dir along the road.
export function tooth(out, mat, c, s, o, dir, base = 0.6, height = 0.9, h = 0) {
  c.frameAt(s, tmp);
  const pc = out.piece(mat), b = pc.p.length / 3;
  const tx = tmp.tx * dir, tz = tmp.tz * dir, nx = -tz, nz = tx;
  const cx = tmp.x + tmp.nx * o, cz = tmp.z + tmp.nz * o, y = tmp.y + h + PAINT_LIFT;
  out.vert(pc, cx - nx * base / 2, y, cz - nz * base / 2, 0, 0);
  out.vert(pc, cx + nx * base / 2, y, cz + nz * base / 2, 1, 0);
  out.vert(pc, cx - tx * height, y, cz - tz * height, 0.5, 1);
  pc.i.push(b, b + 2, b + 1);
}

// Diagonal hatch inside a lateral band (o0..o1) from s0..s1.
export function hatch(out, mat, c, s0, s1, o0, o1, spacing = 3.5, w = 0.18, h = 0) {
  const span = o1 - o0;
  for (let s = s0 + spacing / 2; s < s1 - span; s += spacing) {
    const pc = out.piece(mat), b = pc.p.length / 3;
    const f0 = c.frameAt(s, {}), f1 = c.frameAt(s + span, {});
    const y0 = f0.y + h + PAINT_LIFT, y1 = f1.y + h + PAINT_LIFT;
    const p = (f, o, y, ds) => [f.x + f.nx * o + f.tx * ds, y, f.z + f.nz * o + f.tz * ds];
    const A = p(f0, o0, y0, -w), B = p(f0, o0, y0, w), C = p(f1, o1, y1, -w), D = p(f1, o1, y1, w);
    for (const v of [A, B, C, D]) out.vert(pc, v[0], v[1], v[2], 0, 0);
    pc.i.push(b, b + 2, b + 1, b + 1, b + 2, b + 3);
  }
}

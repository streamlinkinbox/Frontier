// A still of the tablet, rasterised on the CPU.
//
// 🔴 WHY THIS EXISTS.
//
//    There is no headless WebGPU available here — @kmamal/gpu wants to clone chromium, and the
//    dawn-node packages do not exist. So every claim about how this panel LOOKS has had to be taken
//    on trust, and CheckHud.mjs is careful to say it checks numbers and not pixels. That is a bad
//    place for a UI to live.
//
//    This renderer closes it. It imports the real modules — the same constructHudLayout, the same
//    resolve/pack, the same StreakPreset — evaluates the same signed distance functions, and writes
//    a PNG. It is not a mock-up or a drawing of what the panel should look like: it consumes the
//    identical forty-float figure slots the GPU consumes, in the identical submission order.
//
// 🔴 WHERE IT DIFFERS FROM THE GPU, STATED PLAINLY.
//
//    1. Figures are resolved by ray-plane intersection per pixel rather than by rasterising a strip.
//       Same coverage, different route to it. PixelWidth comes from a finite difference against the
//       neighbouring pixel's ray, which is exactly what fwidth estimates.
//    2. Fibres take the minimum perpendicular distance to the strand polyline instead of relying on
//       the expanded quad. The GPU gets one fragment per pixel per strand; so does this.
//    3. No MSAA and no sub-pixel jitter beyond a 2x supersample.
//    4. The glyph outlines are converted out of the .slang at run time (see StrokeGlyphs below), so
//       there is still exactly one authority for the font.
//
// Usage:  node RenderStill.mjs            → all three stills into Stills/
//         node RenderStill.mjs --check    → renders small and asserts the panel is not blank

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { deflateSync } from 'node:zlib';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { pathToFileURL } from 'node:url';

import { constructHudLayout, assignValues } from './js/layout.js';
import { constructShellLayout, assignShellValues } from './js/shell.js';
import { resolve, pack, FloatsPerFigure, Category } from './js/figures.js';
import { StreakPreset, packFibre, FibreFloats } from './js/fibres.js';
import { Chassis } from './js/chassis.js';
import { Approach } from './js/display.js';
import { Glass, DisplayWidth, DisplayHeight, BloomWidth, BloomHeight,
         DisplayProjectionScale } from './js/display.js';
import { PanelHalfWidth, PanelHalfHeight } from './js/layout.js';

const Here = dirname(fileURLToPath(import.meta.url));

// ── scalar helpers ───────────────────────────────────────────────────────────────────────────────

const clamp = (x, a, b) => (x < a ? a : x > b ? b : x);
const mix = (a, b, t) => a + (b - a) * t;
const fract = (x) => x - Math.floor(x);
const length2 = (x, y) => Math.hypot(x, y);

function smoothstep(edge0, edge1, x) {
  const t = clamp((x - edge0) / (edge1 - edge0 || 1e-9), 0, 1);
  return t * t * (3 - 2 * t);
}

// ── the stroke font, converted out of the .slang at run time ─────────────────────────────────────
// The glyph outlines are two hundred lines of StrokeSegment/StrokeArc calls. Copying them here would
// create a second authority for the font, which is the thing GenerateHudShader.py exists to prevent.
// Instead the body of DistanceStrokeGlyph is lifted from the shader and mechanically converted: the
// subset used is small enough that the conversion is three substitutions and a balanced-paren split.

function SplitArguments(text) {
  const out = [];
  let depth = 0, start = 0;
  for (let at = 0; at < text.length; at++) {
    const ch = text[at];
    if (ch === '(' || ch === '[') depth++;
    else if (ch === ')' || ch === ']') depth--;
    else if (ch === ',' && depth === 0) { out.push(text.slice(start, at)); start = at + 1; }
  }
  out.push(text.slice(start));
  return out;
}

// vec2(a, b) → [a, b], innermost first so nesting resolves.
function ConvertVectors(text) {
  for (let guard = 0; guard < 100000; guard++) {
    const at = text.lastIndexOf('vec2(');
    if (at < 0) break;
    let depth = 0, close = -1;
    for (let scan = at + 4; scan < text.length; scan++) {
      if (text[scan] === '(') depth++;
      else if (text[scan] === ')') { depth--; if (depth === 0) { close = scan; break; } }
    }
    if (close < 0) throw new Error('RenderStill: unbalanced vec2 in the glyph outlines');
    const inner = text.slice(at + 5, close);
    text = `${text.slice(0, at)}[${SplitArguments(inner).join(',')}]${text.slice(close + 1)}`;
  }
  return text;
}

function BuildStrokeGlyphs() {
  const slang = readFileSync(join(Here, '../../Engine/Shaders/InterfaceSignedDistance.slang'), 'utf8');
  const opened = slang.indexOf('float DistanceStrokeGlyph(');
  if (opened < 0) throw new Error('RenderStill: DistanceStrokeGlyph is not in the .slang any more');
  const brace = slang.indexOf('{', opened);
  let depth = 0, close = -1;
  for (let at = brace; at < slang.length; at++) {
    if (slang[at] === '{') depth++;
    else if (slang[at] === '}') { depth--; if (depth === 0) { close = at; break; } }
  }
  let body = slang.slice(brace + 1, close);

  body = body.replace(/\/\/[^\n]*/g, '');          // comments carry glyph names, not code
  body = ConvertVectors(body);
  body = body.replace(/\b(\d+)u\b/g, '$1');        // 65u → 65
  body = body.replace(/\b0x([0-9A-Fa-f]+)u\b/g, '0x$1');
  body = body.replace(/\bconst float\b/g, 'const');
  body = body.replace(/\bfloat\b/g, 'let');
  body = body.replace(/\buint\b/g, 'let');
  body = body.replace(/\bmin\(/g, 'Math.min(');

  const made = new Function('P', 'Code', 'HalfThickness', 'StrokeSegment', 'StrokeArc', body);

  // 🔴 A converted function that silently produces garbage is worse than no renderer at all, so it
  //    is pinned before it is used: on the stem of an E, and in the hole of an O.
  const T = 0.07;
  const onStem = made([0.18, 0.5], 69, T, StrokeSegment, StrokeArc);
  const inHole = made([0.5, 0.5], 79, T, StrokeSegment, StrokeArc);
  const unmapped = made([0.5, 0.5], 1, T, StrokeSegment, StrokeArc);
  if (!(onStem < 0)) throw new Error(`RenderStill: the E stem should be inside the stroke, got ${onStem}`);
  if (!(inHole > 0.2)) throw new Error(`RenderStill: the O centre should be well outside, got ${inHole}`);
  if (!(unmapped > 1e8)) throw new Error('RenderStill: an unmapped code should draw nothing');
  return made;
}

function StrokeSegment(P, A, B, HalfThickness) {
  const sx = B[0] - A[0], sy = B[1] - A[1];
  const lx = P[0] - A[0], ly = P[1] - A[1];
  const along = clamp((lx * sx + ly * sy) / Math.max(sx * sx + sy * sy, 1e-6), 0, 1);
  return length2(lx - sx * along, ly - sy * along) - HalfThickness;
}

function StrokeArc(P, C, Radius, Start, Sweep, HalfThickness) {
  const lx = P[0] - C[0], ly = P[1] - C[1];
  let delta = Math.atan2(ly, lx) - Start;
  delta -= 6.28318530718 * Math.floor(delta / 6.28318530718);
  if (delta <= Math.abs(Sweep) || Sweep >= 6.28318530718) {
    return Math.abs(length2(lx, ly) - Radius) - HalfThickness;
  }
  const ax = C[0] + Math.cos(Start) * Radius, ay = C[1] + Math.sin(Start) * Radius;
  const bx = C[0] + Math.cos(Start + Sweep) * Radius, by = C[1] + Math.sin(Start + Sweep) * Radius;
  return Math.min(length2(P[0] - ax, P[1] - ay), length2(P[0] - bx, P[1] - by)) - HalfThickness;
}

const DistanceStrokeGlyph = BuildStrokeGlyphs();

// ── the shapes, ported from InterfaceSignedDistance.slang ────────────────────────────────────────

const ArcStart = -3.6651914;
const ArcSweep = 4.1887902;
const Tau = 6.28318530717959;
const Epsilon = 1.0e-6;

function DistanceRoundedRectangle(px, py, hx, hy, radius) {
  const ix = Math.max(hx - radius, 0), iy = Math.max(hy - radius, 0);
  const dx = Math.abs(px) - ix, dy = Math.abs(py) - iy;
  return length2(Math.max(dx, 0), Math.max(dy, 0)) + Math.min(Math.max(dx, dy), 0) - radius;
}

function DistanceArc(px, py, start, sweep, fill, radius, thickness) {
  const extent = sweep * clamp(fill, 0, 1);
  const radial = Math.abs(length2(px, py) - radius) - thickness * 0.5;
  if (Math.abs(extent) < Epsilon) return 1e9;

  let offset = Math.atan2(py, px) - start;
  offset -= Tau * Math.floor(offset / Tau);
  const span = Math.abs(extent);
  if (extent < 0) offset = Tau - offset;
  if (offset <= span) return radial;

  const nearest = (offset - span) < (Tau - offset) ? span : 0;
  const capAngle = start + (extent < 0 ? -nearest : nearest);
  return length2(px - Math.cos(capAngle) * radius, py - Math.sin(capAngle) * radius) - thickness * 0.5;
}

function DistanceTickRing(px, py, start, sweep, count, radius, thickness, extent) {
  if (count < 1) return 1e9;
  if (length2(px, py) < Epsilon) return 1e9;

  let offset = Math.atan2(py, px) - start;
  offset -= Tau * Math.floor(offset / Tau);
  const span = Math.abs(sweep);
  if (offset > span + Epsilon) return 1e9;

  const step = span / Math.max(count - 1, 1);
  const index = Math.floor(offset / step + 0.5);
  const snapped = start + clamp(index, 0, count - 1) * step * (sweep < 0 ? -1 : 1);

  const ax = Math.cos(snapped), ay = Math.sin(snapped);
  const along = px * ax + py * ay;
  const across = px * -ay + py * ax;

  const radially = Math.abs(along - (radius - extent * 0.5)) - extent * 0.5;
  const laterally = Math.abs(across) - thickness * 0.5;
  return length2(Math.max(radially, 0), Math.max(laterally, 0)) + Math.min(Math.max(radially, laterally), 0);
}

function DistanceNeedle(px, py, angle, extent, root, waist) {
  const ax = Math.cos(angle), ay = Math.sin(angle);
  const along = px * ax + py * ay;
  const across = px * -ay + py * ax;
  const travel = clamp(along / Math.max(extent, Epsilon), 0, 1);
  const width = mix(root, waist, travel) * 0.5;
  const beyond = Math.max(along - extent, -along);
  const lateral = Math.abs(across) - width;
  return length2(Math.max(beyond, 0), Math.max(lateral, 0)) + Math.min(Math.max(beyond, lateral), 0);
}

const SegmentBits = [0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f, 0x00, 0x40];

function DistanceSegmentDigit(px, py, digit, hw, hh, thickness) {
  const lit = SegmentBits[digit] ?? 0;
  if (lit === 0) return 1e9;

  const inset = thickness * 0.75;
  const armH = Math.max(hh * 0.5 - inset, thickness);
  const armW = Math.max(hw - inset, thickness);
  const bar = (cx, cy, ex, ey) => DistanceRoundedRectangle(px - cx, py - cy, ex, ey, thickness * 0.5);

  let best = 1e9;
  if (lit & 0x01) best = Math.min(best, bar(0, hh, armW, thickness * 0.5));
  if (lit & 0x02) best = Math.min(best, bar(hw, hh * 0.5, thickness * 0.5, armH));
  if (lit & 0x04) best = Math.min(best, bar(hw, -hh * 0.5, thickness * 0.5, armH));
  if (lit & 0x08) best = Math.min(best, bar(0, -hh, armW, thickness * 0.5));
  if (lit & 0x10) best = Math.min(best, bar(-hw, -hh * 0.5, thickness * 0.5, armH));
  if (lit & 0x20) best = Math.min(best, bar(-hw, hh * 0.5, thickness * 0.5, armH));
  if (lit & 0x40) best = Math.min(best, bar(0, 0, armW, thickness * 0.5));
  return best;
}

function DistanceFigure(category, px, py, hw, hh, cornerRadius, alpha, beta) {
  if (category === Category.Surface) return DistanceRoundedRectangle(px, py, hw, hh, cornerRadius);
  if (category === Category.Arc) {
    const radius = Math.min(hw, hh) - beta * 0.5;
    return DistanceArc(px, py, ArcStart, ArcSweep, alpha, Math.max(radius, Epsilon), beta);
  }
  if (category === Category.TickRing) {
    const radius = Math.min(hw, hh);
    return DistanceTickRing(px, py, ArcStart, ArcSweep, beta, radius, radius * 0.028, radius * 0.18);
  }
  if (category === Category.Needle) {
    const extent = Math.min(hw, hh);
    const angle = ArcStart + ArcSweep * clamp(alpha, 0, 1);
    const pointer = DistanceNeedle(px, py, angle, extent, extent * 0.075, extent * 0.018);
    return Math.min(pointer, length2(px, py) - Math.max(beta, extent * 0.06));
  }
  if (category === Category.SegmentCell) {
    return DistanceSegmentDigit(px, py, Math.trunc(clamp(alpha, 0, 11)), hw, hh, Math.max(beta, Epsilon));
  }
  if (category === Category.Lamp) return length2(px, py) - Math.min(hw, hh);
  if (category === Category.Glyph) {
    const bx = Math.max(hw, Epsilon), by = Math.max(hh, Epsilon);
    const scale = Math.min(2 * bx, 2 * by);
    const half = Math.max(beta, Epsilon) / Math.max(scale, Epsilon);
    const unit = [px / (2 * bx) + 0.5, py / (2 * by) + 0.5];
    return DistanceStrokeGlyph(unit, Math.trunc(clamp(alpha, 0, 255)), half,
                               StrokeSegment, StrokeArc) * scale;
  }
  return 1e9;
}

const CoverageFromDistance = (distance, pixelWidth) => clamp(0.5 - distance / Math.max(pixelWidth, Epsilon), 0, 1);

// ── the streak field, ported from js/streaks.js ──────────────────────────────────────────────────

function StreakHash(key) {
  let k = key >>> 0;
  k = (k ^ (k >>> 16)) >>> 0;
  k = Math.imul(k, 0x7feb352d) >>> 0;
  k = (k ^ (k >>> 15)) >>> 0;
  k = Math.imul(k, 0x846ca68b) >>> 0;
  return (k ^ (k >>> 16)) >>> 0;
}

function StreakUnit(id, salt, seed) {
  const key = (Math.imul(id >>> 0, 747796405) + Math.imul(salt >>> 0, 2891336453)
             + Math.imul(seed >>> 0, 196613)) >>> 0;
  return StreakHash(key) * (1 / 4294967296);
}

function StreakFieldGlow(px, py, hx, hy, A, B, time) {
  const strands = clamp(A[0], 1, 48), amplitude = A[1], waves = A[2], speed = A[3];
  const tail = Math.max(B[0], 1e-3), seed = B[1], intensity = B[2], core = Math.max(B[3], 1e-4);
  const u = px / Math.max(hx, 1e-6);

  let glow = 0;
  const count = Math.trunc(strands);
  for (let index = 0; index < count; index++) {
    const id = index;
    const row = ((id + 0.5) / strands - 0.5) * 2 * hy;
    const phase = StreakUnit(id, 1, seed) * Tau;
    const pace = 0.55 + 0.9 * StreakUnit(id, 2, seed);
    const swell = 0.45 + 1.1 * StreakUnit(id, 3, seed);
    const bright = 0.35 + 0.65 * StreakUnit(id, 4, seed);

    const drift = time * speed * pace;
    const amp = amplitude * swell;
    const k1 = waves * Math.PI, k2 = k1 * 2.17;
    const a1 = amp * 0.72, a2 = amp * 0.28;
    const height = a1 * Math.sin(k1 * u + phase + drift) + a2 * Math.sin(k2 * u - 1.37 * drift + phase * 1.7);
    const slope = a1 * k1 * Math.cos(k1 * u + phase + drift)
                - a2 * k2 * Math.cos(k2 * u - 1.37 * drift + phase * 1.7) * -1;

    const gap = Math.abs(py - (row + height)) / Math.sqrt(1 + slope * slope);
    const coreGlow = Math.exp(-(gap * gap) / (core * core));
    if (coreGlow < 1e-4) continue;

    const head = fract(phase * 0.15915494 + drift * 0.25) * 2 - 1;
    const behind = head - u;
    const wake = behind >= 0 ? Math.exp(-behind / tail) : Math.exp(behind * 26);
    const spark = Math.exp(-(behind * behind) / 0.0024) * 2.4;
    glow += coreGlow * (wake + spark) * bright;
  }

  const lit = 1 - Math.exp(-glow * Math.max(intensity, 0));
  const edgeX = 1 - smoothstep(0.72, 1.0, Math.abs(px) / Math.max(hx, 1e-6));
  const edgeY = 1 - smoothstep(0.70, 1.0, Math.abs(py) / Math.max(hy, 1e-6));
  return clamp(lit * edgeX * edgeY, 0, 1);
}

// ── the fibres, ported from js/fibres.js ─────────────────────────────────────────────────────────
// Reads the packed uniform rather than the preset object, so it sees exactly what the GPU sees.

class Fibres {
  constructor(packed) {
    const v = (slot) => packed.subarray(slot * 4, slot * 4 + 4);
    this.org = v(0); this.dir = v(1); this.p0 = v(2); this.p2 = v(4); this.p3 = v(5);
    this.p4 = v(6); this.p5 = v(7); this.p6 = v(8); this.p7 = v(9); this.p8 = v(10);
    this.colC = v(11);
    this.stopC = [0, 1, 2, 3, 4, 5, 6, 7].map((at) => v(12 + at));
    this.stopP = [v(20), v(21)];
    this.rows = [v(22), v(23), v(24)];
    this.clip = v(25); this.glass = v(26);
  }

  unit(id, salt) {
    const key = (Math.imul(id >>> 0, 747796405) + Math.imul(salt >>> 0, 2891336453)
               + Math.imul(this.p6[3] >>> 0, 196613)) >>> 0;
    return StreakHash(key) * (1 / 4294967296);
  }

  // The cubic Bezier from a root cluster to a reach, in curve space.
  bezier(s, id) {
    const len = this.p0[0], spread = this.p0[1], amp = this.p0[2];
    const k = Math.max(1, Math.floor(this.p0[3] + 0.5));

    const d = this.dir;
    const dl = Math.hypot(d[0], d[1], d[2]);
    const axis = dl > 1e-5 ? [d[0] / dl, d[1] / dl, d[2] / dl] : [1, 0, 0];
    const up = Math.abs(axis[1]) > 0.9 ? [0, 0, 1] : [0, 1, 0];
    const lx = up[1] * axis[2] - up[2] * axis[1];
    const ly = up[2] * axis[0] - up[0] * axis[2];
    const lz = up[0] * axis[1] - up[1] * axis[0];
    const ll = Math.hypot(lx, ly, lz) || 1;
    const lateral = [lx / ll, ly / ll, lz / ll];
    const normal = [axis[1] * lateral[2] - axis[2] * lateral[1],
                    axis[2] * lateral[0] - axis[0] * lateral[2],
                    axis[0] * lateral[1] - axis[1] * lateral[0]];

    const angle = Tau * this.unit(id, 1);
    const radius = spread * Math.sqrt(this.unit(id, 2));
    const tipAngle = Tau * this.unit(id, 3);
    const tipRadius = spread * 1.6 * Math.sqrt(this.unit(id, 4));
    const wt = Tau * this.p2[1] * this.p5[0];

    const blend = (ca, cb) => [lateral[0] * ca + normal[0] * cb,
                               lateral[1] * ca + normal[1] * cb,
                               lateral[2] * ca + normal[2] * cb];

    const root = blend(Math.cos(angle) * radius, Math.sin(angle) * radius);
    const tip = blend(Math.cos(tipAngle) * tipRadius, Math.sin(tipAngle) * tipRadius);
    const reach = [axis[0] * len + tip[0], axis[1] * len + tip[1], axis[2] * len + tip[2]];

    const w1 = blend(Math.sin(k * wt + Tau * this.unit(id, 5)) * amp,
                     Math.cos(2 * k * wt + Tau * this.unit(id, 6)) * amp);
    const w2 = blend(Math.sin(k * wt + Tau * this.unit(id, 7)) * amp,
                     Math.sin(2 * k * wt + Tau * this.unit(id, 8)) * amp);
    const c1 = [root[0] + axis[0] * len * 0.34 + w1[0],
                root[1] + axis[1] * len * 0.34 + w1[1],
                root[2] + axis[2] * len * 0.34 + w1[2]];
    const c2 = [reach[0] - axis[0] * len * 0.34 + w2[0],
                reach[1] - axis[1] * len * 0.34 + w2[1],
                reach[2] - axis[2] * len * 0.34 + w2[2]];

    const u = 1 - s;
    const a = u * u * u, b = 3 * u * u * s, c = 3 * u * s * s, e = s * s * s;
    return [root[0] * a + c1[0] * b + c2[0] * c + reach[0] * e,
            root[1] * a + c1[1] * b + c2[1] * c + reach[1] * e,
            root[2] * a + c1[2] * b + c2[2] * c + reach[2] * e];
  }

  // Curve space (along, lateral, lift) → panel metres: lateral becomes DEPTH, lift becomes UP.
  panel(local) {
    return [this.org[0] + local[0] * this.org[3],
            this.org[1] + local[2] * this.glass[1] * this.org[3],
            this.org[2] + local[1] * this.glass[2] * this.org[3]];
  }

  world(panel) {
    const r = this.rows;
    return [r[0][0] * panel[0] + r[0][1] * panel[1] + r[0][2] * panel[2] + r[0][3],
            r[1][0] * panel[0] + r[1][1] * panel[1] + r[1][2] * panel[2] + r[1][3],
            r[2][0] * panel[0] + r[2][1] * panel[1] + r[2][2] * panel[2] + r[2][3]];
  }

  // The eye in panel metres. The rotation is orthonormal, so the inverse is its transpose.
  eyePanel(eye) {
    const r = this.rows;
    const dx = eye[0] - r[0][3], dy = eye[1] - r[1][3], dz = eye[2] - r[2][3];
    return [r[0][0] * dx + r[1][0] * dy + r[2][0] * dz,
            r[0][1] * dx + r[1][1] * dy + r[2][1] * dz,
            r[0][2] * dx + r[1][2] * dy + r[2][2] * dz];
  }

  // Where the sightline to this point crosses the glass. The clip belongs at the aperture, not at
  // the strand — see the note beside fbAperture in js/fibres.js.
  aperture(panel, eyeLocal) {
    const rise = panel[2] - eyeLocal[2];
    if (Math.abs(rise) < 1e-7) return [panel[0], panel[1]];
    const cross = -eyeLocal[2] / rise;
    return [eyeLocal[0] + (panel[0] - eyeLocal[0]) * cross,
            eyeLocal[1] + (panel[1] - eyeLocal[1]) * cross];
  }

  glassDepth(z) {
    const sink = Math.exp(Math.min(z, 0) / Math.max(this.clip[3], 1e-4));
    const band = Math.max(this.glass[0], 1e-4);
    return sink * (1 - smoothstep(-band, 0, z));
  }

  pulseLevel(cycle) {
    const gap = (a, b) => { const g = Math.abs(fract(a) - fract(b)); return Math.min(g, 1 - g); };
    if (Math.trunc(this.p5[3]) === 1) {
      const first = Math.exp(-((gap(cycle, 0.10) / 0.045) ** 2));
      const second = 0.55 * Math.exp(-((gap(cycle, 0.27) / 0.055) ** 2));
      return clamp(first + second, 0, 1);
    }
    return 0.5 - 0.5 * Math.cos(Tau * cycle);
  }

  pulse(along) {
    const rate = this.p5[2], depth = this.p4[1], shape = Math.trunc(this.p5[3]);
    if (rate === 0 || depth <= 0) return 1;
    const phase = rate * this.p2[1] - (shape === 2 ? along : 0);
    return 1 - depth + depth * this.pulseLevel(fract(phase));
  }

  colourAt(t, id) {
    const mode = Math.trunc(this.p8[3]);
    const n = Math.max(Math.trunc(this.p8[2]), 1);
    if (mode === 0) return this.stopC[0];
    if (mode === 2) return this.stopC[Math.min(Math.trunc(this.unit(id, 14) * n), n - 1)];
    const at = (i) => this.stopP[Math.floor(i / 4)][i % 4];
    if (n <= 1 || t <= at(0)) return this.stopC[0];
    for (let k = 0; k + 1 < n; k++) {
      const p0 = at(k), p1 = at(k + 1);
      if (t < p1) {
        const f = clamp((t - p0) / Math.max(p1 - p0, 1e-5), 0, 1);
        const A = this.stopC[k], B = this.stopC[k + 1];
        return [mix(A[0], B[0], f), mix(A[1], B[1], f), mix(A[2], B[2], f)];
      }
    }
    return this.stopC[n - 1];
  }
}

// ── the tablet itself, mirrored from js/chassis.js ───────────────────────────────────────────────

const KeyDirection = [-0.3827, -0.6428, 0.6634];

// The card: see the long note beside RoomCard in js/chassis.js. A point lobe cannot show up in a
// flat mirror; a strip can, and a strip is what every photograph of a device actually shows.
function RoomCard(direction, axis, wide, tall, sharpness) {
  const depth = direction[0] * axis[0] + direction[1] * axis[1] + direction[2] * axis[2];
  if (depth <= 0.02) return 0;

  let sx = axis[1] * 1 - axis[2] * 0, sy = axis[2] * 0 - axis[0] * 1, sz = 0;
  const sl = Math.hypot(sx, sy, sz) || 1;
  sx /= sl; sy /= sl; sz /= sl;
  const ux = sy * axis[2] - sz * axis[1];
  const uy = sz * axis[0] - sx * axis[2];
  const uz = sx * axis[1] - sy * axis[0];

  const spread = mix(3.0, 1.0, sharpness);
  const across = (direction[0] * sx + direction[1] * sy + direction[2] * sz) / depth / (wide * spread);
  const along = (direction[0] * ux + direction[1] * uy + direction[2] * uz) / depth / (tall * spread);
  return Math.exp(-(across * across + along * along));
}

const FrontCard = [-0.3302, -0.8805, -0.3402];
const RoomFalloff = 0.55;

const FillDirection = (() => {
  const f = [0.80, -0.36, 0.22];
  const l = Math.hypot(f[0], f[1], f[2]);
  return [f[0] / l, f[1] / l, f[2] / l];
})();

function RoomLight(direction, sharpness) {
  const height = clamp(direction[2] * 0.5 + 0.5, 0, 1);
  const shell = height * height;
  const down = clamp(-direction[2], 0, 1) ** 2;
  const light = [mix(0.0070, 0.0360, shell) + 0.0130 * down,
                 mix(0.0080, 0.0400, shell) + 0.0145 * down,
                 mix(0.0105, 0.0500, shell) + 0.0175 * down];

  const key = RoomCard(direction, KeyDirection, 0.55, 0.070, sharpness) * mix(1.1, 6.0, sharpness);
  light[0] += 1.000 * key; light[1] += 0.985 * key; light[2] += 0.955 * key;

  const front = RoomCard(direction, FrontCard, 1.10, 0.26, sharpness) * mix(0.7, 2.4, sharpness);
  light[0] += 0.940 * front; light[1] += 0.965 * front; light[2] += 1.000 * front;

  const fill = RoomCard(direction, FillDirection, 0.45, 0.30, sharpness) * mix(0.35, 1.5, sharpness);
  light[0] += 0.30 * fill; light[1] += 0.42 * fill; light[2] += 0.62 * fill;
  return light;
}

function Schlick(cosine, base) {
  const f = clamp(1 - cosine, 0, 1);
  return base + (1 - base) * f * f * f * f * f;
}

// Mirrors the world-space march in js/chassis.js: the body in panel space, the desk and the dock
// in world space, all stepped through the world, because a desk expressed in a leaning panel's
// coordinates is a slope.

const Material = { Body: 0, Dock: 1, Desk: 2 };
const DeskFade = 1.5;

class Tablet {
  constructor(rows) {
    this.rows = rows;
    this.half = [Chassis.halfWidth, Chassis.halfHeight, Chassis.halfDepth];
    this.radius = Chassis.cornerRadius;

    // The desk, found rather than guessed: the lowest of the body's eight corners.
    let floor = Infinity;
    for (const sx of [-1, 1]) {
      for (const sy of [-1, 1]) {
        for (const pz of [0, -2 * Chassis.halfDepth]) {
          floor = Math.min(floor, rows[2][0] * sx * Chassis.halfWidth
                                + rows[2][1] * sy * Chassis.halfHeight
                                + rows[2][2] * pz + rows[2][3]);
        }
      }
    }
    this.floor = floor;
  }

  toPanelPoint(world) {
    const r = this.rows;
    const dx = world[0] - r[0][3], dy = world[1] - r[1][3], dz = world[2] - r[2][3];
    return [r[0][0] * dx + r[1][0] * dy + r[2][0] * dz,
            r[0][1] * dx + r[1][1] * dy + r[2][1] * dz,
            r[0][2] * dx + r[1][2] * dy + r[2][2] * dz];
  }

  toPanelDirection(world) {
    const r = this.rows;
    return [r[0][0] * world[0] + r[1][0] * world[1] + r[2][0] * world[2],
            r[0][1] * world[0] + r[1][1] * world[1] + r[2][1] * world[2],
            r[0][2] * world[0] + r[1][2] * world[1] + r[2][2] * world[2]];
  }

  toWorldDirection(panel) {
    const r = this.rows;
    return [r[0][0] * panel[0] + r[0][1] * panel[1] + r[0][2] * panel[2],
            r[1][0] * panel[0] + r[1][1] * panel[1] + r[1][2] * panel[2],
            r[2][0] * panel[0] + r[2][1] * panel[1] + r[2][2] * panel[2]];
  }

  body(p) {
    const radius = this.radius;
    const ix = Math.max(this.half[0] - radius, 0);
    const iy = Math.max(this.half[1] - radius, 0);
    const iz = Math.max(this.half[2] - radius, 0);
    const dx = Math.abs(p[0]) - ix, dy = Math.abs(p[1]) - iy, dz = Math.abs(p[2] + this.half[2]) - iz;
    const ox = Math.max(dx, 0), oy = Math.max(dy, 0), oz = Math.max(dz, 0);
    return Math.hypot(ox, oy, oz) + Math.min(Math.max(dx, Math.max(dy, dz)), 0) - radius;
  }

  dock(w) {
    const half = Chassis.plinthHalf, radius = Chassis.plinthRadius;
    const centre = [0, -0.004, this.floor + half[2]];
    const ix = Math.max(half[0] - radius, 0), iy = Math.max(half[1] - radius, 0);
    const iz = Math.max(half[2] - radius, 0);
    const dx = Math.abs(w[0] - centre[0]) - ix;
    const dy = Math.abs(w[1] - centre[1]) - iy;
    const dz = Math.abs(w[2] - centre[2]) - iz;
    const ox = Math.max(dx, 0), oy = Math.max(dy, 0), oz = Math.max(dz, 0);
    return Math.hypot(ox, oy, oz) + Math.min(Math.max(dx, Math.max(dy, dz)), 0) - radius;
  }

  scene(world) {
    let distance = this.body(this.toPanelPoint(world));
    let material = Material.Body;
    const dock = this.dock(world);
    if (dock < distance) { distance = dock; material = Material.Dock; }
    const desk = world[2] - this.floor;
    if (desk < distance) { distance = desk; material = Material.Desk; }
    return { distance, material };
  }

  normal(p) {
    const h = 2e-5;
    const corners = [[1, -1, -1], [-1, -1, 1], [-1, 1, -1], [1, 1, 1]];
    const out = [0, 0, 0];
    for (const c of corners) {
      const d = this.scene([p[0] + c[0] * h, p[1] + c[1] * h, p[2] + c[2] * h]).distance;
      out[0] += c[0] * d; out[1] += c[1] * d; out[2] += c[2] * d;
    }
    const l = Math.hypot(out[0], out[1], out[2]) || 1;
    return [out[0] / l, out[1] / l, out[2] / l];
  }

  march(origin, direction) {
    let travel = 0;
    for (let step = 0; step < 96; step++) {
      const here = [origin[0] + direction[0] * travel,
                    origin[1] + direction[1] * travel,
                    origin[2] + direction[2] * travel];
      const { distance, material } = this.scene(here);
      if (distance < 1.5e-5) {
        return { struck: true, world: here, panel: this.toPanelPoint(here), travel, material };
      }
      travel += Math.max(distance, 1e-5);
      if (travel > DeskFade * 1.6) break;
    }
    return { struck: false };
  }

  // The soft shadow: keep the smallest ratio of distance-to-scene against distance-travelled.
  // That ratio is the penumbra, so the contact tightens to a hard line for free.
  shadow(origin, direction, sharpness) {
    let shade = 1, travel = 0.004;
    for (let step = 0; step < 40; step++) {
      const d = this.scene([origin[0] + direction[0] * travel,
                            origin[1] + direction[1] * travel,
                            origin[2] + direction[2] * travel]).distance;
      if (d < 1e-5) return 0;
      shade = Math.min(shade, sharpness * d / travel);
      travel += clamp(d, 0.002, 0.08);
      if (travel > 1.2) break;
    }
    return clamp(shade, 0, 1);
  }
}

function DrawRoom(picture, camera, tablet) {
  const { width, height } = picture;
  const sharpness = clamp(1 - Chassis.bodyRoughness, 0, 1);
  const bodyAlbedo = [0.0320, 0.0345, 0.0400];
  const r = tablet.rows;
  const panelFace = (() => {
    const n = [r[0][2], r[1][2], r[2][2]];
    const l = Math.hypot(n[0], n[1], n[2]) || 1;
    return [n[0] / l, n[1] / l, n[2] / l];
  })();
  const origin = [r[0][3], r[1][3], r[2][3]];
  const spillTint = [0.055, 0.175, 0.280];

  for (let py = 0; py < height; py++) {
    for (let px = 0; px < width; px++) {
      const direction = camera.ray(px, py);
      const at = py * width + px;
      const hit = tablet.march(camera.eye, direction);

      if (!hit.struck) {
        const sky = RoomLight(direction, 0);
        picture.lit[at * 3] = sky[0] * RoomFalloff;
        picture.lit[at * 3 + 1] = sky[1] * RoomFalloff;
        picture.lit[at * 3 + 2] = sky[2] * RoomFalloff;
        continue;
      }

      const worldNormal = tablet.normal(hit.world);
      const view = [-direction[0], -direction[1], -direction[2]];
      const facingView = Math.max(worldNormal[0] * view[0] + worldNormal[1] * view[1]
                                + worldNormal[2] * view[2], 0);
      const bounce = [2 * facingView * worldNormal[0] - view[0],
                      2 * facingView * worldNormal[1] - view[1],
                      2 * facingView * worldNormal[2] - view[2]];
      const lambert = Math.max(worldNormal[0] * KeyDirection[0] + worldNormal[1] * KeyDirection[1]
                             + worldNormal[2] * KeyDirection[2], 0);
      const shadow = tablet.shadow(hit.world, KeyDirection, 9.0);

      // The screen is an area light and the desk is right under it.
      const toPanel = [hit.world[0] - origin[0], hit.world[1] - origin[1], hit.world[2] - origin[2]];
      const spillFall = 1 / (1 + (toPanel[0] ** 2 + toPanel[1] ** 2 + toPanel[2] ** 2) * 26);
      const spillFacing = clamp((worldNormal[0] * panelFace[0] + worldNormal[1] * panelFace[1]
                               + worldNormal[2] * panelFace[2]) * 0.5 + 0.5, 0, 1);
      const spill = spillFall * spillFacing;

      const out = [0, 0, 0];

      if (hit.material === Material.Desk) {
        const albedo = [0.0115, 0.0122, 0.0140];
        const ambient = RoomLight(worldNormal, 0);
        const sheen = RoomLight(bounce, 0.25);
        const grazing = Schlick(facingView, 0.030) * 0.9;
        const pooled = spill * 1.35 * tablet.shadow(hit.world,
          [-panelFace[0], -panelFace[1], -panelFace[2]], 5.0);
        const sky = RoomLight(direction, 0);
        const away = clamp(hit.travel / DeskFade, 0, 1) ** 2;
        for (let c = 0; c < 3; c++) {
          out[c] = mix(albedo[c] * (lambert * Chassis.keyIntensity * shadow + 0.10)
                     + albedo[c] * ambient[c] * 3.0 + sheen[c] * grazing + spillTint[c] * pooled,
                       sky[c] * RoomFalloff, away);
        }
      } else if (hit.material === Material.Dock) {
        const albedo = [0.0150, 0.0160, 0.0184];
        const ambient = RoomLight(worldNormal, 0);
        const sheen = RoomLight(bounce, 0.55);
        const grazing = Schlick(facingView, 0.045) * 0.7;
        for (let c = 0; c < 3; c++) {
          out[c] = albedo[c] * (lambert * Chassis.keyIntensity * shadow + 0.14)
                 + albedo[c] * ambient[c] * 3.2 + sheen[c] * grazing + spillTint[c] * spill * 0.85;
        }
      } else {
        const panelNormal = tablet.toPanelDirection(worldNormal);
        const faceReach = DistanceRoundedRectangle(hit.panel[0], hit.panel[1],
          PanelHalfWidth, PanelHalfHeight, Chassis.cornerRadius * 0.86);
        const facing = clamp(panelNormal[2], 0, 1);

        if (faceReach < 0 && panelNormal[2] > 0.86) {
          picture.lit[at * 3] = 0.0042; picture.lit[at * 3 + 1] = 0.0048; picture.lit[at * 3 + 2] = 0.0060;
          continue;
        }

        const ambient = RoomLight(worldNormal, 0);
        const mirror = RoomLight(bounce, sharpness * 0.55);
        const fresnel = Schlick(facingView, 0.055);
        const screenNear = facing * Math.exp(-Math.max(faceReach, 0) / 0.010);
        const turn = clamp(1 - Math.abs(worldNormal[0] * panelFace[0] + worldNormal[1] * panelFace[1]
                                      + worldNormal[2] * panelFace[2]), 0, 1);
        const chamfer = turn * turn * lambert * 0.55;
        const dim = mix(0.55, 1.0, shadow);

        for (let c = 0; c < 3; c++) {
          out[c] = (bodyAlbedo[c] * (lambert * Chassis.keyIntensity + 0.14)
                  + bodyAlbedo[c] * ambient[c] * 3.4
                  + mirror[c] * fresnel * 1.9
                  + [0.30, 0.33, 0.40][c] * chamfer
                  + spillTint[c] * screenNear * Chassis.screenSpill) * dim;
        }

        // Hardware. A bezel with nothing on it is a picture of a bezel.
        if (facing > 0.86) {
          const lens = Math.hypot(hit.panel[0], hit.panel[1] - (PanelHalfHeight + 0.0062)) - 0.0024;
          const pit = 1 - smoothstep(0, 0.0006, lens);
          const glint = 1 - smoothstep(0, 0.0010, Math.abs(lens + 0.0014));
          for (let c = 0; c < 3; c++) {
            out[c] = mix(out[c], [0.0030, 0.0034, 0.0046][c], pit) + [0.26, 0.30, 0.40][c] * glint;
          }
          const slot = DistanceRoundedRectangle(hit.panel[0],
            hit.panel[1] + PanelHalfHeight + 0.0060, 0.0220, 0.0011, 0.0011);
          const holes = 0.5 + 0.5 * Math.cos(hit.panel[0] * 2400);
          const cut = (1 - smoothstep(0, 0.0004, slot)) * holes;
          for (let c = 0; c < 3; c++) out[c] = mix(out[c], out[c] * 0.18, cut);
        }
      }

      picture.lit[at * 3] = out[0]; picture.lit[at * 3 + 1] = out[1]; picture.lit[at * 3 + 2] = out[2];
    }
  }
}

// ── the camera ─────────────────────────────────────────────────────────────────────────────────

function Camera(orbit, tilt, distance, fieldOfView, width, height) {
  const eye = [Math.sin(orbit) * Math.cos(tilt) * distance,
               -Math.cos(orbit) * Math.cos(tilt) * distance,
               Math.sin(tilt) * distance];
  const norm = (A) => { const L = Math.hypot(A[0], A[1], A[2]) || 1; return [A[0] / L, A[1] / L, A[2] / L]; };
  const cross = (A, B) => [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];

  const forward = norm([-eye[0], -eye[1], -eye[2]]);
  const right = norm(cross(forward, [0, 0, 1]));
  const up = cross(right, forward);
  const tanHalf = Math.tan(fieldOfView * 0.5);
  const aspect = width / height;

  return {
    eye, forward, right, up, tanHalf, aspect, width, height,
    projectionScale: 0.5 * height / tanHalf,

    ray(px, py) {
      const nx = (2 * (px + 0.5) / width - 1) * aspect * tanHalf;
      const ny = (1 - 2 * (py + 0.5) / height) * tanHalf;
      return norm([forward[0] + right[0] * nx + up[0] * ny,
                   forward[1] + right[1] * nx + up[1] * ny,
                   forward[2] + right[2] * nx + up[2] * ny]);
    },

    project(P) {
      const dx = P[0] - eye[0], dy = P[1] - eye[1], dz = P[2] - eye[2];
      const depth = dx * forward[0] + dy * forward[1] + dz * forward[2];
      const sx = dx * right[0] + dy * right[1] + dz * right[2];
      const sy = dx * up[0] + dy * up[1] + dz * up[2];
      if (depth <= 1e-4) return null;
      return { x: ((sx / depth) / (aspect * tanHalf) * 0.5 + 0.5) * width,
               y: (0.5 - (sy / depth) / tanHalf * 0.5) * height,
               depth };
    },
  };
}

// ── the glow ─────────────────────────────────────────────────────────────────────────────────────
// Bright pass to a quarter, then one separable Gaussian along each axis. Mirrors fsBloomCut /
// fsBloomAcross / fsBloomDown.

function Bloom(display) {
  const small = { width: BloomWidth, height: BloomHeight, lit: new Float32Array(BloomWidth * BloomHeight * 3) };
  const other = { width: BloomWidth, height: BloomHeight, lit: new Float32Array(BloomWidth * BloomHeight * 3) };

  for (let y = 0; y < BloomHeight; y++) {
    for (let x = 0; x < BloomWidth; x++) {
      const u = (x + 0.5) / BloomWidth, v = (y + 0.5) / BloomHeight;
      const total = [0, 0, 0];
      for (const [ox, oy] of [[-1, -1], [1, -1], [-1, 1], [1, 1]]) {
        const c = SampleBilinear(display, u + ox / DisplayWidth, v + oy / DisplayHeight);
        const level = Math.max(c[0], c[1], c[2]);
        const over = Math.max(level - Glass.bloomThreshold, 0);
        const gain = over / Math.max(level, 1e-4);
        total[0] += c[0] * gain; total[1] += c[1] * gain; total[2] += c[2] * gain;
      }
      const at = (y * BloomWidth + x) * 3;
      small.lit[at] = total[0] * 0.25; small.lit[at + 1] = total[1] * 0.25; small.lit[at + 2] = total[2] * 0.25;
    }
  }

  const weight = [0.2270270, 0.1945946, 0.1216216, 0.0540541, 0.0162162];
  const blur = (from, to, stepX, stepY) => {
    for (let y = 0; y < BloomHeight; y++) {
      for (let x = 0; x < BloomWidth; x++) {
        const u = (x + 0.5) / BloomWidth, v = (y + 0.5) / BloomHeight;
        const total = [0, 0, 0];
        const centre = SampleBilinear(from, u, v);
        total[0] = centre[0] * weight[0]; total[1] = centre[1] * weight[0]; total[2] = centre[2] * weight[0];
        for (let i = 1; i <= 4; i++) {
          const a = SampleBilinear(from, u + stepX * i, v + stepY * i);
          const b = SampleBilinear(from, u - stepX * i, v - stepY * i);
          for (let c = 0; c < 3; c++) total[c] += (a[c] + b[c]) * weight[i];
        }
        const at = (y * BloomWidth + x) * 3;
        to.lit[at] = total[0]; to.lit[at + 1] = total[1]; to.lit[at + 2] = total[2];
      }
    }
  };
  blur(small, other, 1 / BloomWidth, 0);
  blur(other, small, 0, 1 / BloomHeight);
  return small;
}

// Bilinear with clamp-to-edge, matching the sampler the host binds. Clamp matters: the refraction
// offset walks the sample point past the edge at a steep angle, and a wrap would fold the opposite
// side of the interface into the rim.
function SampleBilinear(sheet, u, v) {
  const { width, height, lit } = sheet;
  const x = clamp(u, 0, 1) * width - 0.5, y = clamp(v, 0, 1) * height - 0.5;
  const x0 = Math.floor(x), y0 = Math.floor(y);
  const fx = x - x0, fy = y - y0;
  const out = [0, 0, 0];
  for (let j = 0; j <= 1; j++) {
    for (let i = 0; i <= 1; i++) {
      const sx = Math.min(Math.max(x0 + i, 0), width - 1);
      const sy = Math.min(Math.max(y0 + j, 0), height - 1);
      const w = (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
      const at = (sy * width + sx) * 3;
      out[0] += lit[at] * w; out[1] += lit[at + 1] * w; out[2] += lit[at + 2] * w;
    }
  }
  return out;
}

// ── the glass ────────────────────────────────────────────────────────────────────────────────────
//
// 🔴 IT SAMPLES. That is the whole difference, and it is the difference between glass and a decal.
//
//    The previous version added a reflection on top of an already finished picture. Additive light
//    can only ADD, so it could never bend, displace, dim or tint what was under it — and bending
//    what is under it is the entire physical content of the word glass. Now the interface lives in
//    a texture and this pass decides what you see of it:
//
//      · refraction, per channel, because the emitting plane sits behind the outer face
//      · the glow, added INSIDE the glass so the Fresnel term attenuates it too
//      · a real split: transmitted * (1 - F) + reflected * F, so energy LEAVES the interface as
//        the room takes over, instead of the interface staying at full brightness underneath
//
// Mirrors fsGlass in js/display.js.

function GlassOffset(incident, index) {
  // refract() against the panel normal (0,0,1), in panel space.
  const eta = 1 / index;
  const cosi = -incident[2];
  const k = 1 - eta * eta * (1 - cosi * cosi);
  if (k < 0) return [0, 0];
  const bent = [eta * incident[0], eta * incident[1], eta * incident[2] + (eta * cosi - Math.sqrt(k)) * 1];
  const straightSlide = [incident[0] / Math.max(Math.abs(incident[2]), 1e-4),
                         incident[1] / Math.max(Math.abs(incident[2]), 1e-4)];
  const bentSlide = [bent[0] / Math.max(Math.abs(bent[2]), 1e-4),
                     bent[1] / Math.max(Math.abs(bent[2]), 1e-4)];
  return [(bentSlide[0] - straightSlide[0]) * Glass.thickness,
          (bentSlide[1] - straightSlide[1]) * Glass.thickness];
}

function DrawGlass(picture, camera, tablet, display, bloom) {
  const { width, height } = picture;
  const sharpness = clamp(1 - Chassis.glassRoughness, 0, 1);
  const r = tablet.rows;
  const normal = (() => {
    const n = [r[0][2], r[1][2], r[2][2]];
    const l = Math.hypot(n[0], n[1], n[2]) || 1;
    return [n[0] / l, n[1] / l, n[2] / l];
  })();
  const O = [r[0][3], r[1][3], r[2][3]];
  const U = [r[0][0], r[1][0], r[2][0]];
  const V = [r[0][1], r[1][1], r[2][1]];
  const hw = PanelHalfWidth - 0.0010, hh = PanelHalfHeight - 0.0010;
  const radius = Chassis.cornerRadius * 0.86;

  for (let py = 0; py < height; py++) {
    for (let px = 0; px < width; px++) {
      const direction = camera.ray(px, py);
      const denom = direction[0] * normal[0] + direction[1] * normal[1] + direction[2] * normal[2];
      if (Math.abs(denom) < 1e-9) continue;
      const t = ((O[0] - camera.eye[0]) * normal[0] + (O[1] - camera.eye[1]) * normal[1]
               + (O[2] - camera.eye[2]) * normal[2]) / denom;
      if (t <= 0) continue;

      const P = [camera.eye[0] + direction[0] * t, camera.eye[1] + direction[1] * t,
                 camera.eye[2] + direction[2] * t];
      const dx = P[0] - O[0], dy = P[1] - O[1], dz = P[2] - O[2];
      const lx = dx * U[0] + dy * U[1] + dz * U[2];
      const ly = dx * V[0] + dy * V[1] + dz * V[2];
      const lip = DistanceRoundedRectangle(lx, ly, hw, hh, radius);
      if (lip > 0) continue;

      const view = [-direction[0], -direction[1], -direction[2]];
      const facing = Math.max(normal[0] * view[0] + normal[1] * view[1] + normal[2] * view[2], 0);

      const incident = tablet.toPanelDirection(direction);
      const il = Math.hypot(incident[0], incident[1], incident[2]) || 1;
      incident[0] /= il; incident[1] /= il; incident[2] /= il;

      const base = [lx / PanelHalfWidth * 0.5 + 0.5, 0.5 - ly / PanelHalfHeight * 0.5];
      const slide = GlassOffset(incident, Glass.index);
      const shifted = [base[0] + slide[0] / PanelHalfWidth * 0.5,
                       base[1] - slide[1] / PanelHalfHeight * 0.5];
      const transmitted = SampleBilinear(display, shifted[0], shifted[1]);

      const glow = SampleBilinear(bloom, base[0], base[1]);
      for (let c = 0; c < 3; c++) transmitted[c] += glow[c] * Glass.bloomStrength;

      const fresnel = Schlick(facing, 0.042);
      const bounce = [2 * facing * normal[0] - view[0], 2 * facing * normal[1] - view[1],
                      2 * facing * normal[2] - view[2]];
      const sharp = RoomLight(bounce, sharpness);
      const wide = RoomLight(bounce, sharpness * 0.35);
      const seam = Math.exp(lip / 0.0016) * 0.55 * (0.25 + 0.75 * fresnel);
      const lipTint = [0.35, 0.40, 0.50];

      const at = py * width + px;
      for (let c = 0; c < 3; c++) {
        picture.lit[at * 3 + c] = transmitted[c] * (1 - fresnel)
                                + (sharp[c] + wide[c] * 0.20) * fresnel
                                + lipTint[c] * seam;
      }
    }
  }
}

// ── the display camera ───────────────────────────────────────────────────────────────────────────
//
// 🔴 The off-axis frustum, which in a ray tracer is simply a different ray per pixel: the one from
//    the real eye through that point of the PANEL rectangle. Same apex, different window. The
//    volume behind the glass therefore projects exactly as it does to the window camera, which is
//    the whole reason the display is not baked orthographically — an orthographic bake would flatten
//    the fibre volume to a sticker and kill the parallax this experiment exists to show.
//
//    See the derivation in js/display.js. The matrix there and the rays here are the same map.

function DisplayCamera(eye, rows, width, height) {
  const hw = PanelHalfWidth, hh = PanelHalfHeight;
  const norm = (A) => { const L = Math.hypot(A[0], A[1], A[2]) || 1; return [A[0] / L, A[1] / L, A[2] / L]; };

  const toWorld = (local) => [
    rows[0][0] * local[0] + rows[0][1] * local[1] + rows[0][3],
    rows[1][0] * local[0] + rows[1][1] * local[1] + rows[1][3],
    rows[2][0] * local[0] + rows[2][1] * local[1] + rows[2][3],
  ];
  const toPanel = (world) => {
    const dx = world[0] - rows[0][3], dy = world[1] - rows[1][3], dz = world[2] - rows[2][3];
    return [rows[0][0] * dx + rows[1][0] * dy + rows[2][0] * dz,
            rows[0][1] * dx + rows[1][1] * dy + rows[2][1] * dz,
            rows[0][2] * dx + rows[1][2] * dy + rows[2][2] * dz];
  };
  const eyePanel = toPanel(eye);

  return {
    eye, width, height, eyePanel,
    projectionScale: DisplayProjectionScale(eyePanel[2], hh),
    forward: norm([-rows[0][2], -rows[1][2], -rows[2][2]]),
    right: [rows[0][0], rows[1][0], rows[2][0]],
    up: [rows[0][1], rows[1][1], rows[2][1]],

    ray(px, py) {
      const local = [(2 * (px + 0.5) / width - 1) * hw, (1 - 2 * (py + 0.5) / height) * hh];
      const P = toWorld(local);
      return norm([P[0] - eye[0], P[1] - eye[1], P[2] - eye[2]]);
    },

    // Where the sightline from the eye to P crosses the glass, in display pixels.
    project(P) {
      const p = toPanel(P);
      const rise = eyePanel[2] - p[2];
      if (rise <= 1e-6) return null;

      // 🔴 CLIP AT THE NEAR PLANE, BECAUSE THE HARDWARE DOES.
      //
      //    This renderer existed to show what the browser shows, and for one whole stretch of work
      //    it showed something better: it had no near plane, so it drew the entire interface while
      //    the GPU was discarding every figure of it. The stills looked correct and the device was
      //    black. A renderer more forgiving than the hardware is not a proof of anything.
      //
      //    DisplayClip puts the plane Approach in front of the glass, so anything beyond that is
      //    gone — here as well as there.
      if (p[2] > Approach) return null;
      const t = eyePanel[2] / rise;
      const cx = eyePanel[0] + (p[0] - eyePanel[0]) * t;
      const cy = eyePanel[1] + (p[1] - eyePanel[1]) * t;
      return { x: (cx / hw * 0.5 + 0.5) * width, y: (0.5 - cy / hh * 0.5) * height, depth: rise };
    },
  };
}

// ── the picture ──────────────────────────────────────────────────────────────────────────────────

class Picture {
  constructor(width, height, clear) {
    this.width = width; this.height = height;
    this.lit = new Float32Array(width * height * 3);
    for (let at = 0; at < width * height; at++) {
      this.lit[at * 3] = clear[0]; this.lit[at * 3 + 1] = clear[1]; this.lit[at * 3 + 2] = clear[2];
    }
  }

  // Premultiplied source-over, the interface pipeline's own blend.
  over(at, r, g, b, alpha) {
    const keep = 1 - alpha;
    this.lit[at * 3] = r + this.lit[at * 3] * keep;
    this.lit[at * 3 + 1] = g + this.lit[at * 3 + 1] * keep;
    this.lit[at * 3 + 2] = b + this.lit[at * 3 + 2] * keep;
  }

  add(at, r, g, b) {
    this.lit[at * 3] += r; this.lit[at * 3 + 1] += g; this.lit[at * 3 + 2] += b;
  }

  // Box-average the supersample and encode. The canvas format is bgra8unorm, which is NOT sRGB, so
  // the browser displays these numbers unconverted — doing a gamma encode here would make the still
  // brighter than the panel actually is, which is the one lie this tool must not tell.
  resolve(factor) {
    const width = Math.floor(this.width / factor), height = Math.floor(this.height / factor);
    const out = Buffer.alloc(width * height * 3);
    const share = 1 / (factor * factor);
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        let r = 0, g = 0, b = 0;
        for (let sy = 0; sy < factor; sy++) {
          for (let sx = 0; sx < factor; sx++) {
            const at = ((y * factor + sy) * this.width + (x * factor + sx)) * 3;
            r += this.lit[at]; g += this.lit[at + 1]; b += this.lit[at + 2];
          }
        }
        const o = (y * width + x) * 3;
        out[o] = clamp(r * share, 0, 1) * 255 + 0.5;
        out[o + 1] = clamp(g * share, 0, 1) * 255 + 0.5;
        out[o + 2] = clamp(b * share, 0, 1) * 255 + 0.5;
      }
    }
    return { pixels: out, width, height };
  }
}

export function WritePortableNetwork(path, picture) {
  const { pixels, width, height } = picture;
  const raw = Buffer.alloc((width * 3 + 1) * height);
  for (let y = 0; y < height; y++) {
    raw[y * (width * 3 + 1)] = 0;
    pixels.copy(raw, y * (width * 3 + 1) + 1, y * width * 3, (y + 1) * width * 3);
  }

  const chunk = (kind, body) => {
    const out = Buffer.alloc(body.length + 12);
    out.writeUInt32BE(body.length, 0);
    out.write(kind, 4, 'ascii');
    body.copy(out, 8);
    out.writeInt32BE(Crc(Buffer.concat([Buffer.from(kind, 'ascii'), body])), body.length + 8);
    return out;
  };

  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header[8] = 8; header[9] = 2;               // 8-bit, truecolour
  writeFileSync(path, Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', header),
    chunk('IDAT', deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]));
}

const CrcLookup = (() => {
  const out = new Int32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
    out[n] = c;
  }
  return out;
})();

function Crc(body) {
  let c = 0xffffffff;
  for (let at = 0; at < body.length; at++) c = CrcLookup[(c ^ body[at]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) | 0;
}

// ── the draw ─────────────────────────────────────────────────────────────────────────────────────

function DrawFigures(picture, camera, packed, from, to, ambient, time) {
  const { width, height } = picture;

  for (let slot = from; slot < to; slot++) {
    const f = packed.data.subarray(slot * FloatsPerFigure, (slot + 1) * FloatsPerFigure);
    const rowX = f.subarray(0, 4), rowY = f.subarray(4, 8), rowZ = f.subarray(8, 12);
    const hw = f[12], hh = f[13], cornerRadius = f[14], opacity = f[15];
    const category = Math.trunc(f[20] + 0.5), alpha = f[21], beta = f[22], emissive = f[23];
    const tint = f.subarray(24, 28), base = f.subarray(28, 32);
    const streakA = f.subarray(32, 36), streakB = f.subarray(36, 40);
    if (opacity <= 0 || tint[3] <= 0) continue;

    const O = [rowX[3], rowY[3], rowZ[3]];
    const U = [rowX[0], rowY[0], rowZ[0]];
    const V = [rowX[1], rowY[1], rowZ[1]];
    const N = [U[1] * V[2] - U[2] * V[1], U[2] * V[0] - U[0] * V[2], U[0] * V[1] - U[1] * V[0]];

    // Screen bound from the four corners, so a small figure costs a small number of pixels.
    let minX = width, minY = height, maxX = 0, maxY = 0, visible = false;
    for (const [sx, sy] of [[-1, -1], [1, -1], [-1, 1], [1, 1]]) {
      const corner = camera.project([O[0] + U[0] * sx * hw + V[0] * sy * hh,
                                     O[1] + U[1] * sx * hw + V[1] * sy * hh,
                                     O[2] + U[2] * sx * hw + V[2] * sy * hh]);
      if (!corner) continue;
      visible = true;
      minX = Math.min(minX, corner.x); maxX = Math.max(maxX, corner.x);
      minY = Math.min(minY, corner.y); maxY = Math.max(maxY, corner.y);
    }
    if (!visible) continue;

    const x0 = Math.max(0, Math.floor(minX) - 2), x1 = Math.min(width - 1, Math.ceil(maxX) + 2);
    const y0 = Math.max(0, Math.floor(minY) - 2), y1 = Math.min(height - 1, Math.ceil(maxY) + 2);

    const localAt = (px, py) => {
      const dir = camera.ray(px, py);
      const denom = dir[0] * N[0] + dir[1] * N[1] + dir[2] * N[2];
      if (Math.abs(denom) < 1e-9) return null;
      const t = ((O[0] - camera.eye[0]) * N[0] + (O[1] - camera.eye[1]) * N[1]
               + (O[2] - camera.eye[2]) * N[2]) / denom;
      if (t <= 0) return null;
      const dx = camera.eye[0] + dir[0] * t - O[0];
      const dy = camera.eye[1] + dir[1] * t - O[1];
      const dz = camera.eye[2] + dir[2] * t - O[2];
      return [dx * U[0] + dy * U[1] + dz * U[2], dx * V[0] + dy * V[1] + dz * V[2]];
    };

    for (let py = y0; py <= y1; py++) {
      for (let px = x0; px <= x1; px++) {
        const here = localAt(px, py);
        if (!here) continue;

        let coverage;
        if (category === Category.StreakField) {
          coverage = StreakFieldGlow(here[0], here[1], hw, hh, streakA, streakB, time);
        } else {
          // fwidth: the sum of the absolute derivatives, estimated against both neighbours.
          const ddx = localAt(px + 1, py), ddy = localAt(px, py + 1);
          if (!ddx || !ddy) continue;
          const pixelWidth = Math.max(Math.abs(ddx[0] - here[0]) + Math.abs(ddy[0] - here[0]),
                                      Math.abs(ddx[1] - here[1]) + Math.abs(ddy[1] - here[1]));
          coverage = CoverageFromDistance(
            DistanceFigure(category, here[0], here[1], hw, hh, cornerRadius, alpha, beta), pixelWidth);
        }
        if (coverage <= 0) continue;

        const a = coverage * opacity * tint[3];
        if (a <= 0) continue;
        const weight = clamp(emissive, 0, 1);
        const r = mix(base[0] * ambient[0], tint[0], weight);
        const g = mix(base[1] * ambient[1], tint[1], weight);
        const b = mix(base[2] * ambient[2], tint[2], weight);
        picture.over(py * width + px, r * a, g * a, b * a, a);
      }
    }
  }
}

function DrawFibres(picture, camera, fibres) {
  const { width, height } = picture;
  const strands = Math.trunc(fibres.p6[1]), segments = Math.trunc(fibres.p6[2]);
  const pixelScale = fibres.p4[3];
  const sigmaPx = Math.max(0.8, fibres.p2[2] * pixelScale) * 0.42466;
  const haloPx = Math.max(2.4 * pixelScale, 2 * sigmaPx);
  const envPx = 3.6 * haloPx;
  const eyeLocal = fibres.eyePanel(camera.eye);

  // The back of a tablet is opaque.
  if (eyeLocal[2] <= 0) return;

  // One fragment per pixel per strand, as the GPU gets: the nearest point on the strand wins.
  const bestGap = new Float32Array(width * height).fill(Infinity);
  const bestAlong = new Float32Array(width * height);
  const bestPanelZ = new Float32Array(width * height);
  const bestPanelX = new Float32Array(width * height);
  const bestPanelY = new Float32Array(width * height);
  const touched = [];

  for (let strand = 0; strand < strands; strand++) {
    const id = strand;
    touched.length = 0;

    const spine = [];
    for (let step = 0; step <= segments; step++) {
      const s = step / segments;
      const panel = fibres.panel(fibres.bezier(s, id));
      const screen = camera.project(fibres.world(panel));
      spine.push(screen ? { s, panel, screen } : null);
    }

    for (let step = 0; step < segments; step++) {
      const A = spine[step], B = spine[step + 1];
      if (!A || !B) continue;

      const ax = A.screen.x, ay = A.screen.y, bx = B.screen.x, by = B.screen.y;
      const x0 = Math.max(0, Math.floor(Math.min(ax, bx) - envPx));
      const x1 = Math.min(width - 1, Math.ceil(Math.max(ax, bx) + envPx));
      const y0 = Math.max(0, Math.floor(Math.min(ay, by) - envPx));
      const y1 = Math.min(height - 1, Math.ceil(Math.max(ay, by) + envPx));
      if (x1 < x0 || y1 < y0) continue;

      const sx = bx - ax, sy = by - ay;
      const square = Math.max(sx * sx + sy * sy, 1e-9);

      for (let py = y0; py <= y1; py++) {
        for (let px = x0; px <= x1; px++) {
          const lx = px + 0.5 - ax, ly = py + 0.5 - ay;
          const t = clamp((lx * sx + ly * sy) / square, 0, 1);
          const gap = Math.hypot(lx - sx * t, ly - sy * t);
          if (gap > envPx) continue;
          const at = py * width + px;
          if (gap >= bestGap[at]) continue;
          if (bestGap[at] === Infinity) touched.push(at);
          bestGap[at] = gap;
          bestAlong[at] = mix(A.s, B.s, t);
          bestPanelX[at] = mix(A.panel[0], B.panel[0], t);
          bestPanelY[at] = mix(A.panel[1], B.panel[1], t);
          bestPanelZ[at] = mix(A.panel[2], B.panel[2], t);
        }
      }
    }

    if (!touched.length) continue;

    const head = fract(fibres.unit(id, 9) + fibres.p5[1] * fibres.p2[1]);
    const win = Math.max(fibres.p3[3], 1e-3);
    const baseline = fibres.p3[2];
    const intensity = fibres.p3[0], halo = fibres.p3[1], taper = fibres.p2[3];
    const accentMix = fibres.p4[0] * fibres.unit(id, 12);
    const vary = 0.4 + 0.9 * fibres.unit(id, 13);

    for (const at of touched) {
      const across = bestGap[at], along = bestAlong[at];
      const panelZ = bestPanelZ[at];

      const sink = fibres.glassDepth(panelZ);
      if (sink <= 0) { bestGap[at] = Infinity; continue; }

      // The portal: the panel's rounded rectangle, evaluated where this sightline crosses the glass.
      const aperture = fibres.aperture([bestPanelX[at], bestPanelY[at], panelZ], eyeLocal);
      const inside = CoverageFromDistance(
        DistanceRoundedRectangle(aperture[0], aperture[1], fibres.clip[0], fibres.clip[1], fibres.clip[2]),
        0.0004);
      if (inside <= 0) { bestGap[at] = Infinity; continue; }

      const line = Math.exp(-0.5 * across * across / (sigmaPx * sigmaPx));
      const glow = halo * Math.exp(-0.5 * across * across / (haloPx * haloPx));
      const behind = fract(head - along);
      const front = smoothstep(0, 0.05, behind);
      const streak = behind < win ? ((1 - behind / win) ** 2) * front : 0;
      const lit = baseline + (1 - baseline) * streak;
      const ends = smoothstep(0, 0.04, along) * (1 - smoothstep(0.96, 1.0, along));
      const fade = mix(1, Math.sqrt(Math.max(Math.sin(Math.PI * along), 0)), taper);

      const colour = fibres.colourAt(along, id);
      const accent = fibres.colC;
      const level = intensity * vary * (line + glow) * lit * ends * fade * fibres.pulse(along) * inside * sink;
      if (level > 0) {
        picture.add(at, mix(colour[0], accent[0], accentMix) * level,
                        mix(colour[1], accent[1], accentMix) * level,
                        mix(colour[2], accent[2], accentMix) * level);
      }
      bestGap[at] = Infinity;
    }
  }
}

function DrawSparks(picture, camera, fibres) {
  const { width, height } = picture;
  const strands = Math.trunc(fibres.p6[1]);
  const eyeLocal = fibres.eyePanel(camera.eye);
  if (eyeLocal[2] <= 0) return;

  for (let strand = 0; strand < strands; strand++) {
    const id = strand;
    if (fibres.unit(id, 11) > fibres.p7[1]) continue;

    const s = fract(fibres.unit(id, 9) + fibres.p5[1] * fibres.p2[1]);
    const panel = fibres.panel(fibres.bezier(s, id));
    const spot = fibres.aperture(panel, eyeLocal);
    if (DistanceRoundedRectangle(spot[0], spot[1], fibres.clip[0], fibres.clip[1], fibres.clip[2]) > 0) continue;

    const sink = fibres.glassDepth(panel[2]);
    if (sink <= 0) continue;

    const screen = camera.project(fibres.world(panel));
    if (!screen) continue;

    const sizePx = clamp(fibres.p7[2] * fibres.org[3] * fibres.p4[2] / Math.max(screen.depth, 0.001), 1, 48);
    const half = sizePx * 0.5;
    const colour = fibres.colourAt(s, id);
    const level = fibres.p7[3] * (0.6 + 0.8 * fibres.unit(id, 10)) * fibres.pulse(s) * sink;

    const x0 = Math.max(0, Math.floor(screen.x - half)), x1 = Math.min(width - 1, Math.ceil(screen.x + half));
    const y0 = Math.max(0, Math.floor(screen.y - half)), y1 = Math.min(height - 1, Math.ceil(screen.y + half));
    for (let py = y0; py <= y1; py++) {
      for (let px = x0; px <= x1; px++) {
        const ux = (px + 0.5 - screen.x) / half, uy = (py + 0.5 - screen.y) / half;
        const r2 = ux * ux + uy * uy;
        const disc = Math.exp(-r2 * 7) * (1 - smoothstep(0.7, 1.0, r2));
        if (disc <= 0) continue;
        picture.add(py * width + px, colour[0] * disc * level, colour[1] * disc * level, colour[2] * disc * level);
      }
    }
  }
}

// ── the still ────────────────────────────────────────────────────────────────────────────────────

export function RenderStill(options) {
  const { width = 1280, height = 760, supersample = 2, time = 7.4, backdrop = 'live',
          orbit = 0.42, tilt = 0.18, distance = 0.78, ambient = 0.35, composition = 'dash' } = options;

  const W = width * supersample, H = height * supersample;
  const camera = Camera(orbit, tilt, distance, 0.62, W, H);
  const picture = new Picture(W, H, [0.012, 0.014, 0.018]);

  // Two screens, one device. `dash` is the instrument composition; `shell` is FRONTIER OS.
  // They are separate structures because they share nothing but the housing they are built on.
  const { structure, handles } = composition === 'shell' ? constructShellLayout() : constructHudLayout();
  // 🔴 The lean. layout.js stands the panel bolt upright, which is the right AUTHORING frame —
  //    every figure is placed against it and every check is pinned to it. A tablet in a room is
  //    not bolt upright, though: it sits in its dock and leans back. So the lean is applied here,
  //    to the scene, and the layout never learns about it.
  structure.query(handles.housing).rotationX = Math.PI / 2 - Chassis.lean;


  // The demo cycle app.js runs, sampled at `time`, with the springs at rest on their targets. A
  // still cannot show an envelope, so it shows where the envelope is heading.
  const cycle = (time % 12) / 12;
  const boost = 0.5 - 0.5 * Math.cos(cycle * Math.PI * 4);
  if (composition === 'shell') {
    // A still of a clock has to be a fixed clock, or no two runs of --check agree.
    assignShellValues(structure, handles, { clock: new Date(2026, 9, 9, 9, 41), time });
  } else {
    assignValues(structure, handles, {
      speed: 18 + 160 * boost, boost, regen: 0.5 + 0.45 * Math.sin(cycle * Math.PI * 2),
      sport: cycle > 0.5 ? 1 : 0, time,
    });
  }

  // The field rung is drawn only when it is the rung in use, exactly as the host does it.
  structure.query(handles.streaks).opacity =
    backdrop === 'field' ? (composition === 'shell' ? 0.38 : 0.85) : 0;

  const { placements } = resolve(structure);
  const packed = pack(structure, placements, camera.eye);

  const backdropRank = structure.query(handles.streaks).orderingRank;
  let splitAt = packed.order.findIndex((at) => structure.query(at).orderingRank > backdropRank);
  if (splitAt < 0) splitAt = packed.count;

  const tablet = new Tablet(placements[handles.housing]);
  const room = [ambient, ambient * 1.02, ambient * 1.08];

  // ── pass one: the display, into its own buffer, through the panel's own frustum ──────────────
  //
  // Its resolution is the panel's, not the window's, and it does not get the supersample: a real
  // display has the pixels it has, and the glass resolves it bilinearly like a sampler would.
  const screen = new Picture(DisplayWidth, DisplayHeight, [0.0032, 0.0036, 0.0048]);
  const panelCamera = DisplayCamera(camera.eye, placements[handles.housing],
                                    DisplayWidth, DisplayHeight);

  DrawFigures(screen, panelCamera, packed, 0, splitAt, room, time);

  if (backdrop === 'live') {
    const fibres = new Fibres(packFibre(new Float32Array(FibreFloats), StreakPreset, {
      time, width: DisplayWidth, height: DisplayHeight,
      projectionScale: panelCamera.projectionScale,
      rows: placements[handles.housing],
    }));
    DrawFibres(screen, panelCamera, fibres);
    DrawSparks(screen, panelCamera, fibres);
  }

  DrawFigures(screen, panelCamera, packed, splitAt, packed.count, room, time);

  // ── pass two: the glow, which is the thing a target is actually worth paying for ─────────────
  const glow = Bloom(screen);

  // ── pass three: the tablet in the room, reading the display as a texture ─────────────────────
  DrawRoom(picture, camera, tablet);
  DrawGlass(picture, camera, tablet, screen, glow);

  return { picture: picture.resolve(supersample), figures: packed.count, display: screen };
}

// ── the command ──────────────────────────────────────────────────────────────────────────────────

function Luminance(picture) {
  let total = 0;
  for (let at = 0; at < picture.pixels.length; at += 3) {
    total += (picture.pixels[at] * 0.2126 + picture.pixels[at + 1] * 0.7152 + picture.pixels[at + 2] * 0.0722);
  }
  return total / (picture.pixels.length / 3) / 255;
}

const invoked = process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href;
const asked = invoked ? process.argv.slice(2) : ['--quiet'];
const checking = asked.includes('--check');

const sheets = checking
  ? [{ name: 'check', orbit: 0.42, tilt: 0.18, distance: 0.78, width: 320, height: 190, supersample: 1 }]
  : [
      { name: 'Home', orbit: 0.30, tilt: 0.14, distance: 0.66, backdrop: 'live', composition: 'shell' },
      { name: 'HomeFlat', orbit: 0.04, tilt: 0.03, distance: 0.60, backdrop: 'live', composition: 'shell' },
      { name: 'Tablet', orbit: 0.62, tilt: 0.26, distance: 0.74, backdrop: 'live' },
      { name: 'Screen', orbit: 0.06, tilt: 0.04, distance: 0.62, backdrop: 'live' },
      { name: 'FieldRung', orbit: 0.62, tilt: 0.26, distance: 0.74, backdrop: 'field' },
      // The angle that exposed the flat clip: the volume behind the glass projects well outside the
      // panel's outline here, so this is the still that proves the aperture.
      { name: 'Grazing', orbit: 1.24, tilt: 0.42, distance: 0.70, backdrop: 'live' },
    ];

const folder = join(Here, 'Stills');
if (!checking) mkdirSync(folder, { recursive: true });

for (const sheet of (invoked ? sheets : [])) {
  const began = Date.now();
  const { picture, figures } = RenderStill(sheet);
  const lit = Luminance(picture);

  if (checking) {
    if (!(lit > 0.004)) throw new Error(`RenderStill --check: the panel came out blank (luminance ${lit.toFixed(5)})`);
    console.log(`RenderStill --check  PASS  ${figures} figures, mean luminance ${lit.toFixed(4)}`);
    break;
  }

  const path = join(folder, `${sheet.name}.png`);
  WritePortableNetwork(path, picture);
  console.log(`${sheet.name.padEnd(10)} ${picture.width}x${picture.height}  ${figures} figures  `
            + `luminance ${lit.toFixed(4)}  ${((Date.now() - began) / 1000).toFixed(1)}s  → ${path}`);
}

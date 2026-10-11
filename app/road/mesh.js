/* ============================================================
   FRONTIER · ROAD EDITOR — procedural geometry engine
   Roads are (spline × cross-section assembly) swept into real sections:
   crowned carriageway bands, kerbs with dropped crossings, barriers,
   rails, deck soffits + piers, embankments, rule-driven markings, symbol
   decals and instanced street furniture. Renderer-free: emits raw arrays.
   ============================================================ */

import { Builder, RNG, clamp, lerp, dist, hull, chaikin, dedupe, TAU, hash01, strSeed } from './lib.js';
import { PROFILES, solveSection, levelAt, markingBoundaries, SURFACES, CARRIAGEWAY } from './profiles.js';

/* ------------------------------- material buckets ------------------------------- */
const TEXKEY = {
  asphalt: 'pav:asphalt', concrete: 'pav:concrete', brick: 'pav:brick', cobble: 'pav:cobble',
  paver: 'pav:paver', herring: 'pav:paver', granite: 'pav:granite', gravel: 'pav:gravel',
  grass: 'pav:grass', metal: 'pav:metal', tile: 'pav:tile', timber: 'pav:timber', water: 'pav:water'
};
const surfBucket = s => TEXKEY[(SURFACES[s] || SURFACES.asphalt).tex] || 'pav:asphalt';
const surfTint = s => (SURFACES[s] || SURFACES.asphalt).tint;
const G = {
  asphalt: 'pav:asphalt', concrete: 'pav:concrete', paver: 'pav:paver', gravel: 'pav:gravel',
  grass: 'pav:grass', metal: 'pav:metal', tile: 'pav:tile', water: 'pav:water', soil: 'pav:soil',
  brick: 'pav:brick', cobble: 'pav:cobble', granite: 'pav:granite', timber: 'pav:timber', rubber: 'pav:rubber', paint: 'pav:paint'
};
const WHITE = [.92, .92, .9], YELLOW = [.94, .78, .14], RED = [.78, .12, .1], DARK = [.09, .09, .1];

export const profileOf = ref => typeof ref === 'string' ? (PROFILES.find(p => p.id === ref) || PROFILES[0]) : ref;

/* ------------------------------- build context ------------------------------- */
export class BuildCtx {
  constructor(opts = {}) {
    this.opts = opts;
    this.b = new Builder();             // opaque pavement + structures
    this.decal = new Builder();         // paint + glyph decals (own depth bias pass)
    this.underground = new Builder();   // sub-datum geometry, shown in x-ray
    this.props = new Map();             // protoKey -> transforms[]
    this.animated = [];                 // gates / blinking heads
    this.lights = [];                   // {x,y,z,i,r,color,night}
    this.volumes = [];                  // minimap annotations
    this.notes = [];
    this.roadCount = 0; this.laneMeters = 0;
    this.deckY = 0; this.section = null; this.profile = null;
  }
  layer(key, underground = false) { return (underground ? this.underground : this.b).group(key); }
  prop(proto, x, y, z, rotY = 0, scale = 1, extra) {
    let list = this.props.get(proto);
    if (!list) this.props.set(proto, list = []);
    if (list.length > (this.opts.maxPerProp ?? 30000)) return;
    const a = Array.isArray(scale) ? scale : [scale, scale, scale];
    list.push({ x, y, z, rotY, sx: a[0], sy: a[1], sz: a[2], ...extra });
  }
}

/* ------------------------------- alignment ------------------------------- */
/** arc-length frames for a road's control points (cached; bump road._rev) */
export function framesOf(road, opts = {}) {
  if (road._frames && road._rev === (road._cacheRev ?? 0)) return road._frames;
  const pts = dedupe(road.points.map(p => ({ x: p.x, z: p.z, y: p.y ?? 0 })), 0.25);
  const poly = (road.smooth ?? 2) > 0 ? chaikin(pts, road.smooth ?? 2, !!road.closed) : pts;
  const frames = reframe(rdp(resamplePass(poly, opts.step ?? 3.2), opts.eps ?? 0.04));
  road._frames = frames; road._cacheRev = road._rev ?? 0;
  return frames;
}
function resamplePass(pts, step) {
  if (pts.length < 2) return [];
  const segs = []; let total = 0;
  for (let i = 1; i < pts.length; i++) {
    const d = dist(pts[i - 1], pts[i]);
    if (d > 1e-6) { segs.push({ a: pts[i - 1], b: pts[i], d, s0: total }); total += d; }
  }
  if (!total) return [];
  const n = Math.max(2, Math.round(total / step)), ds = total / n, out = [];
  let si = 0;
  for (let i = 0; i <= n; i++) {
    const t = Math.min(i * ds, total - 1e-4);
    while (si < segs.length - 1 && t > segs[si].s0 + segs[si].d) si++;
    const s = segs[si], k = clamp((t - s.s0) / s.d, 0, 1);
    out.push({ x: lerp(s.a.x, s.b.x, k), z: lerp(s.a.z, s.b.z, k), y: lerp(s.a.y ?? 0, s.b.y ?? 0, k), s: t });
  }
  return out;
}
/** Ramer–Douglas–Peucker so straights stay light and curves stay smooth */
function rdp(pts, eps) {
  if (pts.length < 3) return pts.slice();
  const keep = new Uint8Array(pts.length); keep[0] = keep[pts.length - 1] = 1;
  const stack = [[0, pts.length - 1]];
  while (stack.length) {
    const [a, b] = stack.pop();
    if (b - a < 2) continue;
    const A = pts[a], B = pts[b];
    const dx = B.x - A.x, dz = B.z - A.z, L2 = dx * dx + dz * dz, L = Math.sqrt(L2) || 1;
    let worst = 0, max = 0;
    for (let i = a + 1; i < b; i++) {
      const px = pts[i].x - A.x, pz = pts[i].z - A.z;
      const dLat = Math.abs(px * dz - pz * dx) / L;
      const dVert = Math.abs(pts[i].y - lerp(A.y, B.y, L2 ? (px * dx + pz * dz) / L2 : 0)) * .7;
      const e = Math.max(dLat, dVert);
      if (e > max) { max = e; worst = i; }
    }
    if (max > eps && worst) { keep[worst] = 1; stack.push([a, worst], [worst, b]); }
  }
  return pts.filter((_, i) => keep[i]);
}
function reframe(pts) {
  for (let i = 0; i < pts.length; i++) {
    const a = pts[Math.max(0, i - 1)], b = pts[Math.min(pts.length - 1, i + 1)];
    let tx = b.x - a.x, tz = b.z - a.z;
    const l = Math.hypot(tx, tz) || 1; tx /= l; tz /= l;
    pts[i].tx = tx; pts[i].tz = tz; pts[i].nx = -tz; pts[i].nz = tx;
  }
  return pts;
}
export function frameAt(frames, s) {
  if (!frames.length) return null;
  const end = frames[frames.length - 1].s;
  const t = clamp(s, 0, end);
  let i = 0;
  while (i < frames.length - 2 && frames[i + 1].s < t) i++;
  const a = frames[i], b = frames[i + 1] || a;
  return interpFrame(a, b, a.s === b.s ? 0 : (t - a.s) / (b.s - a.s));
}
function interpFrame(A, B, t) {
  t = clamp(t, 0, 1);
  const f = { x: lerp(A.x, B.x, t), z: lerp(A.z, B.z, t), y: lerp(A.y, B.y, t), s: lerp(A.s, B.s, t) };
  const nx = lerp(A.nx, B.nx, t), nz = lerp(A.nz, B.nz, t), l = Math.hypot(nx, nz) || 1;
  f.nx = nx / l; f.nz = nz / l; f.tx = -f.nz; f.tz = f.nx;
  return f;
}
/** frame at an exact arc position, or null if outside the alignment */
function atS(frames, s) {
  const L = frames[frames.length - 1]?.s ?? 0;
  if (!frames.length || s < -0.001 || s > L + .001) return null;
  return frameAt(frames, s);
}
function subdivide(A, B, n) { const out = []; for (let i = 0; i <= n; i++) out.push(interpFrame(A, B, i / n)); return out; }
/** a segment needs intermediate sections only when it bends or changes grade */
const curvy = (A, B, angTol = .028, gradeTol = .3) => Math.abs(A.tx * B.tz - A.tz * B.tx) > angTol || Math.abs((A.y + A.s * 0) - B.y) > gradeTol;
function partsOf(A, B, maxSeg) {
  if (dist(A, B) <= maxSeg) return [A, B];
  if (!curvy(A, B)) return [A, B];
  return subdivide(A, B, Math.ceil(dist(A, B) / maxSeg));
}

/* ------------------------------- quad primitives ------------------------------- */
function normalOf(p1, p2, p4) {
  const ax = p2.x - p1.x, ay = p2.y - p1.y, az = p2.z - p1.z;
  const bx = p4.x - p1.x, by = p4.y - p1.y, bz = p4.z - p1.z;
  let nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
  const l = Math.hypot(nx, ny, nz) || 1;
  return [nx / l, ny / l, nz / l];
}
function quadRaw(g, p1, p2, p3, p4, uv, col, smooth) {
  const n = normalOf(p1, p2, p4);
  const base = g.n;
  const pts = [p1, p2, p3, p4];
  for (let i = 0; i < 4; i++) {
    g.pos.push(pts[i].x, pts[i].y, pts[i].z);
    g.nrm.push(n[0], n[1], n[2]);
    g.uv.push(uv[i][0], uv[i][1]);
    g.col.push(col ? col[0] : 1, col ? col[1] : 1, col ? col[2] : 1);
  }
  g.idx.push(base, base + 1, base + 2, base, base + 2, base + 3);
  g.n += 4;
}
function triRaw(g, p1, p2, p3, uv, col) {
  const n = normalOf(p1, p2, p3);
  const base = g.n, pts = [p1, p2, p3];
  for (let i = 0; i < 3; i++) {
    g.pos.push(pts[i].x, pts[i].y, pts[i].z);
    g.nrm.push(n[0], n[1], n[2]);
    g.uv.push(uv[i][0], uv[i][1]);
    g.col.push(col ? col[0] : 1, col ? col[1] : 1, col ? col[2] : 1);
  }
  g.idx.push(base, base + 1, base + 2);
  g.n += 3;
}
const corner = (F, off, y) => ({ x: F.x + F.nx * off, y, z: F.z + F.nz * off });

/** one band between frames A→B across [offA,offB]; y0..y3 are the 4 corner heights */
function bandQ(g, A, B, offA, offB, yA0, yA1, yB0, yB1, uvScale, col) {
  const u0 = A.s / uvScale, u1 = B.s / uvScale;
  const v0 = offA / uvScale, v1 = offB / uvScale;
  quadRaw(g, corner(A, offA, yA0), corner(A, offB, yA1), corner(B, offB, yB1), corner(B, offA, yB0),
    [[u0, v0], [u0, v1], [u1, v1], [u1, v0]], col);
}

/** swept surface between two lateral offsets along the whole alignment */
function ribbon(ctx, frames, offA, offB, matKey, yFn, opt = {}) {
  const g = ctx.layer(matKey, opt.underground);
  const col = opt.col || [1, 1, 1];
  const uvScale = opt.uvScale || 5;
  const skip = opt.skipSegment;
  for (let i = 0; i < frames.length - 1; i++) {
    if (skip && skip(frames[i], frames[i + 1], i)) continue;
    const A = frames[i], B = frames[i + 1];
    const parts = partsOf(A, B, opt.subdiv ?? 11);
    for (let k = 0; k < parts.length - 1; k++) {
      const P = parts[k], Q = parts[k + 1];
      bandQ(g, P, Q, offA, offB, yFn(P, offA), yFn(P, offB), yFn(Q, offA), yFn(Q, offB), uvScale, col);
    }
  }
  return g;
}

/** sweep a closed 2D section [{x,y}] (x = lateral, y = up) along the alignment */
function sweepSection(ctx, frames, anchorOff, sec, matKey, opt = {}) {
  const g = ctx.layer(matKey, opt.underground);
  const col = opt.col || [1, 1, 1];
  const uvScale = opt.uvScale || 3;
  const maxSeg = opt.maxSeg ?? 8;
  const baseFn = opt.yBase || (F => F.y + (ctx.deckY || 0) + (opt.level ? opt.level(F, anchorOff) : 0));
  const heightAt = opt.heightAt;
  const pairs = [];
  for (let i = 0; i < sec.length - 1; i++) pairs.push([i, i + 1]);
  if (opt.open !== true) pairs.push([sec.length - 1, 0]);
  const pos = (F, base, h, si) => ({
    x: F.x + F.nx * (anchorOff + sec[si].x),
    z: F.z + F.nz * (anchorOff + sec[si].x),
    y: base + sec[si].y * (heightAt ? clamp(heightAt(F), .02, 1.6) : 1)
  });
  for (let f = 0; f < frames.length - 1; f++) {
    if (opt.skipSegment && opt.skipSegment(frames[f], frames[f + 1], f)) continue;
    const parts = partsOf(frames[f], frames[f + 1], maxSeg);
    for (let k = 0; k < parts.length - 1; k++) {
      const P = parts[k], Q = parts[k + 1];
      const bp = baseFn(P), bq = baseFn(Q);
      for (const [i0, i1] of pairs) {
        const a = pos(P, bp, null, i0), b = pos(P, bp, null, i1), c = pos(Q, bq, null, i1), d = pos(Q, bq, null, i0);
        quadRaw(g, a, b, c, d, [
          [(anchorOff + sec[i0].x) / uvScale, P.s / uvScale], [(anchorOff + sec[i1].x) / uvScale, P.s / uvScale],
          [(anchorOff + sec[i1].x) / uvScale, Q.s / uvScale], [(anchorOff + sec[i0].x) / uvScale, Q.s / uvScale]
        ], col);
      }
    }
  }
  if (opt.caps) {
    for (const F of [frames[0], frames[frames.length - 1]]) {
      const base = baseFn(F);
      for (let i = 1; i < sec.length - 1; i++)
        triRaw(g, pos(F, base, null, 0), pos(F, base, null, i), pos(F, base, null, i + 1), [[0, 0], [1, 0], [1, 1]], col);
    }
  }
  return g;
}

/** rounded kerb section: vertical face + bull-nose + top + back */
export const KERB_SECTION = (h, r, w) => {
  h = Math.max(.04, h); r = clamp(r, .01, h / 2 - .001);
  const seg = 4, s = [{ x: 0, y: 0 }, { x: 0, y: h - r }];
  for (let i = 1; i <= seg; i++) { const a = Math.PI - (i / seg) * (Math.PI / 2); s.push({ x: r + r * Math.cos(a), y: (h - r) + r * Math.sin(a) }); }
  s.push({ x: w, y: h }, { x: w, y: 0 });
  return s;
};

/* ------------------------------- roads ------------------------------- */
export function prepareRoad(road) {
  road._profile = profileOf(road.profile);
  road._section = solveSection(road._profile);
  return road._profile;
}

export function buildRoad(road, ctx, opts = {}) {
  prepareRoad(road);
  const profile = road._profile, section = road._section;
  const frames = framesOf(road, opts);
  if (frames.length < 2) return null;
  const L = frames[frames.length - 1].s;
  const detail = opts.detail ?? 1;
  const M = detail > 0 ? (profile.markings || {}) : {};
  const idSeed = strSeed(road.id) % 99991;
  const deck = road.deckY || 0;
  ctx.deckY = deck; ctx.section = section; ctx.profile = profile;
  ctx.roadCount++;
  const cut = road.cut && road.cut.length ? (A => road.cut.some(c => A.s > c.a && A.s < c.b)) : null;
  const yFn = (F, off) => F.y + deck + levelAt(section, profile, off);

  /* ---- surfaces + sections, element by element ---- */
  for (const e of section.elements) {
    const surf = e.surf || 'asphalt';
    const tint = surfTint(surf);
    const uvScale = (SURFACES[surf] || { scale: 4 }).scale;
    switch (e.k) {
      case 'curb': {
        const h = e.h ?? .15;
        const drops = [...(road.drops || [])];
        const heightAt = drops.length ? (F => {
          for (const d of drops) if (F.s > d.s - (d.len ?? 3) / 2 && F.s < d.s + (d.len ?? 3) / 2) return (d.y ?? .02) / h;
          return 1;
        }) : null;
        sweepSection(ctx, frames, e.x0, KERB_SECTION(h, e.r ?? .05, e.w || .18), G.granite, {
          col: tint, uvScale: .45, skipSegment: cut, maxSeg: 10, heightAt,
          yBase: F => F.y + deck + levelAt(section, profile, e.x0) - h
        });
        break;
      }
      case 'barrier': {
        const h = e.h || .84, w = e.w;
        const sec = [{ x: 0, y: 0 }, { x: w * .2, y: h * .5 }, { x: w * .32, y: h }, { x: w * .68, y: h }, { x: w * .8, y: h * .5 }, { x: w, y: 0 }];
        sweepSection(ctx, frames, e.x0, sec, G.concrete, {
          col: [.68, .68, .66], uvScale: .9, skipSegment: cut, maxSeg: 9, caps: true,
          yBase: F => F.y + deck + levelAt(section, profile, e.x0)
        });
        break;
      }
      case 'median': {
        const grassy = surf === 'grass' || surf === 'grass-mown';
        const h = e.h ?? (grassy ? .12 : .09);
        const lC = e.curbL !== false, rC = e.curbR !== false;
        ribbon(ctx, frames, e.x0 + (lC ? .18 : 0), e.x1 - (rC ? .18 : 0), surfBucket(surf),
          F => yFn(F, e.cx) + h, { col: tint, uvScale: uvScale * 1.4, skipSegment: cut, subdiv: 10 });
        for (const [side, off] of [[-1, e.x0], [1, e.x1 - .18]]) {
          if (side < 0 && !lC) continue; if (side > 0 && !rC) continue;
          sweepSection(ctx, frames, off, KERB_SECTION(h + .03, .05, .18), G.granite, {
            col: [.72, .71, .68], uvScale: .5, skipSegment: cut, maxSeg: 9,
            yBase: F => F.y + deck + levelAt(section, profile, off) - .03
          });
        }
        if (e.barrier) sweepSection(ctx, frames, e.cx - .3, [{ x: 0, y: 0 }, { x: .12, y: .5 }, { x: .48, y: .5 }, { x: .6, y: 0 }], G.concrete, { col: [.7, .7, .68], uvScale: 1, yBase: F => F.y + deck + levelAt(section, profile, e.cx) + h, maxSeg: 9 });
        break;
      }
      case 'rail': case 'tram': {
        const embed = e.k === 'tram';
        ribbon(ctx, frames, e.x0, e.x1, surfBucket(surf), yFn, { col: embed ? tint : [.36, .35, .34], uvScale, skipSegment: cut, subdiv: 10 });
        const halfG = (e.gauge || 1.435) / 2, cx = e.cx;
        const railSec = [{ x: -.085, y: 0 }, { x: .085, y: 0 }, { x: .05, y: .075 }, { x: .042, y: .115 }, { x: .075, y: .152 }, { x: -.075, y: .152 }, { x: -.042, y: .115 }, { x: -.05, y: .075 }];
        for (const sgn of [-1, 1]) {
          const off = cx + sgn * halfG;
          sweepSection(ctx, frames, off - .085, railSec, G.metal, {
            col: embed ? [.5, .51, .53] : [.7, .71, .73], uvScale: 1.1, skipSegment: cut, maxSeg: 3.4,
            yBase: F => F.y + deck + levelAt(section, profile, off) + (embed ? .005 : .04)
          });
        }
        if (!embed) {
          for (let s = .3; s < L; s += .675) { const F = atS(frames, s); if (F) ctx.prop('sleeper', F.x + F.nx * cx, F.y + deck + .015, F.z + F.nz * cx, Math.atan2(F.tx, F.tz)); }
        }
        ribbon(ctx, frames, cx - halfG - .3, cx + halfG + .3, embed && e.greenTrack ? G.grass : G.rubber,
          F => yFn(F, cx) + (embed ? .012 : .028), { col: embed && e.greenTrack ? [.26, .37, .19] : [.13, .13, .14], uvScale: 1.6, skipSegment: cut });
        break;
      }
      case 'fence': {
        for (let s = 1.4; s < L; s += 2.9) { const F = atS(frames, s); if (F) ctx.prop('fence-mesh', F.x + F.nx * e.cx, yFn(F, e.cx), F.z + F.nz * e.cx, Math.atan2(F.tx, F.tz)); }
        break;
      }
      case 'wall': case 'guard': {
        const h = e.h ?? (e.k === 'wall' ? 2.1 : .6);
        const sec = [{ x: 0, y: 0 }, { x: e.w * .16, y: h }, { x: e.w * .84, y: h }, { x: e.w, y: 0 }];
        sweepSection(ctx, frames, e.x0, sec, G.concrete, {
          col: e.k === 'guard' ? [.62, .61, .58] : [.55, .55, .54], uvScale: 1.7, skipSegment: cut, maxSeg: 9, caps: true,
          yBase: F => F.y + deck + levelAt(section, profile, e.x0) - (e.found ? .6 : 0)
        });
        break;
      }
      case 'water': {
        ribbon(ctx, frames, e.x0, e.x1, G.water, F => F.y + deck + levelAt(section, profile, e.cx) - .1, { uvScale: 9, skipSegment: cut });
        break;
      }
      case 'tunnel': case 'tunnel-bore': break;
      default: {
        ribbon(ctx, frames, e.x0, e.x1, surfBucket(surf), yFn, { col: tint, uvScale, skipSegment: cut, subdiv: 10 });
        if (e.k === 'sidewalk' || e.k === 'plaza') {   // slab thickness so paving reads solid
          const t = e.k === 'sidewalk' ? .06 : .04;
          for (const off of [e.x0, e.x1 - .1]) ribbon(ctx, frames, off, off + .1, G.concrete, F => yFn(F, off) - t, { col: [.55, .55, .54], uvScale: 3, skipSegment: cut });
        }
        if (e.k === 'verge' && (e.h ?? .06) > .02) for (const off of [e.x0, e.x1 - .12]) ribbon(ctx, frames, off, off + .12, G.soil, F => yFn(F, off) - .06, { col: [.3, .26, .19], uvScale: 2, skipSegment: cut });
        break;
      }
    }
  }

  if (detail > 0) buildMarkings(road, ctx, { frames, section, profile, M, L, cut, idSeed, detail, yFn });
  buildRoadFeatures(road, ctx, { frames, section, profile, L, yFn, idSeed, cut });
  if (detail > .5) buildProps(road, ctx, { frames, section, profile, L, yFn, idSeed, cut, opts });
  buildStructure(road, ctx, { frames, section, profile, L, yFn, opts, cut });
  for (const c of (road.crossings || [])) buildLevelCrossing(road, ctx, { frames, section, profile, yFn, at: c.s, width: c.width || 13, c });

  ctx.laneMeters += L * Math.max(1, section.elements.filter(e => ['lane', 'bus', 'turn'].includes(e.k) && (e.dirNum ?? 1) !== 0).length);
  return { frames, length: L, section };
}

/* ------------------------------- markings ------------------------------- */
export function carriagewayHalf(section) {
  const trav = section.elements.filter(e => CARRIAGEWAY.includes(e.k));
  if (!trav.length) return section.total / 2;
  return Math.max(1.2, ...trav.flatMap(e => [Math.abs(e.x0), Math.abs(e.x1)]));
}

function buildMarkings(road, ctx, st) {
  const { frames, section, profile, M, L, cut, idSeed } = st;
  const g = ctx.decal.group(G.paint);
  const bounds = markingBoundaries(section, profile);
  const speed = profile.speed || 50;
  const dash = clamp(speed / 22, 1.8, 3.4), gap = dash * (speed > 70 ? 3 : 2.2);
  const yLift = .015;
  for (const b of bounds) {
    const yellow = b.type.includes('yellow'), red = b.type.includes('red');
    const col = red ? RED : yellow ? YELLOW : WHITE;
    const w = b.type.startsWith('solid') ? .14 : .12;
    const dbl = b.type.startsWith('double');
    emitLine(ctx, g, frames, section, profile, b.x, w, yLift, col, {
      dash: b.type.startsWith('broken') ? dash : Infinity, gap: b.type.startsWith('broken') ? gap : 0,
      skip: cut, phase: (idSeed % 11) * .9, double: dbl ? .15 : 0
    });
  }
  if (M.center && M.center !== 'none' && !bounds.some(b => Math.abs(b.x - section.center) < .35)) {
    const col = M.center.includes('yellow') ? YELLOW : WHITE;
    emitLine(ctx, g, frames, section, profile, section.center, M.center.startsWith('double') ? .13 : .12, yLift, col, {
      dash: M.center.startsWith('broken') ? dash : Infinity, gap: M.center.startsWith('broken') ? gap : 0,
      skip: cut, double: M.center.startsWith('double') ? .15 : 0
    });
  }
  // waiting restrictions painted at the kerb line
  const restrict = M.restriction || (M.redRoute ? 'red-route' : null) || (profile.id === 'art-highstreet-uk' ? 'double-yellow' : null);
  if (restrict) {
    const col = restrict === 'red-route' ? [.74, .07, .05] : YELLOW;
    for (const e of section.elements) if (e.k === 'gutter' || (e.k === 'curb')) emitLine(ctx, g, frames, section, profile, e.x0 + e.w * .5, .12, .045, col, { dash: Infinity, gap: 0, skip: cut, double: restrict !== 'red-route' ? .12 : 0 });
  }
  /* symbols, word marks, arrows */
  const trav = section.elements.filter(e => ['lane', 'turn', 'bus', 'cycle'].includes(e.k));
  const symSpace = clamp(speed * 1.1, 42, 120);
  for (const e of trav) {
    const w = e.x1 - e.x0;
    for (let s = 26; s < L - 14; s += symSpace) {
      const at = s + hash01(idSeed, Math.round(s), Math.round(e.cx * 10)) * 10 - 5;
      if (at < 12 || at > L - 12) continue;
      const rot = (e.dirNum ?? 1) < 0 ? Math.PI : 0;
      if (e.k === 'bus' && M.busLane) {
        glyphQuad(ctx, 'glyph:bus', frames, at, e.cx, w * .55, w * .95, rot, section, profile, cut);
        if (M.words) glyphQuad(ctx, 'glyph:word-bus', frames, at + 13, e.cx, w * .82, 2.4, rot, section, profile, cut);
      }
      if (e.k === 'cycle' && M.cycleSymbols) {
        glyphQuad(ctx, 'glyph:cycle', frames, at, e.cx, w * .62, w * .9, rot, section, profile, cut);
        glyphQuad(ctx, 'glyph:cycle-arrows', frames, at + 6.5, e.cx, w * .5, 1.5, rot, section, profile, cut);
      }
      if (M.arrows && e.k === 'lane' && hash01(idSeed, at, 3) > .45) {
        const near = at < 26 || at > L - 26;
        glyphQuad(ctx, 'glyph:' + (near && e.turnLane ? 'arrow-turn' : 'arrow'), frames, near ? (at < L / 2 ? 17 : L - 17) : at, e.cx, w * .52, 3.6, rot, section, profile, cut);
      }
      if (M.words && e.k === 'lane' && hash01(idSeed, at, 9) > .7) glyphQuad(ctx, 'glyph:word-slow', frames, at - 10, e.cx, w * .8, 2.6, rot, section, profile, cut);
    }
  }
  if (M.parkingStalls && M.parkingStalls !== 'none') buildStalls(road, ctx, st, M.parkingStalls);
  if (M.hatch || M.chevrons) emitChevronHatch(road, ctx, st);
  if (M.giveWay) emitGiveWay(road, ctx, st);
  if (M.rumble) emitRumble(road, ctx, st);
  if (M.keepClear) emitKeepClear(road, ctx, st);
  if (M.crosswalk && M.crosswalk !== 'none' && !road.noEndCrossings) for (const end of [3.6, L - 3.6]) emitCrosswalk(road, ctx, { frames, section, profile, at: end, inward: end < L / 2 ? 1 : -1, style: M.crosswalk, cut });
}

function emitLine(ctx, g, frames, section, profile, off, w, lift, col, o) {
  const yAt = F => F.y + (ctx.deckY || 0) + levelAt(section, profile, off) + lift;
  const step = o.dash === Infinity ? 0 : o.dash + o.gap;
  const offs = o.double ? [off - o.double / 2 - w / 2, off + o.double / 2 + w / 2] : [off];
  for (const ln of offs) {
    for (let i = 0; i < frames.length - 1; i++) {
      const A = frames[i], B = frames[i + 1];
      if (o.skip && o.skip(A)) continue;
      const s0 = A.s + o.phase, s1 = B.s + o.phase;
      if (!step) { const yA = yAt(A), yB = yAt(B); bandQ(g, A, B, ln - w / 2, ln + w / 2, yA, yA, yB, yB, 1, col); continue; }
      let c = Math.floor(s0 / step) * step;
      if (c + o.dash < s0) c += step;
      while (c < s1) {
        const a = Math.max(c, s0), b = Math.min(c + o.dash, s1);
        if (b - a > .06) {
          const A2 = interpFrame(A, B, (a - o.phase - A.s) / Math.max(.001, B.s - A.s));
          const B2 = interpFrame(A, B, (b - o.phase - A.s) / Math.max(.001, B.s - A.s));
          const yA = yAt(A2), yB = yAt(B2);
          bandQ(g, A2, B2, ln - w / 2, ln + w / 2, yA, yA, yB, yB, 1, col);
        }
        c += step;
      }
    }
  }
}
function glyphQuad(ctx, key, frames, s, off, w, len, rot, section, profile, skip) {
  const A = atS(frames, s - len / 2), B = atS(frames, s + len / 2);
  if (!A || !B || (skip && skip(A))) return;
  const yA = A.y + (ctx.deckY || 0) + levelAt(section, profile, off) + .018;
  const yB = B.y + (ctx.deckY || 0) + levelAt(section, profile, off) + .018;
  const g = ctx.decal.group(key);
  const flip = rot > 1.5;
  const uv = flip ? [[1, 1], [0, 1], [0, 0], [1, 0]] : [[0, 0], [1, 0], [1, 1], [0, 1]];
  const half = w / 2;
  quadRaw(g, corner(A, off - half, yA), corner(A, off + half, yA), corner(B, off + half, yB), corner(B, off - half, yB),
    uv.map(u => [u[0], u[1]]), [1, 1, 1]);
}

function buildStalls(road, ctx, st, mode) {
  const { frames, section, profile } = st;
  const L = frames[frames.length - 1].s;
  const g = ctx.decal.group(G.paint);
  const idSeed = st.idSeed || 1;
  for (const e of section.elements) {
    if (e.k !== 'parking') continue;
    const angle = e.angle === 45 ? 45 : e.angle === 60 ? 60 : 90;
    const bay = angle === 45 ? 5.4 : angle === 60 ? 5.0 : 5.2;
    const depth = e.w - .25;
    const skew = depth / Math.tan(angle * Math.PI / 180);
    for (let s = 1.5, i = 0; s + .5 < L; s += bay, i++) {
      const A = atS(frames, s);
      if (!A) continue;
      const y = A.y + (ctx.deckY || 0) + levelAt(section, profile, e.cx) + .016;
      const inSign = e.x1 > e.x0 ? 1 : -1;
      const edge = angle === 90 ? e.x0 : e.x0 + inSign * .12;
      const far = e.x0 + inSign * depth;
      const off2 = edge + inSign * skew;
      const P1 = { x: A.x + A.nx * edge, y, z: A.z + A.nz * edge };
      const P2 = { x: A.x + A.nx * far + A.tx * 0, y, z: A.z + A.nz * far };
      const P3 = { x: A.x + A.nx * far + A.tx * .13, y, z: A.z + A.nz * far + A.tz * .13 };
      const P4 = { x: A.x + A.nx * edge + A.tx * .13, y, z: A.z + A.nz * edge + A.tz * .13 };
      quadRaw(g, P1, P2, P3, P4, [[0, 0], [1, 0], [1, 1], [0, 1]], WHITE);
      // bay line along the kerb (fog line for the stall row)
      const B1 = atS(frames, Math.max(0, s - bay / 2)), B2 = atS(frames, Math.min(L, s + bay / 2));
      if (B1 && B2) bandQ(g, B1, B2, edge - inSign * .18, edge - inSign * .06, y, y, y, y, 1, WHITE);
      if (e.disabled && i % 5 === 0) {
        const dA = atS(frames, Math.max(0, s - bay / 2 + .3)), dB = atS(frames, Math.min(L, s + bay / 2 - .3));
        if (dA && dB) bandQ(g, dA, dB, edge + inSign * .4, edge + inSign * (depth - .5), y + .001, y + .001, y + .001, y + .001, 1, [.16, .34, .78]);
      }
      if (e.payBays && i % 3 === 0) { const dA = atS(frames, s - .12), dB = atS(frames, s + .12); if (dA && dB) bandQ(g, dA, dB, edge - inSign * .3, far, y, y, y, y, 1, [.86, .3, .06]); }
    }
  }
}
function emitRumble(road, ctx, st) {
  const { frames, section, profile } = st;
  const g = ctx.layer(G.concrete);
  for (const e of section.elements) {
    if (e.k !== 'shoulder') continue;
    for (let s = 0; s < frames[frames.length - 1].s; s += 3.6) {
      const A = atS(frames, s), B = atS(frames, Math.min(s + .4, frames[frames.length - 1].s));
      if (!A || !B) continue;
      for (let r = 0; r < 3; r++) {
        const yA = A.y + (ctx.deckY || 0) + levelAt(section, profile, e.cx) + .025, yB = B.y + (ctx.deckY || 0) + levelAt(section, profile, e.cx) + .025;
        bandQ(g, A, B, e.x0 + .12 + r * .24, e.x0 + .3 + r * .24, yA, yA, yB, yB, 1, [.52, .52, .5]);
      }
    }
  }
}
function emitGiveWay(road, ctx, st) {
  const { frames, section, L } = st;
  const g = ctx.decal.group(G.paint);
  const w = carriagewayHalf(section);
  for (const at of [4.6, L - 4.6]) {
    const F = atS(frames, at); if (!F) continue;
    const y = F.y + (ctx.deckY || 0) + levelAt(section, road._profile, 0) + .014;
    for (let i = 0; i < 7; i++) {
      const along = .16 + i * .13, half = (w - i * (w / 7)) * .92;
      const A = atS(frames, clamp(at - along, 0, L)), B = atS(frames, clamp(at + along, 0, L));
      if (!A || !B) continue;
      triRaw(g, { x: A.x + A.nx * half, y, z: A.z + A.nz * half }, { x: A.x - A.nx * half, y, z: A.z - A.nz * half }, { x: B.x, y, z: B.z }, [[0, 0], [1, 0], [.5, 1]], WHITE);
    }
  }
}
function emitChevronHatch(road, ctx, st) {
  const { frames, section, profile, L } = st;
  const g = ctx.decal.group(G.paint);
  const w = carriagewayHalf(section) * .5;
  for (let s = 7; s < L - 7; s += 2.6) {
    const A = atS(frames, s), B = atS(frames, Math.min(s + 1.15, L));
    if (!A || !B) continue;
    const y = A.y + (ctx.deckY || 0) + levelAt(section, profile, 0) + .013;
    for (const sgn of [-1, 1]) {
      const p1 = { x: A.x + A.nx * sgn * w, y, z: A.z + A.nz * sgn * w };
      const p2 = { x: A.x + A.nx * sgn * (w - 1), y, z: A.z + A.nz * sgn * (w - 1) };
      const p3 = { x: B.x + B.nx * sgn * (w - 1), y, z: B.z + B.nz * sgn * (w - 1) };
      triRaw(g, p1, p2, p3, [[0, 0], [1, 0], [1, 1]], WHITE);
    }
  }
}
function emitKeepClear(road, ctx, st) {
  const { frames, section, profile, L } = st;
  for (const s of (road.keepClear || [])) {
    const A = atS(frames, s - 4), B = atS(frames, s + 4);
    if (!A || !B) continue;
    const w = carriagewayHalf(section);
    const g = ctx.decal.group('glyph:keep-clear');
    const yA = A.y + (ctx.deckY || 0) + levelAt(section, profile, 0) + .017, yB = B.y + (ctx.deckY || 0) + levelAt(section, profile, 0) + .017;
    bandQ(g, A, B, -w, w, yA, yA, yB, yB, 8, [1, 1, 1]);
  }
}

/* ------------------------------- features (calming, tables, driveways) ------------------------------- */
function buildRoadFeatures(road, ctx, st) {
  const { frames, section, profile, yFn, cut } = st;
  const L = frames[frames.length - 1].s;
  const w = carriagewayHalf(section);
  /* alignments are simplified after smoothing, so station values can drift a
     little past the ends — normalise them once here rather than in every emitter */
  for (const k of ['bumps', 'crosstables', 'crossingsManual', 'driveways', 'stations']) {
    for (const o of (road[k] || [])) o.s = clamp(o.s, 1.5, Math.max(1.6, L - 1.5));
  }
  for (const b of (road.bumps || [])) {
    const kind = b.kind || 'cushion';
    const h = b.height ?? (kind === 'table' ? .1 : .085);
    const len = b.len ?? (kind === 'table' ? 3.6 : .9);
    const A = atS(frames, clamp(b.s - len / 2 - .6, 0, L)), B = atS(frames, clamp(b.s + len / 2 + .6, 0, L));
    if (!A || !B) continue;
    const g = ctx.layer(G.asphalt);
    const gg = ctx.decal.group(G.paint);
    const steps = kind === 'table' ? 9 : 6;
    const bands = kind === 'cushion' ? (b.cushions === 1 ? [[-1.05, 1.05]] : [[-w + .6, -1.15], [1.15, w - .6]]) : [[-w + .25, w - .25]];
    for (let i = 0; i < steps; i++) {
      const P = interpFrame(A, B, i / steps), Q = interpFrame(A, B, (i + 1) / steps);
      const f = t => Math.sin(clamp(t, 0, 1) * Math.PI) * h;
      for (const [oa, ob] of bands) {
        bandQ(g, P, Q, oa, ob, yFn(P, 0) + f(i / steps), yFn(P, 0) + f(i / steps), yFn(Q, 0) + f((i + 1) / steps), yFn(Q, 0) + f((i + 1) / steps), 4, [.42, .42, .44]);
      }
      const yc = (F) => yFn(F, 0) + f(0) + .014;
      if (i === 1 || i === steps - 2) for (const [oa, ob] of bands) {
        bandQ(gg, P, Q, oa - .34, oa, yc(P), yc(P), yc(Q), yc(Q), 1, YELLOW);
        bandQ(gg, P, Q, ob, ob + .34, yc(P), yc(P), yc(Q), yc(Q), 1, YELLOW);
      }
    }
    for (const sgn of [-1, 1]) {  // white triangles on the approach
      const F = atS(frames, clamp(b.s + sgn * (len / 2 + 2.2), 0, L));
      if (!F) continue;
      const y = yFn(F, 0) + .016;
      for (const off of [-w * .55, 0, w * .55]) triRaw(gg, { x: F.x + F.nx * (off - .5), y, z: F.z + F.nz * (off - .5) }, { x: F.x + F.nx * (off + .5), y, z: F.z + F.nz * (off + .5) }, { x: F.x + F.nx * off + F.tx * sgn * 1.1, y, z: F.z + F.nz * off + F.tz * sgn * 1.1 }, [[0, 0], [1, 0], [.5, 1]], WHITE);
    }
  }
  for (const c of (road.crosstables || [])) {
    const A = atS(frames, clamp(c.s - 5, 0, L)), B = atS(frames, clamp(c.s + 5, 0, L));
    if (!A || !B) continue;
    const g = ctx.layer(G.paver);
    for (let i = 0; i < 10; i++) {
      const P = interpFrame(A, B, i / 10), Q = interpFrame(A, B, (i + 1) / 10);
      const h = Math.sin(clamp(i / 10, 0, 1) * Math.PI) * .1;
      const half = Math.max(Math.abs(section.start), Math.abs(section.end));
      bandQ(g, P, Q, -half, half, P.y + (ctx.deckY || 0) + h, P.y + (ctx.deckY || 0) + h, Q.y + (ctx.deckY || 0) + h, Q.y + (ctx.deckY || 0) + h, 2.4, [.6, .56, .5]);
    }
  }
  for (const x of (road.crossingsManual || [])) emitCrosswalk(road, ctx, { frames, section, profile, at: x.s, inward: x.s < L / 2 ? 1 : -1, style: x.style || 'zebra' });
  for (const d of (road.driveways || [])) {
    const F = atS(frames, d.s); if (!F) continue;
    const els = section.elements.filter(e => e.k === 'sidewalk' || e.k === 'plaza');
    const el = els[d.side > 0 ? els.length - 1 : 0] || els[0];
    if (!el) continue;
    const A = atS(frames, clamp(d.s - 2.6, 0, L)), B = atS(frames, clamp(d.s + 2.6, 0, L));
    if (!A || !B) continue;
    const y = F.y + (ctx.deckY || 0) + .015;
    bandQ(ctx.layer(G.paver), A, B, el.x0, el.x1, y, y, y, y, 1.5, [.62, .6, .55]);
    road.drops = road.drops || [];
    road.drops.push({ s: d.s, len: 5.2, y: .02 });
  }
}

export function emitCrosswalk(road, ctx, st) {
  const { frames, section, profile, at, inward, style = 'zebra' } = st;
  const F = atS(frames, at); if (!F) return;
  const L = frames[frames.length - 1].s;
  const w = carriagewayHalf(section);
  const across = w * 2 + 1.8;
  const y = F.y + (ctx.deckY || 0) + levelAt(section, profile, 0) + .018;
  const len = style === 'ladder' ? 4.2 : 3.0;
  const s0 = clamp(at - (inward > 0 ? 0 : len), 0, L), s1 = clamp(at + (inward > 0 ? len : 0), 0, L);
  const A = atS(frames, s0), B = atS(frames, s1);
  if (!A || !B) return;
  const g = ctx.decal.group(G.paint);
  const bars = style === 'continental' ? 9 : style === 'ladder' ? 5 : 7;
  const barW = style === 'zebra' ? .5 : .68;
  for (let i = 0; i < bars; i++) {
    const c = -across / 2 + (i + .5) * (across / bars);
    const a = c - (across / bars - barW) / 2, b = c + (across / bars - barW) / 2;
    bandQ(g, A, B, a, b, y, y, y, y, 1, WHITE);
  }
  if (style === 'ladder') for (const off of [-across / 2, across / 2]) bandQ(g, A, B, off - .13, off + .13, y, y, y, y, 1, WHITE);
  const sl = clamp(at + (inward > 0 ? -len - 1 : len + 1), 0, L);
  const SA = atS(frames, sl - .35), SB = atS(frames, sl + .35);
  if (SA && SB) bandQ(g, SA, SB, -w, w, y - .002, y - .002, y - .002, y - .002, 1, WHITE);
  for (const sgn of [-1, 1]) ctx.prop('tactile-paving', F.x + F.nx * sgn * (across / 2 + .7) - F.tx * 0, y - .006, F.z + F.nz * sgn * (across / 2 + .7), Math.atan2(F.tx, F.tz), [2.3, 1, 1.7]);
}

/* ------------------------------- street furniture ------------------------------- */
function firstWhere(section, kinds, side) {
  const list = section.elements.filter(e => kinds.includes(e.k));
  if (!list.length) return null;
  return side < 0 ? list[0] : list[list.length - 1];
}
const LAMP = { cobra: 'lamp-cobra', modern: 'lamp-modern', heritage: 'lamp-heritage', twin: 'lamp-twin', cycle: 'lamp-cycle', bollard: 'lamp-bollard', sphere: 'lamp-globe', tunnel: 'lamp-tunnel', bridge: 'lamp-modern', wall: 'lamp-wall-bracket' };
const TREE = { plane: 'tree-plane', palm: 'tree-palm', cherry: 'tree-cherry', oak: 'tree-oak', olive: 'tree-olive' };

function buildProps(road, ctx, st) {
  const { frames, section, profile, L, yFn, idSeed, cut, opts } = st;
  const P = profile.props || {};
  const scatter = (proto, el, off, o = {}) => {
    if (!el) return;
    const spacing = o.spacing ?? 20;
    for (let s = (o.from ?? 7) + (o.phase ?? 0); s < L - (o.to ?? 5); s += spacing) {
      if (o.every && Math.round((s - (o.phase ?? 0)) / spacing) % o.every) continue;
      const F = atS(frames, s);
      if (!F || (cut && cut(F))) continue;
      const lateral = el.cx + (off ?? 0);
      const rot = o.align === false ? (o.rot ?? 0) : Math.atan2(F.tx, F.tz) + (o.rot ?? 0);
      const y = o.absolute ? (o.y ?? 0) : yFn(F, lateral) + (o.y ?? 0);
      const sc = o.scale ?? (o.random ? .82 + hash01(idSeed, Math.round(s), Math.round(el.x0 * 7)) * .4 : 1);
      ctx.prop(proto, F.x + F.nx * lateral, y, F.z + F.nz * lateral, rot, sc, o.extra);
      if (o.mirror) {
        const lat2 = -(el.cx + (off ?? 0)) - 2 * (off ?? 0);
        ctx.prop(proto, F.x + F.nx * lat2, y, F.z + F.nz * lat2, -rot + Math.PI, sc, o.extra);
      }
    }
  };
  const swR = firstWhere(section, ['sidewalk', 'plaza'], 1), swL = firstWhere(section, ['sidewalk', 'plaza'], -1);
  const vgR = firstWhere(section, ['verge'], 1);
  const anySw = swR || swL || vgR || section.elements[section.elements.length - 1];

  if (P.lights) {
    const proto = LAMP[P.lights.type] || 'lamp-modern';
    if (proto === 'lamp-twin') scatter(proto, { cx: section.center }, 0, { spacing: P.lights.spacing ?? 45, align: false });
    else scatter(proto, anySw, -.2, { spacing: P.lights.spacing ?? 32, mirror: P.lights.mirror !== false, y: proto === 'lamp-wall-bracket' ? 3.4 : .02, absolute: false });
  }
  if (P.trees) {
    const proto = TREE[P.trees.species] || 'tree-plane';
    const el = P.trees.side === 'median' ? firstWhere(section, ['median'], 1) : P.trees.side === 'both' ? swL : (vgR || swR || swL);
    scatter(proto, el, 0, { spacing: P.trees.spacing ?? 9, random: true, mirror: P.trees.side === 'both' });
    if (opts.treeGuards && el && el.k !== 'median') scatter('street-tree-guard', el, 0, { spacing: P.trees.spacing ?? 9, mirror: P.trees.side === 'both' });
  }
  if (P.bollards) {
    const proto = P.bollards.type === 'post' ? 'bollard-pin' : P.bollards.type === 'retractable' || P.bollards.type === 'removable' ? 'bollard-retractable' : 'bollard';
    scatter(proto, swR || swL, -.36, { spacing: P.bollards.spacing ?? 11, mirror: true });
  }
  if (P.benches) scatter('bench', anySw, -.15, { spacing: 30, mirror: true });
  if (P.bins) scatter('bin', anySw, -.4, { spacing: 44, mirror: true });
  if (P.planters) scatter('planter', anySw, -.5, { spacing: 24, mirror: true });
  if (P.meters) for (const e of section.elements) if (e.k === 'parking') scatter('parking-meter', e, (e.x1 - e.x0) * .42, { spacing: 11 });
  if (P.hydrants) scatter('hydrant', anySw, -.55, { spacing: 120, phase: 55 });
  if (P.drains) for (const e of section.elements) if (e.k === 'gutter' || e.k === 'curb') { scatter('gully', e, 0, { spacing: P.drains.spacing ?? 40, phase: 17 }); break; }
  if (P.signs) scatter('sign-round', anySw, -.7, { spacing: 160, phase: 40 });
  if (P.busStops) {
    const stop = section.elements.find(e => e.k === 'bus');
    const spacing = typeof P.busStops === 'object' && P.busStops.spacing ? P.busStops.spacing : 560;
    if (stop) for (let s = spacing / 2; s < L - 10; s += spacing) {
      const F = atS(frames, s); if (!F) continue;
      const lat = stop.cx + (stop.cx > 0 ? 1.9 : -1.9);
      ctx.prop('bus-shelter', F.x + F.nx * lat, yFn(F, lat) + .03, F.z + F.nz * lat, Math.atan2(F.tx, F.tz) + (lat > 0 ? 0 : Math.PI));
      ctx.prop('bus-flag', F.x + F.nx * (lat + (lat > 0 ? 1.6 : -1.6)), yFn(F, lat + 1.6), F.z + F.nz * (lat + 1.6), Math.atan2(F.tx, F.tz));
      (road.stops = road.stops || []).push({ s, x: F.x, z: F.z, lat, kind: 'bus' });
    }
  }
  if (P.guards && P.guards.type !== 'parapet') {
    const proto = { beam: 'guard-beam', wire: 'guard-wire', stone: 'guard-stone', timber: 'guard-timber' }[P.guards.type] || 'guard-beam';
    const spacing = P.guards.type === 'wire' ? 4.2 : P.guards.type === 'stone' ? 2.3 : 2.0;
    const sides = P.guards.side === 'both' ? [-1, 1] : [P.guards.side === 'left' ? -1 : 1];
    for (const sgn of sides) {
      const edge = (sgn < 0 ? section.start : section.end) + sgn * .06;
      for (let s = 1; s < L; s += spacing) {
        const F = atS(frames, s); if (!F || (cut && cut(F))) continue;
        ctx.prop(proto, F.x + F.nx * edge, yFn(F, edge), F.z + F.nz * edge, Math.atan2(F.tx, F.tz), 1, { len: spacing });
      }
    }
  }
  if (P.catenary) for (let s = 24; s < L; s += 46) {
    const F = atS(frames, s); if (!F) continue;
    for (const sgn of [-1, 1]) { const off = sgn * (Math.max(Math.abs(section.start), Math.abs(section.end)) + .7); ctx.prop('catenary-mast', F.x + F.nx * off, yFn(F, off), F.z + F.nz * off, sgn < 0 ? Math.atan2(F.tx, F.tz) + Math.PI : Math.atan2(F.tx, F.tz)); }
  }
  if (P.telegraph) for (let s = 34; s < L; s += 68) { const F = atS(frames, s); if (!F) continue; const off = section.start - 1.1; ctx.prop('telegraph', F.x + F.nx * off, yFn(F, off), F.z + F.nz * off, Math.atan2(F.tx, F.tz)); }
  if (P.mileposts) for (let s = 60; s < L; s += 180) { const F = atS(frames, s); if (!F) continue; const off = section.end + .8; ctx.prop('milepost', F.x + F.nx * off, yFn(F, off), F.z + F.nz * off, Math.atan2(F.tx, F.tz)); }
  if (profile.markings?.manholes !== false && detailOf(profile) > 0) {
    for (let s = 26; s < L - 12; s += 51) {
      const F = atS(frames, s); if (!F) continue;
      const off = (hash01(idSeed, Math.round(s)) - .5) * carriagewayHalf(section) * 1.4;
      ctx.prop('manhole', F.x + F.nx * off, yFn(F, off) - .005, F.z + F.nz * off, hash01(idSeed, Math.round(s), 3) * 3);
    }
  }
  if (road.pedestrian) { scatter('person-seated', anySw, -.2, { spacing: 40 }); }
}
const detailOf = p => (p.props && p.props.lights) || p.cls === 'freeway' ? 1 : .6;

/* ------------------------------- structures ------------------------------- */
function buildStructure(road, ctx, st) {
  const { frames, section, profile, L, yFn, opts } = st;
  const kind = profile.structure || road.structure;
  const deck = road.deckY || 0;
  const half = Math.max(Math.abs(section.start), Math.abs(section.end));

  if (kind === 'bridge' || kind === 'viaduct' || road.elevated) {
    const soffit = -Math.max(1.1, Math.abs(deck + (opts.waterDepth ?? 4)) * .12 + 1);
    const gC = G.concrete;
    for (const sgn of [-1, 1]) {
      const edge = sgn < 0 ? -half - .5 : half;
      sweepSection(ctx, frames, edge, [{ x: 0, y: 0 }, { x: .5, y: 0 }, { x: .5, y: soffit }, { x: 0, y: soffit }], gC, {
        col: [.42, .42, .43], uvScale: 2, maxSeg: 8,
        yBase: F => F.y + deck + soffit * -1 + soffit
      });
    }
    // underside slab
    for (let i = 0; i < frames.length - 1; i++) {
      const P = frames[i], Q = frames[i + 1];
      bandQ(ctx.layer(gC), P, Q, -half - .5, half + .5, P.y + deck + soffit, P.y + deck + soffit, Q.y + deck + soffit, Q.y + deck + soffit, 3, [.36, .36, .37]);
    }
    const parapet = [{ x: 0, y: 0 }, { x: 0, y: 1.06 }, { x: .16, y: 1.14 }, { x: .4, y: 1.06 }, { x: .4, y: 0 }];
    if (profile.props?.parapet !== false) for (const sgn of [-1, 1]) {
      sweepSection(ctx, frames, sgn * (half + .42) - (sgn < 0 ? 0 : .0), parapet, G.concrete, {
        col: [.68, .67, .64], uvScale: 1.1, caps: true, maxSeg: 8,
        yBase: F => F.y + deck + levelAt(section, profile, sgn * half) - .04
      });
    }
    const jointEvery = profile.props?.jointSpacing ?? 36;
    for (let s = jointEvery / 2; s < L - 3; s += jointEvery) {
      const A = atS(frames, s - .1), B = atS(frames, s + .1);
      if (!A || !B) continue;
      const yA = yFn(A, 0) + .01, yB = yFn(B, 0) + .01;
      bandQ(ctx.decal.group(G.paint), A, B, -half, half, yA, yA, yB, yB, 1, [.15, .15, .16]);
    }
    const pierSpacing = profile.props?.piers?.spacing ?? 26;
    for (let s = pierSpacing / 2; s < L; s += pierSpacing) {
      const F = atS(frames, s); if (!F) continue;
      for (const sgn of [-1, 1]) {
        const off = sgn * half * .55;
        const px = F.x + F.nx * off, pz = F.z + F.nz * off;
        const gh = opts.groundY ? opts.groundY(px, pz) : -(opts.waterDepth ?? 3.2);
        const top = F.y + deck + soffit;
        if (top - gh > .5) { ctx.prop('pier-pile', px, gh, pz, Math.atan2(F.tx, F.tz), [1.1, top - gh, 1.2]); ctx.prop('bearing-pad', px, top - .2, pz, Math.atan2(F.tx, F.tz)); }
      }
    }
    for (const [s, dir] of [[.5, -1], [L - .5, 1]]) {
      const F = atS(frames, s); if (!F) continue;
      for (const sgn of [-1, 1]) {
        const off = sgn * (half + 1.4);
        const px = F.x + F.nx * off, pz = F.z + F.nz * off;
        const gh = opts.groundY ? opts.groundY(px, pz) : 0;
        const top = F.y + deck + .05;
        ctx.prop('wall-block', px, gh, pz, Math.atan2(F.tx, F.tz) + (sgn < 0 ? Math.PI / 2 : Math.PI / 2), [1.2, Math.max(.5, top - gh), 4.6]);
      }
    }
    if (profile.props?.lights && opts.abutmentLamps !== false) { /* deck lamps handled by props pass */ }
    return;
  }

  if (kind === 'tunnel') {
    const rise = 6.4;
    for (const sgn of [-1, 1]) {
      const wall = [{ x: 0, y: 0 }, { x: 0, y: rise * .58 }, { x: .4, y: rise * .62 }, { x: .4, y: 0 }];
      sweepSection(ctx, frames, sgn < 0 ? -half - .4 : half, wall, G.tile, {
        col: [.8, .8, .77], uvScale: 1.4, maxSeg: 7, underground: true,
        yBase: F => F.y + deck - .15
      });
    }
    const arcs = 14;
    for (let i = 0; i < arcs; i++) {
      const a0 = Math.PI * (i / arcs), a1 = Math.PI * ((i + 1) / arcs);
      for (let f = 0; f < frames.length - 1; f++) {
        const P = frames[f], Q = frames[f + 1];
        const mk = (F, a) => ({ x: F.x + F.nx * (Math.cos(a) * (half + .4)), y: F.y + deck + rise * .58 + Math.sin(a) * rise * .42, z: F.z + F.nz * (Math.cos(a) * (half + .4)) });
        quadRaw(ctx.layer(G.tile, true), mk(P, a0), mk(P, a1), mk(Q, a1), mk(Q, a0), [[0, P.s / 5], [0, Q.s / 5], [1, Q.s / 5], [1, P.s / 5]], [.82, .82, .79]);
      }
    }
    for (const [s, dir] of [[.6, -1], [L - .6, 1]]) {
      const F = atS(frames, s); if (!F) continue;
      for (const sgn of [-1, 1]) ctx.prop('wall-block', F.x + F.nx * sgn * (half + 1.2), -2.2, F.z + F.nz * sgn * (half + 1.2), Math.atan2(F.tx, F.tz), [2.4, rise + 4.2, 3.2]);
      ctx.prop('wall-block', F.x + F.tx * dir * 1.4, rise * .78, F.z + F.tz * dir * 1.4, Math.atan2(F.tx, F.tz), [half * 2.6, 1.8, 2.4]);
    }
    for (let s = 7; s < L - 4; s += 12) {
      const F = atS(frames, s); if (!F) continue;
      ctx.prop('lamp-tunnel', F.x, F.y + deck + rise * .86, F.z, Math.atan2(F.tx, F.tz));
      ctx.lights.push({ x: F.x, y: F.y + deck + rise * .84, z: F.z, i: .5, r: 18, color: [1, .96, .86], night: 0 });
    }
    return;
  }

  if (kind === 'subway') {
    const r = 2.9, spread = 1.62;
    for (const sgn of [-1, 1]) {
      const cxo = sgn * (r * .78 + spread * .4);
      const steps = 22, a0 = -Math.PI * .04, a1 = Math.PI * 1.04;
      for (let i = 0; i < steps; i++) {
        const t0 = a0 + (i / steps) * (a1 - a0), t1 = a0 + ((i + 1) / steps) * (a1 - a0);
        for (let f = 0; f < frames.length - 1; f++) {
          const P = frames[f], Q = frames[f + 1];
          const mk = (F, a) => ({ x: F.x + F.nx * (cxo + Math.cos(a) * r), y: F.y + deck + Math.sin(a) * r, z: F.z + F.nz * (cxo + Math.cos(a) * r) });
          quadRaw(ctx.layer(G.tile, true), mk(P, t0), mk(P, t1), mk(Q, t1), mk(Q, t0), [[0, P.s / 8], [0, Q.s / 8], [.25, Q.s / 8], [.25, P.s / 8]], [.74, .74, .72]);
        }
      }
      const yT = F => F.y + deck - r * .82;
      for (let f = 0; f < frames.length - 1; f++) {
        const P = frames[f], Q = frames[f + 1];
        bandQ(ctx.layer(G.concrete, true), P, Q, cxo - r * .95, cxo + r * .95, yT(P), yT(P), yT(Q), yT(Q), 3, [.34, .34, .36]);
        for (const s2 of [-1, 1]) {
          const ro = cxo + s2 * .7175;
          bandQ(ctx.layer(G.metal, true), P, Q, ro - .035, ro + .035, yT(P) + .1, yT(P) + .1, yT(Q) + .1, yT(Q) + .1, 1, [.62, .63, .65]);
        }
      }
    }
    const stationEvery = profile.props?.stations?.spacing ?? 780;
    const stops = road.stations && road.stations.length
      ? road.stations.map(st => ({ s: st.s, name: st.name }))
      : Array.from({ length: Math.max(1, Math.floor(L / stationEvery)) }, (_, i) => ({ s: (i + .5) * stationEvery }));
    for (const st0 of stops) { const s = st0.s; road.stationName = st0.name;
      const F = atS(frames, s); if (!F) continue;
      buildStation(ctx, F, section, road, opts);
      for (const sgn of [-1, 1]) {
        const off = sgn * 9.5;
        const ex = F.x + F.nx * off, ez = F.z + F.nz * off;
        const gy = opts.groundY ? opts.groundY(ex, ez) : 0;
        ctx.prop('subway-entrance', ex, gy + .02, ez, Math.atan2(F.tx, F.tz) + (sgn < 0 ? Math.PI : 0));
        ctx.prop('escalator', F.x + F.nx * (off * .72), F.y + deck + 4.4, F.z + F.nz * (off * .72), Math.atan2(F.tx, F.tz) + (sgn < 0 ? Math.PI : 0), [1, 2.3, 2.0]);
        ctx.prop('vent-grate', F.x + F.nx * (off + sgn * 5.2), F.y + deck + 16.2, F.z + F.nz * (off + sgn * 5.2), Math.atan2(F.tx, F.tz));
      }
    }
    return;
  }

  if (kind === 'underpass') {
    const depth = 3.5;
    for (const sgn of [-1, 1]) {
      const wall = [{ x: 0, y: 0 }, { x: 0, y: depth }, { x: .45, y: depth }, { x: .45, y: .1 }];
      sweepSection(ctx, frames, sgn < 0 ? -half - .45 : half, wall, G.tile, { col: [.74, .74, .72], uvScale: 1.3, maxSeg: 7, underground: true, yBase: F => F.y + deck });
    }
    for (let f = 0; f < frames.length - 1; f++) {
      const P = frames[f], Q = frames[f + 1];
      bandQ(ctx.layer(G.concrete, true), P, Q, -half - 1.6, half + 1.6, P.y + deck + depth - .2, P.y + deck + depth - .2, Q.y + deck + depth - .2, Q.y + deck + depth - .2, 3, [.5, .5, .51]);
    }
    for (let s = 5; s < L; s += 9) {
      const F = atS(frames, s); if (!F) continue;
      const side = (Math.round(s / 9) % 2) ? 1 : -1;
      const off = side * (half - .35);
      ctx.prop('lamp-tunnel', F.x + F.nx * off, F.y + deck + depth - .5, F.z + F.nz * off, 0);
      ctx.lights.push({ x: F.x + F.nx * off, y: F.y + deck + depth - .6, z: F.z + F.nz * off, i: .3, r: 11, color: [1, .97, .9], night: 0 });
      if (s % 27 < 9) ctx.prop('art-panel', F.x - F.nx * off, F.y + deck + depth * .5, F.z - F.nz * off, Math.atan2(F.tx, F.tz) + (side > 0 ? Math.PI : 0));
    }
    return;
  }

  /* embankment / cutting where the alignment leaves the datum */
  if (Math.abs(deck) > .3 && !road.noEmbankment && kind !== 'bridge') {
    const batter = 1.7;
    for (let f = 0; f < frames.length - 1; f++) {
      const P = frames[f], Q = frames[f + 1];
      for (const sgn of [-1, 1]) {
        const d0 = P.y + deck, d1 = Q.y + deck;
        if (Math.abs(d0) < .35 && Math.abs(d1) < .35) continue;
        const foot = (d) => (half + clamp(Math.abs(d) * batter, .8, 30)) * sgn;
        const g = ctx.layer(G.grass);
        const p1 = corner(P, sgn * half, P.y + deck + levelAt(section, profile, sgn * half));
        const p2 = { x: P.x + P.nx * foot(d0), y: 0, z: P.z + P.nz * foot(d0) };
        const p3 = { x: Q.x + Q.nx * foot(d1), y: 0, z: Q.z + Q.nz * foot(d1) };
        const p4 = corner(Q, sgn * half, Q.y + deck + levelAt(section, profile, sgn * half));
        quadRaw(g, p1, p2, p3, p4, [[0, P.s / 6], [1, P.s / 6], [1, Q.s / 6], [0, Q.s / 6]], [.3, .4, .22]);
        if (road.retaining && Math.abs(d0) > 1.2) {
          const sec = [{ x: 0, y: 0 }, { x: 0, y: Math.min(Math.abs(d0), 5) }, { x: .6, y: Math.min(Math.abs(d0), 5) }, { x: .6, y: 0 }];
          sweepSection(ctx, [P, Q], sgn < 0 ? -half - .6 : half, sec, G.concrete, { col: [.55, .55, .54], uvScale: 1.6, yBase: () => 0, maxSeg: 6 });
        }
      }
    }
  }
}

function buildStation(ctx, F, section, road, opts) {
  const y = F.y + (ctx.deckY || 0), along = { x: F.tx, z: F.tz }, lat = { x: F.nx, z: F.nz };
  const W = 11.5, H = 6.2, Ln = 38;
  const P = (a, b) => ({ x: F.x + lat.x * a + along.x * b, z: F.z + lat.z * a + along.z * b });
  const quad = (p1, p2, p3, p4, mat, col, yy) => quadRaw(ctx.layer(mat, true), { ...p1, y: yy[0] }, { ...p2, y: yy[1] }, { ...p3, y: yy[2] }, { ...p4, y: yy[3] }, [[0, 0], [1, 0], [1, 1], [0, 1]], col);
  quad(P(-W, -Ln / 2), P(W, -Ln / 2), P(W, Ln / 2), P(-W, Ln / 2), G.concrete, [.6, .6, .59], [y + H, y + H, y + H, y + H]);
  for (const sgn of [-1, 1]) quad(P(sgn * W, -Ln / 2), P(sgn * W, Ln / 2), P(sgn * W, Ln / 2), P(sgn * W, -Ln / 2), G.concrete, [.48, .48, .48], [y, y, y + H, y + H]);
  quad(P(-W, -Ln / 2), P(W, -Ln / 2), P(W, -Ln / 2), P(-W, -Ln / 2), G.concrete, [.5, .5, .5], [y, y, y + H, y + H]);
  quad(P(-W, Ln / 2), P(W, Ln / 2), P(W, Ln / 2), P(-W, Ln / 2), G.concrete, [.5, .5, .5], [y, y, y + H, y + H]);
  for (const sgn of [-1, 1]) {
    const inner = sgn * 3.4, outer = sgn * (W - .5);
    quad(P(inner, -Ln / 2 + 1), P(outer, -Ln / 2 + 1), P(outer, Ln / 2 - 1), P(inner, Ln / 2 - 1), G.paver, [.6, .58, .55], [y + .95, y + .95, y + .95, y + .95]);
    quad(P(inner, -Ln / 2 + 1), P(inner, Ln / 2 - 1), P(inner, Ln / 2 - 1), P(inner, -Ln / 2 + 1), G.concrete, [.52, .52, .52], [y, y, y + .95, y + .95]);
    for (let b = -Ln / 2 + 2; b < Ln / 2 - 2; b += 5) {
      const p = P(sgn * (W - 1.8), b);
      ctx.prop('platform-seat', p.x, y + .95, p.z, Math.atan2(along.x, along.z) + (sgn < 0 ? Math.PI : 0));
      ctx.prop('platform-sign', p.x, y + .95, P(sgn * (W - 1.2), b).z, Math.atan2(along.x, along.z));
      ctx.lights.push({ x: p.x, y: y + H - .8, z: p.z, i: .55, r: 17, color: [1, .98, .93], night: 0 });
    }
    const g = ctx.decal.group(G.paint);
    for (let b = -Ln / 2 + 1; b < Ln / 2 - 1; b += 1.15) {
      const a1 = P(inner + sgn * .5, b), a2 = P(inner + sgn * .5, b + .75);
      triRaw(g, { x: a1.x, y: y + .97, z: a1.z }, { x: a2.x, y: y + .97, z: a2.z }, { x: P(inner + sgn * .05, b + .375).x, y: y + .97, z: P(inner + sgn * .05, b + .375).z }, [[0, 0], [1, 0], [.5, 1]], YELLOW);
    }
  }
  ctx.volumes.push({ x: F.x, z: F.z, r: 24, kind: 'station', name: (road.stationName || (road.name ? road.name + ' station' : 'Station')) });
}

/* ------------------------------- level crossing ------------------------------- */
function buildLevelCrossing(road, ctx, st) {
  const { frames, section, profile, yFn, at, c } = st;
  const F = atS(frames, at); if (!F) return;
  const w = carriagewayHalf(section);
  const across = st.width * .5;
  const y = F.y + (ctx.deckY || 0) + levelAt(section, profile, 0) + .016;
  const A = atS(frames, at - across), B = atS(frames, at + across);
  if (A && B) bandQ(ctx.layer(G.rubber), A, B, -w - .6, w + .6, y - .006, y - .006, y - .006, y - .006, 2, [.11, .11, .12]);
  const g = ctx.decal.group(G.paint);
  for (const sgn of [-1, 1]) {
    const sl = atS(frames, clamp(at + sgn * (across + 1.8), 0, 1e9)), sl2 = atS(frames, clamp(at + sgn * (across + 2.5), 0, 1e9));
    if (sl && sl2) for (const e of section.elements) if (e.k === 'lane' && (e.dirNum || 1) * -sgn > 0) bandQ(g, sl, sl2, e.x0, e.x1, y, y, y, y, 1, WHITE);
    for (const lat of [sgn * (w + 1.2), -sgn * (w + 1.2)]) {
      const px = F.x + F.nx * lat + F.tx * sgn * (across + .6), pz = F.z + F.nz * lat + F.tz * sgn * (across + .6);
      ctx.prop('crossing-light', px, yFn(F, lat), pz, Math.atan2(F.tx, F.tz) + (sgn > 0 ? 0 : Math.PI), 1, { blink: at % 17 });
      ctx.prop('crossbuck', px - F.nx * sgn * .4, yFn(F, lat), pz - F.nz * sgn * .4, Math.atan2(F.tx, F.tz));
      ctx.prop('gate-post', px, yFn(F, lat) - .01, pz, Math.atan2(F.tx, F.tz) + (sgn > 0 ? 0 : Math.PI));
      ctx.animated.push({ kind: 'gate', proto: 'gate-barrier', pivot: { x: px, y: yFn(F, lat) + 1.08, z: pz }, base: Math.atan2(F.tx, F.tz) + (sgn > 0 ? 0 : Math.PI), phase: (at % 23) / 23 });
    }
    const px = F.x + F.nx * sgn * (w + 2.4), pz = F.z + F.nz * sgn * (w + 2.4);
    ctx.prop('tactile-paving', px, y - .004, pz, Math.atan2(F.tx, F.tz), [3.6, 1, 1.7]);
  }
  glyphQuad(ctx, 'glyph:train', frames, at - sgnOf(c) * (across + 6.5), 0, w * 1.4, 3.2, 0, section, profile, null);
}
const sgnOf = c => (c && c.dir) || 1;

/* ------------------------------- junctions ------------------------------- */
/**
 * node = {x, z, y, style:'signals'|'give-way'|'roundabout'|'uncontrolled', radius,
 *         arms:[{road, x, z, dir:{x,z}}]}
 * Fills the plate, kerbs the corners, drops kerbs + tactile at every
 * crossing, and paints stop lines / give-way teeth per arm.
 */
export function buildJunction(node, ctx, opts = {}) {
  const arms = (node.arms || []).filter(a => a.road && a.road._section && a.road._profile);
  if (arms.length < 2) return null;
  for (const a of arms) {
    const s = a.road._section;
    a.ang = Math.atan2(-a.dir.z, -a.dir.x);
    const trav = s.elements.filter(e => CARRIAGEWAY.includes(e.k));
    a.halfCar = trav.length ? Math.max(...trav.flatMap(e => [Math.abs(e.x0), Math.abs(e.x1)])) : Math.abs(s.start);
    a.halfKerb = Math.max(Math.abs(s.start), Math.abs(s.end));
    const p = { x: -a.dir.z, z: a.dir.x };
    const back = opts.setback ?? 1.2;
    const bx = a.x + a.dir.x * back, bz = a.z + a.dir.z * back;
    a.c = {
      kerbL: { x: bx + p.x * a.halfKerb, z: bz + p.z * a.halfKerb },
      carL: { x: bx + p.x * a.halfCar, z: bz + p.z * a.halfCar },
      carR: { x: bx - p.x * a.halfCar, z: bz - p.z * a.halfCar },
      kerbR: { x: bx - p.x * a.halfKerb, z: bz - p.z * a.halfKerb },
      back, p
    };
  }
  arms.sort((a, b) => a.ang - b.ang);
  const yPlate = node.y ?? 0;
  const plate = [], fillets = [];
  for (let i = 0; i < arms.length; i++) {
    const a = arms[i];
    plate.push(a.c.carR, a.c.carL);
    const nx = arms[(i + 1) % arms.length];
    const from = a.c.carL, to = nx.c.carR;
    const steps = clamp(Math.round(dist(from, to) / 2.2), 3, 14);
    const curve = [];
    for (let k = 1; k < steps; k++) {
      const t = k / steps, it = 1 - t;
      curve.push({ x: it * it * from.x + 2 * it * t * node.x + t * t * to.x, z: it * it * from.z + 2 * it * t * node.z + t * t * to.z });
    }
    plate.push(...curve);
    fillets.push({ from, to, curve, arm: a, next: nx });
  }
  pavePolygon(ctx, G.asphalt, plate, yPlate - .004, [.33, .34, .36], { uvScale: 5 });
  for (const f of fillets) {
    const pts = [f.from, ...f.curve, f.to].map((p, i) => ({ x: p.x, z: p.z, y: yPlate, s: i * 2 }));
    const frames = reframe(pts);
    sweepSection(ctx, frames, -.18, KERB_SECTION(.15, .05, .18), G.granite, { col: [.71, .7, .67], uvScale: .45, maxSeg: 4, caps: true, yBase: F => yPlate - .02 });
  }
  for (const a of arms) {
    const road = a.road, sec = road._section, prof = road._profile;
    const end = a.end === 's' ? 0 : framesOf(road, opts).length ? framesOf(road, opts)[framesOf(road, opts).length - 1].s : 0;
    const frames = framesOf(road, opts);
    const cw = prof.markings?.crosswalk;
    if (!cw || cw === 'none') continue;
    const back = a.back ?? a.c.back;
    for (const dir of [1]) {
      const at = a.end === 's' ? back + 1.2 : frames[frames.length - 1].s - (back + 1.2);
      const inward = a.end === 's' ? -1 : 1;
      const style = cw;
      const F = atS(frames, at); if (!F) continue;
      const across = a.halfCar * 2 + 1.8;
      const y = F.y + (road.deckY || 0) + levelAt(sec, prof, 0) + .018;
      const len = style === 'ladder' ? 4.2 : 3.0;
      const A = atS(frames, clamp(at - (inward > 0 ? 0 : len), 0, 1e9)), B = atS(frames, clamp(at + (inward > 0 ? len : 0), 0, 1e9));
      if (!A || !B) continue;
      const g = ctx.decal.group(G.paint);
      const bars = style === 'continental' ? 8 : style === 'ladder' ? 5 : 7, barW = style === 'zebra' ? .5 : .66;
      for (let i = 0; i < bars; i++) {
        const c0 = -across / 2 + (i + .5) * (across / bars);
        const a0 = c0 - (across / bars - barW) / 2, a1 = c0 + (across / bars - barW) / 2;
        bandQ(g, A, B, a0, a1, y, y, y, y, 1, WHITE);
      }
      if (style === 'ladder') for (const off of [-across / 2, across / 2]) bandQ(g, A, B, off - .13, off + .13, y, y, y, y, 1, WHITE);
      const sLine = at + (inward > 0 ? -len - 1.1 : len + 1.1);
      const SA = atS(frames, clamp(sLine - .35, 0, 1e9)), SB = atS(frames, clamp(sLine + .35, 0, 1e9));
      if (SA && SB) for (const e of sec.elements) if (e.k === 'lane' && (e.dirNum || 1) * (inward > 0 ? -1 : 1) < 0) bandQ(g, SA, SB, e.x0, e.x1, y, y, y, y, 1, WHITE);
      for (const sgn of [-1, 1]) ctx.prop('tactile-paving', F.x + F.nx * sgn * (across / 2 + .8), y - .006, F.z + F.nz * sgn * (across / 2 + .8), Math.atan2(F.tx, F.tz), [2.4, 1, 1.8]);
    }
    void end;
  }
  const style = node.style || 'uncontrolled';
  if (style === 'signals') {
    for (const a of arms) {
      const base = { x: a.x + a.dir.x * (a.c.back + 5.4), z: a.z + a.dir.z * (a.c.back + 5.4) };
      for (const sgn of [-1, 1]) {
        const o = (a.halfCar + .95) * sgn;
        ctx.prop('signal-head', base.x + a.c.p.x * o, yPlate + .02, base.z + a.c.p.z * o, Math.atan2(-a.dir.x, -a.dir.z), 1, { junction: node.id });
        ctx.prop('signal-ped', a.x + a.dir.x * (a.c.back + 1.6) + a.c.p.x * o * 1.15, yPlate + .02, a.z + a.dir.z * (a.c.back + 1.6) + a.c.p.z * o * 1.15, Math.atan2(-a.dir.x, -a.dir.z), 1, { junction: node.id });
      }
    }
  }
  if (style === 'give-way') {
    for (const a of (node.giveArms || arms)) {
      const g = ctx.decal.group(G.paint);
      const F = atS(framesOf(a.road, opts), clamp((a.end === 's' ? a.c.back + 3.4 : framesOf(a.road, opts)[framesOf(a.road, opts).length - 1].s - a.c.back - 3.4), 0, 1e9));
      if (F) bandQ(g, F, F, -a.halfCar, a.halfCar, F.y + levelAt(a.road._section, a.road._profile, 0) + .018, 0, 0, 0, 1, WHITE);
    }
  }
  if (style === 'roundabout') {
    const r = node.radius ?? 11, seg = 30, island = [];
    for (let i = 0; i < seg; i++) { const a = i / seg * TAU; island.push({ x: node.x + Math.cos(a) * r, z: node.z + Math.sin(a) * r }); }
    pavePolygon(ctx, G.granite, island, yPlate + .17, [.72, .71, .68], {});
    pavePolygon(ctx, G.paver, island.map(p => ({ x: node.x + (p.x - node.x) * .8, z: node.z + (p.z - node.z) * .8 })), yPlate + .2, [.56, .56, .57], { uvScale: 2.2 });
    const gc = ctx.decal.group(G.paint);
    for (let i = 0; i < 16; i++) {
      const a = i / 16 * TAU;
      const p0 = { x: node.x + Math.cos(a) * (r - 1.5), y: yPlate + .21, z: node.z + Math.sin(a) * (r - 1.5) };
      const p1 = { x: node.x + Math.cos(a) * (r - .45), y: yPlate + .21, z: node.z + Math.sin(a) * (r - .45) };
      const p2 = { x: node.x + Math.cos(a + .11) * (r - 1.5), y: yPlate + .21, z: node.z + Math.sin(a + .11) * (r - 1.5) };
      triRaw(gc, p0, p1, p2, [[0, 0], [1, 0], [1, 1]], WHITE);
    }
    ctx.prop('fountain', node.x, yPlate + .2, node.z, 0, .7 + r / 26);
    if (node.radius > 16) for (let i = 0; i < 8; i++) { const a = i / 8 * TAU; ctx.prop('shrub', node.x + Math.cos(a) * r * .55, yPlate + .2, node.z + Math.sin(a) * r * .55, a); }
  }
  return { plate, fillets };
}

/* ------------------------------- areas, water, terrain ------------------------------- */
export function pavePolygon(ctx, matKey, pts, y, col, opts = {}) {
  const tris = triangulate(pts);
  const g = ctx.layer(matKey, opts.underground);
  const scale = opts.uvScale ?? 5;
  for (let i = 0; i < tris.length; i += 3) {
    const A = pts[tris[i]], B = pts[tris[i + 1]], C = pts[tris[i + 2]];
    triRaw(g, { x: A.x, y, z: A.z }, { x: B.x, y, z: B.z }, { x: C.x, y, z: C.z },
      [[A.x / scale, A.z / scale], [B.x / scale, B.z / scale], [C.x / scale, C.z / scale]], col || [1, 1, 1]);
  }
  return tris.length / 3;
}
function triangulate(pts) {
  const n = pts.length;
  if (n < 3) return [];
  let area = 0;
  for (let i = 0; i < n; i++) { const a = pts[i], b = pts[(i + 1) % n]; area += a.x * b.z - b.x * a.z; }
  const ccw = area > 0;
  const A = (a, b, c) => (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
  const inTri = (p, a, b, c) => { const d1 = A(a, b, p), d2 = A(b, c, p), d3 = A(c, a, p); return !((d1 < 0 || d2 < 0 || d3 < 0) && (d1 > 0 || d2 > 0 || d3 > 0)); };
  const list = pts.map((_, i) => i), out = [];
  let guard = 0;
  while (list.length > 2 && guard++ < n * n + 200) {
    let clipped = false;
    for (let i = 0; i < list.length; i++) {
      const i0 = list[(i - 1 + list.length) % list.length], i1 = list[i], i2 = list[(i + 1) % list.length];
      const a = pts[i0], b = pts[i1], c = pts[i2];
      if ((ccw ? 1 : -1) * A(a, b, c) <= 1e-3) continue;
      let ok = true;
      for (const j of list) { if (j === i0 || j === i1 || j === i2) continue; if (inTri(pts[j], a, b, c)) { ok = false; break; } }
      if (!ok) continue;
      out.push(i0, i1, i2); list.splice(i, 1); clipped = true; break;
    }
    if (!clipped) { for (let i = 1; i < list.length - 1; i++) out.push(list[0], list[i], list[i + 1]); break; }
  }
  if (out.length < 3) for (let i = 1; i < n - 1; i++) out.push(0, i, i + 1);
  return out;
}
export { triangulate };

export function buildWater(body, ctx, opts = {}) {
  const pts = dedupe(body.points.map(p => ({ x: p.x, z: p.z, y: 0 })), 1);
  const frames = reframe(resamplePass(chaikin(pts, 2, !!body.closed), body.step ?? 7));
  if (frames.length < 2) return null;
  const w = body.width ?? 34, y = body.y ?? -.55;
  const L = frames[frames.length - 1].s;
  for (let i = 0; i < frames.length - 1; i++) {
    const A = frames[i], B = frames[i + 1];
    bandQ(ctx.layer(G.water), A, B, -w / 2, w / 2, y, y, y, y, 9, [.42, .62, .72]);
  }
  const depth = body.wallDepth ?? 4.6;
  for (const sgn of [-1, 1]) {
    const top = y + depth;
    const wall = [{ x: 0, y: 0 }, { x: .7, y: depth }, { x: 1.9, y: depth }, { x: 1.9, y: depth + .22 }];
    sweepSection(ctx, frames, sgn * (w / 2), sgn < 0 ? wall.map(p => ({ x: -p.x + 1.9, y: p.y })) : wall.map(p => ({ x: p.x, y: p.y })), G.granite, {
      col: [.48, .47, .45], uvScale: 1.3, maxSeg: 9, yBase: F => top - depth + (body.bankTilt ? 0 : 0)
    });
    for (let i = 0; i < frames.length - 1; i++) {
      const A = frames[i], B = frames[i + 1];
      bandQ(ctx.layer(G.paver), A, B, sgn * (w / 2 + 1.9), sgn * (w / 2 + 6.2), top + .24, top + .24, top + .24, top + .24, 2.2, [.55, .53, .49]);
    }
    if (body.lamps !== false) for (let s = 14; s < L; s += 30) {
      const F = atS(frames, s); if (!F) continue;
      for (const sg of [-1, 1]) {
        const off = sg * (w / 2 + 4);
        ctx.prop('lamp-modern', F.x + F.nx * off, top + .26, F.z + F.nz * off, Math.atan2(F.tx, F.tz) + (sg < 0 ? Math.PI : 0));
        ctx.prop('chain-barrier', F.x + F.nx * (sg * (w / 2 + 2.05)), top + .26, F.z + F.nz * (sg * (w / 2 + 2.05)), Math.atan2(F.tx, F.tz), [.9, 1, 1]);
      }
    }
    if (body.trees !== false) for (let s = 22; s < L; s += 34) {
      const F = atS(frames, s); if (!F) continue;
      for (const sg of [-1, 1]) { const off = sg * (w / 2 + 8.4); ctx.prop('tree-plane', F.x + F.nx * off, top + .26, F.z + F.nz * off, 0, .85 + hash01(strSeed(body.id || 'w'), s) * .4); }
    }
  }
  return { frames, length: L };
}

export function buildArea(area, ctx, opts = {}) {
  const pts = area.polygon;
  if (!pts || pts.length < 3) return;
  const y = area.y ?? 0;
  pavePolygon(ctx, area.mat || G.paver, pts, y, area.col || [.6, .57, .52], { uvScale: area.uv ?? 3.2 });
  if (area.edge) pavePolygon(ctx, G.granite, pts.map(p => ({ x: p.x, z: p.z })), y + .02, [.68, .67, .64], { uvScale: 2 });
  if (area.scatter) {
    const rng = new RNG(strSeed(area.id || 'area'));
    const bb = boundsOf(pts);
    let placed = 0, tries = area.scatter.count * 6;
    while (placed < area.scatter.count && tries-- > 0) {
      const p = { x: rng.range(bb.minx, bb.maxx), z: rng.range(bb.minz, bb.maxz) };
      if (!pointInPoly(p, pts)) continue;
      placed++;
      ctx.prop(area.scatter.proto, p.x, y + .01, p.z, rng.range(0, TAU), area.scatter.scale ? rng.range(...area.scatter.scale) : 1);
    }
  }
  if (area.perimeterProp) {
    const spacing = area.perimeterSpacing ?? 9;
    for (let i = 0; i < pts.length; i++) {
      const a = pts[i], b = pts[(i + 1) % pts.length];
      const l = Math.hypot(b.x - a.x, b.z - a.z);
      for (let s = spacing / 2; s < l; s += spacing) {
        const t = s / l;
        ctx.prop(area.perimeterProp, lerp(a.x, b.x, t), y + .01, lerp(a.z, b.z, t), Math.atan2(b.x - a.x, b.z - a.z), 1);
      }
    }
  }
}
const boundsOf = pts => { let minx = 1e9, maxx = -1e9, minz = 1e9, maxz = -1e9; for (const p of pts) { minx = Math.min(minx, p.x); maxx = Math.max(maxx, p.x); minz = Math.min(minz, p.z); maxz = Math.max(maxz, p.z); } return { minx, maxx, minz, maxz }; };
export function pointInPoly(p, poly) {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const a = poly[i], b = poly[j];
    if ((a.z > p.z) !== (b.z > p.z) && p.x < (b.x - a.x) * (p.z - a.z) / ((b.z - a.z) || 1e-9) + a.x) inside = !inside;
  }
  return inside;
}

/* ------------------------------- full build ------------------------------- */
export function buildAll(network, ctxOpts = {}) {
  const t0 = Date.now();
  const ctx = new BuildCtx(ctxOpts);
  let laneCount = 0;
  for (const road of network.roads) { prepareRoad(road); buildRoad(road, ctx, ctxOpts); laneCount += (road._section?.elements || []).filter(e => ['lane', 'bus', 'cycle', 'tram'].includes(e.k)).length; }
  for (const node of (network.nodes || [])) buildJunction(node, ctx, ctxOpts);
  for (const w of (network.water || [])) buildWater(w, ctx, ctxOpts);
  for (const a of (network.areas || [])) buildArea(a, ctx, ctxOpts);
  const stats = { tris: 0, verts: 0, buckets: 0, ms: Date.now() - t0, props: 0, instances: 0, roads: ctx.roadCount, lanes: 0 };
  for (const set of [ctx.b, ctx.decal, ctx.underground]) for (const g of set.groups.values()) { stats.tris += g.idx.length / 3; stats.verts += g.n; stats.buckets++; }
  for (const [, list] of ctx.props) { stats.props++; stats.instances += list.length; }
  stats.laneMeters = ctx.laneMeters;
  stats.lanes = laneCount;
  stats.animated = ctx.animated.length;
  stats.lights = ctx.lights.length;
  stats.undergroundTris = ctx.underground.groups.size ? [...ctx.underground.groups.values()].reduce((s, g) => s + g.idx.length / 3, 0) : 0;
  return { ctx, stats };
}

/** sanity sweep used by the headless tests and the editor's self-check */
export function validateNetwork(network, opts = {}) {
  const problems = [];
  for (const r of network.roads) {
    prepareRoad(r);
    const f = framesOf(r, opts);
    if (f.length < 2) problems.push(`${r.id}: alignment too short`);
    for (const s of f) if (!Number.isFinite(s.x) || !Number.isFinite(s.z) || !Number.isFinite(s.y)) problems.push(`${r.id}: non-finite frame`);
    const L = f.length ? f[f.length - 1].s : 0;
    for (const b of (r.bumps || [])) if (b.s < -1 || b.s > L + 1) problems.push(`${r.id}: bump at ${b.s.toFixed(1)} m is outside the ${L.toFixed(1)} m alignment`);
    for (const k of ['crossingsManual', 'driveways', 'crosstables']) for (const o of (r[k] || [])) if (o.s < -1 || o.s > L + 1) problems.push(`${r.id}: ${k} station outside alignment`);
  }
  const { ctx, stats } = buildAll(network, opts);
  for (const [k, g] of ctx.b.groups) {
    let bad = 0;
    for (let i = 0; i < g.pos.length; i++) if (!Number.isFinite(g.pos[i])) bad++;
    if (bad) problems.push(`bucket ${k}: ${bad} non-finite vertex components`);
    for (let i = 0; i < g.idx.length; i++) if (g.idx[i] < 0 || g.idx[i] >= g.n) problems.push(`bucket ${k}: bad index`);
  }
  return { problems, stats };
}

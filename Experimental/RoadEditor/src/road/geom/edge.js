// Road segment mesh builder: per-lane ribbons, curbs, medians with rounded noses, bridge decks,
// piers/retaining walls, paint, decals, traffic control and street furniture.
import { Out, ribbon, wall, sweep, box, polyWall, strip, yawFor } from './out.js';
import { line, dashed, decal, bar, tooth, hatch } from './markings.js';
import { getProfile, layout, LEVEL, RANK } from '../profiles.js';
import { endControl } from './control.js';
import { buildMerge } from './merge.js';

export const CURB_W = 0.2;
export const GROUND = -0.06;
const RAISED = LEVEL.raised;

function hash(str, k = 0) {
  let h = 2166136261 ^ k;
  for (let i = 0; i < str.length; i++) { h ^= str.charCodeAt(i); h = Math.imul(h, 16777619); }
  return ((h >>> 0) % 100000) / 100000;
}

export function curbShapeLeftRoad(h, nb) { // road on the left (−o) of the raised lane
  return [[CURB_W, h], [0.03, h], [0, h - 0.03], [0, nb]];
}
export function curbShapeRightRoad(h, nb) {
  return [[0, nb], [0, h - 0.03], [-0.03, h], [-CURB_W, h]];
}

export function buildEdge(net, edge, JA, JB, ctx) {
  const out = new Out(edge.id);
  const prof = getProfile(edge), lay = layout(prof);
  const c = net.centerline(edge), L = c.length;
  const eA = JA?.endFor?.(edge.id, true), eB = JB?.endFor?.(edge.id, false);
  const s0 = eA ? eA.setback : 0, s1 = L - (eB ? eB.setback : 0);
  out.range = [s0, s1];
  if (s1 - s0 < 0.4) { out.warning = 'Segment too short for its junctions'; return out; }
  // Setbacks are per side (see junction.js). A lane centred at offset oc is cut back by the setback
  // of the corner on its own side: at an acute merge only the acute side gives up its length.
  const sideRange = oc => {
    if (Math.abs(oc) < 0.02) return [s0, s1];
    const pos = oc > 0;
    const a = eA ? (pos ? eA.sbR : eA.sbL) : 0;
    const b = eB ? (pos ? eB.sbL : eB.sbR) : 0;
    return [eA ? Math.max(a, 0) : 0, L - (eB ? Math.max(b, 0) : 0)];
  };
  const frameCache = new Map();
  const framesFor = (a, b) => {
    if (Math.abs(a - s0) < 1e-6 && Math.abs(b - s1) < 1e-6) return frames;
    const key = a.toFixed(2) + ':' + b.toFixed(2);
    if (!frameCache.has(key)) frameCache.set(key, b - a < 0.3 ? [] : c.stations(a, b, extra).map(s => c.frameAt(s)));
    return frameCache.get(key);
  };
  const lanes = lay.lanes;
  const isPathProfile = prof.category === 'active';

  // --- tip taper ("pinch"): the whole cross-section folds onto its inboard edge line so a ramp lane
  // physically dies out instead of ending as a sliced slab. edge.tipTaper = {end, len, inb:'l'|'r'}.
  let pinch = null;
  if (edge.tipTaper) {
    const list = Array.isArray(edge.tipTaper) ? edge.tipTaper : [edge.tipTaper];
    const TT = list[0];
    const inb = TT.inb === 'r' ? lay.right : lay.left;
    const sign = TT.inb === 'r' ? -1 : 1;
    const sm = t => t * t * (3 - 2 * t);
    // one fold line for the whole edge (both ends of a ramp pinch towards the anchor they run
    // alongside, which is the same side of the ramp), one factor per end, multiplied together
    const spec = list.map(t => ({ sTip: t.end === 'a' ? s0 : s1, dn: t.end === 'a' ? 1 : -1, len: Math.max(4, t.len || 60) }));
    const m = f => { let v = 1; for (const sp of spec) { const d = (f.s - sp.sTip) * sp.dn; if (d < sp.len) v = Math.min(v, d <= 0 ? 0 : sm(d / sp.len)); } return v; };
    const PO = (o, f) => ((o - inb) * sign > 0 ? inb + (o - inb) * m(f) : o);
    const first = spec[0];
    pinch = { PO, m, sTip: first.sTip, dn: first.dn, len: first.len };
  }
  // ribbon / wall wrappers that honour the pinch without touching any other call site's behaviour
  const rib = (o, mat, frames, a, b, h, h1) => pinch
    ? ribbon(o, mat, frames, (k, f) => pinch.PO(a, f), (k, f) => pinch.PO(b, f), h, h1)
    : ribbon(o, mat, frames, a, b, h, h1);
  const wl = (o, mat, frames, a, hb, ht, side) => pinch
    ? wall(o, mat, frames, f => pinch.PO(a, f), hb, ht, side)
    : wall(o, mat, frames, a, hb, ht, side);
  // paint stops where the taper starts, so no lane line floats outside the pavement
  let clip = null;
  if (pinch) {
    const rs = (Array.isArray(edge.tipTaper) ? edge.tipTaper : [edge.tipTaper]).map(t => {
      const a = t.end === 'a' ? s0 : s1, b = a + (t.end === 'a' ? 1 : -1) * ((t.len || 60) + 10);
      return [Math.min(a, b), Math.max(a, b)];
    });
    clip = rs.length === 1 ? rs[0] : (rs[0][1] < rs[1][0] ? [rs[0][1], rs[1][0]] : [Math.min(...rs.map(r => r[0])), Math.max(...rs.map(r => r[1]))]);
  }

  // --- control / crosswalk context at each end
  const ctlA = JA && eA ? endControl(JA, eA, net) : null;
  const ctlB = JB && eB ? endControl(JB, eB, net) : null;
  const CW = 3.2;
  const cwRoom = ctl => (ctl?.crosswalk ? CW + 2.2 : ctl && ctl.kind !== 'continuation' ? 1.4 : 0);
  const pullA = (JA && JA.kind !== 'continuation' && JA.kind !== 'flush') ? cwRoom(ctlA) + (JA.kind === 'roundabout' ? 3 : 0) : 0;
  const pullB = (JB && JB.kind !== 'continuation' && JB.kind !== 'flush') ? cwRoom(ctlB) + (JB.kind === 'roundabout' ? 3 : 0) : 0;

  // --- elevation / water analysis on fine samples
  const elevAt = f => f.y > 0.35 || (ctx.waterAt && ctx.waterAt(f.x, f.z));
  const extra = [];
  const fineStep = 4;
  let prevE = null;
  for (let s = s0; s <= s1; s += fineStep) {
    const f = c.frameAt(s);
    const e = elevAt(f);
    if (prevE !== null && e !== prevE) extra.push(s - fineStep / 2);
    prevE = e;
  }
  const medianLanes = lanes.filter((l, i) => l.level === 'raised' && i > lay.roadFrom && i < lay.roadTo);
  for (const m of medianLanes) { extra.push(s0 + pullA, s0 + pullA + m.width / 2, s1 - pullB, s1 - pullB - m.width / 2); }
  for (const ft of edge.features || []) {
    if (ft.type === 'hump') { const half = ft.kind === 'table' ? 3.6 : 2.0; extra.push(ft.s - half, ft.s + half); }
  }
  const stations = c.stations(s0, s1, extra);
  const frames = stations.map(s => c.frameAt(s));
  const elevated = frames.map(f => elevAt(f));
  const anyElevated = elevated.some(Boolean);

  // --- lane surfaces
  const surfaceMat = prof.surface || 'asphalt';
  lanes.forEach((ln, i) => {
    const left = lanes[i - 1], right = lanes[i + 1];
    const isMedian = ln.level === 'raised' && i > lay.roadFrom && i < lay.roadTo;
    const [la, lb] = sideRange((ln.o0 + ln.o1) / 2);
    const lframes = isMedian ? frames : framesFor(la, lb);
    if (!lframes.length) return;
    if (ln.level === 'raised') {
      const nbL = left ? left.h : GROUND, nbR = right ? right.h : GROUND;
      const curbL = left && left.level !== 'raised', curbR = right && right.level !== 'raised';
      const a = ln.o0 + (curbL ? CURB_W : 0), b = ln.o1 - (curbR ? CURB_W : 0);
      let fr = lframes;
      if (isMedian && (pullA > 0 || pullB > 0)) {
        const r = ln.width / 2;
        const m0 = s0 + pullA + (pullA > 0 ? r : 0), m1 = s1 - pullB - (pullB > 0 ? r : 0);
        if (m1 - m0 < 1) { rib(out, surfaceMat, frames, ln.o0, ln.o1, 0); return; }
        fr = frames.filter(f => f.s >= m0 - 1e-6 && f.s <= m1 + 1e-6);
        if (!fr.length || fr[0].s > m0 + 1e-3) fr.unshift(c.frameAt(m0));
        if (fr[fr.length - 1].s < m1 - 1e-3) fr.push(c.frameAt(m1));
        // asphalt under pulled-back sections
        const pre = frames.filter(f => f.s <= m0 + 1e-6); if (pre.length && pre[pre.length - 1].s < m0) pre.push(c.frameAt(m0));
        const post = frames.filter(f => f.s >= m1 - 1e-6); if (post.length && post[0].s > m1) post.unshift(c.frameAt(m1));
        if (pullA > 0 && pre.length >= 2) rib(out, surfaceMat, pre, ln.o0, ln.o1, 0);
        if (pullB > 0 && post.length >= 2) rib(out, surfaceMat, post, ln.o0, ln.o1, 0);
        if (pullA > 0) medianNose(out, c, m0, ln, -1, ln.mat);
        if (pullB > 0) medianNose(out, c, m1, ln, +1, ln.mat);
      }
      rib(out, ln.mat, fr, a, b, ln.h);
      if (curbL) { rib(out, 'curb', fr, ln.o0 + 0.03, a, ln.h); sweep(out, 'curb', fr, curbShapeLeftRoad(ln.h, nbL).slice(1), ln.o0); }
      if (curbR) { rib(out, 'curb', fr, b, ln.o1 - 0.03, ln.h); sweep(out, 'curb', fr, curbShapeRightRoad(ln.h, nbR).slice(0, 3), ln.o1); }
      if (!left && !anyElevated) wl(out, 'curb', fr, ln.o0, GROUND, ln.h, -1);
      if (!right && !anyElevated) wl(out, 'curb', fr, ln.o1, GROUND, ln.h, +1);
    } else {
      rib(out, ln.mat === 'surface' ? surfaceMat : ln.mat, lframes, ln.o0, ln.o1, ln.h);
      // tiny step between ground-level verges and the paved surface
      if (left && left.level === 'ground' && ln.level === 'road') wl(out, surfaceMat, lframes, ln.o0, left.h, ln.h, -1);
      if (right && right.level === 'ground' && ln.level === 'road') wl(out, surfaceMat, lframes, ln.o1, right.h, ln.h, +1);
      if (!left && !anyElevated) wl(out, 'curb', lframes, ln.o0, GROUND, ln.h, -1);
      if (!right && !anyElevated) wl(out, 'curb', lframes, ln.o1, GROUND, ln.h, +1);
      if (ln.type === 'barrier') barrierRuns(out, lframes, ln, edge, i);
      if (ln.type === 'tram') tramRails(out, lframes, (ln.o0 + ln.o1) / 2, ln.h);
    }
  });

  // --- bridges / elevated sections
  if (anyElevated) bridge(out, net, edge, c, frames, elevated, lay, prof, ctx, pinch);

  // --- end caps for dead ends
  if (JA?.kind === 'flat') endCap(out, c, s0, -1, lay);
  if (JB?.kind === 'flat') endCap(out, c, s1, +1, lay);

  // --- paint
  if (ctx.markings !== false && !isPathProfile) {
    const pa = clip ? Math.max(s0, clip[1]) : s0, pb = clip ? Math.min(s1, clip[0]) : s1;
    if (pb - pa > 4) paint(out, net, edge, c, lay, prof, pa, pb, ctlA, ctlB, JA, JB, ctx, sideRange);
  }
  if (isPathProfile && prof.lanes.length > 1 && ctx.markings !== false) {
    for (let i = 0; i < lanes.length - 1; i++) if (lanes[i].type === 'bike' && lanes[i + 1].type === 'bike') dashed(out, 'markWhite', c, s0, s1, lanes[i].o1, 0.1, 1.5, 2.5, 0);
    for (let i = 0; i < lanes.length - 1; i++) if ((lanes[i].type === 'path') !== (lanes[i + 1].type === 'path')) line(out, 'markWhite', c, s0, s1, lanes[i].o1, 0.08, 0);
  }

  // --- interchange ramp blend: gore, nose paint and terminals where this edge meets its mainline
  if (edge.merge) buildMerge(out, net, edge, ctx);

  // --- features
  for (const ft of edge.features || []) {
    if (ft.s < s0 + 2 || ft.s > s1 - 2) continue;
    if (ft.type === 'hump') speedHump(out, c, ft, lay, surfaceMat);
    if (ft.type === 'busstop') busStop(out, c, ft, lay);
    if (ft.type === 'crosswalk') midblockCrossing(out, c, ft, lay);
  }

  // --- traffic control at ends
  if (ctx.props !== false) {
    if (ctlA) controlProps(out, c, lay, s0, true, ctlA);
    if (ctlB) controlProps(out, c, lay, s1, false, ctlB);
    furniture(out, net, edge, c, lay, prof, s0, s1, elevated.some(Boolean) ? frames : null, ctx);
  }
  return out;
}

function medianNose(out, c, s, ln, dir, mat) {
  // Semicircular raised nose with curb ring; dir = -1 nose faces −s (start), +1 faces +s.
  const r = ln.width / 2, oc = (ln.o0 + ln.o1) / 2;
  const f = c.frameAt(s);
  const n = 10;
  const ring = [], inner = [];
  for (let k = 0; k <= n; k++) {
    const th = Math.PI * k / n;              // 0 → right side ... π → left side
    const lat = Math.cos(th), along = Math.sin(th) * dir;
    const px = (rr) => [f.x + f.nx * (oc + lat * rr) + f.tx * along * rr, f.y + RAISED, f.z + f.nz * (oc + lat * rr) + f.tz * along * rr];
    ring.push(px(r)); inner.push(px(Math.max(0.05, r - CURB_W)));
  }
  // order ring so the outward side is to the left of travel (CCW seen from above)
  const ccw = dir > 0 ? ring.slice().reverse() : ring;
  const ccwIn = dir > 0 ? inner.slice().reverse() : inner;
  strip(out, 'curb', ccwIn, ccw);
  polyWall(out, 'curb', ccw.map(p => [p[0], f.y, p[2]]), 0, RAISED, -1);
  const pc = out.piece(mat), base = pc.p.length / 3;
  out.vert(pc, f.x + f.nx * oc, f.y + RAISED, f.z + f.nz * oc, 0, 0);
  for (const p of ccwIn) out.vert(pc, p[0], p[1], p[2], p[0], p[2]);
  for (let k = 0; k < n; k++) pc.i.push(base, base + 2 + k, base + 1 + k);
  const tip = c.frameAt(s);
  out.prop('bollardSign', tip.x + tip.nx * oc + tip.tx * dir * (r * 0.5), tip.y + RAISED, tip.z + tip.nz * oc + tip.tz * dir * (r * 0.5), yawFor(tip.tx * dir, tip.tz * dir));
}

function barrierRuns(out, frames, ln, edge, laneIndex) {
  const gaps = (edge.features || []).filter(f => f.type === 'barrierGap' && f.lane === laneIndex);
  const runs = [];
  let cur = [];
  for (const f of frames) {
    const inGap = gaps.some(g => f.s > g.s0 && f.s < g.s1);
    if (inGap) { if (cur.length > 1) runs.push(cur); cur = []; } else cur.push(f);
  }
  if (cur.length > 1) runs.push(cur);
  const oc = (ln.o0 + ln.o1) / 2, hw = Math.min(0.3, ln.width / 2 - 0.02);
  // F-shape concrete barrier profile (outward right of travel in o,h plane)
  const shape = [[-hw, 0], [-hw + 0.07, 0.08], [-0.09, 0.33], [-0.08, 0.82], [0.08, 0.82], [0.09, 0.33], [hw - 0.07, 0.08], [hw, 0]];
  const traced = shape.slice().reverse(); // traced right→left over the top so the outside faces out
  for (const r of runs) {
    sweep(out, 'barrier', r, traced, oc);
    // end caps
    for (const [f, dir] of [[r[0], -1], [r[r.length - 1], 1]]) {
      box(out, 'barrier', f.x + f.nx * oc + f.tx * dir * 0.02, f.y + 0.41, f.z + f.nz * oc + f.tz * dir * 0.02, f.tx, f.tz, 0.02, hw * 0.6, 0.41);
    }
  }
  return traced;
}

function tramRails(out, frames, oc, h) {
  for (const side of [-0.7175, 0.7175]) {
    ribbon(out, 'steel', frames, oc + side - 0.04, oc + side + 0.04, h + 0.006);
    ribbon(out, 'railGroove', frames, oc + side + (side < 0 ? 0.04 : -0.08), oc + side + (side < 0 ? 0.08 : -0.04), h + 0.004);
  }
}

function endCap(out, c, s, dir, lay) {
  const f = c.frameAt(s);
  const hl = 1.4;
  const cx = f.x + f.tx * dir * hl, cz = f.z + f.tz * dir * hl;
  const oc = (lay.left + lay.right) / 2;
  box(out, 'sidewalk', cx + f.nx * oc, f.y + (RAISED + GROUND) / 2, cz + f.nz * oc, f.tx, f.tz, hl, lay.total / 2, (RAISED - GROUND) / 2);
  out.prop('bollard', f.x + f.tx * dir * 0.5, f.y + RAISED, f.z + f.tz * dir * 0.5, 0);
}

// ----------------------------------------------------------------------------------- bridges
function bridge(out, net, edge, c, frames, elevated, lay, prof, ctx, pinch) {
  const PO = pinch ? pinch.PO : (o => o);
  const rib = (o, mat, fr, a, b, h) => pinch ? ribbon(o, mat, fr, (k, f) => PO(a, f), (k, f) => PO(b, f), h) : ribbon(o, mat, fr, a, b, h);
  const wl = (o, mat, fr, a, hb, ht, side) => pinch ? wall(o, mat, fr, f => PO(a, f), hb, ht, side) : wall(o, mat, fr, a, hb, ht, side);
  const runs = [];
  let cur = null;
  frames.forEach((f, k) => {
    if (elevated[k]) { if (!cur) { cur = []; if (k > 0) cur.push(frames[k - 1]); } cur.push(f); }
    else if (cur) { cur.push(f); runs.push(cur); cur = null; }
  });
  if (cur) runs.push(cur);
  const deck = RANK[edge.profile] >= 8 ? 1.6 : 1.1;
  const outerL = lay.lanes[0], outerR = lay.lanes[lay.lanes.length - 1];
  for (const run of runs) {
    const high = f => f.y > 2.6 || (ctx.waterAt && ctx.waterAt(f.x, f.z));
    const bottom = f => high(f) ? f.y - deck : (ctx.groundAt ? ctx.groundAt(f.x, f.z) : GROUND);
    const hiFrames = run.filter(high);
    // underside
    if (hiFrames.length > 1) {
      let seg = [];
      for (const f of run) { if (high(f)) seg.push(f); else { if (seg.length > 1) rib(out, 'concrete', seg, lay.right, lay.left, -deck); seg = []; } }
      if (seg.length > 1) rib(out, 'concrete', seg, lay.right, lay.left, -deck);
    }
    wl(out, 'concrete', run, lay.left - 0.02, bottom, f => f.y + outerL.h + 0.02, -1);
    wl(out, 'concrete', run, lay.right + 0.02, bottom, f => f.y + outerR.h + 0.02, +1);
    // parapets / railings where the outer lane is not already a barrier
    for (const [ln, side] of [[outerL, -1], [outerR, 1]]) {
      if (ln.type === 'barrier') continue;
      const o = side < 0 ? lay.left : lay.right;
      if (ln.level === 'raised') {
        // steel pedestrian railing: posts + top rail + mid rail
        const oc = o - side * 0.08;
        sweep(out, 'steel', run, [[-0.035, 1.05], [-0.035, 1.12], [0.035, 1.12], [0.035, 1.05], [-0.035, 1.05]].map(([a, h]) => [a, h + ln.h]), oc);
        sweep(out, 'steel', run, [[-0.02, 0.55], [-0.02, 0.6], [0.02, 0.6], [0.02, 0.55], [-0.02, 0.55]].map(([a, h]) => [a, h + ln.h]), oc);
        const s0 = run[0].s, s1 = run[run.length - 1].s;
        for (let s = s0; s <= s1; s += 2.0) { const f = c.frameAt(s); out.prop('railPost', f.x + f.nx * oc, f.y + ln.h, f.z + f.nz * oc, yawFor(f.tx, f.tz)); }
      } else {
        const oc = o - side * 0.2;
        sweep(out, 'barrier', run, [[0.2, ln.h], [0.2, ln.h + 0.95], [0.12, ln.h + 1.0], [-0.12, ln.h + 1.0], [-0.2, ln.h + 0.95], [-0.2, ln.h]], oc);
      }
    }
    // piers
    if (hiFrames.length) {
      const s0 = run[0].s, s1 = run[run.length - 1].s;
      const spacing = RANK[edge.profile] >= 8 ? 34 : 26;
      const W = lay.total;
      for (let s = s0 + spacing * 0.5; s < s1 - 4; s += spacing) {
        let placed = false;
        for (const shift of [0, 6, -6, 11, -11]) {
          const ss = s + shift; if (ss < s0 + 3 || ss > s1 - 3) continue;
          const f = c.frameAt(ss);
          if (!high(f)) continue;
          const water = ctx.waterAt && ctx.waterAt(f.x, f.z);
          const gy = water ? -6 : GROUND;
          const capTop = f.y - deck, capH = 1.3;
          const colTop = capTop - capH;
          if (colTop - gy < 1.2) continue;
          const cols = W > 16 ? [-W * 0.27, W * 0.27] : [0];
          const clear = cols.every(o => !ctx.pierBlocked || !ctx.pierBlocked(f.x + f.nx * o, f.z + f.nz * o, 1.4, f.y, edge.id));
          if (!clear) continue;
          const oc = (lay.left + lay.right) / 2;
          out.prop('pierCap', f.x + f.nx * oc, capTop - capH, f.z + f.nz * oc, yawFor(f.nx, f.nz), W * 0.86, capH, 1.6);
          for (const o of cols) out.prop(W > 16 ? 'pierCol' : 'pierColWide', f.x + f.nx * (oc + o), gy, f.z + f.nz * (oc + o), yawFor(f.tx, f.tz), 1, colTop - gy, 1);
          placed = true; break;
        }
        if (!placed) continue;
      }
    }
  }
}

// ----------------------------------------------------------------------------------- paint
function laneLineRule(a, b, prof, style) {
  const TA = a.type, TB = b.type;
  const traffic = t => t === 'drive' || t === 'bus' || t === 'tram';
  const eu = style === 'eu';
  const Y = eu ? 'markWhite' : 'markYellow';
  if (a.level === 'raised' || b.level === 'raised') {
    // edge line along medians on divided roads (yellow on driver's left)
    return null;
  }
  if (TA === 'drive' && TB === 'drive') {
    if ((a.dir || 0) === 0 || (b.dir || 0) === 0) return null;
    if (a.dir !== b.dir) {
      if (prof.noCenterLine) return null;
      if (prof.centerLine === 'dashedYellow') return { kind: 'dashed', mat: Y, w: 0.12, dash: 3, gap: 9, center: true };
      return eu ? { kind: 'dashed', mat: 'markWhite', w: 0.12, dash: 4, gap: 8, center: true } : { kind: 'double', mat: Y, w: 0.11, center: true };
    }
    return { kind: 'dashed', mat: 'markWhite', w: 0.12, dash: 3, gap: 9, lane: true };
  }
  if (traffic(TA) && traffic(TB)) {
    if (TA === 'tram' && TB === 'tram') return a.dir !== b.dir ? null : null;
    if (a.dir !== b.dir && !(TA === 'bus' && TB === 'bus')) return { kind: 'double', mat: Y, w: 0.11, center: true };
    if (TA === 'bus' && TB === 'bus') return { kind: 'dashed', mat: Y, w: 0.12, dash: 3, gap: 6 };
    return { kind: 'solid', mat: 'markWhite', w: TA === 'bus' || TB === 'bus' ? 0.25 : 0.12 };
  }
  if ((traffic(TA) && TB === 'bike') || (TA === 'bike' && traffic(TB))) return { kind: 'solid', mat: 'markWhite', w: 0.15 };
  if (TA === 'bike' && TB === 'bike') return { kind: 'dashed', mat: 'markWhite', w: 0.1, dash: 1.5, gap: 2.5 };
  if (TA === 'buffer' || TB === 'buffer') return { kind: 'solid', mat: 'markWhite', w: 0.12 };
  if ((traffic(TA) && TB === 'parking') || (TA === 'parking' && traffic(TB))) return { kind: 'solid', mat: 'markWhite', w: 0.1 };
  if (TA === 'shoulder' || TB === 'shoulder' || TA === 'gravel' || TB === 'gravel' || TA === 'verge' || TB === 'verge' || TA === 'barrier' || TB === 'barrier') {
    const d = traffic(TA) ? a : traffic(TB) ? b : null;
    if (!d) return null;
    // shoulder on the driver's left → yellow (US)
    const shoulderLeftOfDriver = (d === b && d.dir === 1) || (d === a && d.dir === -1);
    return { kind: 'solid', mat: shoulderLeftOfDriver && !eu ? 'markYellow' : 'markWhite', w: 0.15 };
  }
  return null;
}

function incomingLanes(lay, atA) {
  // Lanes whose traffic arrives at this end. Right-hand traffic.
  return lay.lanes.filter(l => l.meta.traffic && (atA ? (l.dir === -1) : (l.dir === 1)) || (l.meta.traffic && (l.dir || 0) === 0));
}

function paint(out, net, edge, c, lay, prof, s0, s1, ctlA, ctlB, JA, JB, ctx, sideRange = null) {
  const lanes = lay.lanes, style = net.meta.markingStyle || 'us';
  const CW = 3.2;
  const endInfo = (ctl, J, atA) => {
    const cont = !J || J.kind === 'continuation' || J.kind === 'flush';
    const cross = ctl?.crosswalk;
    const stopOff = cross ? 0.6 + CW + 1.2 : (ctl && (ctl.stop || ctl.signal) ? 1.0 : 0);
    return { cont, cross, stopOff, ctl };
  };
  const A = endInfo(ctlA, JA, true), B = endInfo(ctlB, JB, false);
  const lineStart = s0 + (A.cont ? 0 : Math.max(0.4, A.stopOff));
  const lineEnd = s1 - (B.cont ? 0 : Math.max(0.4, B.stopOff));
  const yh = l => l.h;
  // each divider is painted over its own side's extent (an acute merge trims only its acute side)
  const rangeAt = o => {
    const r = sideRange ? sideRange(o) : [s0, s1];
    return [Math.max(r[0], s0), Math.min(r[1], s1)];
  };
  for (let i = 0; i < lanes.length - 1; i++) {
    const a = lanes[i], b = lanes[i + 1];
    const r = laneLineRule(a, b, prof, style);
    if (!r) continue;
    const o = a.o1, h = Math.max(yh(a), yh(b));
    const [ra, rb] = rangeAt(o);
    const lineStart = ra + (A.cont ? 0 : Math.max(0.4, A.stopOff));
    const lineEnd = rb - (B.cont ? 0 : Math.max(0.4, B.stopOff));
    if (lineEnd - lineStart < 0.05) continue;
    if (r.kind === 'solid') line(out, r.mat, c, lineStart, lineEnd, o, r.w, h);
    else if (r.kind === 'double') { line(out, r.mat, c, lineStart, lineEnd, o - 0.11, r.w, h); line(out, r.mat, c, lineStart, lineEnd, o + 0.11, r.w, h); }
    else if (r.kind === 'dashed') {
      if (r.lane && !A.cont && lineEnd - lineStart > 60) {
        // solid lane line on the final approach to a junction
        const ap = 22;
        line(out, r.mat, c, lineStart, lineStart + ap, o, r.w, h);
        dashed(out, r.mat, c, lineStart + ap, B.cont ? lineEnd : lineEnd - ap, o, r.w, r.dash, r.gap, h);
        if (!B.cont) line(out, r.mat, c, lineEnd - ap, lineEnd, o, r.w, h);
      } else dashed(out, r.mat, c, lineStart, lineEnd, o, r.w, r.dash, r.gap, h);
    }
  }
  const lineStartAll = s0 + (A.cont ? 0 : Math.max(0.4, A.stopOff));
  const lineEndAll = s1 - (B.cont ? 0 : Math.max(0.4, B.stopOff));
  // median-adjacent yellow edge lines on divided roads
  lanes.forEach((ln, i) => {
    if (!(ln.level === 'raised' && i > lay.roadFrom && i < lay.roadTo)) return;
    const l = lanes[i - 1], r = lanes[i + 1];
    const Y = style === 'eu' ? 'markWhite' : 'markYellow';
    if (l && l.meta.traffic) line(out, Y, c, lineStartAll, lineEndAll, ln.o0 - 0.25, 0.12, 0);
    if (r && r.meta.traffic) line(out, Y, c, lineStartAll, lineEndAll, ln.o1 + 0.25, 0.12, 0);
  });

  // parking bays and hatched no-parking zones near ends
  const bayLen = 6.4;
  lanes.forEach(ln => {
    if (ln.type !== 'parking') return;
    const pA = s0 + (A.cont ? 0 : A.stopOff + 7), pB = s1 - (B.cont ? 0 : B.stopOff + 7);
    if (!A.cont) hatch(out, 'markWhite', c, s0 + Math.max(0.4, A.stopOff), pA, ln.o0 + 0.1, ln.o1 - 0.1, 2.2, 0.12);
    if (!B.cont) hatch(out, 'markWhite', c, pB, s1 - Math.max(0.4, B.stopOff), ln.o0 + 0.1, ln.o1 - 0.1, 2.2, 0.12);
    for (let s = pA; s <= pB + 0.01; s += bayLen) bar(out, 'markWhite', c, s, (ln.o0 + ln.o1) / 2, 0.05, ln.width / 2 - 0.05);
    if (lanes.indexOf(ln) >= 0) {
      const curbSide = ln.o0 > 0 ? ln.o1 - 0.12 : ln.o0 + 0.12;
      for (let s = pA; s <= pB; s += bayLen) bar(out, 'markWhite', c, s, curbSide, 0.35, 0.05);
    }
  });
  // buffers: diagonal hatch
  lanes.forEach(ln => { if (ln.type === 'buffer' && ln.width > 0.5) hatch(out, 'markWhite', c, lineStart, lineEnd, ln.o0 + 0.08, ln.o1 - 0.08, 3.2, 0.1); });

  // symbols along lanes
  const span = lineEnd - lineStart;
  lanes.forEach((ln, i) => {
    const dir = ln.dir || 1;
    if (ln.type === 'bus' && span > 40) {
      for (let s = lineStart + 18; s < lineEnd - 18; s += 90) {
        decal(out, 'bus', c, s, (ln.o0 + ln.o1) / 2, dir, 0);
        decal(out, 'only', c, s + 4 * dir, (ln.o0 + ln.o1) / 2, dir, 0);
      }
    }
    if (ln.type === 'tram' && span > 40) for (let s = lineStart + 25; s < lineEnd - 20; s += 120) decal(out, 'tram', c, s, (ln.o0 + ln.o1) / 2, dir, 0);
    if (ln.type === 'bike' && span > 30) {
      for (let s = lineStart + 12; s < lineEnd - 10; s += 70) { decal(out, 'bike', c, s, (ln.o0 + ln.o1) / 2, dir, 0); decal(out, 'arrowS', c, s + 4 * dir, (ln.o0 + ln.o1) / 2, dir, 0, 0.6); }
    }
    if (ln.type === 'drive' && span > 50) {
      // utility covers
      for (let s = lineStart + 25 + hash(edge.id, i) * 40; s < lineEnd - 10; s += 75) decal(out, 'manhole', c, s, (ln.o0 + ln.o1) / 2 + (hash(edge.id, i + 7) - 0.5) * 1.2, 1, 0);
    }
  });
  // storm drains against curbs
  if (lay.hasCurbL || lay.hasCurbR) {
    for (let s = lineStart + 15; s < lineEnd - 8; s += 42) {
      if (lay.hasCurbL) decal(out, 'drain', c, s, lay.curbL + 0.3, 1, 0);
      if (lay.hasCurbR) decal(out, 'drain', c, s + 8, lay.curbR - 0.3, 1, 0);
    }
  }

  // junction approach markings
  const endMarks = (E, atA, J) => {
    if (E.cont || !E.ctl) return;
    const sEnd = atA ? s0 : s1, dirIn = atA ? -1 : 1;  // travel direction (in s) of incoming traffic
    const at = d => atA ? s0 + d : s1 - d;
    if (E.cross) {
      const cA = lay.curbL + 0.5, cB = lay.curbR - 0.5;
      const sc = at(0.6 + CW / 2);
      for (let o = cA + 0.3; o < cB - 0.2; o += 1.2) bar(out, 'markWhite', c, sc, o, CW / 2, 0.3, 0);
      // tactile paving at both curb ramps
      for (const [o, side] of [[lay.curbL, -1], [lay.curbR, 1]]) {
        if ((side < 0 && !lay.hasCurbL) || (side > 0 && !lay.hasCurbR)) continue;
        bar(out, 'tactile', c, sc, o + side * (CURB_W + 0.45), CW / 2 - 0.2, 0.4, LEVEL.raised - 0.009);
      }
    }
    const inc = incomingLanes(lay, atA).filter(l => l.type !== 'bike' || true);
    if (!inc.length) return;
    const oMin = Math.min(...inc.map(l => l.o0)), oMax = Math.max(...inc.map(l => l.o1));
    if (E.ctl.yield) {
      for (const l of inc) {
        // each lane's teeth sit at its own side's setback (acute merges cut back one side only)
        const [ra, rb] = sideRange((l.o0 + l.o1) / 2);
        const ty = atA ? ra + 0.9 : rb - 0.9;
        for (let o = l.o0 + 0.4; o < l.o1 - 0.3; o += 0.9) tooth(out, 'markWhite', c, ty, o + 0.3, -dirIn, 0.55, 0.85, 0);
      }
      return;
    }
    if (E.ctl.stop || E.ctl.signal) {
      bar(out, 'markWhite', c, at(E.stopOff - 0.25), (oMin + oMax) / 2, 0.25, (oMax - oMin) / 2 - 0.05, 0);
      if (E.ctl.stop && !E.ctl.signal) for (const l of inc) if (l.type === 'drive') decal(out, 'stop', c, at(E.stopOff + 3), (l.o0 + l.o1) / 2, dirIn, 0);
    }
    // lane-use arrows
    const drive = inc.filter(l => l.type === 'drive');
    if (drive.length && span > 45 && J && J.ends.length >= 3) {
      const sorted = drive.slice().sort((p, q) => atA ? q.o0 - p.o0 : p.o0 - q.o0); // left→right from driver
      sorted.forEach((l, k) => {
        let name = 'arrowS';
        if (sorted.length === 1) name = J.ends.length >= 4 ? 'arrowS' : 'arrowSR';
        else if (k === 0) name = 'arrowSL';
        else if (k === sorted.length - 1) name = 'arrowSR';
        const d0 = E.stopOff + 7;
        decal(out, name, c, at(d0), (l.o0 + l.o1) / 2, dirIn, 0);
        if (span > 110) decal(out, name, c, at(d0 + 30), (l.o0 + l.o1) / 2, dirIn, 0);
      });
    }
  };
  endMarks(A, true, JA);
  endMarks(B, false, JB);
}

// ----------------------------------------------------------------------------------- features
function speedHump(out, c, ft, lay, surfaceMat) {
  const table = ft.kind === 'table';
  const Lh = table ? 7.2 : 4.0, H = table ? 0.1 : 0.085;
  const hf = u => {
    const x = u + Lh / 2;
    if (table) { if (x < 1.6) return H * (x / 1.6); if (x > Lh - 1.6) return H * ((Lh - x) / 1.6); return H; }
    return H * Math.cos(Math.PI * u / Lh) ** 2;
  };
  const n = table ? 1 : 14;
  const us = table ? [-Lh / 2, -Lh / 2 + 1.6, Lh / 2 - 1.6, Lh / 2] : Array.from({ length: n + 1 }, (_, k) => -Lh / 2 + Lh * k / n);
  const frames = us.map(u => { const f = c.frameAt(ft.s + u); f.y += hf(u) + 0.006; return f; });
  const a = lay.curbL + 0.01, b = lay.curbR - 0.01;
  ribbon(out, surfaceMat, frames, a, b, 0);
  // painted chevrons / zebra
  if (table) {
    const sc = ft.s;
    for (let o = a + 0.6; o < b - 0.3; o += 1.2) {
      const f = c.frameAt(sc); const pc = out.piece('markWhite'), base = pc.p.length / 3;
      for (const [du, side] of [[-1.5, -0.3], [-1.5, 0.3], [1.5, -0.3], [1.5, 0.3]]) {
        const fx = c.frameAt(sc + du);
        out.vert(pc, fx.x + fx.nx * (o + side), fx.y + H + 0.02, fx.z + fx.nz * (o + side), 0, 0);
      }
      pc.i.push(base, base + 1, base + 2, base + 1, base + 3, base + 2);
    }
    for (const sgn of [-1, 1]) for (let o = a + 0.5; o < b - 0.3; o += 1.0) {
      const f = c.frameAt(ft.s + sgn * (Lh / 2 - 0.8));
      tooth(out, 'markWhite', c, ft.s + sgn * (Lh / 2 - 0.4), o + 0.25, sgn, 0.5, 0.7, hf(sgn * (Lh / 2 - 0.4)) + 0.008);
    }
  } else {
    for (let o = a + 0.5; o < b - 0.3; o += 0.9) {
      const pc = out.piece('markWhite'), base = pc.p.length / 3;
      const m = 8;
      for (let k = 0; k <= m; k++) {
        const u = -Lh / 2 + 0.3 + (Lh - 0.6) * k / m, f = c.frameAt(ft.s + u), y = f.y + hf(u) + 0.02;
        out.vert(pc, f.x + f.nx * (o - 0.15), y, f.z + f.nz * (o - 0.15), 0, 0);
        out.vert(pc, f.x + f.nx * (o + 0.15), y, f.z + f.nz * (o + 0.15), 1, 0);
      }
      for (let k = 0; k < m; k++) { const l0 = base + k * 2; pc.i.push(l0, l0 + 1, l0 + 2, l0 + 1, l0 + 3, l0 + 2); }
    }
  }
  // advance warnings
  for (const ln of lay.lanes) {
    if (ln.type !== 'drive') continue;
    const dir = ln.dir || 1;
    decal(out, 'slow', c, ft.s - dir * 14, (ln.o0 + ln.o1) / 2, dir, 0);
  }
  for (const [o, side] of [[lay.curbL, -1], [lay.curbR, 1]]) {
    const f = c.frameAt(ft.s - side * 18);
    const hasSide = side < 0 ? lay.hasCurbL : lay.hasCurbR;
    const oo = o + side * (hasSide ? 0.6 : 0.8);
    out.prop('signHump', f.x + f.nx * oo, f.y + (hasSide ? RAISED : 0), f.z + f.nz * oo, yawFor(-f.tx * side, -f.tz * side));
  }
}

function busStop(out, c, ft, lay) {
  const side = ft.side || 1;
  const curb = side > 0 ? lay.curbR : lay.curbL;
  const hasSide = side > 0 ? lay.hasCurbR : lay.hasCurbL;
  if (!hasSide) return;
  const zoneW = side > 0 ? lay.sideR : lay.sideL;
  const f = c.frameAt(ft.s);
  const oo = curb + side * Math.min(zoneW - 0.9, 2.4);
  out.prop('busShelter', f.x + f.nx * oo, f.y + RAISED, f.z + f.nz * oo, yawFor(-f.nx * side, -f.nz * side));
  // curbside box + text
  const lo = curb - side * 3.2, hi = curb - side * 0.15;
  const o0 = Math.min(lo, hi), o1 = Math.max(lo, hi);
  const sA = ft.s - 12, sB = ft.s + 12;
  line(out, 'markYellow', c, sA, sB, o0, 0.12, 0); line(out, 'markYellow', c, sA, sB, o1, 0.12, 0);
  bar(out, 'markYellow', c, sA, (o0 + o1) / 2, 0.06, (o1 - o0) / 2);
  bar(out, 'markYellow', c, sB, (o0 + o1) / 2, 0.06, (o1 - o0) / 2);
  decal(out, 'busStop', c, ft.s, (o0 + o1) / 2, side > 0 ? 1 : -1, 0);
}

function midblockCrossing(out, c, ft, lay) {
  const CW = 3.2;
  for (let o = lay.curbL + 0.8; o < lay.curbR - 0.4; o += 1.2) bar(out, 'markWhite', c, ft.s, o, CW / 2, 0.3, 0);
  for (const [o, side] of [[lay.curbL, -1], [lay.curbR, 1]]) {
    const has = side < 0 ? lay.hasCurbL : lay.hasCurbR;
    if (has) bar(out, 'tactile', c, ft.s, o + side * (CURB_W + 0.45), CW / 2 - 0.2, 0.4, LEVEL.raised - 0.009);
    const f = c.frameAt(ft.s - side * (CW / 2 + 0.8));
    out.prop('signCrossing', f.x + f.nx * (o + side * 0.6), f.y + (has ? RAISED : 0), f.z + f.nz * (o + side * 0.6), yawFor(-f.tx * side, -f.tz * side));
  }
  for (const ln of lay.lanes) if (ln.meta.traffic && ln.type === 'drive') {
    const dir = ln.dir || 1;
    for (let o = ln.o0 + 0.4; o < ln.o1 - 0.3; o += 0.9) tooth(out, 'markWhite', c, ft.s - dir * (CW / 2 + 6), o + 0.3, dir, 0.5, 0.75, 0);
  }
}

// ----------------------------------------------------------------------------------- control props
function controlProps(out, c, lay, sEnd, atA, ctl) {
  if (!ctl.signal && !ctl.stop && !ctl.yield) return;
  const inc = incomingLanes(lay, atA).filter(l => l.type === 'drive' || l.type === 'bus');
  if (!inc.length) return;
  // incoming side: for end A, traffic arriving uses negative offsets (left); for B, positive
  const side = atA ? -1 : 1;
  const curb = side < 0 ? lay.curbL : lay.curbR;
  const hasSide = side < 0 ? lay.hasCurbL : lay.hasCurbR;
  const o = curb + side * (hasSide ? 0.75 : 1.4);
  const sPole = atA ? sEnd + 0.4 : sEnd - 0.4;
  const f = c.frameAt(sPole);
  const dx = atA ? f.tx : -f.tx, dz = atA ? f.tz : -f.tz;   // outward (away from node)
  const baseY = f.y + (hasSide ? RAISED : 0);
  const px = f.x + f.nx * o, pz = f.z + f.nz * o;
  if (ctl.signal) {
    out.prop('signalPole', px, baseY, pz, yawFor(dx, dz));
    const innerO = side < 0 ? Math.max(...inc.map(l => l.o1)) : Math.min(...inc.map(l => l.o0));
    const armLen = Math.max(2, Math.abs(o - innerO) - 0.5);
    const toward = side < 0 ? 1 : -1;
    out.prop('signalArm', px, baseY + 6.2, pz, yawFor(f.nx * toward, f.nz * toward), armLen, 1, 1);
    for (const l of inc) {
      const oc = (l.o0 + l.o1) / 2;
      out.prop('signalHead', f.x + f.nx * oc, baseY + 5.35, f.z + f.nz * oc, yawFor(dx, dz));
    }
    out.prop('signalHead', px + f.nx * -side * 0.25, baseY + 2.9, pz + f.nz * -side * 0.25, yawFor(dx, dz));
    out.prop('pedSignal', px, baseY + 2.3, pz, yawFor(f.nx * toward, f.nz * toward));
  } else if (ctl.stop) {
    out.prop('signStop', px, baseY, pz, yawFor(dx, dz));
  } else if (ctl.yield) {
    out.prop('signYield', px, baseY, pz, yawFor(dx, dz));
  }
}

// ----------------------------------------------------------------------------------- furniture
function furniture(out, net, edge, c, lay, prof, s0, s1, elevatedFrames, ctx) {
  const lanes = lay.lanes;
  const isActive = prof.category === 'active';
  // street lamps
  const spacing = prof.lampSpacing || 30;
  const h = prof.lampHeight || 8;
  const type = h <= 5 ? 'lampPost' : h >= 10 ? 'lampHigh' : 'lamp';
  const margin = 7;
  const frameY = f => f.y;
  if (prof.lamps && prof.lamps !== 'none' && s1 - s0 > margin * 2) {
    const phase = hash(edge.id) * spacing;
    if (prof.lamps === 'median') {
      const mid = lanes.reduce((best, l) => (l.type === 'barrier' || l.type === 'median') && Math.abs((l.o0 + l.o1) / 2) < Math.abs(best ? (best.o0 + best.o1) / 2 : 1e9) ? l : best, null);
      if (mid) for (let s = s0 + margin + phase % spacing; s < s1 - margin; s += spacing) {
        const f = c.frameAt(s), oc = (mid.o0 + mid.o1) / 2;
        out.prop('lampDouble', f.x + f.nx * oc, frameY(f) + (mid.type === 'barrier' ? 0.82 : mid.h), f.z + f.nz * oc, yawFor(f.nx, f.nz));
      }
    } else {
      const sides = prof.lamps === 'both' ? [-1, 1] : [1];
      for (const side of sides) {
        const has = side < 0 ? lay.hasCurbL : lay.hasCurbR;
        const curb = side < 0 ? lay.curbL : lay.curbR;
        const outerLane = side < 0 ? lanes[0] : lanes[lanes.length - 1];
        let o, y0;
        if (has) { o = curb + side * 0.55; y0 = RAISED; }
        else if (outerLane.type === 'barrier') { o = (outerLane.o0 + outerLane.o1) / 2; y0 = 0.82; }
        else { o = (side < 0 ? lay.left : lay.right) + side * 0.5; y0 = isActive ? 0 : 0; }
        // keep clear of planter trees: lamps sit just behind the curb
        for (let s = s0 + margin + ((phase + (side > 0 ? spacing / 2 : 0)) % spacing); s < s1 - margin; s += spacing) {
          const f = c.frameAt(s);
          const yaw = isActive && type === 'lampPost' ? 0 : yawFor(-f.nx * side, -f.nz * side);
          out.prop(type, f.x + f.nx * o, frameY(f) + y0, f.z + f.nz * o, yaw);
        }
      }
    }
  }
  // trees in planters / medians
  if (ctx.trees !== false) {
    lanes.forEach((ln, i) => {
      if (!ln.trees || ln.mat !== 'grass') return;
      const isMedian = i > lay.roadFrom && i < lay.roadTo;
      const step = isMedian ? 11 : 9.5;
      const m0 = s0 + (isMedian ? 9 : 6), m1 = s1 - (isMedian ? 9 : 6);
      const phase = hash(edge.id, i) * step;
      for (let s = m0 + phase; s < m1; s += step) {
        const f = c.frameAt(s);
        if (f.y > 0.5) continue;
        const r = hash(edge.id, Math.round(s * 10) + i);
        const oc = (ln.o0 + ln.o1) / 2;
        out.prop(r > 0.5 ? 'treeA' : 'treeB', f.x + f.nx * oc, f.y + ln.h, f.z + f.nz * oc, r * 6.28, 0.8 + r * 0.45, 0.8 + r * 0.45, 0.8 + r * 0.45);
      }
    });
  }
  // hydrants, bins and benches on sidewalks
  lanes.forEach((ln, i) => {
    if (ln.type !== 'sidewalk' || ln.width < 1.8) return;
    const side = (ln.o0 + ln.o1) / 2 < 0 ? -1 : 1;
    const inner = side < 0 ? ln.o1 : ln.o0;
    const oo = inner + side * (ln.mat === 'sidewalk' ? 0.55 : 0.5);
    for (let s = s0 + 20 + hash(edge.id, i + 3) * 40; s < s1 - 15; s += 120) {
      const f = c.frameAt(s); if (f.y > 0.5) continue;
      out.prop('hydrant', f.x + f.nx * oo, f.y + RAISED, f.z + f.nz * oo, hash(edge.id, i) * 6);
      const g = c.frameAt(s + 25); if (g.s < s1 - 10 && g.y < 0.5) out.prop('bin', g.x + g.nx * oo, g.y + RAISED, g.z + g.nz * oo, yawFor(-g.nx * side, -g.nz * side));
    }
  });
  if (isActive && prof.lanes.some(l => l.type === 'path')) {
    const right = lay.right + 0.7;
    for (let s = s0 + 15 + hash(edge.id) * 20; s < s1 - 10; s += 45) {
      const f = c.frameAt(s);
      out.prop('bench', f.x + f.nx * right, f.y, f.z + f.nz * right, yawFor(-f.nx, -f.nz));
    }
  }
  // bollards between shared-surface zones
  if (prof.bollardsAt) {
    lanes.forEach((ln, i) => {
      const nb = lanes[i + 1];
      if (!nb || (ln.type === prof.bollardsAt) === (nb.type === prof.bollardsAt)) return;
      for (let s = s0 + 2; s < s1 - 2; s += 3.2) { const f = c.frameAt(s); out.prop('bollard', f.x + f.nx * ln.o1, f.y, f.z + f.nz * ln.o1, 0); }
    });
  }
  // flexible delineator posts in bike buffers
  lanes.forEach((ln, i) => {
    if (ln.type !== 'buffer') return;
    const nb = lanes[i - 1]?.type === 'bike' || lanes[i + 1]?.type === 'bike';
    if (!nb) return;
    for (let s = s0 + 12; s < s1 - 12; s += 7) { const f = c.frameAt(s); const oc = (ln.o0 + ln.o1) / 2; out.prop('delineator', f.x + f.nx * oc, f.y, f.z + f.nz * oc, 0); }
  });
}

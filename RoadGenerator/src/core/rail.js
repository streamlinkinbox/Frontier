// Railways: ballasted track (standard gauge 1435 mm), sleepers, flat-bottom rails, level crossings with
// roads (rubber panels, crossbucks, half-barriers) and bored subway tunnels (6 m segmental lining).
import { MeshBuilder } from "./mesh.js";
import { Spline, madd, mul, add, sub, norm, rightXZ, lerp, clamp, mat4, cross } from "./math.js";
import { loft, box, lathe, frames, sweep } from "./primitives.js";
import { footprint, roadPoint } from "./query.js";
import { basis, basisAlong } from "./furnish.js";

export const GAUGE = 1.435, RAIL_H = 0.172, SLEEPER = { w: 2.6, h: 0.2, d: 0.26, spacing: 0.6 };
export function defaultRailSettings() { return { enabled: true, sleepers: true, crossings: true, barriers: true, trains: true, tunnelRadius: 3.0 }; }

/** Rail line record from the network: { id, pts:[[x,y,z]], tunnel: bool, name } → analysed { spline, length, ... } */
export function analyseRail(line) {
  const spline = new Spline(line.pts, null, null);
  return { line, spline, length: spline.length, tunnel: !!line.tunnel, halfWidth: line.tunnel ? 3.2 : 2.6 };
}
/** World point on the track at station s, lateral x (right +), lift above rail-top datum. */
export function railPoint(RL, s, x, lift = 0) { const F = RL.spline.frameAt(s); const r = rightXZ(F.dxz); return [F.p[0] + r[0] * x, F.p[1] + lift, F.p[2] + r[2] * x]; }

const railSection = (() => { // flat-bottom rail, half-profile mirrored (metres), origin at rail top centre
  const h = RAIL_H, foot = 0.15 / 2, head = 0.072 / 2, web = 0.016 / 2;
  const pts = [[-head, 0], [head, 0], [head, -0.038], [web, -0.052], [web, -(h - 0.03)], [foot, -(h - 0.012)], [foot, -h], [-foot, -h], [-foot, -(h - 0.012)], [-web, -(h - 0.03)], [-web, -0.052], [-head, -0.038]];
  return pts.map(([x, y]) => ({ x, y }));
})();

/** Build track geometry for all rail lines. Returns { solid, instances, crossings, lines }. */
export function buildRails(net, plan, S = defaultRailSettings()) {
  S = { ...defaultRailSettings(), ...S };
  const mb = new MeshBuilder("rail");
  const ballast = mb.material("ballast"), steel = mb.material("metal"), rubber = mb.material("rubber"), concrete = mb.material("concrete"), tiles = mb.material("tiles"), lamp = mb.material("lamp");
  const instances = new Map();
  const inst = (prop, variant, m, group = "") => { const k = prop + "|" + variant + "|" + group; (instances.get(k) || instances.set(k, { prop, variant, group, mats: [] }).get(k)).mats.push(m); };
  const lines = [...net.rails.values()].map(analyseRail);
  const crossings = [];
  for (const RL of lines) {
    const L = RL.length;
    // --- level crossings: where the rail polyline passes through a road's carriageway at similar height
    const xs = [];
    if (S.crossings && !RL.tunnel) for (const R of plan.roads.values()) {
      const fp = footprint(R);
      const bb = roadBounds(R, fp);
      for (let s = 0; s < L; s += 1) {
        const p = RL.spline.pointAt(s);
        if (p[0] < bb.minX || p[0] > bb.maxX || p[2] < bb.minZ || p[2] > bb.maxZ) continue;
        const near = R.spline.closestStation(p);
        if (!near) continue;
        const F = R.frameAt(near.s);
        const d = sub(p, F.p); const lat = d[0] * F.r[0] + d[2] * F.r[2];
        if (lat > -R.section.widthL - 0.05 && lat < R.section.widthR + 0.05 && Math.abs(p[1] - F.p[1]) < 1.5 && near.s > R.cutA + 2 && near.s < R.length - R.cutB - 2) {
          const last = xs[xs.length - 1];
          if (last && last.R === R && s - last.s1 < 3) { last.s1 = s; continue; }
          xs.push({ R, s0: s, s1: s, roadS: near.s });
        }
      }
    }
    for (const x of xs) { // extend over the full carriageway + 1.5 m apron each side
      const Fd = RL.spline.frameAt((x.s0 + x.s1) / 2).dxz, Rd = x.R.frameAt(x.roadS).dxz;
      const sinA = Math.max(0.3, Math.abs(Fd[0] * Rd[2] - Fd[2] * Rd[0]));
      const halfW = (x.R.section.widthL + x.R.section.widthR) / 2 / sinA;
      const mid = (x.s0 + x.s1) / 2;
      x.a = mid - halfW - 1.5; x.b = mid + halfW + 1.5; x.mid = mid;
      crossings.push({ rail: RL, road: x.R, s: mid, roadS: x.roadS, a: x.a, b: x.b });
    }
    const inCrossing = (s) => xs.find((x) => s >= x.a && s <= x.b);
    // --- ballast bed / tunnel lining
    if (!RL.tunnel) {
      // cross-section columns: [x, dy] relative to rail-top datum
      const topY = -0.33, baseY = -0.7;
      const cols = [[-4.2, -0.02], [-2.4, baseY], [-1.75, topY], [0, topY + 0.02], [1.75, topY], [2.4, baseY], [4.2, -0.02]];
      const matCols = [ballast, ballast, ballast, ballast, ballast, ballast];
      const grass = mb.material("grass");
      const mats = (j) => (j === 0 || j === 5 ? grass : ballast);
      let rows = [];
      const flush = () => { if (rows.length > 1) mb.gridAuto(mats, rows, RL.line.id); rows = []; };
      for (let s = 0; s <= L + 1e-6; s += 1.5) {
        const ss = Math.min(s, L);
        if (inCrossing(ss)) { flush(); continue; }
        rows.push(cols.map(([x, dy]) => mb.vertex(railPoint(RL, ss, x, dy), [ss, x], [0, 1, 0.5, 0])));
        if (ss >= L) break;
      }
      flush();
      // sleepers
      if (S.sleepers) for (let s = 0.3; s < L; s += SLEEPER.spacing) { if (inCrossing(s)) continue; const F = RL.spline.frameAt(s); inst("sleeper", "", basisAlong(railPoint(RL, s, 0, -RAIL_H - SLEEPER.h / 2), rightXZ(F.dxz))); }
    } else {
      buildTunnel(mb, RL, S.tunnelRadius, concrete, tiles, lamp, ballast);
    }
    // --- rails: two swept profiles (quads), continuous through crossings
    for (const side of [-1, 1]) {
      const x = side * GAUGE / 2 + side * 0.036;
      const path = [];
      const n = Math.max(2, Math.ceil(L / 1.0));
      for (let i = 0; i <= n; i++) path.push(railPoint(RL, (i / n) * L, x, 0.0));
      const frs = frames(path).map((f) => ({ ...f, n: mul(rightXZ(f.t), 1), b: [0, 1, 0] }));
      sweep(mb, steel, frs, railSection, { owner: RL.line.id, facing: (c) => { const q = c; return [0, 1, 0]; } });
    }
    // --- crossing furniture
    for (const x of xs) {
      // rubber panels: between the rails and outside, top 10 mm below rail top, spanning the crossing
      const seg = (xa, xb, lift) => { const rows = []; for (const s of [x.a, x.b]) rows.push([railPoint(RL, s, xa, lift), railPoint(RL, s, xb, lift)].map((p) => mb.vertex(p, [s, p[0]], [0.3, 1, 0.5, 0]))); mb.gridAuto(rubber, rows, RL.line.id); };
      seg(-GAUGE / 2 + 0.07, GAUGE / 2 - 0.07, -0.012); seg(-GAUGE / 2 - 0.5, -GAUGE / 2 - 0.08, -0.012); seg(GAUGE / 2 + 0.08, GAUGE / 2 + 0.5, -0.012);
      // asphalt apron between the road surface and the panels
      const asphalt = mb.material("asphalt");
      for (const [xa, xb] of [[-2.0, -GAUGE / 2 - 0.5], [GAUGE / 2 + 0.5, 2.0]]) { const rows = []; for (const s of [x.a, x.b]) rows.push([railPoint(RL, s, xa, -0.1), railPoint(RL, s, xb, -0.012)].map((p) => mb.vertex(p, [s, p[0]], [0.2, 1, 0.5, 0]))); mb.gridAuto(asphalt, rows, RL.line.id); }
      // crossbucks + half barriers on each approach, drivers' right, 4 m before the near rail
      const R = x.R;
      for (const dir of [-1, 1]) {
        const sRoad = x.roadS + dir * (halfTrackSpan(RL, R, x) + 4.5);
        const side = dir > 0 ? -1 : 1; // approaching from larger stations (dir +1) → travelling −s → right side is −x
        const xr = side * ((side > 0 ? R.section.widthR : R.section.widthL) + (R.road.profile.curbWidth || 0.3) + 0.5);
        if (sRoad < R.cutA + 1 || sRoad > R.length - R.cutB - 1) continue;
        const p = roadPoint(R, sRoad, xr); const F = R.frameAt(sRoad);
        const facing = mul(F.dxz, dir); // face the approaching traffic
        inst("crossbuck", "", basis(p, facing));
        if (S.barriers) inst("boom", "", basis(p, facing));
      }
    }
  }
  mb.compact();
  return { solid: mb, instances, crossings, lines };
}
function roadBounds(R, fp) { let minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity; const m = Math.max(-fp.left, fp.right) + 1; for (const F of R.frames) { minX = Math.min(minX, F.p[0] - m); maxX = Math.max(maxX, F.p[0] + m); minZ = Math.min(minZ, F.p[2] - m); maxZ = Math.max(maxZ, F.p[2] + m); } return { minX, maxX, minZ, maxZ }; }
function halfTrackSpan(RL, R, x) { const Fd = RL.spline.frameAt(x.mid).dxz, Rd = R.frameAt(x.roadS).dxz; const sinA = Math.max(0.3, Math.abs(Fd[0] * Rd[2] - Fd[2] * Rd[0])); return 1.6 / sinA; }

function buildTunnel(mb, RL, radius, concrete, tiles, lamp, ballast) {
  const L = RL.length, n = Math.max(2, Math.ceil(L / 3));
  const rings = [];
  const K = 20;
  for (let i = 0; i <= n; i++) {
    const s = (i / n) * L, F = RL.spline.frameAt(s), r = rightXZ(F.dxz);
    const c = [F.p[0], F.p[1] - 0.9 + radius * 0.0 + 1.3, F.p[2]]; // tube centre ~1.3 m above rail top
    const ring = [];
    for (let k = 0; k < K; k++) { const a = (k / K) * Math.PI * 2; ring.push(add(add(c, mul(r, Math.cos(a) * radius)), [0, Math.sin(a) * radius, 0])); }
    rings.push(ring);
  }
  // lining faces INWARD (we are inside the tube)
  loft(mb, concrete, rings, { closed: true, owner: RL.line.id, facing: (c) => { let best = rings[0], bd = Infinity; for (const ring of rings) { const cc = ring.reduce((a, p) => add(a, mul(p, 1 / ring.length)), [0, 0, 0]); const d = (cc[0] - c[0]) ** 2 + (cc[2] - c[2]) ** 2; if (d < bd) { bd = d; best = cc; } } return sub(best, c); } });
  // invert bed: flat concrete slab + ballast strip + walkway + cable trays + lights
  const rows = [], rows2 = [];
  for (let i = 0; i <= n; i++) {
    const s = (i / n) * L;
    rows.push([[-2.3, -0.45], [-1.7, -0.45], [-1.7, -0.33], [1.7, -0.33], [1.7, -0.45], [2.3, -0.45]].map(([x, dy]) => mb.vertex(railPoint(RL, s, x, dy), [s, x], [0, 1, 0.5, 0])));
    rows2.push([[-2.9, 0.4], [-2.3, 0.4], [-2.3, -0.45]].map(([x, dy]) => mb.vertex(railPoint(RL, s, x, dy), [s, x], [0, 1, 0.5, 0])));
  }
  mb.gridAuto((j) => (j === 2 ? ballast : concrete), rows, RL.line.id);
  mb.gridFacing(concrete, rows2, RL.line.id, [0, 1, 0]);
  for (let s = 6; s < L - 3; s += 12) box(mb, lamp, railPoint(RL, s, 2.2, 2.6), [0.15, 0.08, 1.2]);
}

/** Props specific to rail (sleeper, crossbuck sign, half-barrier boom). */
export function railProp(name) {
  const mb = new MeshBuilder(name);
  if (name === "sleeper") { box(mb, mb.material("concrete"), [0, 0, 0], [SLEEPER.w, SLEEPER.h, SLEEPER.d], { bevel: 0.015 }); for (const x of [-GAUGE / 2 - 0.036, GAUGE / 2 + 0.036]) box(mb, mb.material("metalPainted"), [x, SLEEPER.h / 2 + 0.01, 0], [0.18, 0.02, 0.2]); }
  else if (name === "crossbuck") {
    const st = mb.material("steelGalv"), w = mb.material("paint"), red = mb.material("lampRed");
    lathe(mb, st, [{ r: 0.06, y: 0 }, { r: 0.06, y: 0.02 }, { r: 0.045, y: 0.02 }, { r: 0.04, y: 3.4 }, { r: 0, y: 3.42 }], 10);
    for (const a of [Math.PI / 4, -Math.PI / 4]) { const b = new MeshBuilder("b"); box(b, b.material("paint"), [0, 0, 0], [1.2, 0.22, 0.02]); b.transform((p) => [p[0] * Math.cos(a) - p[1] * Math.sin(a), 3.0 + p[0] * Math.sin(a) + p[1] * Math.cos(a), 0.06 + p[2]]); mb.append(b); }
    box(mb, mb.material("metalPainted"), [0, 2.3, 0.05], [0.9, 0.3, 0.12], { bevel: 0.02 });
    for (const x of [-0.3, 0.3]) { const l = new MeshBuilder("l"); lathe(l, l.material("lampRed"), [{ r: 0, y: 0 }, { r: 0.1, y: 0 }, { r: 0.1, y: 0.03 }, { r: 0, y: 0.03 }], 12); l.transform((p) => [x + p[0], 2.3 - p[2], 0.11 + p[1]]); mb.append(l); }
  } else if (name === "boom") {
    const st = mb.material("metalPainted"), w = mb.material("paint"), red = mb.material("plastic");
    box(mb, st, [0.5, 0.55, 0], [0.5, 1.1, 0.4], { bevel: 0.02 });
    // pivot + arm 5 m to the LEFT (local +X is left of facing) over the carriageway
    lathe(mb, st, [{ r: 0.08, y: -0.2 }, { r: 0.08, y: 0.2 }], 10, [0, 0, 0]).forEach(() => {});
    mb.transform((p) => (Math.abs(p[1]) <= 0.2 && Math.hypot(p[0], p[2]) <= 0.081 ? [0.5 + p[1], 1.1 + p[2], p[0]] : p));
    for (let i = 0; i < 10; i++) box(mb, i % 2 ? red : w, [0.8 + 0.25 + i * 0.5, 1.1, 0], [0.5, 0.08, 0.06]);
  }
  return mb;
}

// Procedural city road network for large open-world driving maps.
// Everything is laid out in a planar "grid space" (u, v) and mapped to the world through a
// smooth, orientation-preserving warp — grids stay coherent while streets gain organic curvature.
import { RoadNetwork } from './network.js';
import { rng, makeNoise, smooth, resample, polylineLength, pointPolylineDist } from './math.js';
import { planarize } from './planarize.js';
import { PROFILES, layout } from './profiles.js';

const NAMES = ['Harbor', 'Union', 'Market', 'Grand', 'Lincoln', 'Mission', 'Sunset', 'Ocean', 'Pacific', 'Broadway', 'Central', 'Park', 'Maple', 'Oak', 'Cedar', 'Pine', 'Elm', 'Kingston', 'Riverside', 'Bayview', 'Hillcrest', 'Lakeshore', 'Westlake', 'Fairview', 'Highland', 'Summit', 'Victoria', 'Regent', 'Wellington', 'Madison', 'Jefferson', 'Franklin', 'Granite', 'Copper', 'Willow', 'Aspen', 'Juniper', 'Laurel', 'Magnolia', 'Spruce', 'Chestnut', 'Hawthorne', 'Beacon', 'Carlton', 'Dover', 'Easton', 'Foster', 'Garnet', 'Hudson', 'Irving', 'Jasper', 'Kensington', 'Lombard', 'Monroe', 'Newport', 'Orchard', 'Prospect', 'Quincy', 'Raleigh', 'Sterling', 'Tremont', 'Vernon', 'Whitney', 'York'];
const ORD = n => n + (n % 100 >= 11 && n % 100 <= 13 ? 'th' : ['th', 'st', 'nd', 'rd'][n % 10] || 'th');
const SUFFIX = { boulevard: 'Boulevard', avenueBike: 'Avenue', arterialBRT: 'Avenue', tramAvenue: 'Avenue', arterial4: 'Road', collector: 'Street', local: 'Street', oneway: 'Street', residential: 'Drive', cobblestone: 'Lane', sharedStreet: 'Mews', alley: 'Alley', industrial: 'Way', rural2: 'Road', countryLane: 'Lane', gravelRoad: 'Track', dirtRoad: 'Trail', expressway: 'Parkway', footpath: 'Walk', gravelPath: 'Trail', promenade: 'Promenade', cycleway: 'Cycleway' };

export const DEFAULT_GEN = { seed: 1337, size: 4096, density: 1, river: true, freeway: true, rail: true, subway: true, tram: true, roundabouts: true, parks: true, oldTown: true, suburbs: true, markingStyle: 'us' };

export function generateCity(options = {}) {
  const o = { ...DEFAULT_GEN, ...options };
  const R = rng(o.seed), noise = makeNoise(o.seed + 7);
  const half = o.size / 2;
  const net = new RoadNetwork();
  net.meta = { name: 'Generated city', size: o.size, seed: o.seed, markingStyle: o.markingStyle, gen: o };

  // ---------------------------------------------------------------- warp
  const rot = R.range(-0.25, 0.25);
  const cr = Math.cos(rot), sr = Math.sin(rot);
  const p = Array.from({ length: 6 }, () => R.range(0, Math.PI * 2));
  const A1 = 44 * Math.min(1, o.size / 4096), f1 = Math.PI * 2 / 1800, A2 = 14, f2 = Math.PI * 2 / 640;
  const warp = (u, v) => {
    const x = u + A1 * Math.sin(v * f1 + p[0]) + A2 * Math.sin((u + v) * f2 + p[1]);
    const z = v + A1 * Math.sin(u * f1 + p[2]) + A2 * Math.sin((u - v) * f2 + p[3]);
    return [x * cr - z * sr, x * sr + z * cr];
  };
  const unwarp = (x, z) => {
    const xr = x * cr + z * sr, zr = -x * sr + z * cr;
    let u = xr, v = zr;
    for (let k = 0; k < 6; k++) {
      const dx = A1 * Math.sin(v * f1 + p[0]) + A2 * Math.sin((u + v) * f2 + p[1]);
      const dz = A1 * Math.sin(u * f1 + p[2]) + A2 * Math.sin((u - v) * f2 + p[3]);
      u = xr - dx; v = zr - dz;
    }
    return [u, v];
  };

  // ---------------------------------------------------------------- arterial lattice
  const P = half - 70;                       // perimeter road position
  const lines = axis => {
    const out = [];
    const sp = () => R.range(400, 520) / Math.sqrt(o.density);
    let x = R.range(-90, 90);
    out.push(x);
    for (let y = x + sp(); y < P - 230; y += sp()) out.push(y);
    for (let y = x - sp(); y > -P + 230; y -= sp()) out.unshift(y);
    return out;
  };
  const U = lines('u'), V = lines('v');
  const Ub = [-P, ...U, P], Vb = [-P, ...V, P];   // cell boundaries incl. perimeter
  const cells = [];
  for (let i = 0; i < Ub.length - 1; i++) for (let j = 0; j < Vb.length - 1; j++) {
    const u0 = Ub[i], u1 = Ub[i + 1], v0 = Vb[j], v1 = Vb[j + 1];
    const uc = (u0 + u1) / 2, vc = (v0 + v1) / 2;
    cells.push({ i, j, u0, u1, v0, v1, uc, vc, d: Math.max(Math.abs(uc), Math.abs(vc)) / half, type: 'residential' });
  }

  // ---------------------------------------------------------------- river
  let river = null, riverUV = null;
  const riverHalf = 36;
  const rdOff = riverHalf + 6 + 11.3;         // riverside drive centerline offset
  if (o.river) {
    const vr = R.range(-0.28, 0.28) * half, ph = R.range(0, 6.28), ph2 = R.range(0, 6.28);
    const pts = [];
    for (let u = -half - 400; u <= half + 400; u += 20) pts.push([u, vr + 170 * Math.sin(u * Math.PI * 2 / 2700 + ph) + 45 * Math.sin(u * Math.PI * 2 / 950 + ph2)]);
    riverUV = pts;
  }
  const riverDist = (u, v) => riverUV ? pointPolylineDist(u, v, riverUV).d : Infinity;

  // ---------------------------------------------------------------- districts
  const railU = o.rail ? pickRailU() : null;
  function pickRailU() {
    // middle of a column ~35-55% out from the centre on one side
    const side = R.chance(0.5) ? 1 : -1;
    const col = U.map((u, k) => [u, U[k + 1]]).filter(([a, b]) => b !== undefined && Math.abs((a + b) / 2) > half * 0.3 && Math.abs((a + b) / 2) < half * 0.62 && Math.sign(a + b) === side);
    const c = col[0] || [U[U.length - 2], U[U.length - 1]];
    return (c[0] + c[1]) / 2 + R.range(-30, 30);
  }
  let oldTownCell = null;
  for (const c of cells) {
    const nz = noise(c.uc / 700, c.vc / 700);
    const nearRiver = riverDist(c.uc, c.vc) < 380;
    const railCol = railU !== null && railU > c.u0 && railU < c.u1;
    if (c.d < 0.17) c.type = 'downtown';
    else if (c.d < 0.36) c.type = 'midtown';
    else if (c.d < 0.62) c.type = railCol ? 'industrial' : nz > 0.38 && o.parks ? 'park' : nz < -0.45 ? 'industrial' : 'residential';
    else if (c.d < 0.86) c.type = railCol ? 'industrial' : nz > 0.3 && o.parks ? 'park' : o.suburbs ? 'suburb' : 'residential';
    else c.type = 'rural';
    if (railCol && (c.type === 'park' || c.type === 'suburb')) c.type = 'industrial';
    if (!oldTownCell && o.oldTown && c.type === 'midtown' && nearRiver && !railCol) { c.type = 'oldtown'; oldTownCell = c; }
    if (c.type === 'midtown' && o.parks && !railCol && R.chance(0.1)) c.type = 'park';
  }

  // ---------------------------------------------------------------- polylines (grid space)
  const polys = [];       // ground layer
  const paths = [];       // pedestrian layer
  const add = (pts, profile, extra = {}) => { const pl = { pts, profile, layer: 'ground', ...extra }; polys.push(pl); return pl; };
  // perimeter (rounded square)
  const rr = 300;
  const perim = roundedRect(-P, -P, P, P, rr, 25);
  add(perim, 'expressway', { name: 'Perimeter Parkway', cls: 'perimeter', closed: true });
  const urbanAt = (u, v) => Math.max(Math.abs(u), Math.abs(v)) / half < 0.8;
  const pickArterial = (k, n, axis, pos) => {
    if (axis === 'u' && o.tram && k === Math.floor(n / 2) - 1) return 'tramAvenue';
    if (Math.abs(pos) < 140) return 'boulevard';
    if (axis === 'v' && k === Math.floor(n / 2) + 1) return 'arterialBRT';
    if (Math.abs(pos) < half * 0.4) return 'avenueBike';
    return 'arterial4';
  };
  let nameIdx = Math.floor(R() * 20);
  U.forEach((u, k) => {
    const prof = pickArterial(k, U.length, 'u', u);
    const nm = ORD(k + 1);
    add(seg([u, -P - 1], [u, P + 1], 25), prof, { name: nm, cls: 'arterial', axis: 'u', at: u, ruralProfile: 'rural2' });
  });
  V.forEach((v, k) => {
    const prof = pickArterial(k, V.length, 'v', v);
    add(seg([-P - 1, v], [P + 1, v], 25), prof, { name: NAMES[(nameIdx++) % NAMES.length], cls: 'arterial', axis: 'v', at: v, ruralProfile: 'rural2' });
  });
  // diagonal boulevard through the quadrant opposite the railway
  let diagSeg = null;
  if (o.density >= 0.6) {
    const sgn = railU !== null ? (-Math.sign(railU) || 1) : 1;
    const closest = arr => arr.reduce((a, b) => Math.abs(b) < Math.abs(a) ? b : a);
    const U0 = closest(U), V0 = closest(V);
    const nextU = U.filter(u => sgn > 0 ? u > U0 : u < U0).sort((a, b) => Math.abs(a - U0) - Math.abs(b - U0))[0] ?? U0 + sgn * 420;
    const a = [(U0 + nextU) / 2, V0];
    const len = P * 1.6;
    const b = [a[0] + sgn * len / Math.SQRT2, a[1] - len / Math.SQRT2];
    const pts = seg(a, b, 25).filter(([u, v]) => Math.max(Math.abs(u), Math.abs(v)) < P + 40);
    add(pts, 'avenueBike', { name: 'Broadway Diagonal', cls: 'arterial', diagonal: true });
    diagSeg = [a, pts[pts.length - 1]];
  }
  // riverside drives
  if (riverUV) {
    for (const side of [-1, 1]) {
      const off = offsetPolyline(riverUV, side * rdOff).filter(([u, v]) => Math.abs(u) < P + 1 && Math.abs(v) < P + 1);
      if (off.length > 2) add(off, 'collector', { name: side < 0 ? 'North Riverside Drive' : 'South Riverside Drive', cls: 'riverside' });
      const prom = offsetPolyline(riverUV, side * (riverHalf + 3.3)).filter(([u, v]) => Math.abs(u) < P - 30 && Math.abs(v) < P - 30);
      if (prom.length > 2) paths.push({ pts: prom, profile: 'promenade', layer: 'path', name: (side < 0 ? 'North' : 'South') + ' Quay Promenade' });
    }
  }
  // locals per district cell
  const localNames = () => NAMES[(nameIdx++) % NAMES.length];
  const humps = [];
  for (const c of cells) cellStreets(c);
  function inner(c) { return { u0: c.u0, u1: c.u1, v0: c.v0, v1: c.v1, w: c.u1 - c.u0, h: c.v1 - c.v0 }; }
  function gridLines(c, bu, bv, profU, profV, opts = {}) {
    const b = inner(c);
    const nu = Math.max(1, Math.round(b.w / bu)), nv = Math.max(1, Math.round(b.h / bv));
    const us = [], vs = [];
    for (let k = 1; k < nu; k++) us.push(b.u0 + b.w * k / nu);
    for (let k = 1; k < nv; k++) vs.push(b.v0 + b.h * k / nv);
    const fu = us.filter(u => railU === null || Math.abs(u - railU) > 34);
    fu.forEach((u, k) => {
      const prof = typeof profU === 'function' ? profU(k, fu.length) : profU;
      const pts = seg([u, b.v0], [u, b.v1], 25);
      if (prof === 'oneway' && k % 2) pts.reverse();
      add(pts, prof, { name: localNames(), cls: 'local', cell: c });
    });
    vs.forEach((v, k) => {
      const prof = typeof profV === 'function' ? profV(k, vs.length) : profV;
      const pts = seg([b.u0, v], [b.u1, v], 25);
      if (prof === 'oneway' && k % 2) pts.reverse();
      add(pts, prof, { name: localNames(), cls: 'local', cell: c });
    });
    // mid-block alleys
    if (opts.alleys) {
      const ub = [b.u0, ...fu, b.u1], vb = [b.v0, ...vs, b.v1];
      for (let i = 0; i < ub.length - 1; i++) for (let j = 0; j < vb.length - 1; j++) {
        if (!R.chance(opts.alleys)) continue;
        if (ub[i + 1] - ub[i] > vb[j + 1] - vb[j]) { const vm = (vb[j] + vb[j + 1]) / 2; add(seg([ub[i], vm], [ub[i + 1], vm], 25), 'alley', { name: 'Alley', cls: 'alley', cell: c }); }
        else { const um = (ub[i] + ub[i + 1]) / 2; if (railU !== null && Math.abs(um - railU) < 30) continue; add(seg([um, vb[j]], [um, vb[j + 1]], 25), 'alley', { name: 'Alley', cls: 'alley', cell: c }); }
      }
    }
    return { us: fu, vs };
  }
  function cellStreets(c) {
    const b = inner(c);
    switch (c.type) {
      case 'downtown':
        gridLines(c, 118, 88, (k, n) => (k === Math.floor(n / 2) ? 'collector' : 'oneway'), (k, n) => (k === Math.floor(n / 2) ? 'collector' : 'oneway'), { alleys: 0.35 });
        break;
      case 'midtown':
        gridLines(c, 135, 100, (k, n) => (k % 2 ? 'local' : 'collector'), 'local', { alleys: 0.15 });
        break;
      case 'residential': {
        gridLines(c, 185, 80, k => (k % 3 === 1 ? 'collector' : 'residential'), 'residential');
        break;
      }
      case 'industrial':
        gridLines(c, 240, 190, 'industrial', 'industrial');
        break;
      case 'oldtown': oldTown(c); break;
      case 'suburb': suburb(c); break;
      case 'park': park(c); break;
      case 'rural': rural(c); break;
    }
  }
  function oldTown(c) {
    const b = inner(c), sp = 64;
    const nu = Math.max(2, Math.round(b.w / sp)), nv = Math.max(2, Math.round(b.h / sp));
    const pt = (i, j) => {
      let u = b.u0 + b.w * i / nu, v = b.v0 + b.h * j / nv;
      const onU = i === 0 || i === nu, onV = j === 0 || j === nv;
      if (!onU) u += R.range(-13, 13);
      if (!onV) v += R.range(-13, 13);
      return [u, v];
    };
    const G = [];
    for (let i = 0; i <= nu; i++) { G.push([]); for (let j = 0; j <= nv; j++) G[i].push(pt(i, j)); }
    for (let i = 0; i <= nu; i++) for (let j = 0; j <= nv; j++) {
      const onU = i === 0 || i === nu, onV = j === 0 || j === nv;
      if (i < nu && !onV && !(onU && false)) if (R.chance(0.85)) add([G[i][j], G[i + 1][j]], R.chance(0.3) ? 'sharedStreet' : 'cobblestone', { name: localNames(), cls: 'local', cell: c, old: true });
      if (j < nv && !onU) if (R.chance(0.85)) add([G[i][j], G[i][j + 1]], R.chance(0.3) ? 'sharedStreet' : 'cobblestone', { name: localNames(), cls: 'local', cell: c, old: true });
    }
    // a central piazza ring as a roundabout node is added after planarization
    c.piazza = G[Math.floor(nu / 2)][Math.floor(nv / 2)];
  }
  function suburb(c) {
    const b = inner(c);
    const ins = 70;
    const loop = roundedRect(b.u0 + ins, b.v0 + ins, b.u1 - ins, b.v1 - ins, 60, 18);
    for (let k = 0; k < loop.length; k++) { const q = loop[k]; q[0] += noise(q[0] / 120, q[1] / 120) * 12; q[1] += noise(q[1] / 120 + 9, q[0] / 120) * 12; }
    loop.push(loop[0].slice());
    // split the loop in four so it is a valid polyline chain
    const n4 = Math.floor(loop.length / 4);
    for (let q = 0; q < 4; q++) add(loop.slice(q * n4, q === 3 ? loop.length : (q + 1) * n4 + 1), 'residential', { name: localNames() + ' Loop', cls: 'local', cell: c });
    // connectors to the arterials at side midpoints
    const um = (b.u0 + b.u1) / 2 + R.range(-40, 40), vm = (b.v0 + b.v1) / 2 + R.range(-40, 40);
    const conn = [[[um, b.v0], [um, b.v0 + ins + 25]], [[um + R.range(-30, 30), b.v1], [um, b.v1 - ins - 25]], [[b.u0, vm], [b.u0 + ins + 25, vm]], [[b.u1, vm + R.range(-30, 30)], [b.u1 - ins - 25, vm]]];
    conn.forEach((s, k) => { if (k < 2 || R.chance(0.7)) add(seg(s[0], s[1], 15), 'residential', { name: localNames() + ' Drive', cls: 'local', cell: c }); });
    // cul-de-sacs pointing inward from the loop
    const innerU0 = b.u0 + ins, innerU1 = b.u1 - ins, innerV0 = b.v0 + ins, innerV1 = b.v1 - ins;
    const cu = (innerU0 + innerU1) / 2, cv = (innerV0 + innerV1) / 2;
    const maxLen = Math.min(innerU1 - innerU0, innerV1 - innerV0) * 0.32;
    for (let u = innerU0 + 75; u < innerU1 - 60; u += 95) {
      if (Math.abs(u - um) < 40) continue;
      for (const [v0, dir] of [[innerV0, 1], [innerV1, -1]]) {
        const len = Math.min(maxLen, R.range(55, 85));
        if (len < 35) continue;
        add(seg([u, v0], [u + R.range(-8, 8), v0 + dir * len], 12), 'residential', { name: localNames() + ' Court', cls: 'local', cell: c, culdesac: true });
      }
    }
    // outward cul-de-sacs from the loop toward the arterial side
    for (let v = innerV0 + 60; v < innerV1 - 50; v += 110) {
      if (Math.abs(v - vm) < 45) continue;
      for (const [u0, dir] of [[innerU0, -1], [innerU1, 1]]) {
        add(seg([u0, v], [u0 + dir * (ins - 32), v], 10), 'residential', { name: localNames() + ' Court', cls: 'local', cell: c, culdesac: true });
      }
    }
    c.cu = cu; c.cv = cv;
  }
  function park(c) {
    const b = inner(c);
    const ins = 34;
    const loop = roundedRect(b.u0 + ins, b.v0 + ins, b.u1 - ins, b.v1 - ins, 80, 20);
    for (const q of loop) { q[0] += noise(q[0] / 90, q[1] / 90) * 14; q[1] += noise(q[1] / 90 + 3, q[0] / 90) * 14; }
    loop.push(loop[0].slice());
    const n4 = Math.floor(loop.length / 4);
    for (let q = 0; q < 4; q++) paths.push({ pts: loop.slice(q * n4, q === 3 ? loop.length : (q + 1) * n4 + 1), profile: 'footpath', layer: 'path', name: 'Park Loop', cell: c });
    // meandering cross paths, entering at the arterial sidewalks
    const crossers = [[[b.u0 + 14, b.v0 + b.h * R.range(0.3, 0.7)], [b.u1 - 14, b.v0 + b.h * R.range(0.3, 0.7)]], [[b.u0 + b.w * R.range(0.3, 0.7), b.v0 + 14], [b.u0 + b.w * R.range(0.3, 0.7), b.v1 - 14]]];
    for (const [a, z] of crossers) {
      const pts = [];
      const n = 24;
      for (let k = 0; k <= n; k++) {
        const t = k / n; let u = a[0] + (z[0] - a[0]) * t, v = a[1] + (z[1] - a[1]) * t;
        const w = Math.sin(t * Math.PI) * 45;
        u += noise(u / 140 + 5, v / 140) * w; v += noise(v / 140, u / 140 + 2) * w;
        pts.push([u, v]);
      }
      paths.push({ pts, profile: R.chance(0.5) ? 'gravelPath' : 'footpath', layer: 'path', name: 'Park Trail', cell: c });
    }
    c.isPark = true;
  }
  function rural(c) {
    const b = inner(c);
    if (R.chance(0.55)) {
      const horizontal = R.chance(0.5);
      const a = horizontal ? [b.u0, b.v0 + b.h * R.range(0.3, 0.7)] : [b.u0 + b.w * R.range(0.3, 0.7), b.v0];
      const z = horizontal ? [b.u1, b.v0 + b.h * R.range(0.3, 0.7)] : [b.u0 + b.w * R.range(0.3, 0.7), b.v1];
      const pts = [];
      const n = 20;
      for (let k = 0; k <= n; k++) {
        const t = k / n; let u = a[0] + (z[0] - a[0]) * t, v = a[1] + (z[1] - a[1]) * t;
        const w = Math.sin(t * Math.PI) * 60;
        if (horizontal) v += noise(u / 220, v / 220) * w; else u += noise(v / 220, u / 220) * w;
        pts.push([u, v]);
      }
      add(pts, R.chance(0.6) ? 'countryLane' : 'gravelRoad', { name: localNames() + ' Lane', cls: 'rural', cell: c });
      // a dirt spur to nowhere (farm access)
      const m = pts[Math.floor(n / 2)];
      const dir = horizontal ? [0, R.chance(0.5) ? 1 : -1] : [R.chance(0.5) ? 1 : -1, 0];
      const len = R.range(80, 140);
      const spur = [];
      for (let k = 0; k <= 8; k++) { const t = k / 8; spur.push([m[0] + dir[0] * len * t + (dir[1] ? noise(t * 3, 1) * 15 : 0), m[1] + dir[1] * len * t + (dir[0] ? noise(t * 3, 2) * 15 : 0)]); }
      add(spur, 'dirtRoad', { name: 'Farm Track', cls: 'rural', cell: c, flush: true });
    }
  }

  // ---------------------------------------------------------------- freeway ring + interchanges
  const ramps = [];
  const polysFreeway = [];
  let ring = null;
  const exclusions = [];     // {type:'band'|'rect', ...} in grid space
  if (o.freeway) {
    const midsU = U.slice(0, -1).map((u, k) => (u + U[k + 1]) / 2);
    const midsV = V.slice(0, -1).map((v, k) => (v + V[k + 1]) / 2);
    const nearest = (arr, t) => arr.reduce((a, b) => Math.abs(b - t) < Math.abs(a - t) ? b : a);
    const t = half * 0.42;
    const uL = nearest(midsU, -t), uR = nearest(midsU, t), vT = nearest(midsV, -t), vB = nearest(midsV, t);
    const rc = 230;
    const loop = roundedRect(uL, vT, uR, vB, rc, 20);
    // traverse CCW in grid space (as generated), split at the 4 corner midpoints
    ring = { uL, uR, vT, vB, loop };
    const corners = [];
    for (let k = 0; k < loop.length; k++) if (loop[k].corner) corners.push(k);
    const L = loop.concat([loop[0]]);
    const cuts = corners.length === 4 ? corners : [0, Math.floor(L.length / 4), Math.floor(L.length / 2), Math.floor(L.length * 3 / 4)];
    for (let q = 0; q < 4; q++) {
      const a = cuts[q], b = q === 3 ? cuts[0] + loop.length : cuts[q + 1];
      const pts = [];
      for (let k = a; k <= b; k++) pts.push(loop[k % loop.length]);
      polysFreeway.push({ pts: pts.map(p => [p[0], p[1]]), profile: 'freeway6', layer: 'freeway', name: 'Ring Freeway', cls: 'freeway' });
    }
    // straight sides for exclusion bands + interchanges
    const sides = [
      { axis: 'v', at: vT, from: uL + rc, to: uR - rc, e: [1, 0] },
      { axis: 'u', at: uR, from: vT + rc, to: vB - rc, e: [0, 1] },
      { axis: 'v', at: vB, from: uR - rc, to: uL + rc, e: [-1, 0] },
      { axis: 'u', at: uL, from: vB - rc, to: vT + rc, e: [0, -1] },
    ];
    for (const s of sides) exclusions.push({ type: 'band', axis: s.axis, at: s.at, half: 30 });
    const fwHalf = layout(PROFILES.freeway6).total / 2, rampHalf = layout(PROFILES.ramp1).total / 2;
    const lf = fwHalf + rampHalf, lg = lf + 10;
    for (const s of sides) {
      const crossLines = s.axis === 'v' ? U : V;
      const lo = Math.min(s.from, s.to), hi = Math.max(s.from, s.to);
      let count = 0;
      for (const x of crossLines) {
        if (x < lo + 430 || x > hi - 430) continue;
        if (count >= 2) break;
        const P0 = s.axis === 'v' ? [x, s.at] : [s.at, x];
        if (riverDist(P0[0], P0[1]) < 520) continue;
        if (diagSeg && pointPolylineDist(P0[0], P0[1], diagSeg).d < 480) continue;
        if (railU !== null && s.axis === 'v' && Math.abs(x - railU) < 470) continue;
        if (railU !== null && s.axis === 'u' && Math.abs(s.at - railU) < 60) continue;
        if (count && R.chance(0.3)) continue;
        count++;
        const e = s.e, n = [-e[1], e[0]];   // right normal in grid space (x=u, z=v)
        const at = (sig, lat) => [P0[0] + e[0] * sig + n[0] * lat, P0[1] + e[1] * sig + n[1] * lat];
        const lat = sig => { const a = Math.abs(sig); if (a >= 350) return lf; if (a <= 260) return lg; return lf + (lg - lf) * smooth((350 - a) / 90); };
        const mk = (sigs, sideSgn) => sigs.map(sg => at(sg, sideSgn * lat(sg)));
        const sigOff = []; for (let sg = -420; sg <= 0; sg += 15) sigOff.push(sg);
        const sigOn = []; for (let sg = 0; sg <= 420; sg += 15) sigOn.push(sg);
        // +n side carries forward traffic: off before, on after; −n side mirrored
        ramps.push({ pts: mk(sigOff, 1), kind: 'off', side: 1, P0, e, n, name: 'Exit ' + (ramps.length / 4 + 1 | 0) });
        ramps.push({ pts: mk(sigOn, 1), kind: 'on', side: 1, P0, e, n, name: 'Entrance' });
        ramps.push({ pts: mk(sigOn.slice().reverse(), -1), kind: 'off', side: -1, P0, e, n, name: 'Exit' });
        ramps.push({ pts: mk(sigOff.slice().reverse(), -1), kind: 'on', side: -1, P0, e, n, name: 'Entrance' });
        exclusions.push({ type: 'rect', P0, e, n, along: 250, lat: lg + 20 });
      }
    }
  }
  // rail exclusion: no parallel locals in band (already filtered); rail line itself
  let railPts = null;
  if (railU !== null) {
    railPts = [];
    for (let v = -half - 200; v <= half + 200; v += 20) railPts.push([railU + 25 * Math.sin(v / 900), v]);
  }

  // ---------------------------------------------------------------- clip locals in exclusion zones / river band
  if (diagSeg) exclusions.push({ type: 'seg', a: diagSeg[0], b: diagSeg[1], half: 42 });
  const inExcl = (u, v, pl) => {
    if (pl.cls === 'arterial' || pl.cls === 'perimeter') return false;
    for (const x of exclusions) {
      if (x.type === 'seg') {
        if (pl.cls !== 'riverside' && pointPolylineDist(u, v, [x.a, x.b]).d < x.half) return true;
        continue;
      }
      if (x.type === 'band') {
        if (pl.cls === 'riverside') continue;
        // only remove streets running parallel to the band
        const d = x.axis === 'v' ? Math.abs(v - x.at) : Math.abs(u - x.at);
        if (d < x.half && pl._dirAxis === x.axis) return true;
      } else {
        const du = u - x.P0[0], dv = v - x.P0[1];
        const a = du * x.e[0] + dv * x.e[1], l = du * x.n[0] + dv * x.n[1];
        if (Math.abs(a) < x.along && Math.abs(l) < x.lat) return true;
      }
    }
    if (riverUV && pl.cls !== 'riverside' && riverDist(u, v) < rdOff - 3) return true;
    return false;
  };
  const clipped = [];
  for (const pl of polys) {
    if (pl.cls === 'arterial' || pl.cls === 'perimeter') { clipped.push(pl); continue; }
    const a = pl.pts[0], b = pl.pts[pl.pts.length - 1];
    pl._dirAxis = Math.abs(b[0] - a[0]) > Math.abs(b[1] - a[1]) ? 'v' : 'u';
    const dense = resample(pl.pts, 4);
    let run = [];
    const flush = (clippedEnd) => {
      if (run.length > 1 && polylineLength(run) > 25) clipped.push({ ...pl, pts: simplify(run), clippedStart: run._cs, clippedEnd });
      run = [];
    };
    for (let k = 0; k < dense.length; k++) {
      const [u, v] = dense[k];
      if (inExcl(u, v, pl)) { if (run.length) flush(true); run._cs = true; }
      else { if (!run.length) { run._cs = k > 0; } run.push([u, v]); }
    }
    flush(false);
  }
  // paths: clip against river and roads later (planarize separately)
  const pathPolys = paths.flatMap(pl => {
    const dense = resample(pl.pts, 4), out = [];
    let run = [];
    for (const [u, v] of dense) {
      const bad = (riverUV && riverDist(u, v) < riverHalf + 1.2) || inExcl(u, v, { cls: 'path' });
      if (bad) { if (run.length > 1) out.push({ ...pl, pts: simplify(run) }); run = []; } else run.push([u, v]);
    }
    if (run.length > 1) out.push({ ...pl, pts: simplify(run) });
    return out.filter(q => polylineLength(q.pts) > 20);
  });

  // ---------------------------------------------------------------- planarize ground layer
  const G = planarize(clipped, { tol: 2 });
  // edges inside the river band (between riverside drives) are only kept for arterials (bridges)
  let edges = G.edges.filter(e => {
    if (!riverUV) return true;
    if (e.src.cls === 'arterial' || e.src.cls === 'perimeter') return true;
    const A = G.nodes[e.a], B = G.nodes[e.b];
    const mid = e.pts.length ? e.pts[Math.floor(e.pts.length / 2)] : [(A.x + B.x) / 2, (A.z + B.z) / 2];
    return e.src.cls === 'riverside' || riverDist(mid[0], mid[1]) > rdOff - 2;
  }).filter(e => {
    if (e.src.cls === 'perimeter') return true;
    const A = G.nodes[e.a], B = G.nodes[e.b];
    const mid = e.pts.length ? e.pts[Math.floor(e.pts.length / 2)] : [(A.x + B.x) / 2, (A.z + B.z) / 2];
    const au = Math.abs(mid[0]), av = Math.abs(mid[1]);
    if (au > P - 2 || av > P - 2) return false;
    if (au > P - rr && av > P - rr) return Math.hypot(au - (P - rr), av - (P - rr)) < rr - 2;
    return true;
  });
  // perimeter: profile per class; arterial rural segments become rural roads
  const nodeMap = new Map();
  const ensureNode = gn => {
    let n = nodeMap.get(gn.id);
    if (!n) { const [x, z] = warp(gn.x, gn.z); n = net.addNode({ x, z, y: 0 }); n._uv = [gn.x, gn.z]; nodeMap.set(gn.id, n); }
    return n;
  };
  // collapse very short edges
  edges = collapseShort(G.nodes, edges, 16);
  for (const e of edges) {
    const A = G.nodes[e.a], B = G.nodes[e.b];
    if (e.a === e.b) continue;
    const pts = [[A.x, A.z], ...e.pts, [B.x, B.z]];
    const len = polylineLength(pts);
    if (len < 1) continue;
    let profile = e.src.profile;
    const mid = pts[Math.floor(pts.length / 2)];
    if (e.src.ruralProfile && !urbanAt(mid[0], mid[1])) profile = e.src.ruralProfile;
    if (e.src.diagonal && !urbanAt(mid[0], mid[1])) profile = 'rural2';
    const na = ensureNode(A), nb = ensureNode(B);
    const dense = resample(pts, 30).slice(1, -1).map(([u, v]) => { const [x, z] = warp(u, v); return { x, z }; });
    const name = e.src.name ? (e.src.name + (e.src.cls === 'perimeter' || e.src.name.endsWith('Loop') || e.src.name.endsWith('Drive') || e.src.name.endsWith('Court') || e.src.name.endsWith('Lane') || e.src.name.includes('Riverside') || e.src.name === 'Alley' || e.src.name.includes('Diagonal') || e.src.name.includes('Track') ? '' : ' ' + (SUFFIX[profile] || 'Street'))) : '';
    const edge = net.addEdge({ a: na.id, b: nb.id, profile, pts: dense, name });
    edge._uvMid = mid; edge._cls = e.src.cls; edge._len = len;
    if (e.src.culdesac && e.src.pts) { const endUV = e.src.pts[e.src.pts.length - 1]; if (Math.hypot(endUV[0] - B.x, endUV[1] - B.z) < 3) nb.end = 'culdesac'; }
    if (e.src.flush) { const endUV = e.src.pts[e.src.pts.length - 1]; if (Math.hypot(endUV[0] - B.x, endUV[1] - B.z) < 3) nb.end = 'flush'; }
    if (e.src.clippedStart) { const s = e.src.pts[0]; if (Math.hypot(s[0] - A.x, s[1] - A.z) < 3) na.end = 'culdesac'; }
    if (e.src.clippedEnd) { const s = e.src.pts[e.src.pts.length - 1]; if (Math.hypot(s[0] - B.x, s[1] - B.z) < 3) nb.end = 'culdesac'; }
  }
  // bridges: arterial edges crossing the river get a gentle hump
  if (riverUV) {
    for (const e of net.edges.values()) {
      if (e._cls !== 'arterial') continue;
      const A = net.nodes.get(e.a), B = net.nodes.get(e.b);
      const dA = riverDist(...A._uv), dB = riverDist(...B._uv);
      const m = e._uvMid; if (!m) continue;
      if (riverDist(m[0], m[1]) < riverHalf && dA > riverHalf && dB > riverHalf) {
        const k = Math.floor(e.pts.length / 2);
        if (e.pts.length >= 3) e.pts[k].y = 1.6;
        else if (e.pts.length) e.pts[0].y = 1.6;
        e.flags.bridge = true;
      }
    }
  }
  // ---------------------------------------------------------------- freeway + ramps (grade-separated)
  if (o.freeway && ring) {
    const fwNodes = [];
    const segs = polysFreeway;
    const FW_Y = 9;
    const endNode = (uv) => {
      for (const n of fwNodes) if (Math.hypot(n._uv[0] - uv[0], n._uv[1] - uv[1]) < 1) return n;
      const [x, z] = warp(uv[0], uv[1]);
      const n = net.addNode({ x, z, y: FW_Y }); n._uv = uv; fwNodes.push(n); return n;
    };
    const fwEdges = [];
    for (const s of segs) {
      const a = endNode(s.pts[0]), b = endNode(s.pts[s.pts.length - 1]);
      const dense = resample(s.pts, 30).slice(1, -1).map(([u, v]) => { const [x, z] = warp(u, v); return { x, z }; });
      fwEdges.push(net.addEdge({ a: a.id, b: b.id, profile: 'freeway6', pts: dense, name: 'Ring Freeway (I-9)' }));
    }
    // ramps
    const index = { edges: fwEdges };
    for (const r of ramps) {
      const uvPts = r.pts;
      const endUV = r.kind === 'off' ? uvPts[uvPts.length - 1] : uvPts[0];     // on the arterial
      const flushUV = r.kind === 'off' ? uvPts[0] : uvPts[uvPts.length - 1];
      // attach arterial end by splitting the arterial edge nearest to that point
      const [ex, ez] = warp(endUV[0], endUV[1]);
      const host = splitNearest(net, ex, ez, e => e._cls === 'arterial');
      if (!host) continue;
      const [fx, fz] = warp(flushUV[0], flushUV[1]);
      const flushNode = net.addNode({ x: fx, z: fz, y: FW_Y, end: 'flush' });
      const world = uvPts.map(([u, v]) => { const [x, z] = warp(u, v); return { x, z }; });
      // vertical keys: flat at freeway level for |σ|>=330, ground for |σ|<=95
      const sig = uvPts.map(([u, v]) => (u - r.P0[0]) * r.e[0] + (v - r.P0[1]) * r.e[1]);
      const inner = world.slice(1, -1).map((p, k) => {
        const a = Math.abs(sig[k + 1]);
        const q = { x: p.x, z: p.z };
        if (Math.abs(a - 330) < 7.6) q.y = FW_Y;
        if (Math.abs(a - 95) < 7.6) q.y = 0;
        return q;
      });
      const isOff = r.kind === 'off';
      const edge = net.addEdge({ a: isOff ? flushNode.id : host.id, b: isOff ? host.id : flushNode.id, profile: 'ramp1', pts: inner, name: (isOff ? 'Exit ramp' : 'On-ramp') + ' · ' + (net.edges.get([...net.adj.get(host.id)][0])?.name || '') });
      edge._cls = 'ramp';
      const c = net.centerline(edge);
      // ramp's freeway-side barrier is lane 0 (driver's left); gap it along the flush section
      edge.features.push(isOff ? { type: 'barrierGap', lane: 0, s0: -1, s1: 92 } : { type: 'barrierGap', lane: 0, s0: c.length - 92, s1: c.length + 1 });
      // matching gap in the freeway outer barrier
      const fp = isOff ? c.frameAt(0) : c.frameAt(c.length), fq = isOff ? c.frameAt(95) : c.frameAt(c.length - 95);
      let best = null;
      for (const fe of fwEdges) { const fc = net.centerline(fe), q0 = fc.nearest(fp.x, fp.z), q1 = fc.nearest(fq.x, fq.z); if (!best || q0.d < best.d) best = { fe, d: q0.d, s0: q0.s, s1: q1.s, fc }; }
      if (best) {
        const fc = best.fc, f = fc.frameAt(best.s0);
        const sideSign = Math.sign((fp.x - f.x) * f.nx + (fp.z - f.z) * f.nz);
        const lanes = PROFILES.freeway6.lanes.length;
        best.fe.features.push({ type: 'barrierGap', lane: sideSign > 0 ? lanes - 1 : 0, s0: Math.min(best.s0, best.s1) - 2, s1: Math.max(best.s0, best.s1) + 1 });
      }
    }
  }
  // prune short dead-end stubs left by clipping / connectors overshooting loops
  for (let pass = 0; pass < 2; pass++) {
    for (const n of [...net.nodes.values()]) {
      const es = net.nodeEdges(n.id);
      if (es.length !== 1 || n.end === 'flush' || es[0]._cls === 'ramp') continue;
      if (net.centerline(es[0]).length < 38) { net.removeEdge(es[0].id); net.removeNode(n.id); }
    }
    for (const n of [...net.nodes.values()]) if (!net.nodeEdges(n.id).length) net.removeNode(n.id);
  }
  // ---------------------------------------------------------------- controls, roundabouts, ends
  for (const n of net.nodes.values()) {
    const es = net.nodeEdges(n.id);
    if (es.length === 1 && !n.end) {
      const pr = PROFILES[es[0].profile];
      n.end = pr.category === 'active' || pr.category === 'rural' ? 'flush' : (layout(pr).hasCurbL ? 'culdesac' : 'flush');
    }
    if (o.roundabouts && es.length === 4 && n._uv) {
      const d = Math.max(Math.abs(n._uv[0]), Math.abs(n._uv[1])) / half;
      const allArt = es.every(e => e._cls === 'arterial');
      if (allArt && d > 0.45 && d < 0.85 && R.chance(0.45)) n.kind = 'roundabout';
    }
    if (o.roundabouts && n._uv && es.length >= 3 && es.every(e => e._cls === 'local' && (e.profile === 'cobblestone' || e.profile === 'sharedStreet'))) {
      for (const c of cells) if (c.piazza && Math.hypot(c.piazza[0] - n._uv[0], c.piazza[1] - n._uv[1]) < 30 && !c.piazzaNode) { n.kind = 'roundabout'; n.rbRadius = 17; c.piazzaNode = n; }
    }
  }
  // ---------------------------------------------------------------- features: humps, bus stops, crossings
  for (const e of net.edges.values()) {
    const c = net.centerline(e), L = c.length;
    if (e.profile === 'residential' && L > 130 && R.chance(0.7)) e.features.push({ type: 'hump', s: L * R.range(0.4, 0.6), kind: 'hump' });
    if (e.profile === 'local' && L > 110 && R.chance(0.25)) e.features.push({ type: 'hump', s: L * 0.5, kind: 'table' });
    if (e.profile === 'cobblestone' && L > 60 && R.chance(0.2)) e.features.push({ type: 'hump', s: L * 0.5, kind: 'table' });
    if ((e.profile === 'boulevard' || e.profile === 'tramAvenue') && L > 220) { e.features.push({ type: 'busstop', s: L * 0.38, side: 1 }); e.features.push({ type: 'busstop', s: L * 0.62, side: -1 }); }
    if (e.profile === 'collector' && L > 230 && R.chance(0.5)) e.features.push({ type: 'crosswalk', s: L * 0.5 });
  }

  // ---------------------------------------------------------------- pedestrian paths (separate layer)
  if (pathPolys.length) {
    const PG = planarize(pathPolys, { tol: 1.5 });
    const pmap = new Map();
    const pnode = gn => { let n = pmap.get(gn.id); if (!n) { const [x, z] = warp(gn.x, gn.z); n = net.addNode({ x, z, y: 0, end: 'flush' }); pmap.set(gn.id, n); } return n; };
    for (const e of collapseShort(PG.nodes, PG.edges, 8)) {
      if (e.a === e.b) continue;
      const A = PG.nodes[e.a], B = PG.nodes[e.b];
      const pts = [[A.x, A.z], ...e.pts, [B.x, B.z]];
      if (polylineLength(pts) < 6) continue;
      const dense = resample(pts, 12).slice(1, -1).map(([u, v]) => { const [x, z] = warp(u, v); return { x, z }; });
      const edge = net.addEdge({ a: pnode(A).id, b: pnode(B).id, profile: e.src.profile, pts: dense, name: e.src.name });
      edge._cls = 'path';
    }
  }

  // ---------------------------------------------------------------- river, rail, subway (world space)
  if (riverUV) net.river = { pts: riverUV.map(([u, v]) => warp(u, v)), width: riverHalf * 2, name: 'Frontier River' };
  if (railPts) {
    const pts = railPts.map(([u, v]) => { const [x, z] = warp(u, v); return { x, z }; });
    net.rails.set('rail1', { id: 'rail1', kind: 'rail', tracks: 2, pts, name: 'Harbor Line (Freight & Commuter)' });
  }
  if (o.subway) {
    const lineAlong = (axis, at, color, name, depth) => {
      const pts = [];
      const lim = half * 0.62;
      for (let t = -lim; t <= lim; t += 20) { const [x, z] = axis === 'u' ? warp(at, t) : warp(t, at); pts.push({ x, z }); }
      const stations = [];
      const crosses = axis === 'u' ? V : U;
      let acc = 0;
      const S = [0]; for (let k = 1; k < pts.length; k++) { acc += Math.hypot(pts[k].x - pts[k - 1].x, pts[k].z - pts[k - 1].z); S.push(acc); }
      for (const cx of crosses) {
        if (Math.abs(cx) > lim - 120) continue;
        const k = Math.round((cx + lim) / 20);
        if (k > 0 && k < pts.length) stations.push({ s: S[k], name: (axis === 'u' ? NAMES[(Math.abs(Math.round(cx)) >> 4) % NAMES.length] : ORD(Math.abs(Math.round(cx / 97)) + 1)) + ' Station' });
      }
      return { id: 's' + name.replace(/\W/g, ''), pts, depth, stations, name, color };
    };
    const ua = U.reduce((a, b) => Math.abs(b) < Math.abs(a) ? b : a), va = V.reduce((a, b) => Math.abs(b) < Math.abs(a) ? b : a);
    const l1 = lineAlong('u', ua, '#d8584a', 'Line 1 · Red', 15);
    const l2 = lineAlong('v', va, '#4a8fd8', 'Line 2 · Blue', 26);
    net.subways.set(l1.id, l1); net.subways.set(l2.id, l2);
  }
  // park + rural vegetation, district raster for ground shading
  net.trees = [];
  for (const c of cells) {
    if (c.type === 'park') {
      for (let k = 0; k < 140; k++) {
        const u = R.range(c.u0 + 30, c.u1 - 30), v = R.range(c.v0 + 30, c.v1 - 30);
        if (pathPolys.some(pl => pl.cell === c && pointPolylineDist(u, v, pl.pts).d < 6)) continue;
        if (riverUV && riverDist(u, v) < rdOff + 15) continue;
        const [x, z] = warp(u, v); net.trees.push([x, z, R.range(0.8, 1.5), R.chance(0.5) ? 0 : 1]);
      }
    } else if (c.type === 'rural') {
      const clusters = R.int(1, 3);
      for (let q = 0; q < clusters; q++) {
        const cu = R.range(c.u0 + 60, c.u1 - 60), cv = R.range(c.v0 + 60, c.v1 - 60);
        for (let k = 0; k < 40; k++) {
          const u = cu + R.range(-55, 55), v = cv + R.range(-55, 55);
          if (polys.some(pl => pl.cell === c && pointPolylineDist(u, v, pl.pts).d < 10)) continue;
          const [x, z] = warp(u, v); net.trees.push([x, z, R.range(0.9, 1.7), 1]);
        }
      }
    }
  }
  const RES = 256, ext = half * 1.6;
  const raster = new Uint8Array(RES * RES);
  const typeId = { downtown: 1, midtown: 2, oldtown: 3, residential: 4, industrial: 5, park: 6, suburb: 7, rural: 8 };
  for (let j = 0; j < RES; j++) for (let i = 0; i < RES; i++) {
    const x = -ext + (i + 0.5) / RES * ext * 2, z = -ext + (j + 0.5) / RES * ext * 2;
    const [u, v] = unwarp(x, z);
    let t = 8;
    if (Math.abs(u) < P && Math.abs(v) < P) { const c = cells.find(c => u >= c.u0 && u < c.u1 && v >= c.v0 && v < c.v1); if (c) t = typeId[c.type]; }
    raster[j * RES + i] = t;
  }
  net.districtRaster = { res: RES, ext, data: raster, types: Object.keys(typeId) };
  net.districts = cells.map(c => ({ type: c.type, center: warp(c.uc, c.vc) }));
  // tidy temp fields
  for (const n of net.nodes.values()) delete n._uv;
  for (const e of net.edges.values()) { delete e._uvMid; delete e._len; }
  return net;
}


// ---------------------------------------------------------------- helpers
function seg(a, b, step) {
  const L = Math.hypot(b[0] - a[0], b[1] - a[1]);
  const n = Math.max(1, Math.round(L / step));
  const out = [];
  for (let k = 0; k <= n; k++) out.push([a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n]);
  return out;
}

function roundedRect(u0, v0, u1, v1, r, step) {
  const out = [];
  const arc = (cu, cv, a0) => {
    const n = Math.max(4, Math.ceil(Math.PI / 2 * r / step));
    for (let k = 0; k <= n; k++) {
      const a = a0 + Math.PI / 2 * k / n;
      const p = [cu + Math.cos(a) * r, cv + Math.sin(a) * r];
      if (k === Math.floor(n / 2)) p.corner = true;
      out.push(p);
    }
  };
  const line = (a, b) => { const s = seg(a, b, step); s.shift(); s.pop(); out.push(...s); };
  // CCW in (u, v) with v down = going: top edge left→right is clockwise visually; keep consistent order
  arc(u1 - r, v0 + r, -Math.PI / 2);         // top-right
  line([u1, v0 + r], [u1, v1 - r]);
  arc(u1 - r, v1 - r, 0);                    // bottom-right
  line([u1 - r, v1], [u0 + r, v1]);
  arc(u0 + r, v1 - r, Math.PI / 2);          // bottom-left
  line([u0, v1 - r], [u0, v0 + r]);
  arc(u0 + r, v0 + r, Math.PI);              // top-left
  line([u0 + r, v0], [u1 - r, v0]);
  return out;
}

function offsetPolyline(pts, d) {
  return pts.map((p, i) => {
    const a = pts[Math.max(0, i - 1)], b = pts[Math.min(pts.length - 1, i + 1)];
    const dx = b[0] - a[0], dz = b[1] - a[1], l = Math.hypot(dx, dz) || 1;
    return [p[0] - dz / l * d, p[1] + dx / l * d];
  });
}

function simplify(pts) {
  // drop nearly collinear points while keeping curvature
  if (pts.length < 3) return pts;
  const out = [pts[0]];
  for (let i = 1; i < pts.length - 1; i++) {
    const a = out[out.length - 1], b = pts[i], c = pts[i + 1];
    const cross = Math.abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]));
    if (cross > 2 || Math.hypot(b[0] - a[0], b[1] - a[1]) > 30) out.push(b);
  }
  out.push(pts[pts.length - 1]);
  return out;
}

function collapseShort(nodes, edges, minLen) {
  const parent = nodes.map((_, i) => i);
  const find = i => { while (parent[i] !== i) i = parent[i] = parent[parent[i]]; return i; };
  for (const e of edges) {
    const A = nodes[e.a], B = nodes[e.b];
    const pts = [[A.x, A.z], ...e.pts, [B.x, B.z]];
    if (polylineLength(pts) < minLen) {
      const ra = find(e.a), rb = find(e.b);
      if (ra !== rb) { parent[rb] = ra; nodes[ra].x = (nodes[ra].x + nodes[rb].x) / 2; nodes[ra].z = (nodes[ra].z + nodes[rb].z) / 2; }
    }
  }
  const seen = new Set();
  return edges.map(e => ({ ...e, a: find(e.a), b: find(e.b) })).filter(e => {
    if (e.a === e.b) return false;
    const k = (e.a < e.b ? e.a + ':' + e.b : e.b + ':' + e.a) + ':' + e.pts.length;
    if (seen.has(k)) return false; seen.add(k); return true;
  });
}

// Split the nearest qualifying edge at the projection of (x,z); returns the new node.
export function splitNearest(net, x, z, filter = () => true, maxDist = 40) {
  let best = null;
  for (const e of net.edges.values()) {
    if (!filter(e)) continue;
    const c = net.centerline(e);
    // quick reject by control bbox
    const q = c.nearest(x, z);
    if (q.d < maxDist && (!best || q.d < best.q.d)) best = { e, q, c };
  }
  if (!best) return null;
  return splitEdgeAt(net, best.e, best.q.s);
}

export function splitEdgeAt(net, e, s) {
  const c = net.centerline(e);
  const L = c.length;
  if (s < 2) return net.nodes.get(e.a);
  if (s > L - 2) return net.nodes.get(e.b);
  const f = c.frameAt(s);
  const n = net.addNode({ x: f.x, z: f.z, y: f.y });
  // partition control points by arclength
  const ctrl = net.controlPoints(e);
  const cs = c.ctrlS;
  const left = [], right = [];
  for (let i = 1; i < ctrl.length - 1; i++) {
    if (cs[i] < s - 3) left.push(e.pts[i - 1]); else if (cs[i] > s + 3) right.push(e.pts[i - 1]);
  }
  const base = { profile: e.profile, custom: e.custom, name: e.name, flags: { ...e.flags } };
  const e1 = net.addEdge({ ...base, a: e.a, b: n.id, pts: left, features: e.features.filter(ft => ft.s < s - 1).map(ft => ({ ...ft })) });
  const e2 = net.addEdge({ ...base, a: n.id, b: e.b, pts: right, features: e.features.filter(ft => ft.s > s + 1).map(ft => ({ ...ft, s: ft.s - s, ...(ft.s0 !== undefined ? { s0: ft.s0 - s, s1: ft.s1 - s } : {}) })) });
  e1._cls = e._cls; e2._cls = e._cls;
  net.removeEdge(e.id);
  return n;
}

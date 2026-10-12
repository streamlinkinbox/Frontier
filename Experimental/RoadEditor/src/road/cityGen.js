// Procedural city road network for large open-world driving maps.
// Everything is laid out in a planar "grid space" (u, v) and mapped to the world through a
// smooth, orientation-preserving warp — grids stay coherent while streets gain organic curvature.
import { RoadNetwork } from './network.js';
import { rng, makeNoise, smooth, resample, polylineLength, pointPolylineDist } from './math.js';
import { planarize } from './planarize.js';
import { PROFILES, layout, getProfile } from './profiles.js';
import { ARCHETYPES, mergeEnd, hermitePts, arcPts, edgeOffset } from './interchange.js';

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
  const sites = [];
  let it_name = 1;
  let fwLf = 0, fwLg = 0, icGap = 340, icRoom = true;
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
    const fwHalf = layout(PROFILES.freeway6).total / 2;
    fwLf = fwHalf + layout(PROFILES.rampLoop).total / 2; fwLg = fwLf + 10;
    // ramp offsets are dimensioned for a ~340 m block; scale them to this grid so an interchange
    // never reaches past the crossing it belongs to
    const medGap = a => { const d = []; for (let i = 1; i < a.length; i++) d.push(a[i] - a[i - 1]); d.sort((x, y) => x - y); return d[d.length >> 1] || 340; };
    icGap = Math.min(medGap(U), medGap(V));
    // A grade-separated interchange needs roughly 250 m between cross streets: that is what it takes
    // to drop from the deck to the grid at a legal grade. Below that a small map gets the freeway
    // without ramps rather than a ramp that cannot be built to standard.
    const icK = Math.max(0.62, Math.min(1.3, icGap / 340));
    icRoom = icGap >= 240;
    for (const s of sides) {
      const crossLines = s.axis === 'v' ? U : V;
      const lo = Math.min(s.from, s.to), hi = Math.max(s.from, s.to);
      let count = 0;
      // keep sites clear of the ring's corners and of the river / diagonal / railway, but scale those
      // margins with the map, otherwise a small city simply gets no interchange at all
      if (!icRoom) break;
      const margin = Math.max(90, Math.min(430, (hi - lo) * 0.17));
      const clr = Math.max(130, Math.min(520, (hi - lo) * 0.2));
      for (const x of crossLines) {
        if (x < lo + margin || x > hi - margin) continue;
        if (count >= 2) break;
        const P0 = s.axis === 'v' ? [x, s.at] : [s.at, x];
        if (riverDist(P0[0], P0[1]) < clr) continue;
        if (diagSeg && pointPolylineDist(P0[0], P0[1], diagSeg).d < clr) continue;
        if (railU !== null && s.axis === 'v' && Math.abs(x - railU) < clr) continue;
        if (railU !== null && s.axis === 'u' && Math.abs(s.at - railU) < 60) continue;
        if (count && R.chance(0.3)) continue;
        count++;
        const e = s.e, n = [-e[1], e[0]];   // right normal in grid space (x=u, z=v)
        // Inner sites get a full archetype (loops need quadrant land); outer ones stay simple.
        const rad = Math.hypot(P0[0], P0[1]);
        const type = rad < half * 0.46
          ? (R.chance(0.45) ? 'cloverleaf' : R.chance(0.55) ? 'parclo' : 'turbine')
          : (R.chance(0.7) ? 'diamond' : 'cloverleaf');
        const plan = ARCHETYPES[type];
        sites.push({ P0, e, n, axis: s.axis, type, name: it_name++, k: icK });
        // nothing else gets built inside the freeway's own corridor, or in the quadrants
        exclusions.push({ type: 'rect', P0, e, n, along: 250 + 150 * icK, lat: fwLg + 20 + 55 * icK });
        // a loop belongs in a clear quadrant, so the four corners of the crossing are reserved too
        if (plan.R) for (const q of [[1, 1], [1, -1], [-1, 1], [-1, -1]]) {
          const Rq = plan.R * icK, ac = 20 + Rq, lc = fwLg + Rq;
          const Q = [P0[0] + e[0] * q[0] * ac + n[0] * q[1] * lc, P0[1] + e[1] * q[0] * ac + n[1] * q[1] * lc];
          exclusions.push({ type: 'rect', P0: Q, e, n, along: Rq + 52, lat: Rq + 36 });
        }
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
    const FW_Y = 7;   // every ramp has to lose this height; at 9 m the grades stop being buildable inside a city block
    const endNode = (uv) => {
      for (const n of fwNodes) if (Math.hypot(n._uv[0] - uv[0], n._uv[1] - uv[1]) < 1) return n;
      const [x, z] = warp(uv[0], uv[1]);
      const n = net.addNode({ x, z, y: FW_Y }); n._uv = uv; fwNodes.push(n); return n;
    };
    const fwEdges = [];
    const fwEdgeSet = new Set();
    for (const s of segs) {
      const a = endNode(s.pts[0]), b = endNode(s.pts[s.pts.length - 1]);
      const dense = resample(s.pts, 30).slice(1, -1).map(([u, v]) => { const [x, z] = warp(u, v); return { x, z }; });
      const fe = net.addEdge({ a: a.id, b: b.id, profile: 'freeway6', pts: dense, name: 'Ring Freeway (I-9)' });
      fwEdges.push(fe); fwEdgeSet.add(fe.id);
    }
    // ---- interchanges -------------------------------------------------------
    // Ramps are sampled from the world centreline of the roadway they join, so the blend lands on
    // a measurable nose: [tip | flush | flare] + body + [flare | flush | tip], the cross-section
    // pinched onto its inboard line at each tip. geom/merge.js paves and marks every gore.
    const halfOf = pr => layout(PROFILES[pr] || PROFILES.local).total / 2;
    const nearestOn = (list, x, z) => {
      let b = null;
      for (const e of list) {
        const c = net.centerline(e), q = c.nearest(x, z);
        if (!b || q.d < b.d) b = { e, c, s: q.s, d: q.d, f: c.frameAt(q.s) };
      }
      return b;
    };
    const latOf = (f, x, z) => (x - f.x) * f.nx + (z - f.z) * f.nz;
    const dirOf = (f, th) => (f.tx * Math.cos(th) + f.tz * Math.sin(th)) > 0 ? 1 : -1;
    const edgeLat = (e, sg) => edgeOffset(layout(getProfile(e)), sg);
    const clampN = (v, a, b) => (v < a ? a : v > b ? b : v);
    // A ramp's tail ties into a node on the street it lands on. Reuse a node that is already there
    // rather than splitting again: two splits close together leave a stub too short for its
    // junctions, and the split is what the "segment too short" warning is about.
    const attachNode = (e, st, f) => {
      for (const id of [e.a, e.b]) {
        const n = net.nodes.get(id);
        if (n && Math.hypot(n.x - f.x, n.z - f.z) < 95) return n;
      }
      const c = net.centerline(e), L = c.length;
      if (L < 200) return null;
      return splitEdgeAt(net, e, clampN(st, 80, L - 80));
    };
    const edgeBox = new Map();
    const boxOf = e => {
      let b = edgeBox.get(e);
      if (b) return b;
      const c = net.centerline(e);
      b = { x0: 1e9, x1: -1e9, z0: 1e9, z1: -1e9 };
      for (let k = 0; k < c.X.length; k++) {
        if (c.X[k] < b.x0) b.x0 = c.X[k]; if (c.X[k] > b.x1) b.x1 = c.X[k];
        if (c.Z[k] < b.z0) b.z0 = c.Z[k]; if (c.Z[k] > b.z1) b.z1 = c.Z[k];
      }
      edgeBox.set(e, b); return b;
    };
    // the lane whose hardware must stand down while a ramp merges: the barrier, else the walk
    const gapLaneOf = (e, sg) => {
      const lay = layout(getProfile(e));
      const side = lay.lanes.filter(l => (sg > 0 ? l.o1 >= 0 : l.o0 <= 0));
      if (!side.length) return -1;
      const bar = side.filter(l => l.type === 'barrier');
      return (bar.length ? bar[0] : side[side.length - 1]).i;
    };
    // the anchored road's outer barrier (or its kerb run) has to stand down for the width of the
    // ramp's flare, otherwise a wall stands in the gore: same reason real drawings end it at the nose
    const gapOn = (anchor, nose, dirSide, flare, mode) => {
      const lane = gapLaneOf(anchor, dirSide);
      if (lane < 0 || !anchor.features) return;
      const open = mode === 'out' ? -1 : 1;                  // which side of the nose the flare is on
      const a = nose + open * dirSide * flare * 0.85, b = nose - open * dirSide * 2;
      anchor.features.push({ type: 'barrierGap', lane, s0: Math.min(a, b) - 1, s1: Math.max(a, b) + 1 });
    };
    {                                                    // ---- the deck dips at each interchange
      const IC_Y = 5.6;
      const lvl = (x, z) => {
        let t = 0;
        for (const q of sites) {
          const [ax, az] = warp(q.P0[0], q.P0[1]);
          const d = Math.hypot(x - ax, z - az);
          t = Math.max(t, 1 - clampN((d - 150) / 250, 0, 1));
        }
        return FW_Y - (FW_Y - IC_Y) * t;
      };
      for (const e of fwEdges) {
        for (const p of e.pts) p.y = lvl(p.x, p.z);
        for (const id of [e.a, e.b]) { const nd = net.nodes.get(id); if (nd) nd.y = lvl(nd.x, nd.z); }
      }
      net.invalidate(fwEdges.map(e => e.id));
    }
    net.meta.icLog = ['sites: ' + sites.length + ' (' + sites.map(x => x.type).join(',') + ')' + (sites.length || !icRoom
      ? (icRoom ? '' : ' - street spacing ' + icGap.toFixed(0) + 'm is too fine for ramp grades')
      : ' - no crossing on a ring side clear of the river, diagonal and railway')];
    for (const it of sites) {
      const plan = ARCHETYPES[it.type] || ARCHETYPES.diamond;
      const [sx, sz] = warp(it.P0[0], it.P0[1]);
      // re-read from the live network each ramp: attaching a ramp splits the arterial it meets,
      // and a stale list would aim the next ramp at an edge that no longer exists
      const artsLive = () => [...net.edges.values()].filter(e => e._cls === 'arterial' && net.centerline(e).nearest(sx, sz).d < 470);
      let arts = artsLive();
      const why = m => net.meta.icLog.push(it.type + ': ' + m);
      if (!arts.length) { why('no arterial in reach (0 edges)'); continue; }
      net.meta.icLog.push(it.type + ': ' + arts.length + ' arterial edges, specs ' + plan.plan([{ sg: 1 }, { sg: -1 }]).length);
      const siteArt = nearestOn(arts, sx, sz);
      const artName = siteArt.e.name;
      const artAt = (x, z) => nearestOn(artName ? arts.filter(e => e.name === artName) : arts, x, z);
      const artPav = Math.max(7, Math.abs(edgeLat(siteArt.e, 1)));
      it.ramps = [];
      for (const rp of plan.plan([{ sg: 1 }, { sg: -1 }])) {
        const kind = rp.kind;
        const prof = kind === 'loop' ? 'rampLoop' : kind === 'diag' ? 'rampWide' : 'ramp2';
        const K = it.k || 1;
        const half = halfOf(prof), Rnom = (rp.R || 70) * K;
        // only the plan-view REACH scales with the block; the taper runs are driven by the legal
        // merge rate (>= 1:6 lateral) so compressing them would break the blend, not the fit
        // The coplanar run has to be long enough for the pinch to read as one surface (~50 m) and
        // short enough that the ramp still has room to lose the freeway's height at a legal grade.
        const flush = Math.max(34, Math.min(80, (rp.flush || (kind === 'off' || kind === 'on' ? 150 : 120)) * K * 0.75));
        const flare = Math.max(52, rp.flare || 62);
        arts = artsLive();
        const gp = [it.P0[0] + it.n[0] * rp.sg * fwLf, it.P0[1] + it.n[1] * rp.sg * fwLf];
        const [gx, gz] = warp(gp[0], gp[1]);
        const fw = nearestOn(fwEdges, gx, gz);
        if (!fw) { why(kind + ': no carriageway'); continue; }
        // a ramp always pulls off to the right of its own flow, so the side it sits on and the
        // direction it runs in are the same sign in the anchor's frame (positive offset = right)
        const dirFw = Math.sign(latOf(fw.f, gx, gz)) || 1;
        const offTravel = K * (kind === 'on' ? (rp.along || 110) * 1.2 : kind === 'off' ? -(rp.along || 110) * 1.2
          : kind === 'loop' ? (artPav + Rnom) * 0.5 : -(artPav + Rnom) * 1.15);
        // the run has to fit inside the anchor: a flush section that would start before the edge
        // begins gets pulled back rather than extrapolated off the end of the mainline
        const fitAt = (c, at, mode, flush, flare) => {
          const back = mode === 'in' ? flush : flare, fwd = mode === 'in' ? flare : flush;
          return clampN(at, Math.min(28, c.length / 2) + back, c.length - Math.min(28, c.length / 2) - fwd);
        };
        const atFw = fitAt(fw.c, fw.s + offTravel * dirFw, kind === 'on' ? 'out' : 'in', flush, flare);
        if (Math.abs(atFw - (fw.s + offTravel * dirFw)) > 60) { why(kind + ': no room on the mainline'); continue; }
        const F = mergeEnd(net, fw.e, {
          at: atFw, sg: dirFw, dir: dirFw, mode: kind === 'on' ? 'out' : 'in',
          flush, flare, gap: rp.gap || 9, half, step: 3,
        });
        if (!F || F.pts.length < 4) { why(kind + ': no merge run'); continue; }
        // mergeEnd always returns pts in the ramp's own travel order; for an on-ramp the nose is
        // at pts[0] (mode 'out'), for everything else the nose is at the far end. freeP/freeTh are
        // the END THAT IS NOT ON THE RING - that is where a loop or diagonal picks up.
        const freeP = F.pts[0], freeTh = F.headIn;
        let G = null, body = null, jct = null, Rloop = 0;
        if (kind === 'loop') {
          // A loop is a constant-radius 270 deg arc, sized so its end lands exactly on the
          // arterial's edge line. Hand (which way it curls) and side (which carriageway it joins)
          // are both tried; whatever fails to fit the quadrant is simply not built.
          // the loop can be hung off either merge end - the crossing sits between them, so which
          // one gives a radius that fits the quadrant depends on where the nose landed. Try both.
          const ENDS = [{ P: F.pts[0], th: F.headIn }, { P: F.pts[F.pts.length - 1], th: F.headOut }];
          const hand = (sgn, sgt, useEnd) => {
            // the sweep's end has to come to rest on the arterial's outer lane edge, at the same
            // level and travelling the same way. The landing equation has one root per arterial it
            // crosses, so scan outward from the tightest radius and take the first that works.
            const thE = ENDS[useEnd].th + sgn * 1.5 * Math.PI;
            const t = { x: Math.cos(ENDS[useEnd].th), z: Math.sin(ENDS[useEnd].th) };
            const n = { x: -t.z, z: t.x };
            const P1 = ENDS[useEnd].P;
            const half = halfOf(rp.body === 'arc' ? PROFILES.rampLoop : prof);
            const errAt = R => {
              const end = { x: P1.x + n.x * sgn * R - t.x * R, z: P1.z + n.z * sgn * R - t.z * R };
              const q = nearestOn(arts, end.x, end.z);
              if (!q) return null;
              const req = edgeLat(q.e, sgt) + sgt * half;
              if (Math.abs(req) < 3 || Math.abs(req) > 70) return null;
              const f = dirOf(q.f, thE) === sgt ? q.f : { yaw: q.f.yaw + Math.PI, tx: -q.f.tx, tz: -q.f.tz };
              return { q, end, R, lat: latOf(f, end.x, end.z) - req, turn: sgn * 1.5 * Math.PI, d: q.d };
            };
            const R0 = 30 * K, R1 = Math.min(120, (plan.R || 80) * 1.4 * K);
            let pv = null, pr = null;
            for (let i = 0; i <= 20; i++) {
              const R = R0 + (R1 - R0) * i / 20, v = errAt(R);
              if (!v) { pv = null; continue; }
              if (pv && pv.lat * v.lat <= 0) {
                let a = pr, b = R, ea = pv.lat;
                for (let j = 0; j < 16; j++) {
                  const m = (a + b) / 2, vm = errAt(m);
                  if (!vm) break;
                  if (ea * vm.lat <= 0) b = m; else { a = m; ea = vm.lat; }
                }
                const w = errAt((a + b) / 2);
                if (!w || w.q.d > 240) continue;
                if (w.R < 30 * K || w.R > R1) continue;
                return w;
              }
              pv = v; pr = R;
            }
            return null;
          };
          const fits = [];
          for (let ue = 0; ue < 2; ue++) for (const sgn of [1, -1]) for (const sgt of [dirFw, -dirFw]) {
            const w = hand(sgn, sgt, ue);
            if (w) fits.push({ R: w.R, q: w.q, end: w.end, sgn, sgt, turn: w.turn, ue });
          }
          if (!fits.length) { why('loop: no landing'); continue; }
          // the tightest loop that works is the right one: it stays inside the quadrant it was
          // designed for, while a wide one reaches into the next block and starts crossing traffic
          const fit = fits.sort((a, b) => a.R - b.R)[0];
          Rloop = fit.R;
          body = arcPts(ENDS[fit.ue].P, ENDS[fit.ue].th, fit.R, fit.sgn * Math.PI * 1.5, 7);
          if (!body || body.length < 8) { why('loop: arc degenerate'); continue; }
          G = mergeEnd(net, fit.q.e, {
            at: fitAt(fit.q.c, fit.q.s, 'out', rp.tip || 92, 1.5), sg: fit.sgt, dir: fit.sgt, mode: 'out',
            flush: Math.max(60, rp.tip || 92), flare: 1.5, gap: 0, half, step: 3,
          });
          if (!G || G.pts.length < 3) { why('loop: no landing run'); continue; }
        } else {
          const look = (kind === 'diag' ? 150 : 25) * K;
          const q = nearestOn(arts, freeP.x + Math.cos(freeTh) * look, freeP.z + Math.sin(freeTh) * look);
          if (!q) { why(kind + ': no arterial alongside'); continue; }
          // a left/right-turn ramp belongs to the crossing it was built for: it may not be handed
          // to some other street further along, or the ramp ends up running a kilometre across town
          // (the name test below is what pins the ramp to this crossing; this is only a runaway guard)
          if (q.d > (kind === 'diag' || kind === 'loop' ? 460 : 340)) { why(kind + ': junction ' + q.d.toFixed(0) + 'm off the ramp end'); continue; }
          if ((kind === 'on' || kind === 'off') && siteArt && q.e.name !== siteArt.e.name) { why(kind + ': reaches ' + (q.e.name || q.e.id) + ', not ' + siteArt.e.name); continue; }
          if (kind === 'diag') {
            // join whichever carriageway the ramp actually sweeps into, crossing the road if need be
            const cands = [];
            for (const d of [1, -1]) {
              const th = d > 0 ? Math.atan2(q.f.tz, q.f.tx) : Math.atan2(-q.f.tz, -q.f.tx);
              const lat = edgeLat(q.e, d) + d * half;
              const px = q.f.x + q.f.nx * lat, pz = q.f.z + q.f.nz * lat;
              let dn = th - freeTh;
              while (dn > Math.PI) dn -= Math.PI * 2;
              while (dn < -Math.PI) dn += Math.PI * 2;
              if (Math.abs(dn) > 2.45) continue;                    // would have to arrive backwards
              cands.push({ d, th, cost: Math.abs(dn) });
            }
            if (!cands.length) { why('diag: no flow to join'); continue; }
            const c0 = cands.sort((a, b) => a.cost - b.cost)[0];
            G = mergeEnd(net, q.e, {
              at: fitAt(q.c, q.s, 'out', rp.tip || 100, rp.flareB || 48), sg: c0.d, dir: c0.d, mode: 'out',
              flush: rp.tip || 100, flare: rp.flareB || 48, gap: rp.gapB || 9, half, step: 3,
            });
            if (!G || G.pts.length < 4) { why('diag: no landing run'); continue; }
            body = hermitePts(freeP, freeTh, G.pts[0], G.headIn, 9);
          } else {
            // the tail ties into a node on the cross street. If the ramp paired with it already tied
            // in nearby, share that node - sawing the street again would leave a stub too short to
            // carry two junctions, which is what the "segment too short" warning is about.
            const nd = attachNode(q.e, q.s, q.f);
            if (!nd) { why(kind + ': no legal station on ' + (q.e.name || q.e.id)); continue; }
            jct = { e: q.e, s: q.s, node: nd, p: { x: nd.x, z: nd.z, y: nd.y } };
            const thTo = Math.atan2(jct.p.z - freeP.z, jct.p.x - freeP.x);
            body = kind === 'on'
              ? hermitePts(jct.p, thTo, freeP, freeTh, 9)
              : hermitePts(freeP, freeTh, jct.p, thTo, 9);
          }
        }
        // a node picked off an existing junction sits where that junction is, so the ramp is
        // re-aimed at it before the polyline is stitched
        if (jct && jct.node) {
          const nd = jct.node, thFrom = Math.atan2(nd.z - freeP.z, nd.x - freeP.x);
          body = kind === 'on'
            ? hermitePts(nd, thFrom + Math.PI, freeP, freeTh, 9)
            : hermitePts(freeP, freeTh, nd, thFrom, 9);
        }
        if (!body || body.length < 2) continue;
        // ---- the polyline, in travel order, tip to tip. Pieces are stitched with a minimum
        // spacing: a repeated or sub-metre point makes the interpolation ring at the joint, and a
        // ringing spline is exactly what opens a gap where the pavement should meet flush.
        const raw = kind === 'on' ? [jct.p, ...body, ...F.pts] : [...F.pts, ...body, ...(G ? G.pts : [jct.p])];
        // ---- vertical alignment. Between the tip and the nose the ramp is coplanar with the deck it
        // is attached to, so those points keep their own level; everything between the two noses falls
        // (or climbs) straight between them, which is where an off-ramp actually loses its height.
        // Where a point sits within `flush` of its own merge tip it is coplanar with the road it is
        // attached to and its level is fixed there; outside that the profile is free to fall.
        const levelTo = (arr, st, off, fl) => {
          for (let k = 0; k < arr.length; k++) if (Math.abs((arr[k].s ?? st) - st) <= fl) keep[off + k] = 1;
        };
        const keep = new Uint8Array(raw.length);
        levelTo(F.pts, atFw, 0, flush);
        // hold the last 20 m level where the ramp meets the street it joins, so the joint has no step
        for (let q = raw.length - 1, acc = 0; q >= 0 && acc < 20; q--) { keep[q] = 1; acc += q ? Math.hypot(raw[q].x - raw[q - 1].x, raw[q].z - raw[q - 1].z) : 0; }
        if (G) levelTo(G.pts, G.at, (kind === 'on' ? 1 : 0) + (body ? body.length : 0) + (kind === 'on' ? 0 : F.pts.length - 1), Math.max(40, Math.min(86, rp.tip || 92)));
        for (let k = 1; k < raw.length - 1; k++) if (!keep[k]) raw[k].y = undefined;
        let len = 0; const cum = [0];
        for (let k = 1; k < raw.length; k++) { len += Math.hypot(raw[k].x - raw[k - 1].x, raw[k].z - raw[k - 1].z); cum.push(len); }
        {
          let k = 0;
          while (k < raw.length) {
            if (raw[k].y !== undefined) { k++; continue; }
            let m = k; while (m < raw.length && raw[m].y === undefined) m++;
            // A run that reaches the end of the path keeps that end's own level: the tail of an
            // off-ramp runs beside the deck it left so it stays at deck level, its head sits on the
            // road it joins so it finishes at that road's level, and the middle is one even slope.
            const yA = raw[k - 1] ? raw[k - 1].y : raw[0].y;
            const yB = raw[m] ? raw[m].y : raw[raw.length - 1].y;
            const sA = k - 1 >= 0 ? cum[Math.max(0, k - 1)] : 0, sB = m < raw.length ? cum[m] : cum[cum.length - 1];
            const s0 = Math.max(1, sB - sA);
          // eased rather than straight: a ramp has to arrive at the road it joins level with it, and
          // the steepening this costs is checked against the design limit once the path is built
          for (let q = k; q < m; q++) { const u = (cum[q] - sA) / s0; raw[q].y = yA + (yB - yA) * (u * u * (3 - 2 * u)); }
            k = m;
          }
        }
        const pts = [];
        for (let k = 0; k < raw.length; k++) {
          const p = raw[k], last = pts[pts.length - 1];
          if (!last || Math.hypot(p.x - last.x, p.z - last.z) >= 2.6) { pts.push(p); continue; }
          if (k === raw.length - 1 && pts.length > 1) {
            const prev = pts[pts.length - 2];
            if (Math.hypot(p.x - prev.x, p.z - prev.z) >= 2.6) pts[pts.length - 1] = p;
          }
        }
        if (pts.length < 6) { why(kind + ': path degenerated'); continue; }
        len = 0; cum.length = 1; cum[0] = 0;
        for (let k = 1; k < pts.length; k++) { len += Math.hypot(pts[k].x - pts[k - 1].x, pts[k].z - pts[k - 1].z); cum.push(len); }
        // A ramp may not thread a roadway it does not join, and it may not dive under one either, so
        // the only lawful way through is over it: rise 5.4 m, spread far enough to keep the grade sane.
        let bx0 = 1e9, bx1 = -1e9, bz0 = 1e9, bz1 = -1e9;
        for (const p of pts) { if (p.x < bx0) bx0 = p.x; if (p.x > bx1) bx1 = p.x; if (p.z < bz0) bz0 = p.z; if (p.z > bz1) bz1 = p.z; }
        const mine = new Set(it.ramps.map(r => r.edge));
        const avoid = [];
        for (const e of net.edges.values()) {
          // the ring is corridor, not an obstacle - but only the edges this ramp is attached to and
          // the ones that run on from its ends; a loop that would clip the ring's far side is a clash
          if (e.id === fw.e.id || e.a === fw.e.a || e.b === fw.e.a || e.a === fw.e.b || e.b === fw.e.b) continue;
          if (e._cls === 'ramp') { if (mine.has(e.id)) continue; }
          else if (e._cls !== 'freeway' && e._cls !== 'arterial' && e._cls !== 'rail' && !e.name) continue;
          if (jct && e.id === jct.e.id) continue;          // the street the head is spliced into
          const b = boxOf(e);
          if (b.x1 < bx0 - 26 || b.x0 > bx1 + 26 || b.z1 < bz0 - 26 || b.z0 > bz1 + 26) continue;
          avoid.push(e);
        }
        const tipA = pts[0], tipB = pts[pts.length - 1];
        const jctNear = [];
        if (jct) for (const e of net.edges.values()) {
          if (e._cls === 'ramp' || e._cls === 'freeway') continue;
          for (const id of [e.a, e.b]) {
            const n = net.nodes.get(id);
            if (n && Math.hypot(n.x - jct.p.x, n.z - jct.p.z) < 82) { jctNear.push(e.id); break; }
          }
        }
        const inJunctionCluster = e => jctNear.indexOf(e.id) >= 0;
        let hit = null;
        for (let k = 0; k < pts.length && !hit; k++) {
          const p = pts[k];
          for (const e of avoid) {
            const oc = net.centerline(e), q = oc.nearest(p.x, p.z);
            const reach = half + Math.max(6, Math.abs(edgeLat(e, 1)));
            if (q.d > reach + 1.5) continue;
            // a street that ends in the junction the ramp lands in is joining the same node, not
            // being run through; and near its own ends a ramp is allowed to sit inside the corridor of
            // the road it is joining - that stretch is the connection, not a crossing
            if (inJunctionCluster(e) && (cum[k] < 60 || cum[cum.length - 1] - cum[k] < 60)) continue;
            const near = cum[k] < flush + 70 || cum[cum.length - 1] - cum[k] < Math.max(100, (rp.tip || 100) * K + 70);
            if (near && e.name && (e.name === (G && G.name) || e.name === (jct && jct.e.name))) continue;
            // anything a ramp crosses that it does not join has to be cleared outright: over it by
            // more than the structure depth plus a giveaway, or buried under it by the same. Anything
            // in between is a collision waiting to be rendered, so the ramp is not built.
            const f = oc.frameAt(q.s), d = p.y - f.y;
            if (d > 4.8 || d < -4.2) continue;
            hit = `${e.id}:${e.name || e.profile} (${e._cls}) s=${cum[k].toFixed(0)} lvl ${d.toFixed(1)} | G=${G ? G.edgeId + ":" + G.name : "-"} jct=${jct ? jct.e.name : "-"}`;
            break;
          }
        }
        if (hit) { why(kind + ': would run through ' + hit); continue; }
        // ---- nodes and the edge
        const tipNode = p => { const n = net.addNode({ x: p.x, z: p.z, y: p.y, end: 'flush' }); return n; };
        let nStart, nEnd;
        if (kind === 'on') { nStart = jct.node; nEnd = tipNode(pts[pts.length - 1]); }
        else if (kind === 'off') { nStart = tipNode(pts[0]); nEnd = jct.node; }
        else { nStart = tipNode(pts[0]); nEnd = tipNode(pts[pts.length - 1]); }
        const inner = pts.slice(1, -1).map(p => ({ x: p.x, z: p.z, y: p.y }));
        const edge = net.addEdge({
          a: nStart.id, b: nEnd.id, profile: prof, pts: inner,
          name: (kind === 'off' ? 'Exit ramp' : kind === 'on' ? 'On-ramp' : kind === 'loop' ? 'Loop ramp' : 'Connector') + ' \u00b7 ' + (nStart._icName || it.name || ''),
        });
        edge._cls = 'ramp';
        const cR = net.centerline(edge), LR = cR.length;
        // The centerline is a spline through the control points and can pull a metre or so off them,
        // which is the difference between clearing a street and running through it. Check the path
        // that will actually be built, and drop the ramp rather than ship a clip.
        let clip = null; const ss = [0];
        for (let k = 1; k < cR.X.length; k++) ss.push(ss[k - 1] + Math.hypot(cR.X[k] - cR.X[k - 1], cR.Z[k] - cR.Z[k - 1]));
        for (let k = 0; k < cR.X.length && !clip; k += 2) {
          for (const e2 of avoid) {
            const b2 = boxOf(e2);
            if (cR.X[k] < b2.x0 - 6 || cR.X[k] > b2.x1 + 6 || cR.Z[k] < b2.z0 - 6 || cR.Z[k] > b2.z1 + 6) continue;
            const o2 = net.centerline(e2), q2 = o2.nearest(cR.X[k], cR.Z[k]);
            if (q2.d > half + Math.max(6, Math.abs(edgeLat(e2, 1))) + 1.5) continue;
            const d2 = cR.Y[k] - o2.frameAt(q2.s).y;
            if (d2 > 4.8 || d2 < -4.2) continue;
            const nearB = ss[k] < flush + 70 || ss[ss.length - 1] - ss[k] < Math.max(100, (rp.tip || 100) * K + 70);
            if (nearB && e2.name && (e2.name === (G && G.name) || e2.name === (jct && jct.e.name))) continue;
            clip = `${e2.name || e2.profile} at s=${k}m, ${(d2 >= 0 ? d2.toFixed(1) + 'm above' : (-d2).toFixed(1) + 'm below')}`;
            break;
          }
        }
        let steep = 0;
        for (let k = 0; k + 8 < cR.X.length; k++) {
          let j = k + 8; while (j < cR.X.length - 1 && ss[j] - ss[k] < 9) j++;
          steep = Math.max(steep, Math.abs(cR.Y[j] - cR.Y[k]) / Math.max(1, ss[j] - ss[k]));
        }
        if (clip) {
          why('would clip ' + clip);
          net.edges.delete(edge.id); net.invalidate([edge.id]);
          for (const nd of [nStart, nEnd]) if (nd && !net.nodeEdges(nd.id).length) net.nodes.delete(nd.id);
          continue;
        }
        if (steep > 0.084) {                       // the eased profile no longer fits the ramp
          why('grade ' + (steep * 100).toFixed(1) + '% over ' + LR.toFixed(0) + 'm of ramp');
          net.edges.delete(edge.id); net.invalidate([edge.id]);
          for (const nd of [nStart, nEnd]) if (nd && !net.nodeEdges(nd.id).length) net.nodes.delete(nd.id);
          continue;
        }
        const layR = layout(PROFILES[prof]);
        const laneR = layR.lanes.findIndex(l => l.type === 'barrier');
        const spec = [];
        const tap = [];
        if (kind !== 'on') {                            // the freeway end is at s=0
          spec.push({ main: fw.e.id, end: 'a' });
          tap.push({ end: 'a', len: Math.max(24, flush - 4), inb: 'l' });
          if (laneR >= 0) edge.features.push({ type: 'barrierGap', lane: laneR, s0: -1, s1: flush + flare * 0.9 });
          gapOn(fw.e, atFw, dirFw, flare, 'in');
        } else {                                        // the freeway end is the merge, at s=LR
          spec.push({ main: fw.e.id, end: 'b' });
          tap.push({ end: 'b', len: Math.max(24, flush - 4), inb: 'l' });
          if (laneR >= 0) edge.features.push({ type: 'barrierGap', lane: laneR, s0: LR - flush - flare * 0.9, s1: LR + 1 });
          gapOn(fw.e, atFw, dirFw, flare, 'out');
        }
        if (G) {
          if (G.flare > 3 && (rp.gapB || 0) > 0.5) spec.push({ main: G.edgeId, end: 'b' });
          tap.push({ end: 'b', len: Math.max(22, (kind === 'loop' ? rp.tip || 92 : rp.tip || 100) * K - 4), inb: 'l' });
          gapOn(net.edges.get(G.edgeId), G.at, G.dir, G.flare, 'out');
        }
        if (spec.length) edge.merge = spec;
        if (tap.length === 1) edge.tipTaper = tap[0];
        else if (tap.length) edge.tipTaper = tap;
        it.ramps.push({ kind, edge: edge.id, R: Math.round(Rloop) });
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

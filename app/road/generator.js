/* ============================================================
   FRONTIER · ROAD EDITOR — city generator
   Builds a real road network: hierarchy (freeway → arterial → collector →
   local), river with bridges, railway with level crossings, metro under the
   strongest corridors, cycle / bus / parking provision, calming, parks,
   squares, surface car parks and optional context massing.
   Everything it produces stays hand-editable: roads are just splines +
   profiles, junctions are graph nodes.
   ============================================================ */

import { RNG, strSeed, clamp, lerp, dist, norm, perp, add, mul, sub, round, TAU, hash01 } from './lib.js';
import { PROFILES, profileById } from './profiles.js';
import { pointInPoly } from './mesh.js';

export const DEFAULT_CITY = {
  seed: 20264,
  blocksX: 7, blocksZ: 6,
  blockSize: 118,
  blockJitter: .12,
  rotation: 0,
  meander: 5.5,
  avenueEvery: 3,
  diag: 1,
  ring: true,
  river: true, riverWidth: 46, riverAngle: 14, riverOffset: .18, riverSinuosity: 26,
  rail: true, railOffset: -.40, railStyle: 'rail-main', railEmbankment: true,
  subway: 2, subwayDepth: -15,
  cycle: 'arterials',           // none | arterials | all | grid
  bus: true, parking: true,
  roundabouts: .28, signalsEvery: 1,
  parks: .16, plazas: 1, lots: 4, culs: 3,
  greenway: true,
  markings: 1, props: 1, streetTrees: 1,
  massing: true, massingHeight: 1, massingCover: .74,
  districtBlend: .5,
  oldTown: true,
};

const STREET_NAMES = ['King', 'Queen', 'Church', 'Market', 'High', 'Bridge', 'Station', 'Mill', 'Park', 'Castle', 'Abbey', 'Union', 'Canal', 'Foundry', 'Harbour', 'Cathedral', 'Granary', 'Copperfield', 'Kingsway', 'Riverside', 'Beacon', 'Kilner', 'Wexford', 'Ashgrove', 'Norwood', 'Balmoral', 'Wellington', 'Lambert', 'Fenwick', 'Marchmont'];
const SUFFIX = { arterial: 'Avenue', collector: 'Street', local: 'Street', service: 'Lane', path: 'Path', cycle: 'Way', tram: 'Route' };

/* ------------------------------- geometry helpers ------------------------------- */
const rot = (p, a) => ({ x: p.x * Math.cos(a) - p.z * Math.sin(a), z: p.x * Math.sin(a) + p.z * Math.cos(a) });
const key = (i, j) => `${i}|${j}`;

function polylineLength(pts) { let L = 0; for (let i = 1; i < pts.length; i++) L += dist(pts[i - 1], pts[i]); return L; }
function nearestOnPolyline(pts, p) {
  let best = { d: Infinity, s: 0, seg: 0, t: 0 };
  let acc = 0;
  for (let i = 1; i < pts.length; i++) {
    const a = pts[i - 1], b = pts[i];
    const ab = sub(b, a), l2 = ab.x * ab.x + ab.z * ab.z || 1e-9;
    const t = clamp(((p.x - a.x) * ab.x + (p.z - a.z) * ab.z) / l2, 0, 1);
    const q = { x: a.x + ab.x * t, z: a.z + ab.z * t };
    const d = dist(q, p);
    if (d < best.d) best = { d, s: acc + Math.sqrt(l2) * t, seg: i - 1, t, q, len: Math.sqrt(l2) };
    acc += Math.sqrt(l2);
  }
  return best;
}
/** all crossings of a segment p1→p2 against a polyline, as fraction along the segment */
function segPolyIntersections(p1, p2, poly, halfWidth) {
  const out = [];
  const d = sub(p2, p1), len = dist(p1, p2) || 1e-9;
  for (let i = 1; i < poly.length; i++) {
    const a = poly[i - 1], b = poly[i];
    const e = sub(b, a);
    const den = d.x * e.z - d.z * e.x;
    if (Math.abs(den) < 1e-9) continue;
    const t = ((a.x - p1.x) * e.z - (a.z - p1.z) * e.x) / den;
    const u = ((a.x - p1.x) * d.z - (a.z - p1.z) * d.x) / den;
    if (t >= 0 && t <= 1 && u >= 0 && u <= 1) out.push(t);
  }
  // also include the "near" band ends so bridge spans have abutments outside the water
  if (!out.length) {
    for (let s = 0; s <= 1.0001; s += .02) {
      const p = { x: p1.x + d.x * s, z: p1.z + d.z * s };
      if (nearestOnPolyline(poly, p).d < halfWidth) out.push(s);
    }
  }
  return out;
}
const segNearPolyline = (p1, p2, poly, halfWidth, steps = 40) => {
  const d = sub(p2, p1);
  for (let i = 0; i <= steps; i++) {
    const s = i / steps;
    if (nearestOnPolyline(poly, { x: p1.x + d.x * s, z: p1.z + d.z * s }).d < halfWidth) return true;
  }
  return false;
};

const square = (c, r, a) => { const out = []; for (let k = 0; k < 4; k++) { const t = a + k * Math.PI / 2 + Math.PI / 4; out.push({ x: c.x + Math.cos(t) * r, z: c.z + Math.sin(t) * r }); } return out; };
const dot2 = (a, b) => a.x * b.x + a.z * b.z;

/* ------------------------------- generator ------------------------------- */
export function generateCity(userParams = {}) {
  const p = { ...DEFAULT_CITY, ...userParams };
  const rng = new RNG(strSeed('city-' + p.seed));
  const roads = [], nodes = [], water = [], areas = [], blocks = [], meta = { issues: [] };
  const angle = TAU * 0 + (p.rotation || 0) * Math.PI / 180;
  const B = p.blockSize;
  const nx = Math.max(2, Math.round(p.blocksX)), nz = Math.max(2, Math.round(p.blocksZ));
  const city = { w: nx * B, h: nz * B };
  const T = t => { const r = rot(t, angle); return { x: r.x + 0, z: r.z + 0 }; };   // grid → world

  /* node lattice */
  const spacingX = [], spacingZ = [];
  for (let i = 0; i <= nx; i++) spacingX.push(i);
  for (let j = 0; j <= nz; j++) spacingZ.push(j);
  const nodeAt = new Map();
  const half = { x: nx / 2, z: nz / 2 };
  for (let i = 0; i <= nx; i++) for (let j = 0; j <= nz; j++) {
    const gx = (i - half.x) * B, gz = (j - half.z) * B;
    const jitterX = p.blockJitter ? rng.gauss(B * p.blockJitter * .35) : 0;
    const jitterZ = p.blockJitter ? rng.gauss(B * p.blockJitter * .35) : 0;
    const n = {
      id: 'n-' + i + '-' + j, gi: i, gj: j, ...T({ x: gx + jitterX, z: gz + jitterZ }), y: 0,
      arms: [], style: 'uncontrolled', radius: 0
    };
    n.iAmAve = i % p.avenueEvery === 0 || i === 0 || i === nx;
    n.jAmAve = j % p.avenueEvery === 0 || j === 0 || j === nz;
    n.avenue = n.iAmAve || n.jAmAve;
    nodeAt.set(key(i, j), n);
    nodes.push(n);
  }
  const N = (i, j) => nodeAt.get(key(i, j));

  /* river corridor */
  let river = null;
  if (p.river) {
    const ra = (p.riverAngle || 0) * Math.PI / 180;
    const across = Math.max(city.w, city.h) * 1.22;
    const c = { x: -Math.sin(ra) * across / 2, z: Math.cos(ra) * across / 2 };
    const dirv = { x: Math.cos(ra) * across, z: Math.sin(ra) * across };
    const offv = { x: -Math.sin(ra), z: Math.cos(ra) };
    const pts = [];
    const steps = 16;
    const base = (p.riverOffset - .5) * city.h * 1.4;
    for (let i = 0; i <= steps; i++) {
      const t = i / steps;
      const sinus = Math.sin(t * Math.PI * 2.2 + rng.next() * .4) * (p.riverSinuosity ?? 20) + Math.sin(t * Math.PI * 5.1) * (p.riverSinuosity ?? 20) * .35;
      pts.push(T({ x: c.x + dirv.x * t + offv.x * (base + sinus), z: c.z + dirv.z * t + offv.z * (base + sinus) }));
    }
    river = pts;
    water.push({ id: 'river', points: pts, width: p.riverWidth, y: -1.35, wallDepth: 5.0, closed: false, lamps: true, trees: true, step: 9 });
  }
  const riverHalf = p.riverWidth / 2 + 8;

  /* rail corridor */
  let railLine = null;
  if (p.rail) {
    const gz = Math.round(p.railOffset * nz + nz / 2);
    const j = clamp(gz, 0, nz);
    const pts = [];
    for (let i = -1; i <= nx + 1; i++) pts.push(T({ x: (i - half.x) * B * 1.05, z: (j - half.z) * B }));
    railLine = pts;
  }

  /* ---- road class decision per grid line ---- */
  const lineClass = (i, j, axis) => {
    const isAve = axis === 'x' ? (i % p.avenueEvery === 0) : (j % p.avenueEvery === 0);
    if (isAve) return 'arterial';
    const alt = axis === 'x' ? (i % 2 === 0) : (j % 2 === 0);
    return alt ? 'collector' : 'local';
  };
  const profileFor = (cls, axis, i, j) => {
    const downtown = 1 - clamp(Math.hypot((i - half.x) / (nx * .32), (j - half.z) / (nz * .32)), 0, 1);
    if (cls === 'arterial') {
      if (downtown > .72 && p.districtBlend > .25) return p.bus ? 'art-main' : 'art-ave-4';
      if (p.oldTown && downtown > .35 && hash01(strSeed('old'), i, j) < p.districtBlend) return 'art-boulevard';
      return p.bus ? 'art-ave-4' : 'str-30';
    }
    if (cls === 'collector') return p.cycle === 'all' || p.cycle === 'grid' ? 'str-curb-parking' : 'str-30';
    if (p.oldTown && downtown > .55 && hash01(strSeed('cobb'), i, j) < .5) return 'str-alley';
    return p.parking ? 'str-30' : 'str-local';
  };

  /* ---- build one street segment ---- */
  let seq = 0;
  const segment = (a, b, cls, axis, i, j, extra = {}) => {
    if (!a || !b) return null;
    const prof = extra.profile || profileFor(cls, axis, i, j);
    const mid = [];
    const len = dist(a, b);
    const meander = p.meander * (cls === 'local' ? 1 : cls === 'collector' ? .55 : .22);
    if (meander > .5 && len > B * .8) {
      const n = 2;
      const dirv = norm(sub(b, a)), nv = perp(dirv);
      for (let k = 1; k <= n; k++) {
        const t = k / (n + 1);
        const off = rng.gauss(meander * .5) * (1 - Math.abs(t - .5) * .6);
        mid.push({ x: lerp(a.x, b.x, t) + nv.x * off, z: lerp(a.z, b.z, t) + nv.z * off, y: 0 });
      }
    }
    const road = {
      id: 'rd-' + (++seq).toString(36), name: roadName(i, j, axis, cls), cls,
      profile: prof,
      points: [{ x: a.x, z: a.z, y: 0 }, ...mid, { x: b.x, z: b.z, y: 0 }],
      smooth: cls === 'arterial' ? 2 : 1,
      crossings: [], bumps: [], driveways: [], drops: [], crosstables: [],
      source: 'generated'
    };
    /* bridge over water — keep this street's own assembly, lift the deck */
    if (river && cls !== 'path') {
      const xs = segPolyIntersections(road.points[0], road.points[road.points.length - 1], river, riverHalf);
      if (xs.length) {
        const L = polylineLength(road.points);
        const t0 = clamp(Math.min(...xs) - .16, 0, 1), t1 = clamp(Math.max(...xs) + .16, 0, 1);
        const span = t1 - t0, mid = (t0 + t1) / 2;
        const clearance = (p.riverWidth / 2 + 5.2) - (p.riverWidth / 2) + 3.4;
        const h = clamp(clearance + (cls === 'arterial' ? 1.4 : .4), 3.2, 9);
        const cuts = [];
        for (let k = 0; k <= 6; k++) {
          const t = lerp(Math.max(0, t0 - .18), Math.min(1, t1 + .18), k / 6);
          const bell = Math.sin(Math.PI * clamp((t - Math.max(0, t0 - .18)) / ((Math.min(1, t1 + .18)) - Math.max(0, t0 - .18)), 0, 1));
          const y = h * Math.pow(bell, .55);
          cuts.push({ x: lerp(a.x, b.x, t), z: lerp(a.z, b.z, t), y });
        }
        road.points = cuts;
        road.structure = 'bridge';
        road.bridge = { from: L * t0, to: L * t1 };
        road.name = 'Bridge · ' + road.name;
        road.crossings = road.crossings || [];
      }
    }
    /* level / grade crossing over the railway */
    if (railLine) {
      const hits = segPolyIntersections(road.points[0], road.points[road.points.length - 1], railLine, 12);
      if (hits.length) {
        const L = polylineLength(road.points);
        for (const h of hits) {
          const s = h * L;
          const near = nearestOnPolyline(railLine, { x: lerp(a.x, b.x, h), z: lerp(a.z, b.z, h) });
          const seg2 = railLine[near.seg] ? sub(railLine[near.seg + 1] || railLine[near.seg], railLine[near.seg]) : { x: 1, z: 0 };
          if (cls === 'arterial') {
            road.structure = 'bridge';
            const H = 8.4;
            road.points = road.points.map((q, k, arr) => {
              const t = k / (arr.length - 1);
              const bell = Math.sin(Math.PI * clamp((t - .08) / .84, 0, 1));
              return { ...q, y: Math.max(q.y || 0, H * Math.pow(bell, .5)) };
            });
            road.bridge = { from: L * .18, to: L * .82 };
            road.name = 'Flyover · ' + road.name;
          } else {
            road.crossings.push({ s, width: 15, angle: Math.atan2(seg2.x, -seg2.z) });
          }
        }
      }
    }
    /* traffic calming on the quiet end of the network */
    if ((cls === 'local' || road.profile === 'str-30') && p.markings > .4 && len > 60) {
      for (let s = 42 + rng.range(-8, 8); s < len - 30; s += 78 + rng.range(-16, 22)) {
        road.bumps.push({ s, kind: rng.chance(.3) ? 'table' : 'cushion', cushions: rng.chance(.5) ? 1 : 2 });
      }
      if (rng.chance(.25)) road.crosstables.push({ s: len * .5 });
    }
    /* driveways / crossovers onto the flank streets */
    if (p.props > .5 && cls !== 'arterial') {
      const count = Math.max(2, Math.round(len / 26));
      for (let k = 0; k < count; k++) road.driveways.push({ s: (k + .5) * (len / count) + rng.gauss(2), side: rng.sign() });
    }
    /* bus + cycle priority */
    if (p.bus && cls === 'arterial' && profileById(road.profile).elements.some(e => e.k === 'bus') === false && ['str-30', 'art-main'].includes(road.profile) === false && rng.chance(.34)) road.profile = 'bus-lane-only';
    if (p.cycle === 'grid' && cls === 'local' && rng.chance(.5)) road.profile = 'cyc-path';
    if (p.cycle === 'all' && cls === 'collector') road.profile = 'str-curb-parking';
    /* suppress road-end crossings next to junctions — the plate paints them */
    a.arms.push({ roadId: road.id, end: 's', road });
    b.arms.push({ roadId: road.id, end: 'e', road });
    road.nodeS = a.id; road.nodeE = b.id;
    if (a.arms.length > 1) road.noEndCrossings = true;
    if (b.arms.length > 1) road.noEndCrossings = true;
    roads.push(road);
    return road;
  };
  function roadName(i, j, axis, cls) {
    const nameList = axis === 'x' ? STREET_NAMES : STREET_NAMES.slice(11).concat(STREET_NAMES.slice(0, 11));
    const nm = nameList[Math.abs((axis === 'x' ? j : i) * 3 + 1) % nameList.length];
    const num = axis === 'x' ? (i + 1) : (j + 1);
    return cls === 'local' ? `${nm} ${SUFFIX.local}` : `${num}${ordinal(num)} ${SUFFIX[cls] || 'Street'}`;
  }
  const ordinal = n => (n % 10 === 1 && n !== 11 ? 'st' : n % 10 === 2 && n !== 12 ? 'nd' : n % 10 === 3 && n !== 13 ? 'rd' : 'th');

  /* ---- grid ---- */
  for (let j = 0; j <= nz; j++) for (let i = 0; i < nx; i++) segment(N(i, j), N(i + 1, j), cls0(i, j, 'x'), 'x', i, j);
  for (let i = 0; i <= nx; i++) for (let j = 0; j < nz; j++) {
    segment(N(i, j), N(i, j + 1), cls0(i, j, 'y'), 'y', i, j);
  }
  function cls0(i, j, axis) {
    const cls = lineClass(axis === 'x' ? i : j, axis === 'x' ? j : i, axis);
    // perimeter of the grid reads as an arterial ring
    if (axis === 'x' && (j === 0 || j === nz)) return 'arterial';
    if (axis === 'y' && (i === 0 || i === nx)) return 'arterial';
    return cls;
  }

  /* ---- diagonals through the core ---- */
  for (let d = 0; d < (p.diag ?? 0); d++) {
    const i0 = 0, j0 = d === 0 ? 0 : nz, i1 = nx, j1 = d === 0 ? nz : 0;
    const steps = 4;
    let prev = N(i0, j0);
    for (let k = 1; k <= steps; k++) {
      const t = k / steps;
      const gi = Math.round(lerp(i0, i1, t)), gj = Math.round(lerp(j0, j1, t));
      const nxt = N(gi, gj) || N(i1, j1);
      if (nxt && prev && dist(prev, nxt) > B * .5) {
        const r = segment(prev, nxt, 'arterial', k % 2 ? 'x' : 'y', gi, gj, { profile: 'art-boulevard' });
        if (r) r.name = 'Diagonal ' + (d ? 'SW–NE' : 'NW–SE') + ' Boulevard';
      }
      prev = nxt;
    }
  }

  /* ---- ring road ---- */
  if (p.ring) {
    const pad = B * .62;
    const cx = 0, cz = 0;
    const rx = (nx / 2) * B + pad, rz = (nz / 2) * B + pad;
    const pts = [];
    const segs = 44;
    for (let i = 0; i < segs; i++) {
      const a = i / segs * TAU;
      const wobble = 1 + Math.sin(a * 3) * .025;
      pts.push(T({ x: cx + Math.cos(a) * rx * wobble, z: cz + Math.sin(a) * rz * wobble }));
    }
    for (let i = 0; i < segs; i++) {
      const a = pts[i], b = pts[(i + 1) % segs];
      if (river && segNearPolyline(a, b, river, riverHalf * .75)) continue;
      const r = {
        id: 'rd-ring' + i, name: i < 6 ? 'North Ring Road' : i > segs - 7 ? 'South Ring Road' : 'Ring Road',
        cls: 'freeway', profile: 'fwy-2', points: [{ ...a, y: 0 }, { ...b, y: 0 }], smooth: 1,
        crossings: [], bumps: [], driveways: [], noEndCrossings: true, source: 'generated'
      };
      roads.push(r);
    }
  }

  /* ---- parks, plazas, car parks, culs-de-sac ---- */
  const blockRect = (i, j) => {
    const a = N(i, j), b = N(i + 1, j), c = N(i + 1, j + 1), d = N(i, j + 1);
    if (!a || !b || !c || !d) return null;
    const inset = 7;
    const cen = { x: (a.x + b.x + c.x + d.x) / 4, z: (a.z + b.z + c.z + d.z) / 4 };
    const poly = [a, b, c, d].map(q => { const v = sub(q, cen); const l = Math.hypot(v.x, v.z) || 1; const s = (l - inset) / l; return { x: cen.x + v.x * s, z: cen.z + v.z * s }; });
    return { poly, cen, nodes: [a, b, c, d] };
  };
  const usedBlocks = new Set();
  for (let j = 0; j < nz; j++) for (let i = 0; i < nx; i++) {
    const blk = blockRect(i, j);
    if (!blk) continue;
    if (river && segNearPolyline(blk.poly[0], blk.poly[2], river, riverHalf * .5)) { usedBlocks.add(key(i, j)); continue; }
    blocks.push({ i, j, ...blk });
  }
  const blockPick = prob => blocks.filter(() => rng.chance(prob));
  for (const blk of blockPick(p.parks ?? .15)) {
    usedBlocks.add(key(blk.i, blk.j));
    areas.push({
      id: 'park-' + blk.i + '-' + blk.j, polygon: blk.poly, mat: 'pav:grass', y: .05,
      col: [.29, .38, .2], uv: 3.4, edge: true,
      scatter: { count: 10 + Math.round(rng.range(0, 12)), proto: 'tree-plane', scale: [.85, 1.5] },
      perimeterProp: null, kind: 'park'
    });
    const a = blk.nodes[0], b = blk.nodes[2];
    const path = {
      id: 'path-' + blk.i + '-' + blk.j, name: 'Park path', cls: 'path', profile: 'park-path',
      points: [{ ...a, y: 0 }, { x: lerp(a.x, b.x, .5) + rng.gauss(6), z: lerp(a.z, b.z, .5) + rng.gauss(6), y: 0 }, { ...b, y: 0 }],
      smooth: 2, crossings: [], bumps: [], driveways: [], noEndCrossings: true, source: 'generated'
    };
    roads.push(path);
  }
  if (p.plazas) {
    const c = N(Math.round(nx * .5), Math.round(nz * .5));
    if (c) {
      const r = B * .34;
      const poly = [];
      for (let k = 0; k < 8; k++) { const a = k / 8 * TAU + .4; poly.push({ x: c.x + Math.cos(a) * r, z: c.z + Math.sin(a) * r }); }
      areas.push({
        id: 'civic-plaza', polygon: poly, mat: 'pav:paver', y: .06, col: [.62, .59, .54], uv: 2.4, edge: true, kind: 'plaza',
        perimeterProp: 'lamp-bollard', perimeterSpacing: 11,
        scatter: { count: 8, proto: 'bench' }
      });
      const ped = {
        id: 'ped-street', name: 'Old Town pedestrian street', cls: 'pedestrian', profile: 'ped-street',
        points: [{ x: c.x - r * 1.7, z: c.z + r * .2, y: 0 }, { x: c.x - r * .3, z: c.z - r * .12, y: 0 }, { x: c.x + r * 1.6, z: c.z + r * .05, y: 0 }],
        smooth: 2, crossings: [], bumps: [], driveways: [], noEndCrossings: true, source: 'generated'
      };
      roads.push(ped);
    }
  }
  for (const blk of blockPick((p.lots ?? 0) / Math.max(1, blocks.length))) {
    usedBlocks.add(key(blk.i, blk.j));
    const inset = 12;
    const poly = blk.poly.map(q => { const v = sub(q, blk.cen), l = Math.hypot(v.x, v.z) || 1; const s = (l - inset) / l; return { x: blk.cen.x + v.x * s, z: blk.cen.z + v.z * s }; });
    const loop = poly.slice();
    roads.push({
      id: 'lot-' + blk.i + '-' + blk.j, name: 'Surface car park', cls: 'parking-lot', profile: 'lot-surface',
      points: loop.map(q => ({ ...q, y: 0 })), closed: true, smooth: 1,
      crossings: [], bumps: [], driveways: [], noEndCrossings: true, source: 'generated'
    });
  }
  for (let k = 0; k < (p.culs ?? 0); k++) {
    const i = rng.int(1, nx - 1), j = rng.int(0, nz);
    const a = N(i, j), b = N(i + 1, j);
    if (!a || !b) continue;
    const dirv = norm(sub(b, a)), nv = perp(dirv);
    const depth = B * .42;
    const mouth = { x: a.x + nv.x * 6, z: a.z + nv.z * 6 };
    const head1 = { x: mouth.x + dirv.x * depth + nv.x * 6, z: mouth.z + dirv.z * depth + nv.z * 6 };
    const head2 = { x: mouth.x + dirv.x * depth - nv.x * 6, z: mouth.z + dirv.z * depth - nv.z * 6 };
    roads.push({
      id: 'cul-' + k + '-' + i + '-' + j, name: `Cul-de-sac ${rng.int(2, 40)}`, cls: 'cul-de-sac', profile: 'cul-de-sac',
      points: [{ ...head1, y: 0 }, { ...mouth, y: 0 }, { ...head2, y: 0 }], smooth: 2,
      crossings: [], bumps: [], driveways: [], noEndCrossings: true, source: 'generated', hammerhead: true
    });
  }

  /* ---- greenway / canal-side path ---- */
  if (p.greenway && river) {
    for (const sgn of [-1, 1]) {
      const off = (p.riverWidth / 2 + 10) * sgn;
      const bb = blocks.reduce((o, bl) => { for (const q of bl.poly) { o.x0 = Math.min(o.x0, q.x); o.x1 = Math.max(o.x1, q.x); o.z0 = Math.min(o.z0, q.z); o.z1 = Math.max(o.z1, q.z); } return o; }, { x0: 1e9, x1: -1e9, z0: 1e9, z1: -1e9 });
      const pad = 56;
      const inside = q => q.x > bb.x0 - pad && q.x < bb.x1 + pad && q.z > bb.z0 - pad && q.z < bb.z1 + pad;
      let pts = river.map((q, idx) => { const nq = idx ? sub(q, river[idx - 1]) : sub(river[1], q); const nv = perp(norm(nq)); return { x: q.x + nv.x * off, z: q.z + nv.z * off, y: 0 }; });
      const kept = pts.filter(inside);
      if (kept.length < 4) continue;
      pts = kept;
      roads.push({ id: 'greenway-' + (sgn > 0 ? 'n' : 's'), name: 'Riverside walk', cls: 'path', profile: 'cyc-path', points: pts, smooth: 1, crossings: [], bumps: [], driveways: [], noEndCrossings: true, source: 'generated' });
    }
  }

  /* ---- railway + level crossings on the rail itself ---- */
  if (railLine) {
    const rail = {
      id: 'rail-1', name: 'Main line', cls: 'rail', profile: p.railStyle || 'rail-main',
      points: railLine.map(q => ({ ...q, y: p.railEmbankment ? .45 : 0 })), smooth: 1,
      crossings: [], bumps: [], driveways: [], noEndCrossings: true, source: 'generated', noEmbankment: false, retaining: true
    };
    roads.push(rail);
    const stPos = railLine[Math.floor(railLine.length / 2)];
    areas.push({ id: 'railway-station', polygon: square(stPos, 34, angle), mat: 'pav:concrete', y: .3, col: [.6, .6, .59], uv: 3, edge: true, kind: 'station', perimeterProp: 'lamp-modern', perimeterSpacing: 14 });
  }

  /* ---- metro under the strongest corridors ---- */
  const lines = Math.round(p.subway ?? 0);
  if (lines > 0) {
    const candidates = roads.filter(r => r.cls === 'arterial' && dist(r.points[0], r.points[r.points.length - 1]) > B * 1.6);
    candidates.sort((a, b) => dist(b.points[0], b.points[b.points.length - 1]) - dist(a.points[0], a.points[a.points.length - 1]));
    const chosen = [];
    for (const c of candidates) {
      if (chosen.length >= lines) break;
      const dirv = norm(sub(c.points[c.points.length - 1], c.points[0]));
      if (chosen.some(o => Math.abs(dot2(dirv, o.dir)) > .9)) continue;
      chosen.push({ dir: dirv, road: c });
    }
    chosen.forEach((ch, li) => {
      const src = ch.road;
      const pts = src.points.map(q => ({ ...q, y: p.subwayDepth }));
      const sw = {
        id: 'metro-' + li, name: `Metro line ${'ABCD'[li] || li + 1}`, cls: 'subway', profile: 'subway-line',
        points: pts, smooth: src.smooth, crossings: [], bumps: [], driveways: [], noEndCrossings: true, source: 'generated',
        stationEvery: B * 1.35
      };
      sw.profile = { ...profileById('subway-line'), props: { ...profileById('subway-line').props, stations: { spacing: sw.stationEvery } } };
      sw._profile = sw.profile;
      sw.stations = [];
      roads.push(sw);
      // station names from the junctions it passes
      for (const n of nodes) {
        const near = nearestOnPolyline(pts, n);
        if (near.d < B * .62 && (n.avenue || n.iAmAve)) nearRoadStation(sw, n, near.s, pts);
      }
    });
  }
  function nearRoadStation(road, node, s, pts) {
    road.stations = road.stations || [];
    if (road.stations.some(x => Math.abs(x.s - s) < B * .8)) return;
    road.stations.push({ s, name: stationName(node), x: node.x, z: node.z });
  }
  function stationName(node) {
    const arm0 = node.arms[0]?.road;
    const arm1 = node.arms[1]?.road;
    const a = (arm0?.name || 'North').split(' ').slice(0, 2).join(' ');
    const b = (arm1?.name || 'Central').split(' ').slice(0, 2).join(' ');
    return a === b ? a + ' Central' : a + ' & ' + b;
  }

  /* ---- junction styling + node arms ---- */
  for (const n of nodes) {
    const arms = n.arms.filter(a => a.road);
    if (arms.length < 2) { n.isolated = true; continue; }
    const avers = arms.filter(a => a.road.cls === 'arterial' || a.road.cls === 'freeway').length;
    const degree = arms.length;
    if (degree >= 4 && (avers >= 2 || (n.avenue && avers))) n.style = 'signals';
    else if (degree >= 3 && p.roundabouts && rng.chance(p.roundabouts) && degree === 4 && avers === 0) { n.style = 'roundabout'; n.radius = 5.4; }
    else if (degree >= 3) n.style = 'give-way';
    else n.style = 'uncontrolled';
    if (n.style === 'roundabout' && degree < 3) n.style = 'give-way';
    for (const a of arms) {
      a.x = n.x; a.z = n.z;
      const pts = a.road.points;
      const other = a.end === 's' ? (pts[1] || pts[pts.length - 1]) : (pts[pts.length - 2] || pts[0]);
      a.dir = norm(sub(other, { x: n.x, z: n.z }));
      a.node = n.id;
    }
  }

  const blocksInfo = [];
  /* ---- context massing ---- */
  if (p.massing) {
    for (const blk of blocks) {
      if (usedBlocks.has(key(blk.i, blk.j))) continue;
      const downtown = 1 - clamp(Math.hypot((blk.i + .5 - half.x) / (nx * .34), (blk.j + .5 - half.z) / (nz * .34)), 0, 1);
      if (river && segNearPolyline(blk.poly[0], blk.poly[2], river, riverHalf * .55)) continue;
      const cover = clamp(p.massingCover, .3, .95);
      const lots = splitBlock(blk.poly, blk.cen, rng, 2 + Math.round(downtown * 3));
      for (const lot of lots) {
        if (rng.chance(1 - cover)) continue;
        const h = (6 + Math.pow(rng.next(), 2.1) * (10 + downtown * 78) * (p.massingHeight || 1)) * (1 + (downtown > .8 ? rng.next() * 1.6 : 0));
        const cen = centroid(lot);
        const size = polyExtent(lot);
        blocksInfo.push({ x: cen.x, z: cen.z, y: 0, sx: size.w * .92, sz: size.h * .92, sy: h, rot: rng.range(0, .02) + angle, lot });
      }
    }
  }

  /* final: name junctions for the outliner */
  for (const n of nodes) if (n.arms.length >= 2) n.name = junctionName(n);
  meta.city = city; meta.blocks = blocks.length; meta.nodeCount = nodes.length;
  return { roads, nodes: nodes.filter(n => n.arms.length >= 2 && !n.isolated), water, areas, blocks, meta, params: p, massing: blocksInfo };
}

function junctionName(n) {
  const names = n.arms.map(a => a.road?.name).filter(Boolean);
  const uniq = [...new Set(names.map(x => x.split(' ').slice(0, 2).join(' ')))];
  return uniq.length > 1 ? uniq[0] + ' × ' + uniq[1] : (uniq[0] || 'Junction');
}
function centroid(poly) { let x = 0, z = 0; for (const p of poly) { x += p.x; z += p.z; } return { x: x / poly.length, z: z / poly.length }; }
function polyExtent(poly) {
  const c = centroid(poly);
  let r = 0;
  for (const p of poly) r = Math.max(r, dist(p, c));
  return { w: r * 1.32, h: r * 1.32 };
}
/** cut a convex block into `n` strips so buildings read as separate plots */
function splitBlock(poly, cen, rng, n) {
  if (n < 2) return [poly];
  const out = [];
  const a = rng.range(0, Math.PI);
  const dirv = { x: Math.cos(a), z: Math.sin(a) };
  const nv = { x: -dirv.z, z: dirv.x };
  const proj = poly.map(p => ({ d: p.x * dirv.x + p.z * dirv.z, l: p.x * nv.x + p.z * nv.z, p }));
  const min = Math.min(...proj.map(q => q.d)), max = Math.max(...proj.map(q => q.d));
  let prev = min;
  for (let k = 1; k <= n; k++) {
    const cut = k === n ? max : lerp(min, max, k / n) + rng.gauss((max - min) / n * .12);
    const lo = prev, hi = cut;
    prev = cut;
    if (hi - lo < 3) continue;
    const pts = [];
    const pad = 3.2;
    pts.push({ x: cen.x + dirv.x * (lo + pad) + nv.x * 60, z: cen.z + dirv.z * (lo + pad) + nv.z * 60 });
    pts.push({ x: cen.x + dirv.x * (hi - pad) + nv.x * 60, z: cen.z + dirv.z * (hi - pad) + nv.z * 60 });
    pts.push({ x: cen.x + dirv.x * (hi - pad) - nv.x * 60, z: cen.z + dirv.z * (hi - pad) - nv.z * 60 });
    pts.push({ x: cen.x + dirv.x * (lo + pad) - nv.x * 60, z: cen.z + dirv.z * (lo + pad) - nv.z * 60 });
    const clipped = clipConvex(pts, poly);
    if (clipped.length > 2) out.push(clipped);
  }
  return out.length ? out : [poly];
}
/** Sutherland–Hodgman: block polygon ∩ plot strip (both convex-ish) */
function clipConvex(subject, clip) {
  let out = subject.slice();
  for (let i = 0; i < clip.length && out.length; i++) {
    const a = clip[i], b = clip[(i + 1) % clip.length];
    const inside = p => (b.x - a.x) * (p.z - a.z) - (b.z - a.z) * (p.x - a.x) >= 0;
    const input = out; out = [];
    for (let j = 0; j < input.length; j++) {
      const cur = input[j], nxt = input[(j + 1) % input.length];
      const ci = inside(cur), ni = inside(nxt);
      if (ci) out.push(cur);
      if (ci !== ni) {
        const t = lineIntersection(a, b, cur, nxt);
        if (t) out.push(t);
      }
    }
  }
  return out;
}
function lineIntersection(a, b, c, d) {
  const p = sub(b, a), q = sub(d, c);
  const den = p.x * q.z - p.z * q.x;
  if (Math.abs(den) < 1e-9) return null;
  const t = ((c.x - a.x) * q.z - (c.z - a.z) * q.x) / den;
  return { x: a.x + p.x * t, z: a.z + p.z * t };
}

/* ------------------------------- presets ------------------------------- */
export const CITY_PRESETS = {
  'compact-grid': { label: 'Compact grid · 6×5, transit rich', p: { blocksX: 6, blocksZ: 5, blockSize: 108, avenueEvery: 2, subway: 2, cycle: 'all', roundabouts: .18 } },
  'river-capital': { label: 'River capital · bridges + embankments', p: { blocksX: 8, blocksZ: 6, blockSize: 116, river: true, riverWidth: 58, riverAngle: 8, rail: true, subway: 2, cycle: 'arterials', parks: .18 } },
  'freeway-city': { label: 'Freeway city · wide ring, no metro', p: { blocksX: 7, blocksZ: 6, blockSize: 132, avenueEvery: 3, ring: true, subway: 0, bus: false, parking: true, cycle: 'none', lots: 7, massingHeight: .8 } },
  'transit-town': { label: 'Transit town · tram + BRT + woonerfen', p: { blocksX: 6, blocksZ: 6, blockSize: 96, avenueEvery: 2, cycle: 'grid', bus: true, roundabouts: .5, massingHeight: .7, oldTown: true, rail: false } },
  'industrial-port': { label: 'Port / industrial · wide bays, rail spur', p: { blocksX: 6, blocksZ: 4, blockSize: 150, avenueEvery: 2, massingHeight: .45, massingCover: .55, parks: .06, lots: 8, rail: true, subway: 0, cycle: 'none' } },
  'garden-suburb': { label: 'Garden suburb · low key, culs-de-sac', p: { blocksX: 7, blocksZ: 5, blockSize: 96, avenueEvery: 4, meander: 12, roundabouts: .6, subway: 0, rail: false, parks: .3, culs: 8, massingHeight: .45 } },
};
export function applyPreset(name, params) {
  const preset = CITY_PRESETS[name];
  return { ...(params || {}), ...(preset ? preset.p : {}), preset: name };
}

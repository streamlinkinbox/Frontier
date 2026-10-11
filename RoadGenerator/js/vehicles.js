// Traffic: procedural vehicle models (lofted bodies, glazing bands, wheels, lamps) and a lane-following
// simulation. Hard cap of 25 simultaneous vehicles. Vehicles change approach at junctions along a
// Bezier that runs through the junction centre, so they never teleport across the intersection.

import { MeshBuilder, normalize } from './mesh.js';
import { pointAt } from './spline.js';
import { nodeArms, indexNodes } from './model.js';
import { rng } from './math.js';

export const MAX_VEHICLES = 25;
const STRIDE = 12;

// ---------- models ------------------------------------------------------------------------------
// Loft: a list of cross-sections along z (front -> back). Each section is a rounded rectangle.
function loft(mb, sections, colorOf, { segCorner = 3, mat = -1 } = {}) {
  const rings = sections.map(sec => {
    const { z, hw, y0, y1, r } = sec;
    const rr = Math.min(r, hw * 0.9, (y1 - y0) / 2 * 0.95);
    const pts = [];
    const corner = (cx, cy, a0) => {
      for (let i = 0; i <= segCorner; i++) {
        const a = a0 + (i / segCorner) * (Math.PI / 2);
        pts.push([cx + Math.cos(a) * rr, cy + Math.sin(a) * rr]);
      }
    };
    // Walk the rounded rectangle counter-clockwise in (x, y).
    corner(hw - rr, y0 + rr, -Math.PI / 2);
    corner(hw - rr, y1 - rr, 0);
    corner(-hw + rr, y1 - rr, Math.PI / 2);
    corner(-hw + rr, y0 + rr, Math.PI);
    return pts.map(([x, y]) => [x, y, z]);
  });
  const centroid = (ring) => [0, 1].map(c => ring.reduce((s, p) => s + p[c], 0) / ring.length);
  for (let k = 0; k < rings.length - 1; k++) {
    const A = rings[k], B = rings[k + 1];
    const n = A.length;
    const [ccx, ccy] = centroid(A);
    for (let i = 0; i < n; i++) {
      const j = (i + 1) % n;
      const a = A[i], b = A[j], c = B[j], d = B[i];
      const mid = [(a[0] + b[0] + c[0] + d[0]) / 4, (a[1] + b[1] + c[1] + d[1]) / 4, (a[2] + b[2] + c[2] + d[2]) / 4];
      const col = colorOf(mid, k, i, n);
      // Outward normal: from the section centre through the face midpoint (xy plane).
      mb.quad(a, b, c, d, { col: col.rgb, mat: col.mat ?? mat, expect: [mid[0] - ccx, mid[1] - ccy, 0] });
    }
  }
  // Caps
  const first = rings[0], last = rings[rings.length - 1];
  for (const [ring, sign] of [[first, -1], [last, 1]]) {
    const cx = ring.reduce((s, p) => s + p[0], 0) / ring.length;
    const cy = ring.reduce((s, p) => s + p[1], 0) / ring.length;
    const cz = ring[0][2];
    for (let i = 0; i < ring.length; i++) {
      const j = (i + 1) % ring.length;
      const col = colorOf([cx, cy, cz], 0, i, ring.length);
      mb.tri([cx, cy, cz], ring[i], ring[j], { col: col.rgb, mat: col.mat ?? mat, expect: [0, 0, sign] });
    }
  }
}

function revolveX(mb, cx, cy, cz, prof, seg, col) {
  for (let i = 0; i < prof.length - 1; i++) {
    const [r0, x0] = prof[i], [r1, x1] = prof[i + 1];
    for (let k = 0; k < seg; k++) {
      const a0 = (k / seg) * Math.PI * 2, a1 = ((k + 1) / seg) * Math.PI * 2;
      const P = (r, x, a) => [cx + x, cy + Math.cos(a) * r, cz + Math.sin(a) * r];
      const nrm = (a) => normalize([0, Math.cos(a), Math.sin(a)]) || [0, 1, 0];
      mb.quad(P(r0, x0, a0), P(r1, x1, a0), P(r1, x1, a1), P(r0, x0, a1), { col, mat: -1, normals: [nrm(a0), nrm(a0), nrm(a1), nrm(a1)] });
    }
  }
}

function box(mb, cx, cy, cz, sx, sy, sz, col, mat = -1) {
  const x0 = cx - sx / 2, x1 = cx + sx / 2, y0 = cy - sy / 2, y1 = cy + sy / 2, z0 = cz - sz / 2, z1 = cz + sz / 2;
  const faces = [
    [[x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1], [0, 0, 1]],
    [[x1, y0, z0], [x0, y0, z0], [x0, y1, z0], [x1, y1, z0], [0, 0, -1]],
    [[x1, y0, z1], [x1, y0, z0], [x1, y1, z0], [x1, y1, z1], [1, 0, 0]],
    [[x0, y0, z0], [x0, y0, z1], [x0, y1, z1], [x0, y1, z0], [-1, 0, 0]],
    [[x0, y1, z1], [x1, y1, z1], [x1, y1, z0], [x0, y1, z0], [0, 1, 0]],
  ];
  for (const f of faces) mb.quad(f[0], f[1], f[2], f[3], { col, mat, expect: f[4] });
}

const PAINT = [0.5, 0.5, 0.5]; // replaced per instance at transform time (mat -5 marks paint)
const GLASS = [0.06, 0.08, 0.1];
const TIRE = [0.035, 0.035, 0.037];
const RIM = [0.62, 0.63, 0.66];
const HEAD = [1.0, 0.97, 0.88];
const TAIL = [1.0, 0.1, 0.08];

export const VEHICLE_TYPES = {
  car: { label: 'Car', L: 4.6, W: 1.8, bodyTop: 0.9, cabinTop: 1.42, cabinFrom: -1.2, cabinTo: 0.9, wheelZ: 1.45, wheelR: 0.33 },
  suv: { label: 'SUV', L: 4.9, W: 1.9, bodyTop: 1.0, cabinTop: 1.72, cabinFrom: -1.5, cabinTo: 1.0, wheelZ: 1.5, wheelR: 0.36 },
  van: { label: 'Van', L: 5.3, W: 2.0, bodyTop: 2.35, cabinTop: 2.35, cabinFrom: -2.5, cabinTo: 2.4, wheelZ: 1.7, wheelR: 0.37 },
  bus: { label: 'Bus', L: 11.2, W: 2.55, bodyTop: 3.15, cabinTop: 3.15, cabinFrom: -5.6, cabinTo: 5.6, wheelZ: 3.6, wheelR: 0.5 },
};

function buildTemplate(type, seedColor) {
  const T = VEHICLE_TYPES[type];
  const mb = new MeshBuilder();
  const hl = T.L / 2, hw = T.W / 2;
  const isBox = type === 'van' || type === 'bus';
  if (type === 'bus') {
    // Single boxy body with glazing bands on the sides
    const secs = [
      { z: -hl, hw: hw - 0.12, y0: 0.45, y1: T.bodyTop - 0.1, r: 0.25 },
      { z: -hl + 0.6, hw, y0: 0.4, y1: T.bodyTop, r: 0.3 },
      { z: hl - 0.6, hw, y0: 0.4, y1: T.bodyTop, r: 0.3 },
      { z: hl, hw: hw - 0.12, y0: 0.45, y1: T.bodyTop - 0.1, r: 0.25 },
    ];
    loft(mb, secs, (mid) => {
      const sideGlass = Math.abs(Math.abs(mid[0]) - hw) < 0.2 && mid[1] > 1.1 && mid[1] < 2.6 && mid[2] > -hl + 0.9 && mid[2] < hl - 0.9;
      if (sideGlass) return { rgb: GLASS };
      if (mid[1] > T.bodyTop - 0.6 && Math.abs(mid[0]) < hw - 0.3) return { rgb: seedColor.roof };
      return { rgb: PAINT, mat: -5 };
    });
  } else {
    // Body (lower) + cabin (upper). Front is +z.
    const body = [
      { z: -hl, hw: hw * 0.9, y0: 0.26, y1: 0.72, r: 0.14 },
      { z: -hl + 0.15, hw: hw * 0.98, y0: 0.24, y1: 0.85, r: 0.2 },
      { z: T.cabinFrom, hw, y0: 0.24, y1: T.bodyTop, r: 0.2 },
      { z: T.cabinTo, hw, y0: 0.24, y1: T.bodyTop, r: 0.2 },
      { z: hl - 0.9, hw: hw * 0.98, y0: 0.26, y1: T.bodyTop - 0.12, r: 0.2 },
      { z: hl - 0.15, hw: hw * 0.92, y0: 0.3, y1: T.bodyTop - 0.25, r: 0.15 },
      { z: hl, hw: hw * 0.82, y0: 0.32, y1: T.bodyTop - 0.4, r: 0.12 },
    ];
    loft(mb, body, (mid) => (mid[1] < 0.3 ? { rgb: [0.05, 0.05, 0.05] } : { rgb: PAINT, mat: -5 }));
    if (type !== 'van') {
      const cab = [
        { z: T.cabinFrom, hw: hw * 0.88, y0: T.bodyTop - 0.02, y1: T.cabinTop, r: 0.25 },
        { z: T.cabinFrom + 0.5, hw: hw * 0.9, y0: T.bodyTop - 0.02, y1: T.cabinTop, r: 0.28 },
        { z: T.cabinTo - 0.4, hw: hw * 0.9, y0: T.bodyTop - 0.02, y1: T.cabinTop - 0.05, r: 0.28 },
        { z: T.cabinTo, hw: hw * 0.86, y0: T.bodyTop - 0.02, y1: T.bodyTop + 0.03, r: 0.22 },
      ];
      loft(mb, cab, (mid) => {
        const side = Math.abs(mid[0]) > hw * 0.8 && mid[1] < T.cabinTop - 0.12;
        if (side && mid[2] > T.cabinFrom + 0.4 && mid[2] < T.cabinTo - 0.4) return { rgb: GLASS };
        return { rgb: PAINT, mat: -5 };
      });
    } else {
      // van: glass band on the front windscreen only
      box(mb, 0, T.cabinTop - 0.5, hl - 0.05, T.W * 0.7, 0.5, 0.05, GLASS);
    }
    // wheel arches are implied by the dark lower body; add tyres + rims
    for (const sx of [-1, 1]) for (const sz of [-1, 1]) {
      const wx = sx * (hw - 0.04), wz = sz * T.wheelZ, r = T.wheelR;
      revolveX(mb, wx, r, wz, [[r * 0.62, -0.1], [r * 0.9, -0.12], [r, -0.1], [r, 0.1], [r * 0.9, 0.12], [r * 0.62, 0.1]], 14, TIRE);
      revolveX(mb, wx, r, wz, [[0.0, -0.11], [r * 0.6, -0.11], [r * 0.6, 0.11], [0.0, 0.11]], 10, RIM);
    }
  }
  // headlights (front) and tail lamps (back)
  const hlz = isBox ? T.L / 2 + 0.01 : T.L / 2 + 0.01;
  const lampY = type === 'bus' ? 1.7 : 0.7;
  for (const sx of [-1, 1]) {
    const x = sx * (hw - 0.25), yy = lampY;
    mbQuad(mb, [x - 0.18, yy - 0.05, hlz], [x + 0.18, yy - 0.05, hlz], [x + 0.18, yy + 0.05, hlz], [x - 0.18, yy + 0.05, hlz], HEAD, -2, [0, 0, 1]);
    mbQuad(mb, [x - 0.2, yy - 0.04, -hlz], [x + 0.2, yy - 0.04, -hlz], [x + 0.2, yy + 0.04, -hlz], [x - 0.2, yy + 0.04, -hlz], TAIL, -2, [0, 0, -1]);
  }
  return mb.build();
}

function mbQuad(mb, a, b, c, d, col, mat, n) {
  mb.quad(a, b, c, d, { col, mat, expect: n });
}

const TEMPLATES = {};
export function vehicleTemplate(type, paintSeed = 0) {
  // Templates are shared per type; paint colour is applied per instance.
  const key = type;
  if (!TEMPLATES[key]) TEMPLATES[key] = buildTemplate(type, { roof: [0.9, 0.9, 0.9] });
  return TEMPLATES[key];
}

const PAINTS = [
  [0.86, 0.86, 0.87], [0.09, 0.1, 0.12], [0.62, 0.08, 0.07], [0.12, 0.25, 0.5], [0.85, 0.7, 0.2],
  [0.2, 0.42, 0.28], [0.7, 0.72, 0.75], [0.95, 0.95, 0.95], [0.1, 0.1, 0.1], [0.45, 0.2, 0.5],
];

// ---------- traffic simulation ---------------------------------------------------------------
export class Traffic {
  constructor() {
    this.vehicles = [];
    this.rand = rng(4242);
    this.paused = false;
    this.stats = { active: 0, transfers: 0 };
  }

  // Keep the set of eligible roads in sync; re-seed when the network topology changes.
  sync(net, project, settings) {
    // Always refresh references: the network is rebuilt on every edit.
    this.net = net;
    this.project = project;
    this.eligible = [...net.roadInfo.values()].filter(i => i.road.traffic && i.s1 - i.s0 > 20);
    this.nodeArms = nodeArms(project);
    this.nodes = indexNodes(project);
    // Vehicles on roads that disappeared or stopped being eligible are dropped and respawned.
    this.vehicles = this.vehicles.filter(v => {
      const info = net.roadInfo.get(v.roadId);
      if (!info || !info.road.traffic || info.s1 - info.s0 <= 20) return false;
      if (!v.bridge) v.s = Math.min(Math.max(v.s, info.s0), info.s1);
      return true;
    });
    const want = settings.trafficOn ? Math.min(MAX_VEHICLES, Math.max(0, settings.trafficCount | 0)) : 0;
    while (this.vehicles.length < want && this.eligible.length) this.vehicles.push(this.spawn());
    this.vehicles = this.vehicles.filter(Boolean);
    if (this.vehicles.length > want) this.vehicles.length = want;
  }

  spawn() {
    if (!this.eligible || !this.eligible.length) return null;
    const info = this.rand.pick(this.eligible);
    const r = this.rand;
    const dir = r() < 0.5 ? 1 : -1;
    const lane = r.int(0, info.dims.lanesPerDir - 1);
    const span = info.s1 - info.s0;
    const s = info.s0 + r() * span;
    const type = this.pickType(info);
    const paint = PAINTS[r.int(0, PAINTS.length - 1)];
    return {
      roadId: info.road.id, dir, lane, s, type, paint,
      speed: (info.road.speed / 3.6) * r.range(0.82, 1.0),
      target: 0, bridge: null, key: Math.random(),
    };
  }

  pickType(info) {
    const r = this.rand();
    const big = info.road.cls === 'arterial' || info.road.cls === 'highway';
    if (big && r < 0.07) return 'bus';
    if (r < 0.2) return 'van';
    if (r < 0.45) return 'suv';
    return 'car';
  }

  lateral(info, dir, lane) {
    const d = info.dims;
    const base = d.mh + (lane + 0.5) * d.laneWidth;
    return dir > 0 ? -base : base;
  }

  worldOf(info, s, dir, lane) {
    const p = pointAt(info.curve, s);
    const lat = this.lateral(info, dir, lane);
    const nx = -p.tz, nz = p.tx;
    return { x: p.x + nx * lat, z: p.z + nz * lat, hx: p.tx * dir, hz: p.tz * dir };
  }

  step(dt) {
    if (this.paused || !this.vehicles.length) return;
    const dtc = Math.min(dt, 0.1);
    const infoOf = id => this.net.roadInfo.get(id);
    // Leader gap check per lane (same road, direction, lane)
    this.vehicles = this.vehicles.filter(v => !v.dead);
    for (const v of this.vehicles) {
      const info = infoOf(v.roadId);
      if (!info) { v.dead = true; continue; }
      if (v.bridge) {
        v.bridge.t += dtc / v.bridge.dur;
        if (v.bridge.t >= 1) v.bridge = null;
        continue;
      }
      let limit = v.speed;
      for (const o of this.vehicles) {
        if (o === v || o.roadId !== v.roadId || o.dir !== v.dir || o.lane !== v.lane || o.bridge) continue;
        const gap = (o.s - v.s) * v.dir;
        if (gap > 0 && gap < 14) limit = Math.min(limit, Math.max(0, o.speed * (gap - 6) / 8));
      }
      v.speed = v.speed + (limit - v.speed) * Math.min(1, dtc * 2.5);
      v.s += v.dir * v.speed * dtc;
      const endHit = v.dir > 0 ? v.s >= info.s1 : v.s <= info.s0;
      if (endHit) this.transfer(v, info);
    }
  }

  transfer(v, info) {
    const atEnd = v.dir > 0;
    const nodeId = atEnd ? info.road.b : info.road.a;
    const cur = this.worldOf(info, atEnd ? info.s1 : info.s0, v.dir, v.lane);
    const node = this.nodes.get(nodeId);
    const candidates = (this.nodeArms.get(nodeId) || []).filter(a => a.road.id !== info.road.id && this.net.roadInfo.has(a.road.id) && this.net.roadInfo.get(a.road.id).road.traffic);
    let target;
    if (candidates.length) {
      const pick = this.rand.pick(candidates);
      const ni = this.net.roadInfo.get(pick.road.id);
      const dir2 = pick.atStart ? 1 : -1;
      const lane2 = Math.min(v.lane, ni.dims.lanesPerDir - 1);
      const s2 = pick.atStart ? ni.s0 : ni.s1;
      v.roadId = ni.road.id; v.dir = dir2; v.lane = lane2; v.s = s2;
      target = this.worldOf(ni, s2, dir2, lane2);
    } else {
      // dead end: turn around on the same road
      v.dir = -v.dir;
      v.s = atEnd ? info.s1 : info.s0;
      target = this.worldOf(info, v.s, v.dir, v.lane);
    }
    const ctrl = node ? [node.x, node.z] : [(cur.x + target.x) / 2, (cur.z + target.z) / 2];
    const dist = Math.hypot(target.x - cur.x, target.z - cur.z);
    v.bridge = {
      p0: [cur.x, cur.z], c: ctrl, p2: [target.x, target.z],
      t: 0, dur: Math.max(0.6, dist / Math.max(v.speed, 3)),
    };
    this.stats.transfers++;
  }

  // Current world pose of a vehicle.
  pose(v) {
    if (v.bridge) {
      const b = v.bridge;
      const t = Math.min(1, b.t);
      const u = 1 - t;
      const x = u * u * b.p0[0] + 2 * u * t * b.c[0] + t * t * b.p2[0];
      const z = u * u * b.p0[1] + 2 * u * t * b.c[1] + t * t * b.p2[1];
      const dx = 2 * u * (b.c[0] - b.p0[0]) + 2 * t * (b.p2[0] - b.c[0]);
      const dz = 2 * u * (b.c[1] - b.p0[1]) + 2 * t * (b.p2[1] - b.c[1]);
      const l = Math.hypot(dx, dz) || 1;
      return { x, z, hx: dx / l, hz: dz / l };
    }
    const info = this.net.roadInfo.get(v.roadId);
    if (!info) return { x: 0, z: 0, hx: 1, hz: 0 };
    return this.worldOf(info, v.s, v.dir, v.lane);
  }

  // Dynamic mesh for all vehicles (positions transformed on the CPU; at most 25 instances).
  build() {
    const vertsPer = [];
    let totalV = 0, totalI = 0;
    const parts = [];
    for (const v of this.vehicles) {
      const t = vehicleTemplate(v.type);
      parts.push(t);
      totalV += t.vertexCount;
      totalI += t.indexCount;
    }
    const verts = new Float32Array(totalV * STRIDE);
    const idx = new Uint32Array(totalI);
    let vOff = 0, iOff = 0, vBase = 0;
    this.vehicles.forEach((v, n) => {
      const t = parts[n];
      const pose = this.pose(v);
      const fx = pose.hx, fz = pose.hz;
      // local -> world: x*R + y*U + z*F with R = (fz, 0, -fx), F = (fx, 0, fz)
      const rx = fz, rz = -fx;
      const src = t.vertices;
      const paint = v.paint;
      for (let i = 0; i < t.vertexCount; i++) {
        const o = i * STRIDE, w = (vBase + i) * STRIDE;
        const lx = src[o], ly = src[o + 1], lz = src[o + 2];
        const nx = src[o + 3], ny = src[o + 4], nz = src[o + 5];
        const x = rx * lx + fx * lz + pose.x;
        const y = ly + 0.0;
        const z = rz * lx + fz * lz + pose.z;
        verts[w] = x; verts[w + 1] = y; verts[w + 2] = z;
        const wnx = rx * nx + fx * nz, wny = ny, wnz = rz * nx + fz * nz;
        verts[w + 3] = wnx; verts[w + 4] = wny; verts[w + 5] = wnz;
        verts[w + 6] = x; verts[w + 7] = z;
        let r = src[o + 8], g = src[o + 9], b = src[o + 10], m = src[o + 11];
        if (m === -5) { r = paint[0]; g = paint[1]; b = paint[2]; m = -1; }
        verts[w + 8] = r; verts[w + 9] = g; verts[w + 10] = b; verts[w + 11] = m;
      }
      const ix = t.indices;
      for (let i = 0; i < t.indexCount; i++) idx[iOff + i] = ix[i] + vBase;
      vBase += t.vertexCount;
      iOff += t.indexCount;
    });
    this.stats.active = this.vehicles.length;
    return { vertices: verts, indices: idx };
  }
}

// Builds three.js geometry from the topology graph. All road surfaces, markings,
// bridges and props go into a handful of vertex-coloured meshes.
import * as THREE from 'three';
import { normalAt, pointAtS, cumLen, sliceS, makeRng, clamp } from './geometry.js';
import { junctionFootprint, trimmedEdgePts, trimDistance, JUNCTION_Y } from './junction.js';
import { BRIDGE_TYPES } from './network.js';

const RAISED = new Set(['sidewalk', 'platform', 'median']);
const NO_MARK = new Set(['country', 'offroad', 'footpath', 'railway', 'subway']);
const RAISE_H = 0.15;

const PAL = {
  car: [0.22, 0.22, 0.24],
  bus: [0.45, 0.12, 0.1],
  bike: [0.12, 0.42, 0.24],
  parking: [0.3, 0.3, 0.32],
  sidewalk: [0.62, 0.61, 0.58],
  platform: [0.7, 0.68, 0.64],
  footpath: [0.66, 0.6, 0.45],
  median: [0.2, 0.42, 0.18],
  shoulder: [0.42, 0.4, 0.34],
  grass: [0.26, 0.46, 0.2],
  dirt: [0.4, 0.29, 0.17],
  rail: [0.33, 0.31, 0.28],
  curb: [0.5, 0.5, 0.48],
  white: [0.92, 0.92, 0.9],
  yellow: [0.95, 0.78, 0.12],
  red: [0.75, 0.1, 0.08],
  deck: [0.42, 0.42, 0.44],
  parapet: [0.7, 0.7, 0.68],
  steel: [0.5, 0.52, 0.56],
  pier: [0.6, 0.58, 0.55],
  rails: [0.7, 0.7, 0.72],
  sleeper: [0.3, 0.2, 0.12],
  bollard: [0.95, 0.8, 0.1],
  trunk: [0.36, 0.25, 0.14],
  pole: [0.3, 0.3, 0.32],
};

// Per-road asphalt tint so repeated profiles still look different.
function asphalt(tint) {
  const v = 0.2 + tint * 0.08;
  return [v, v * 0.98, v * 1.04];
}
function tintColor(base, tint, amt = 0.12) {
  const k = 1 + (tint - 0.5) * amt;
  return base.map((c) => clamp(c * k, 0, 1));
}

class MB {
  constructor() {
    this.pos = [];
    this.nor = [];
    this.col = [];
    this.idx = [];
  }
  get count() {
    return this.pos.length / 3;
  }
  quad(a, b, c, d, col) {
    const ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    const vx = d[0] - a[0], vy = d[1] - a[1], vz = d[2] - a[2];
    let nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    const l = Math.hypot(nx, ny, nz) || 1;
    nx /= l; ny /= l; nz /= l;
    const base = this.count;
    for (const p of [a, b, c, d]) {
      this.pos.push(p[0], p[1], p[2]);
      this.nor.push(nx, ny, nz);
      this.col.push(col[0], col[1], col[2]);
    }
    this.idx.push(base, base + 1, base + 2, base, base + 2, base + 3);
  }
  // Ribbon between polyline A and polyline B (same length).
  strip(A, B, col) {
    for (let i = 0; i < A.length - 1; i++) this.quad(A[i], B[i], B[i + 1], A[i + 1], col);
  }
  // Oriented box: centre (cx,cz), base y0, top y1, tangent (tx,tz), half extents along/across.
  box(cx, cz, y0, y1, tx, tz, ha, hb, col) {
    const nx = -tz;
    const nz = tx;
    const P = (a, b, y) => [cx + tx * a + nx * b, y, cz + tz * a + nz * b];
    this.quad(P(-ha, -hb, y1), P(ha, -hb, y1), P(ha, hb, y1), P(-ha, hb, y1), col); // top
    this.quad(P(ha, -hb, y0), P(ha, -hb, y1), P(ha, hb, y1), P(ha, hb, y0), col);
    this.quad(P(-ha, hb, y0), P(-ha, hb, y1), P(-ha, -hb, y1), P(-ha, -hb, y0), col);
    this.quad(P(-ha, hb, y0), P(ha, hb, y0), P(ha, hb, y1), P(-ha, hb, y1), col);
    this.quad(P(ha, -hb, y0), P(-ha, -hb, y0), P(-ha, -hb, y1), P(ha, -hb, y1), col);
  }
  // Square-section beam between two points.
  beam(A, B, t, col) {
    let dx = B[0] - A[0], dy = B[1] - A[1], dz = B[2] - A[2];
    const l = Math.hypot(dx, dy, dz) || 1;
    dx /= l; dy /= l; dz /= l;
    let up = [0, 1, 0];
    if (Math.abs(dy) > 0.95) up = [1, 0, 0];
    // side = dir x up
    let sx = dy * up[2] - dz * up[1], sy = dz * up[0] - dx * up[2], sz = dx * up[1] - dy * up[0];
    const sl = Math.hypot(sx, sy, sz) || 1;
    sx /= sl; sy /= sl; sz /= sl;
    // up2 = side x dir
    const ux = sy * dz - sz * dy, uy = sz * dx - sx * dz, uz = sx * dy - sy * dx;
    const C = (P, a, b) => [P[0] + sx * a + ux * b, P[1] + sy * a + uy * b, P[2] + sz * a + uz * b];
    const k = [[-t, -t], [t, -t], [t, t], [-t, t]];
    for (let i = 0; i < 4; i++) {
      const [a0, b0] = k[i];
      const [a1, b1] = k[(i + 1) % 4];
      this.quad(C(A, a0, b0), C(A, a1, b1), C(B, a1, b1), C(B, a0, b0), col);
    }
  }
  geometry() {
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(this.pos, 3));
    g.setAttribute('normal', new THREE.Float32BufferAttribute(this.nor, 3));
    g.setAttribute('color', new THREE.Float32BufferAttribute(this.col, 3));
    g.setIndex(this.idx);
    g.computeBoundingSphere();
    return g;
  }
}

// Ribbon polyline between offsets o0 and o1 (lateral to direction) at height dy.
function ribbonPts(pts, o0, o1, dy) {
  const A = [];
  const B = [];
  for (let i = 0; i < pts.length; i++) {
    const [nx, nz] = normalAt(pts, i);
    A.push([pts[i][0] + nx * o0, pts[i][2] + dy, pts[i][1] + nz * o0]);
    B.push([pts[i][0] + nx * o1, pts[i][2] + dy, pts[i][1] + nz * o1]);
  }
  return [A, B];
}
// Convert [x,z,y] pts to [x,y,z] order for MB and back-compat helpers.
function ribbon(mb, pts, o0, o1, col, dy = 0) {
  const [A, B] = ribbonPts(pts, o0, o1, dy);
  mb.strip(A, B, col);
}

// Vertical face along the polyline at lateral offset o from y0 to y1.
function curbFace(mb, pts, o, y0, y1, col) {
  const A = [];
  const B = [];
  for (let i = 0; i < pts.length; i++) {
    const [nx, nz] = normalAt(pts, i);
    A.push([pts[i][0] + nx * o, pts[i][2] + y0, pts[i][1] + nz * o]);
    B.push([pts[i][0] + nx * o, pts[i][2] + y1, pts[i][1] + nz * o]);
  }
  mb.strip(A, B, col);
}

function dashed(mb, pts, cum, o, w, dash, gap, col, dy) {
  const L = cum[cum.length - 1];
  let s = 0;
  while (s < L) {
    const e = Math.min(L, s + dash);
    if (e - s > 0.2) {
      const seg = sliceS(pts, cum, s, e);
      ribbon(mb, seg, o - w / 2, o + w / 2, col, dy);
    }
    s = e + gap;
  }
}

// Quad across the road between two stations, offsets o0..o1.
function acrossQuad(mb, pts, cum, s0, s1, o0, o1, col, dy) {
  const f = (s, o) => {
    const p = pointAtS(pts, cum, s);
    const a = pointAtS(pts, cum, clamp(s - 0.1, 0, cum[cum.length - 1]));
    const b = pointAtS(pts, cum, clamp(s + 0.1, 0, cum[cum.length - 1]));
    let tx = b[0] - a[0], tz = b[1] - a[1];
    const l = Math.hypot(tx, tz) || 1;
    tx /= l; tz /= l;
    return [p[0] - tz * o, p[2] + dy, p[1] + tx * o];
  };
  mb.quad(f(s0, o0), f(s0, o1), f(s1, o1), f(s1, o0), col);
}

function frameAt(pts, cum, s) {
  const L = cum[cum.length - 1];
  const p = pointAtS(pts, cum, s);
  const a = pointAtS(pts, cum, clamp(s - 0.2, 0, L));
  const b = pointAtS(pts, cum, clamp(s + 0.2, 0, L));
  let tx = b[0] - a[0], tz = b[1] - a[1];
  const l = Math.hypot(tx, tz) || 1;
  return { x: p[0], y: p[2], z: p[1], tx: tx / l, tz: tz / l, nx: -tz / l, nz: tx / l };
}

function laneColor(type, tint, seed) {
  if (type === 'car' || type === 'parking' || type === 'bike' || type === 'bus') {
    if (type === 'bus') return tintColor(PAL.bus, tint, 0.2);
    if (type === 'bike') return tintColor(PAL.bike, tint, 0.2);
    if (type === 'parking') return tintColor(asphalt(tint).map((c) => c * 1.12), tint, 0.1);
    return asphalt(tint);
  }
  return tintColor(PAL[type] || PAL.grass, (seed % 97) / 97, 0.15);
}

// Markings between adjacent carriageway lanes.
function markings(mb, pts, cum, section, col) {
  const lanes = section.lanes.slice().sort((a, b) => a.o0 - b.o0);
  const kind = section.markings;
  if (kind === 'none' || NO_MARK.has(section.profile)) return;
  for (let i = 0; i < lanes.length - 1; i++) {
    const A = lanes[i];
    const B = lanes[i + 1];
    if (Math.abs(A.o1 - B.o0) > 1e-3) continue;
    const o = A.o1;
    const pair = [A.type, B.type];
    const isRaised = (t) => RAISED.has(t);
    if (isRaised(A.type) || isRaised(B.type)) continue; // curb, no paint
    if (A.type === 'rail' || B.type === 'rail') continue;
    if (A.side === 'c' && B.side === 'c') continue;
    if (A.side !== B.side && A.type === 'car' && B.type === 'car') {
      // Centre line between two directions.
      if (kind === 'highway') {
        ribbon(mb, pts, o - 0.12, o + 0.12, PAL.white, 0.02);
      } else {
        ribbon(mb, pts, o - 0.4, o - 0.22, PAL.yellow, 0.02);
        ribbon(mb, pts, o + 0.22, o + 0.4, PAL.yellow, 0.02);
      }
      continue;
    }
    if (pair.includes('bus')) {
      ribbon(mb, pts, o - 0.15, o + 0.15, PAL.red, 0.02);
      continue;
    }
    if (pair.includes('bike')) {
      ribbon(mb, pts, o - 0.12, o + 0.12, PAL.white, 0.02);
      continue;
    }
    if (pair.includes('parking')) {
      ribbon(mb, pts, o - 0.1, o + 0.1, PAL.white, 0.02);
      // Parking bays: short stall lines every stallSpacing m
      const spacing = section.stallSpacing;
      const L = cum[cum.length - 1];
      const po = A.type === 'parking' ? A : B;
      for (let s = 2; s < L - 2; s += spacing) {
        acrossQuad(mb, pts, cum, s, s + 0.14, po.o0 + 0.05, po.o1 - 0.05, PAL.white, 0.02);
      }
      continue;
    }
    // Lane divider between car lanes on the same side: dashed white.
    if (kind === 'highway' || A.type === 'shoulder' || B.type === 'shoulder') {
      ribbon(mb, pts, o - 0.1, o + 0.1, PAL.white, 0.02);
    } else {
      dashed(mb, pts, cum, o, 0.14, section.dashLen, section.dashGap, PAL.white, 0.02);
    }
  }
  // Outer edge lines on highways.
  if (kind === 'highway') {
    const carL = section.lanes.filter((l) => l.type === 'car' || l.type === 'shoulder');
    for (const l of carL) {
      if (l.type === 'shoulder') ribbon(mb, pts, l.o0 + 0.15, l.o0 + 0.27, PAL.white, 0.02);
    }
  }
}

// Zebra crossing bars across the carriageway at distance s0 along the road.
function zebra(mb, pts, cum, section, s0, col) {
  const L = cum[cum.length - 1];
  const halfW = section.carMax;
  const minO = section.carMin;
  for (let k = 0; k < 6; k++) {
    const s = s0 + k * 1.1;
    if (s > L - 1) break;
    acrossQuad(mb, pts, cum, s, s + 0.55, minO, halfW, col, 0.025);
  }
}

// Node-centred footprint triangles.
function addFootprint(mb, fp, col) {
  for (let i = 0; i < fp.indices.length; i += 3) {
    const ids = [fp.indices[i], fp.indices[i + 1], fp.indices[i + 2]];
    const P = ids.map((k) => [fp.vertices[k][0], JUNCTION_Y, fp.vertices[k][1]]);
    mb.quad(P[0], P[1], P[2], P[2], col); // degenerate quad = triangle
  }
}

export function buildRoadScene(net, opts = {}) {
  const roadMB = new MB();
  const markMB = new MB();
  const propMB = new MB();
  const kinds = net.nodes.map((n) => n.kind);
  const edgesById = Object.fromEntries(net.edges.map((e) => [e.id, e]));
  const nodeById = net.nodes;

  // Ground plane (oversized so the map reads as endless countryside).
  const G = opts.groundSize || 14000;
  const gcol = [0.2, 0.36, 0.16];
  roadMB.quad([-G, -0.05, -G], [-G, -0.05, G], [G, -0.05, G], [G, -0.05, -G], gcol);

  for (const e of net.edges) {
    const sec = e.section;
    const cum = cumLen(e.pts);
    const L = cum[cum.length - 1];
    const trimA = kinds[e.a] === 'junction' ? trimDistance(e) : 0;
    const trimB = kinds[e.b] === 'junction' ? trimDistance(e) : 0;
    const tp = trimA || trimB ? trimmedEdgePts(e, kinds) : e.pts;
    const tcum = cumLen(tp);
    const tint = sec.asphaltTint;
    const rng = makeRng((e.seed ^ 0x51ed) + e.id.length * 7919 + Number(e.id.slice(1)) * 104729);

    // Carriageway & pedestrian lanes (trimmed at junctions).
    for (const lane of sec.lanes) {
      if (lane.type === 'rail') continue;
      const raised = RAISED.has(lane.type);
      const col = laneColor(lane.type, tint, e.seed);
      ribbon(roadMB, tp, lane.o0, lane.o1, col, raised ? RAISE_H : 0);
      if (raised) {
        // Kerb faces on the carriageway side, plus a shadow strip on the outside.
        const roadSideO = lane.side === 'L' ? lane.o0 : lane.o1;
        curbFace(roadMB, tp, roadSideO, 0, RAISE_H, PAL.curb);
        const outerO = lane.side === 'L' ? lane.o1 : lane.o0;
        curbFace(roadMB, tp, outerO, 0, RAISE_H, PAL.curb);
      }
    }
    // Shoulder/grass edges are plain surfaces; bridges get deck edges & parapets.
    markings(markMB, tp, tcum, sec, PAL.white);

    // Rail lanes: full length, ballast + sleepers + rails (rail crosses junctions).
    for (const lane of sec.lanes) {
      if (lane.type !== 'rail') continue;
      ribbon(roadMB, e.pts, lane.o0, lane.o1, PAL.rail, 0);
      const sleeperCount = Math.floor(L / 0.7);
      for (let k = 0; k < sleeperCount; k++) {
        const s = (k + 0.5) * 0.7;
        if (s > L) break;
        acrossQuad(roadMB, e.pts, cum, s, s + 0.3, lane.o0 + 0.2, lane.o1 - 0.2, PAL.sleeper, 0.03);
      }
      const mid = (lane.o0 + lane.o1) / 2;
      ribbon(roadMB, e.pts, mid - 0.75, mid - 0.63, PAL.rails, 0.12);
      ribbon(roadMB, e.pts, mid + 0.63, mid + 0.75, PAL.rails, 0.12);
    }

    // Bridge structure.
    if (e.bridge) addBridge(roadMB, propMB, e, sec, cum, BRIDGE_TYPES[e.bridge]);

    // Edge props, only on the un-trimmed stretch.
    const s0 = trimA + 2;
    const s1 = L - trimB - 2;
    if (s1 > s0 + 1 && !e.bridge) {
      // Speed bumps across the carriageway.
      if (sec.speedBumps > 0) {
        const n = Math.floor((L / 90) * sec.speedBumps * (0.5 + rng()) * 2);
        for (let k = 0; k < n; k++) {
          const s = s0 + rng() * (s1 - s0);
          const f = frameAt(e.pts, cum, s);
          const o = (sec.carMin + sec.carMax) / 2;
          const hb = (sec.carMax - sec.carMin) / 2;
          propMB.box(f.x + f.nx * o, f.z + f.nz * o, f.y, f.y + 0.09, f.tx, f.tz, 0.45, hb, PAL.yellow);
        }
      }
      // Bollards along the kerb.
      if (sec.bollards) {
        for (const lane of sec.lanes) {
          if (lane.type !== 'sidewalk' && lane.type !== 'platform') continue;
          const roadSideO = lane.side === 'L' ? lane.o0 : lane.o1;
          const dir = lane.side === 'L' ? -1 : 1;
          for (let s = s0; s < s1; s += 6) {
            const f = frameAt(e.pts, cum, s);
            const o = roadSideO + dir * 0.35;
            propMB.box(f.x + f.nx * o, f.z + f.nz * o, f.y, f.y + 0.9, f.tx, f.tz, 0.12, 0.12, PAL.bollard);
          }
        }
      }
      // Street trees & lamps on sidewalks.
      for (const lane of sec.lanes) {
        if (lane.type !== 'sidewalk') continue;
        const o = (lane.o0 + lane.o1) / 2;
        const step = 9 + rng() * 4;
        if (sec.trees < 0.05) continue;
        for (let s = s0 + rng() * 4; s < s1; s += step) {
          if (rng() > sec.trees + 0.3) continue;
          const f = frameAt(e.pts, cum, s);
          const x = f.x + f.nx * o;
          const z = f.z + f.nz * o;
          propMB.box(x, z, f.y, f.y + 2.4, 0, 1, 0.12, 0.12, PAL.trunk);
          const g = 0.25 + rng() * 0.15;
          propMB.box(x, z, f.y + 2.2, f.y + 4.2, 1, 0, 1.4, 1.4, [g * 0.9, g * 1.6, g * 0.7]);
        }
      }
    }

    // Subway station kiosks at dead ends.
    if (sec.profile === 'subway') {
      for (const [nodeId, s] of [[e.a, 0], [e.b, L]]) {
        if (kinds[nodeId] !== 'end') continue;
        const f = frameAt(e.pts, cum, s === 0 ? 4 : L - 4);
        propMB.box(f.x + f.nx * 5, f.z + f.nz * 5, f.y, f.y + 2.8, f.tx, f.tz, 2.2, 1.6, [0.85, 0.85, 0.82]);
        propMB.box(f.x + f.nx * 5, f.z + f.nz * 5, f.y + 2.8, f.y + 3.0, f.tx, f.tz, 2.4, 1.8, [0.1, 0.35, 0.8]);
      }
    }
  }

  // Junction paving + approach zebra crossings + rail crossing gates.
  for (const node of nodeById) {
    if (node.kind !== 'junction') continue;
    const fp = junctionFootprint(node, edgesById);
    addFootprint(roadMB, fp, asphalt(0.4));
    const hasRail = node.edges.some((eid) => edgesById[eid].section.hasRail);
    for (const eid of node.edges) {
      const e = edgesById[eid];
      if (e.section.hasRail) continue;
      const endKey = e.a === node.id ? 'a' : 'b';
      const cum = cumLen(e.pts);
      const L = cum[cum.length - 1];
      const D = trimDistance(e);
        if (!NO_MARK.has(e.profile) && e.section.profile !== 'highway') {
        zebra(markMB, e.pts, cum, e.section, endKey === 'a' ? D + 1.4 : L - D - 1.4 - 6.6, PAL.white);
      }
      if (hasRail) {
        // Barrier arm across the approach + warning posts on both kerbs.
        const f = frameAt(e.pts, cum, endKey === 'a' ? D + 3 : L - D - 3);
        const hw = e.section.hwL + e.section.hwR;
        const o = (e.section.hwL - e.section.hwR) / 2;
        propMB.box(f.x + f.nx * o, f.z + f.nz * o, f.y + 1.1, f.y + 1.25, f.tx, f.tz, 0.15, hw / 2, PAL.white);
        for (const side of [e.section.hwL + 0.6, -e.section.hwR - 0.6]) {
          propMB.box(f.x + f.nx * side, f.z + f.nz * side, f.y, f.y + 1.4, f.tx, f.tz, 0.12, 0.12, PAL.pole);
          propMB.box(f.x + f.nx * side, f.z + f.nz * side, f.y + 1.4, f.y + 1.7, f.tx, f.tz, 0.22, 0.22, PAL.red);
        }
      }
    }
  }

  return {
    road: roadMB.geometry(),
    marks: markMB.geometry(),
    props: propMB.geometry(),
    stats: {
      roadTris: roadMB.idx.length / 3,
      markTris: markMB.idx.length / 3,
      propTris: propMB.idx.length / 3,
    },
  };
}

// Bridge deck edges, parapets and per-type supports.
function addBridge(mb, pmb, e, sec, cum, type) {
  const pts = e.pts;
  const L = cum[cum.length - 1];
  const DECK = 0.9;
  const { hwL, hwR } = sec;
  // Deck side faces + parapets (both sides).
  for (const [o, dir] of [[hwL, 1], [-hwR, -1]]) {
    curbFace(mb, pts, o, -DECK, 0, PAL.deck);
    ribbonAt(mb, pts, o + 0.15 * dir, o + 0.45 * dir, PAL.parapet, 0.0, 0.9);
  }
  const rng = makeRng(e.seed + 3);
  const spacing = { girder: 18, arch: 16, cable: 40, viaduct: 12, truss: 10 }[e.bridge] || 18;
  let s = spacing / 2;
  while (s < L) {
    const f = frameAt(pts, cum, s);
    const deckY = f.y;
    if (deckY > 0.5) {
      if (e.bridge === 'girder' || e.bridge === 'viaduct') {
        const cols = e.bridge === 'viaduct' ? [0] : [-hwL * 0.55, hwR * 0.55];
        for (const o of cols) {
          pmb.box(f.x + f.nx * o, f.z + f.nz * o, 0, deckY - DECK, f.tx, f.tz, e.bridge === 'viaduct' ? 1.3 : 0.7, e.bridge === 'viaduct' ? 2.0 : 0.6, PAL.pier);
        }
        pmb.box(f.x, f.z, deckY - DECK - 0.5, deckY - DECK, f.tx, f.tz, 0.6, hwL + hwR, PAL.pier);
      } else if (e.bridge === 'arch') {
        // Columns + a parabolic arch rib across the road.
        for (const o of [-hwL - 0.3, hwR + 0.3]) {
          pmb.box(f.x + f.nx * o, f.z + f.nz * o, 0, deckY - DECK, f.tx, f.tz, 1.1, 1.1, PAL.pier);
        }
        const H = deckY - DECK - 0.3;
        const W = hwL + hwR + 1.2;
        let prev = null;
        for (let k = 0; k <= 12; k++) {
          const u = -W / 2 + (W * k) / 12;
          const y = deckY - DECK - H * Math.sqrt(Math.max(0, 1 - (u / (W / 2)) ** 2));
          const o = u + (hwL - hwR) / 2;
          const p = [f.x + f.nx * o, y, f.z + f.nz * o];
          if (prev) pmb.beam(prev, p, 0.35, PAL.pier);
          prev = p;
        }
      } else if (e.bridge === 'cable') {
        // Towers and stays (handled once per tower by the caller loop below).
      } else if (e.bridge === 'truss') {
        // Vertical truss members on both sides.
        for (const o of [hwL + 0.4, -hwR - 0.4]) {
          const a = [f.x + f.nx * o, deckY, f.z + f.nz * o];
          const top = [a[0], deckY + 4.2, a[2]];
          pmb.beam(a, top, 0.14, PAL.steel);
        }
        pmb.box(f.x, f.z, 0, deckY - DECK, f.tx, f.tz, 0.5, 0.5, PAL.pier);
      }
    }
    s += spacing;
  }
  // Cable-stayed towers and stays.
  if (e.bridge === 'cable') {
    for (const frac of [0.3, 0.7]) {
      const sTower = L * frac;
      const f = frameAt(pts, cum, sTower);
      const top = f.y + 16;
      for (const o of [-hwR - 1.2, hwL + 1.2]) {
        const bx = f.x + f.nx * o;
        const bz = f.z + f.nz * o;
        pmb.box(bx, bz, 0, top, f.tx, f.tz, 1.1, 0.9, PAL.steel);
        pmb.beam([bx, top, bz], [bx, top + 0.0, bz], 0.2, PAL.steel);
      }
      // Stays fanning from tower top to deck.
      for (let k = 1; k <= 8; k++) {
        for (const side of [-1, 1]) {
          const ds = (k * L * 0.12) / 8 * side;
          const sd = clamp(sTower + ds, 0, L);
          const g = frameAt(pts, cum, sd);
          const o = side < 0 ? -hwR - 1.2 : hwL + 1.2;
          const towerTop = [f.x + f.nx * o, top, f.z + f.nz * o];
          const deck = [g.x, g.y + 0.1, g.z];
          pmb.beam(towerTop, deck, 0.06, PAL.steel);
        }
      }
    }
  }
  void rng;
}

// Ribbon raised from 'h0' to 'h1' above the deck at lateral offsets o0..o1.
function ribbonAt(mb, pts, o0, o1, col, h0, h1) {
  const A = [];
  const B = [];
  for (let i = 0; i < pts.length; i++) {
    const [nx, nz] = normalAt(pts, i);
    A.push([pts[i][0] + nx * o0, pts[i][2] + h0, pts[i][1] + nz * o0]);
    B.push([pts[i][0] + nx * o1, pts[i][2] + h1, pts[i][1] + nz * o1]);
  }
  mb.strip(A, B, col);
}

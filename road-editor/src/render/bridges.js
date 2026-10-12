// Bridge structures. Each bridge road gets its own structure model (4 types), built from the
// road's full centreline (not per edge), so towers/arches span the whole crossing.
import { elevationAt, rampAt, roadWidth } from '../core/model.js';
import { terrainY } from '../core/terrain.js';

export function buildBridge(R, ctx) {
  const road = R.road;
  const L = R.L;
  const W = roadWidth(road);
  const half = W / 2;
  const pts = R.pts.map((p, i) => {
    const s = R.s[i];
    const g = terrainY(p.x, p.z);
    return { x: p.x, z: p.z, s, g, y: elevationAt(road, s, L, p.x, p.z), ramp: rampAt(road, s, L) };
  });
  const bridge = ctx.get('bridge');
  const steel = ctx.get('bridge_steel');
  const cable = ctx.get('cable');

  // Parapets along the deck where it is raised above the ground.
  for (let i = 0; i < pts.length - 1; i++) {
    const a = pts[i], b = pts[i + 1];
    if (a.ramp < 0.02 && b.ramp < 0.02) continue;
    const dx = b.x - a.x, dz = b.z - a.z, sl = Math.hypot(dx, dz) || 1;
    const nx = -dz / sl, nz = dx / sl;
    for (const sgn of [-1, 1]) {
      const o = sgn * (half + 0.25);
      const p0 = { x: a.x + nx * o, y: a.y, z: a.z + nz * o };
      const p1 = { x: b.x + nx * o, y: b.y, z: b.z + nz * o };
      const top0 = { ...p0, y: a.y + 0.95 }, top1 = { ...p1, y: b.y + 0.95 };
      bridge.quad(p0, p1, top1, top0, 0.95);
      // thin coping on top of the parapet
      bridge.quad(top0, top1, { x: p1.x, y: b.y + 1.0, z: p1.z }, { x: p0.x, y: a.y + 1.0, z: p0.z }, 0.9);
    }
  }

  // Full-height section: where the ramp has finished. Piers stand on the terrain under the deck.
  const full = pts.filter((p) => p.ramp > 0.999);
  if (full.length < 2) return;
  const sA = full[0].s, sB = full[full.length - 1].s;
  const span = Math.max(24, Math.min(40, (sB - sA) / Math.max(1, Math.round((sB - sA) / 32))));
  const piers = [];
  for (let s = sA; s <= sB + 0.01; s += span) piers.push(sample(pts, s));

  for (const pr of piers) {
    const h = pr.y - pr.g - 0.6;
    if (h > 0.5) {
      bridge.cylinder(pr.x, pr.g - 0.5, pr.z, 1.1, h + 0.5, 10, 0.95);
      bridge.box(pr.x, pr.y - 1.2, pr.z, W + 2, 0.7, 2.2, Math.atan2(-(sample(pts, pr.s + 1).z - pr.z), sample(pts, pr.s + 1).x - pr.x), 0.9);
    }
  }

  switch (road.layer.bridge) {
    case 'arch':
      archBridge(piers, pts, half, span, bridge, steel);
      break;
    case 'suspension':
      suspensionBridge(sA, sB, pts, half, bridge, steel, cable);
      break;
    case 'cable':
      cableStayedBridge(sA, sB, pts, half, bridge, steel, cable);
      break;
    default:
      girderBridge(piers, pts, half, bridge, steel);
  }
}

/** Girder: exposed longitudinal girders under the deck. */
function girderBridge(piers, pts, half, bridge, steel) {
  for (let i = 0; i < pts.length - 1; i++) {
    const a = pts[i], b = pts[i + 1];
    if (a.ramp < 0.999 || b.ramp < 0.999) continue;
    const len = Math.hypot(b.x - a.x, b.z - a.z);
    const yaw = Math.atan2(-(b.z - a.z), b.x - a.x);
    const mx = (a.x + b.x) / 2, mz = (a.z + b.z) / 2, my = (a.y + b.y) / 2;
    const nx = -(b.z - a.z) / (len || 1), nz = (b.x - a.x) / (len || 1);
    for (const sgn of [-1, 1]) {
      steel.box(mx + nx * sgn * (half - 0.8), my - 1.4, mz + nz * sgn * (half - 0.8), len + 0.05, 1.1, 0.35, yaw, 0.9);
    }
  }
}

/** Tied arch: ribs rise above the deck over each span, hangers tie them down. */
function archBridge(piers, pts, half, span, bridge, steel) {
  for (let k = 0; k < piers.length - 1; k++) {
    const a = piers[k], b = piers[k + 1];
    const H = Math.min(14, span * 0.45);
    const ribs = [[], []];
    const steps = 18;
    for (let u = 0; u <= steps; u++) {
      const t = u / steps;
      const cx = a.x + (b.x - a.x) * t, cz = a.z + (b.z - a.z) * t, cy = a.y + (b.y - a.y) * t;
      const len = Math.hypot(b.x - a.x, b.z - a.z) || 1;
      const nx = -(b.z - a.z) / len, nz = (b.x - a.x) / len;
      const rise = H * Math.sin(Math.PI * t);
      for (const [idx, sgn] of [[0, -1], [1, 1]]) {
        const o = sgn * (half + 0.6);
        ribs[idx].push({ x: cx + nx * o, y: cy + rise, z: cz + nz * o });
      }
      if (u > 1 && u < steps - 1 && u % 2 === 0) {
        for (const sgn of [-1, 1]) {
          const o = sgn * (half + 0.6);
          const top = { x: cx + nx * o, y: cy + rise, z: cz + nz * o };
          const base = { x: cx + nx * o, y: cy + 0.1, z: cz + nz * o };
          steel.tube([base, top], 0.09, 5, 0.9);
        }
      }
    }
    steel.tube(ribs[0], 0.38, 7, 0.95);
    steel.tube(ribs[1], 0.38, 7, 0.95);
    // Cross-bracing between the ribs at the crown.
    const mid = ribs[0].length >> 1;
    steel.tube([ribs[0][mid], ribs[1][mid]], 0.2, 5, 0.9);
  }
}

/** Suspension: two towers, catenary main cables, vertical hangers. */
function suspensionBridge(sA, sB, pts, half, bridge, steel, cable) {
  const len = sB - sA;
  if (len < 60) return girderBridge([], pts, half, bridge, steel);
  const t1 = sA + len * 0.25, t2 = sA + len * 0.75;
  const towerH = 26;
  for (const t of [t1, t2]) {
    const c = sample(pts, t);
    const nx = -(sample(pts, t + 1).z - c.z), nz = sample(pts, t + 1).x - c.x;
    const nl = Math.hypot(nx, nz) || 1;
    const yaw = Math.atan2(-(sample(pts, t + 1).z - c.z), sample(pts, t + 1).x - c.x);
    for (const sgn of [-1, 1]) {
      const ox = (nx / nl) * sgn * (half + 1.2), oz = (nz / nl) * sgn * (half + 1.2);
      steel.box(c.x + ox, c.y - 0.5, c.z + oz, 1.8, towerH + 0.5, 1.8, yaw, 0.95);
    }
    steel.box(c.x, c.y + towerH - 1.5, c.z, 2.0, 1.2, 2 * (half + 1.2) + 0.6, yaw, 0.95);
  }
  // Main cables on each side: height above deck as a function of s.
  const hOf = (s) => {
    if (s <= t1) { const q = (s - sA) / (t1 - sA); return towerH * q * q; }
    if (s <= t2) { const q = (s - t1) / (t2 - t1); return towerH - 18 * 4 * q * (1 - q); }
    const q = (sB - s) / (sB - t2);
    return towerH * q * q;
  };
  const cablePts = [];
  for (let s = sA; s <= sB + 0.01; s += 2) cablePts.push({ s, p: sample(pts, Math.min(s, sB)), h: hOf(s) });
  for (const sgn of [-1, 1]) {
    const line = cablePts.map(({ p, h }) => {
      const nx = -(sample(pts, p.s + 1).z - p.z), nz = sample(pts, p.s + 1).x - p.x;
      const nl = Math.hypot(nx, nz) || 1;
      const o = sgn * (half + 1.1);
      return { x: p.x + (nx / nl) * o, y: p.y + h + 0.5, z: p.z + (nz / nl) * o };
    });
    cable.tube(line, 0.22, 6, 0.95);
    cablePts.forEach(({ p, h, s }) => {
      if (s > t1 + 2 && s < t2 - 2 && (Math.round((s - sA) / 2) % 4 === 0)) {
        const nx = -(sample(pts, p.s + 1).z - p.z), nz = sample(pts, p.s + 1).x - p.x;
        const nl = Math.hypot(nx, nz) || 1;
        const o = sgn * (half + 1.1);
        const x = p.x + (nx / nl) * o, z = p.z + (nz / nl) * o;
        const yc = p.y + h + 0.5;
        steel.tube([{ x, y: p.y + 0.1, z }, { x, y: yc, z }], 0.05, 4, 0.9);
      }
    });
  }
}

/** Cable-stayed: one central tower, fan of stays to the deck. */
function cableStayedBridge(sA, sB, pts, half, bridge, steel, cable) {
  const len = sB - sA;
  if (len < 50) return girderBridge([], pts, half, bridge, steel);
  const sM = (sA + sB) / 2;
  const c = sample(pts, sM);
  const towerH = 34;
  const yaw = Math.atan2(-(sample(pts, sM + 1).z - c.z), sample(pts, sM + 1).x - c.x);
  // Two inclined pylon legs joined at the top.
  const top = { x: c.x, y: c.y + towerH, z: c.z };
  for (const sgn of [-1, 1]) {
    const nx = -(sample(pts, sM + 1).z - c.z), nz = sample(pts, sM + 1).x - c.x;
    const nl = Math.hypot(nx, nz) || 1;
    const base = { x: c.x + (nx / nl) * sgn * (half + 1.4), y: c.y - 0.4, z: c.z + (nz / nl) * sgn * (half + 1.4) };
    steel.tube([base, top], 0.9, 8, 0.95);
  }
  steel.box(top.x, top.y - 0.5, top.z, 2, 1.1, 2 * (half + 1.4), yaw, 0.95);
  // Stays: from tower top to deck points at regular spacing on both sides.
  for (let s = sA + 8; s <= sB - 8; s += 8) {
    if (Math.abs(s - sM) < 4) continue;
    const p = sample(pts, s);
    const nx = -(sample(pts, s + 1).z - p.z), nz = sample(pts, s + 1).x - p.x;
    const nl = Math.hypot(nx, nz) || 1;
    for (const sgn of [-1, 1]) {
      const o = sgn * (half + 1.1);
      const end = { x: p.x + (nx / nl) * o, y: p.y + 0.6, z: p.z + (nz / nl) * o };
      cable.tube([top, end], 0.06, 4, 0.92);
    }
  }
}

/** Sample a bridge centreline at arc length s (interpolated). */
function sample(pts, s) {
  if (s <= pts[0].s) return { ...pts[0] };
  for (let i = 0; i < pts.length - 1; i++) {
    const a = pts[i], b = pts[i + 1];
    if (s <= b.s) {
      const t = (s - a.s) / (b.s - a.s || 1e-6);
      return { x: a.x + (b.x - a.x) * t, z: a.z + (b.z - a.z) * t, s, y: a.y + (b.y - a.y) * t, g: a.g + (b.g - a.g) * t, ramp: 1 };
    }
  }
  return { ...pts[pts.length - 1] };
}

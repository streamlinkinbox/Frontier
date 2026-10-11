// Terrain: a smooth height field that follows the back of the footway kits of every at-grade road, plus a
// ground mesh cut *exactly* along the network footprint (marching squares on a two-level grid) so that no
// grass ever shows through crowned carriageways, gutters or junction slabs.
//
//   terrain = buildTerrain(plan, S)                       -> { heightAt(x,z), bounds, datum }
//   field   = footprintField(plan, terrain, holes)        -> { distance(x,z) }  signed, negative inside the cut
//   ground  = buildGroundMesh(plan, terrain, field, xray, S) -> MeshBuilder ("ground" / "ground-xray" groups)
import { MeshBuilder } from "./mesh.js";
import { roadPoint, footprint } from "./query.js";
import { lerp, clamp, madd, rightXZ, pointInPolygonXZ } from "./math.js";
import { blendKits } from "./profile.js";

export const DATUM = -0.02;          // natural ground level far away from any road
const AT_GRADE = 1.2;                // |kit back height| below this -> the road shapes the terrain
const INFLUENCE = 42;                // metres of terrain influence around a kit back sample
const TERRAIN_CELL = 4;

function bounds(plan, pad) {
  let minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity;
  for (const R of plan.roads.values()) for (const F of R.frames) { minX = Math.min(minX, F.p[0]); maxX = Math.max(maxX, F.p[0]); minZ = Math.min(minZ, F.p[2]); maxZ = Math.max(maxZ, F.p[2]); }
  if (!isFinite(minX)) { minX = minZ = -100; maxX = maxZ = 100; }
  return { minX: minX - pad, maxX: maxX + pad, minZ: minZ - pad, maxZ: maxZ + pad };
}

/** Kit-back polylines [[x, y, z], ...] per road side and per junction ring (one array per source). */
function kitBackLines(plan, step = 4) {
  const out = [];
  for (const R of plan.roads.values()) {
    const fp = footprint(R);
    const s0 = R.cutA, s1 = R.length - R.cutB; if (s1 <= s0) continue;
    const n = Math.max(1, Math.ceil((s1 - s0) / step));
    const L = [], Rr = [];
    for (let i = 0; i <= n; i++) {
      const s = lerp(s0, s1, i / n);
      L.push(roadPoint(R, s, fp.left - 0.02, -0.03)); Rr.push(roadPoint(R, s, fp.right + 0.02, -0.03));
    }
    out.push(L, Rr);
  }
  for (const J of plan.junctions || []) { const ring = junctionRing(J, 0.02).map((q) => [q[0], q[1] - 0.03, q[2]]); if (ring.length > 1) out.push([...ring, ring[0]]); }
  return out;
}

/**
 * Closed outline of a junction slab at the back of the footway kits (offset outward by `off` metres), with the
 * same kit-width blend and radial arc offset as buildCornerKit so it coincides with the corner kit geometry.
 */
export function cornerBack(c, off = 0) {
  const kitA = c.A.kitRight, kitB = c.B.kitLeft; if (!c.path || !kitA || !kitB) return [];
  const P = c.path.pts, Nn = c.path.normals, m = P.length, out = [];
  for (let k = 0; k < m; k++) {
    const kit = blendKits(kitA, kitB, k / (m - 1));
    const last = (kit.strips.find((st) => st.kind === "back") || kit.strips[kit.strips.length - 1]).verts, back = last[last.length - 1];
    const d = back.d + off, p = P[k];
    let q;
    if (c.path.arc && c.path.arc[k] && c.C) { const C = c.C, rr = Math.max(0.05, c.R - d) / c.R; q = [C[0] + (p[0] - C[0]) * rr, p[1] + back.h, C[2] + (p[2] - C[2]) * rr]; }
    else q = [p[0] + Nn[k][0] * d, p[1] + back.h, p[2] + Nn[k][2] * d];
    out.push(q);
  }
  return out;
}
export function junctionRing(J, off = 0) {
  const out = [];
  for (const c of J.corners || []) out.push(...cornerBack(c, off));
  const clean = [];
  for (const q of out) if (!clean.length || Math.hypot(q[0] - clean[clean.length - 1][0], q[2] - clean[clean.length - 1][2]) > 0.05) clean.push(q);
  if (clean.length > 2 && Math.hypot(clean[0][0] - clean[clean.length - 1][0], clean[0][2] - clean[clean.length - 1][2]) < 0.05) clean.pop();
  return clean;
}

/**
 * Terrain height field: for every grid node, each at-grade kit-back line (plus `extraLines`, e.g. railway formation
 * edges) contributes its *nearest* point (distance
 * to the polyline, height interpolated along it) with a sharp inverse-square kernel, so the ground hugs the back of
 * the closest footway and blends smoothly between roads of different level; far from the network it relaxes to DATUM.
 */
export function buildTerrain(plan, S = {}, extraLines = []) {
  const pad = (S.groundExtent || 160) + INFLUENCE;
  const B = bounds(plan, pad);
  const cell = TERRAIN_CELL;
  const nx = Math.ceil((B.maxX - B.minX) / cell) + 1, nz = Math.ceil((B.maxZ - B.minZ) / cell) + 1;
  const wsum = new Float32Array(nx * nz), hsum = new Float32Array(nx * nz);
  const wTmp = new Float32Array(nx * nz), hTmp = new Float32Array(nx * nz);
  const r = INFLUENCE, rc = Math.ceil(r / cell);
  const kernel = (d) => (1 - d / r) ** 2 / (0.25 + d * d);
  for (const line of [...kitBackLines(plan), ...extraLines]) {
    const touched = [];
    for (let k = 0; k + 1 < line.length; k++) {
      const a = line[k], b = line[k + 1];
      if (Math.abs(a[1]) > AT_GRADE || Math.abs(b[1]) > AT_GRADE) continue;
      const dx = b[0] - a[0], dz = b[2] - a[2], l2 = dx * dx + dz * dz || 1e-9;
      const i0 = Math.max(0, Math.floor((Math.min(a[0], b[0]) - B.minX) / cell) - rc), i1 = Math.min(nx - 1, Math.ceil((Math.max(a[0], b[0]) - B.minX) / cell) + rc);
      const j0 = Math.max(0, Math.floor((Math.min(a[2], b[2]) - B.minZ) / cell) - rc), j1 = Math.min(nz - 1, Math.ceil((Math.max(a[2], b[2]) - B.minZ) / cell) + rc);
      for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) {
        const x = B.minX + i * cell, z = B.minZ + j * cell;
        const t = clamp(((x - a[0]) * dx + (z - a[2]) * dz) / l2, 0, 1);
        const d = Math.hypot(x - a[0] - t * dx, z - a[2] - t * dz); if (d >= r) continue;
        const w = kernel(d), idx = i + j * nx;
        if (w > wTmp[idx]) { if (!wTmp[idx]) touched.push(idx); wTmp[idx] = w; hTmp[idx] = lerp(a[1], b[1], t); }
      }
    }
    for (const idx of touched) { wsum[idx] += wTmp[idx]; hsum[idx] += wTmp[idx] * hTmp[idx]; wTmp[idx] = 0; }
  }
  const W0 = 0.002; // weak pull toward the datum so the field relaxes far from the network
  const H = new Float32Array(nx * nz);
  for (let k = 0; k < H.length; k++) H[k] = (hsum[k] + W0 * DATUM) / (wsum[k] + W0);
  const heightAt = (x, z) => {
    const fx = clamp((x - B.minX) / cell, 0, nx - 1.0001), fz = clamp((z - B.minZ) / cell, 0, nz - 1.0001);
    const i = Math.floor(fx), j = Math.floor(fz), tx = fx - i, tz = fz - j;
    const a = H[i + j * nx], b = H[i + 1 + j * nx], c = H[i + (j + 1) * nx], d = H[i + 1 + (j + 1) * nx];
    return lerp(lerp(a, b, tx), lerp(c, d, tx), tz);
  };
  const flatAt = (x, z) => Math.abs(heightAt(x, z) - DATUM) < 1e-3;
  return { heightAt, flatAt, bounds: B, datum: DATUM, cell, nx, nz, H };
}

// ---------------------------------------------------------------------------------------------------------
// Footprint distance field: union of road strips (per-sample lateral extents), junction rings and hole polygons.

const HASH = 16;
class Hash {
  constructor() { this.m = new Map(); }
  key(i, j) { return (i + 65536) * 131072 + (j + 65536); }
  /** Insert with a HASH margin so that any query point within HASH of the bbox lists the item. */
  insert(item, minX, minZ, maxX, maxZ) {
    minX -= HASH; minZ -= HASH; maxX += HASH; maxZ += HASH;
    for (let j = Math.floor(minZ / HASH); j <= Math.floor(maxZ / HASH); j++) for (let i = Math.floor(minX / HASH); i <= Math.floor(maxX / HASH); i++) {
      const k = this.key(i, j); const b = this.m.get(k); if (b) b.push(item); else this.m.set(k, [item]);
    }
  }
  at(x, z) { return this.m.get(this.key(Math.floor(x / HASH), Math.floor(z / HASH))) || []; }
}

function segDist(x, z, a, b) {
  const dx = b[0] - a[0], dz = b[2] - a[2], l2 = dx * dx + dz * dz || 1e-9;
  const t = clamp(((x - a[0]) * dx + (z - a[2]) * dz) / l2, 0, 1);
  return Math.hypot(x - a[0] - t * dx, z - a[2] - t * dz);
}
/**
 * Signed distance to one mitred strip quad [AL, BL, BR, AR] (frame-normal offsets, so consecutive quads share their
 * end rows exactly). Inside: minus the distance to the nearer *lateral* side (end rows are interior seams).
 */
function stripDistance(x, z, q) {
  const [AL, BL, BR, AR] = q.p;
  if (pointInPolygonXZ([x, 0, z], q.p)) {
    let d = Math.min(segDist(x, z, AL, BL), segDist(x, z, AR, BR));
    if (q.endA) d = Math.min(d, segDist(x, z, AR, AL));
    if (q.endB) d = Math.min(d, segDist(x, z, BL, BR));
    return -d;
  }
  return Math.min(segDist(x, z, AL, BL), segDist(x, z, BL, BR), segDist(x, z, BR, AR), segDist(x, z, AR, AL));
}

function polyDistance(x, z, poly) {
  let best = Infinity;
  for (let i = 0, n = poly.length; i < n; i++) {
    const a = poly[i], b = poly[(i + 1) % n];
    const dx = b[0] - a[0], dz = b[2] - a[2], l2 = dx * dx + dz * dz || 1e-9;
    const t = clamp(((x - a[0]) * dx + (z - a[2]) * dz) / l2, 0, 1);
    best = Math.min(best, Math.hypot(x - a[0] - t * dx, z - a[2] - t * dz));
  }
  return pointInPolygonXZ([x, 0, z], poly) ? -best : best;
}

/**
 * Build the cut region of the ground. Roads at grade cut the ground 5 cm under the kit back; roads in a cutting cut
 * out to the toe of their 1:2 slope; elevated roads (> 0.3 m above the terrain) leave the ground untouched (the
 * embankment skirt / piers sit on it). Junction rings cut when the node is at grade; `holes` always cut.
 */
export function footprintField(plan, terrain, holes = []) {
  const hash = new Hash();
  const strips = [], polys = [];
  for (const R of plan.roads.values()) {
    const fp = footprint(R);
    const s0 = R.cutA, s1 = R.length - R.cutB; if (s1 <= s0 + 0.2) continue;
    const n = Math.max(1, Math.ceil((s1 - s0) / 3));
    const rows = [];
    for (let i = 0; i <= n; i++) {
      const s = lerp(s0, s1, i / n), F = R.frameAt(s);
      const r = F.r || rightXZ(F.dxz);
      const off = (side) => {
        const edge = side > 0 ? fp.right + 0.02 : fp.left - 0.02;
        const kb = roadPoint(R, s, edge, -0.03);
        const h = kb[1] - terrain.heightAt(kb[0], kb[2]);
        if (h > 0.3) return -1;                        // elevated: no cut on this side
        const toe = h < -0.3 ? Math.abs(h) * 2 - 0.2 : 0; // cutting: out to 20 cm short of the skirt toe (skirt tucks under)
        return Math.abs(edge) - 0.05 + toe;
      };
      rows.push({ p: F.p, r, offL: off(-1), offR: off(1) });
    }
    for (let i = 0; i + 1 < rows.length; i++) {
      const a = rows[i], b = rows[i + 1];
      if (a.offL < 0 && a.offR < 0 && b.offL < 0 && b.offR < 0) continue;
      const AL = madd(a.p, a.r, -Math.max(a.offL, 0)), AR = madd(a.p, a.r, Math.max(a.offR, 0));
      const BL = madd(b.p, b.r, -Math.max(b.offL, 0)), BR = madd(b.p, b.r, Math.max(b.offR, 0));
      const q = { p: [AL, BL, BR, AR], endA: i === 0, endB: i + 2 === rows.length };
      strips.push(q);
      const xs = q.p.map((v) => v[0]), zs = q.p.map((v) => v[2]);
      hash.insert({ s: q }, Math.min(...xs), Math.min(...zs), Math.max(...xs), Math.max(...zs));
    }
  }
  const addPoly = (poly) => {
    if (poly.length < 3) return;
    let minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity;
    for (const p of poly) { minX = Math.min(minX, p[0]); maxX = Math.max(maxX, p[0]); minZ = Math.min(minZ, p[2]); maxZ = Math.max(maxZ, p[2]); }
    const item = { p: poly, minX, maxX, minZ, maxZ };
    polys.push(item); hash.insert(item, minX, minZ, maxX, maxZ);
  };
  for (const J of plan.junctions || []) {
    const ring = junctionRing(J, -0.05); if (ring.length < 3) continue;
    let h = 0; for (const q of ring) h += (q[1] - terrain.heightAt(q[0], q[2])) / ring.length;
    if (h > 0.3) continue; // elevated junction: deck + piers, ground stays
    addPoly(ring);
  }
  for (const poly of holes) addPoly(poly);
  const distance = (x, z) => {
    let d = Infinity;
    for (const it of hash.at(x, z)) {
      if (it.s) d = Math.min(d, stripDistance(x, z, it.s));
      else {
        // cheap reject: outside the bbox by more than the current best
        const ex = Math.max(it.minX - x, 0, x - it.maxX), ez = Math.max(it.minZ - z, 0, z - it.maxZ);
        if (Math.hypot(ex, ez) >= d) continue;
        d = Math.min(d, polyDistance(x, z, it.p));
      }
    }
    // the hash only lists pieces whose bbox reaches this cell; anything else is at least ~HASH away
    return Math.min(d, HASH);
  };
  return { distance, strips, polys };
}

// ---------------------------------------------------------------------------------------------------------
// Ground mesh: two-level marching squares, vertices shared through a position cache.

export function buildGroundMesh(plan, terrain, field, xrayHoles = [], S = {}) {
  const g = new MeshBuilder("ground");
  const grass = g.material("grass");
  const E = S.groundExtent || 160, coarse = clamp(S.groundCell || 12, 4, 20), SUB = 4, fine = coarse / SUB;
  const B = bounds(plan, E);
  const minX = Math.floor(B.minX / coarse) * coarse, minZ = Math.floor(B.minZ / coarse) * coarse;
  const nx = Math.ceil((B.maxX - minX) / coarse), nz = Math.ceil((B.maxZ - minZ) / coarse);
  const cache = new Map();
  const V = (x, z) => {
    const k = Math.round(x * 1000) + "," + Math.round(z * 1000);
    let id = cache.get(k);
    if (id === undefined) { id = g.vertex([x, terrain.heightAt(x, z), z], [x, z], [0, 1, 0.5, 0]); cache.set(k, id); }
    return id;
  };
  const xrayAt = (x, z) => { for (const poly of xrayHoles) if (pointInPolygonXZ([x, 0, z], poly)) return true; return false; };
  const emitPoly = (ids, grp) => {
    if (ids.length < 3) return;
    if (ids.length === 3) g.tri(grass, ids[0], ids[1], ids[2], grp);
    else if (ids.length === 4) g.quad(grass, ids[0], ids[1], ids[2], ids[3], grp);
    else { g.quad(grass, ids[0], ids[1], ids[2], ids[3], grp); for (let i = 4; i < ids.length; i++) g.tri(grass, ids[0], ids[i - 1], ids[i], grp); }
  };
  const halfDiag = Math.SQRT1_2 * coarse, MAX_DEPTH = 2; // fine 3 m -> 0.75 m where slivers hide between corners
  for (let j = 0; j < nz; j++) for (let i = 0; i < nx; i++) {
    const x0 = minX + i * coarse, z0 = minZ + j * coarse, x1 = x0 + coarse, z1 = z0 + coarse;
    const dc = field.distance(x0 + coarse / 2, z0 + coarse / 2);
    if (dc < -halfDiag - 0.1) continue; // entirely inside the footprint
    const flat = terrain.flatAt(x0, z0) && terrain.flatAt(x1, z0) && terrain.flatAt(x1, z1) && terrain.flatAt(x0, z1);
    if (dc > halfDiag + 0.1 && flat) { // entirely outside, on flat ground: one coarse quad
      const grp = xrayAt(x0 + coarse / 2, z0 + coarse / 2) ? "ground-xray" : "ground";
      g.quad(grass, V(x0, z0), V(x1, z0), V(x1, z1), V(x0, z1), grp);
      continue;
    }
    for (let fj = 0; fj < SUB; fj++) for (let fi = 0; fi < SUB; fi++) cell(x0 + fi * fine, z0 + fj * fine, fine, 0);
  }
  /** Marching squares on one fine cell; cells whose corners agree but whose centre disagrees are subdivided (thin slivers). */
  function cell(ax, az, size, depth) {
    const bx = ax + size, bz = az + size;
    // corners counter-clockwise seen from +y: (ax,az) (bx,az) (bx,bz) (ax,bz)
    const cx = [ax, bx, bx, ax], cz = [az, az, bz, bz];
    const d = cx.map((x, k) => field.distance(x, cz[k]));
    const out = d.map((v) => v > 0);
    const grp = xrayAt(ax + size / 2, az + size / 2) ? "ground-xray" : "ground";
    const nOut = out.filter((v) => v).length;
    if (nOut === 4 || nOut === 0) {
      const dm = field.distance(ax + size / 2, az + size / 2);
      if ((dm > 0) !== (nOut === 4) && depth < MAX_DEPTH) { const h = size / 2; cell(ax, az, h, depth + 1); cell(ax + h, az, h, depth + 1); cell(ax, az + h, h, depth + 1); cell(ax + h, az + h, h, depth + 1); return; }
      if (nOut === 4) g.quad(grass, V(ax, az), V(bx, az), V(bx, bz), V(ax, bz), grp);
      return;
    }
    // mixed cell: subdivide while the field is not close to linear across it (thin slivers, tight curves)
    const dm = field.distance(ax + size / 2, az + size / 2), dl = (d[0] + d[1] + d[2] + d[3]) / 4;
    if (depth < MAX_DEPTH && ((dm > 0) !== (dl > 0) || Math.abs(dm - dl) > size * 0.3)) {
      const h = size / 2; cell(ax, az, h, depth + 1); cell(ax + h, az, h, depth + 1); cell(ax, az + h, h, depth + 1); cell(ax + h, az + h, h, depth + 1); return;
    }
    // marching squares: keep outside corners, insert edge crossings
    const ids = [], isCorner = [];
    for (let k = 0; k < 4; k++) {
      const k2 = (k + 1) % 4;
      if (out[k]) { ids.push(V(cx[k], cz[k])); isCorner.push(true); }
      if (out[k] !== out[k2]) {
        // regula falsi on the exact field along the edge so the crossing sits on the true footprint boundary
        let x0 = cx[k], z0 = cz[k], d0 = d[k], x1 = cx[k2], z1 = cz[k2], d1 = d[k2], t = d0 / (d0 - d1), xm = lerp(x0, x1, t), zm = lerp(z0, z1, t);
        for (let it = 0; it < 3; it++) {
          const dm2 = field.distance(xm, zm); if (Math.abs(dm2) < 0.005) break;
          if ((dm2 > 0) === (d0 > 0)) { x0 = xm; z0 = zm; d0 = dm2; } else { x1 = xm; z1 = zm; d1 = dm2; }
          t = d0 / (d0 - d1); xm = lerp(x0, x1, t); zm = lerp(z0, z1, t);
        }
        ids.push(V(xm, zm)); isCorner.push(false);
      }
    }
    // the emitted polygon's centroid must be ground; otherwise the boundary curves inside this cell -> subdivide
    if (depth < MAX_DEPTH + 1) {
      let mx = 0, mz = 0; for (const id of ids) { mx += g.positions[id * 3] / ids.length; mz += g.positions[id * 3 + 2] / ids.length; }
      if (field.distance(mx, mz) < -0.02) { const h = size / 2; cell(ax, az, h, depth + 1); cell(ax + h, az, h, depth + 1); cell(ax, az + h, h, depth + 1); cell(ax + h, az + h, h, depth + 1); return; }
    }
    if (nOut === 2 && out[0] === out[2] && dm <= 0) {
      // saddle with the centre inside the footprint: two separate corner triangles (crossing, corner, crossing)
      for (let k = 0; k < ids.length; k++) if (isCorner[k]) emitPoly([ids[(k + ids.length - 1) % ids.length], ids[k], ids[(k + 1) % ids.length]], grp);
      return;
    }
    emitPoly(ids, grp);
  }
  g.compact();
  return g;
}

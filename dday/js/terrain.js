// Heightfield terrain, road network & spatial queries
import * as THREE from '../vendor/three.module.min.js';
import { clamp, lerp, smooth, fbm, pchip, segDist, rng } from './util.js';
import { PROFILE, ROADS, MOUNDS, TRENCHES, HEDGES, DRAWS, ROAD_HW, BUNKERS, WALL_Z } from './layout.js';

export const X0 = -250, X1 = 250, Z0 = -320, Z1 = 1740, CELL = 2;
export const NX = Math.round((X1 - X0) / CELL) + 1, NZ = Math.round((Z1 - Z0) / CELL) + 1;

const profile = pchip(PROFILE);
export const profileAt = profile;

// ---------- segment spatial hash ----------
class SegHash {
  constructor(cell) { this.cell = cell; this.map = new Map(); }
  key(i, j) { return i * 73856093 ^ j * 19349663; }
  add(seg, pad) {
    const minx = Math.min(seg.ax, seg.bx) - pad, maxx = Math.max(seg.ax, seg.bx) + pad;
    const minz = Math.min(seg.az, seg.bz) - pad, maxz = Math.max(seg.az, seg.bz) + pad;
    for (let i = Math.floor(minx / this.cell); i <= Math.floor(maxx / this.cell); i++)
      for (let j = Math.floor(minz / this.cell); j <= Math.floor(maxz / this.cell); j++) {
        const k = this.key(i, j); let a = this.map.get(k); if (!a) this.map.set(k, a = []); a.push(seg);
      }
  }
  near(x, z) { return this.map.get(this.key(Math.floor(x / this.cell), Math.floor(z / this.cell))); }
}

export class Terrain {
  constructor() {
    this.r = rng(1944);
    this.buildRoads();
    this.buildLines();
    this.buildHeights();
  }

  // ---------------- roads ----------------
  buildRoads() {
    this.roads = [];
    this.roadHash = new SegHash(16);
    ROADS.forEach((rd, ri) => {
      const curve = new THREE.CatmullRomCurve3(rd.pts.map(p => new THREE.Vector3(p[0], 0, p[1])), false, 'centripetal', 0.5);
      const len = curve.getLength();
      const n = Math.ceil(len / 1.0);
      const pts = curve.getSpacedPoints(n);
      const samples = pts.map(p => ({ x: p.x, z: p.z }));
      for (let i = 0; i < samples.length; i++) {
        const a = samples[Math.max(0, i - 1)], b = samples[Math.min(samples.length - 1, i + 1)];
        const tx = b.x - a.x, tz = b.z - a.z, l = Math.hypot(tx, tz) || 1;
        samples[i].tx = tx / l; samples[i].tz = tz / l; samples[i].s = i * (len / n);
      }
      const road = { name: rd.name, index: ri, samples, length: len };
      this.roads.push(road);
      for (let i = 0; i < samples.length - 1; i++) {
        const a = samples[i], b = samples[i + 1];
        this.roadHash.add({ ax: a.x, az: a.z, bx: b.x, bz: b.z, road, i }, 14);
      }
    });
  }
  /** nearest road: {d, road, i, t, x, z} (d = Infinity if nothing within ~14m) */
  roadInfo(x, z) {
    const list = this.roadHash.near(x, z);
    let best = { d: Infinity };
    if (!list) return best;
    for (const s of list) {
      const r = segDist(x, z, s.ax, s.az, s.bx, s.bz);
      if (r.d < best.d) best = { d: r.d, road: s.road, i: s.i, t: r.t };
    }
    return best;
  }
  /** point + tangent on a road at arc length s */
  roadAt(road, s) {
    const smp = road.samples, step = road.length / (smp.length - 1);
    const f = clamp(s / step, 0, smp.length - 1.001), i = Math.floor(f), t = f - i;
    const a = smp[i], b = smp[i + 1];
    return { x: lerp(a.x, b.x, t), z: lerp(a.z, b.z, t), tx: a.tx, tz: a.tz };
  }

  // ---------------- linear features: trenches / hedgerows ----------------
  buildLines() {
    this.lineHash = new SegHash(16);
    this.trenches = [];
    const r = rng(612);
    for (const T of TRENCHES) {
      const pts = [];
      let k = 0;
      for (let x = T.x0; x <= T.x1 + 0.01; x += 9, k++) {
        const off = (k % 2 ? 1 : -1) * T.amp + (r() - 0.5) * 1.2;
        pts.push([x, T.z + off + Math.sin(x * 0.013) * 6]);
      }
      this.trenches.push(pts);
    }
    this.hedges = HEDGES.map(h => h.map(p => [p[0], p[1]]));
    const addLine = (pts, type) => {
      for (let i = 0; i < pts.length - 1; i++)
        this.lineHash.add({ ax: pts[i][0], az: pts[i][1], bx: pts[i + 1][0], bz: pts[i + 1][1], type }, 8);
    };
    this.trenches.forEach(p => addLine(p, 'trench'));
    this.hedges.forEach(p => addLine(p, 'hedge'));
  }
  lineInfo(x, z) {
    const list = this.lineHash.near(x, z);
    let best = { d: Infinity };
    if (!list) return best;
    for (const s of list) {
      const r = segDist(x, z, s.ax, s.az, s.bx, s.bz);
      if (r.d < best.d) best = { d: r.d, type: s.type };
    }
    return best;
  }

  // ---------------- height functions ----------------
  /** smooth base the roads follow: profile + side ridges + broad undulation */
  base(x, z) {
    const ax = Math.abs(x);
    const side = 38 * Math.pow(smooth(128, 222, ax), 1.4);
    const inland = smooth(340, 460, z);
    const und = fbm(x * 0.0065 + 3.1, z * 0.0065 - 1.7, 2) * 2.6 * inland;
    return profile(z) + side + und;
  }
  natural(x, z) {
    let h = this.base(x, z);
    const inland = smooth(330, 420, z);
    // fine undulation (kept small so the ground stays clean)
    h += fbm(x * 0.03, z * 0.03, 2) * (0.12 + 0.38 * inland);
    // bluff lip, cut by the draws
    const lip = Math.exp(-((z - 372) ** 2) / (2 * 16 ** 2)) * 6.5;
    let cut = 1;
    for (const d of DRAWS) cut = Math.min(cut, d.sealed ? 0.55 + 0.45 * smooth(6, 26, Math.abs(x - d.x)) : smooth(10, 34, Math.abs(x - d.x)));
    h += lip * cut;
    // gentle beach runnels
    if (z < 300) h += Math.sin(z * 0.09 + Math.sin(x * 0.02) * 2) * 0.12 * smooth(20, 80, z) * (1 - smooth(240, 300, z));
    // mounds
    for (const m of this.moundList) {
      const dx = x - m.x, dz = z - m.z;
      if (Math.abs(dx) > m.ext || Math.abs(dz) > m.ext) continue;
      const lx = (dx * m.c + dz * m.s) / m.rx, lz = (-dx * m.s + dz * m.c) / m.rz;
      const q = lx * lx + lz * lz;
      if (q < 4) h += m.h * Math.pow(Math.max(0, 1 - q / 4), 2.2) * (1 + 0.08 * Math.sin(lx * 3 + lz * 2));
    }
    // craters
    for (const c of this.craterList) {
      const dx = x - c.x, dz = z - c.z;
      if (Math.abs(dx) > c.r * 1.8 || Math.abs(dz) > c.r * 1.8) continue;
      const d = Math.hypot(dx, dz) / c.r;
      if (d < 1) h -= c.depth * (1 - d * d);
      h += c.depth * 0.35 * Math.exp(-((d - 1) ** 2) / 0.08);
    }
    // bunker pads
    for (const b of BUNKERS) {
      const d = Math.hypot(x - b[0], z - b[1]);
      if (d < 16) h += 1.3 * (1 - smooth(6, 16, d));
    }
    // trench & hedge profiles
    const li = this.lineInfo(x, z);
    if (li.d < 8) {
      if (li.type === 'trench') {
        const d = li.d;
        h += d < 1.7 ? -1.05 : d < 3.4 ? lerp(-1.05, 0.95, smooth(1.7, 3.4, d)) : 0.95 * (1 - smooth(3.4, 7.5, d));
      } else {
        h += 1.1 * (1 - smooth(1.5, 4.5, li.d));
      }
    }
    return h;
  }
  roadHeight(x, z) { return this.base(x, z); }

  buildHeights() {
    const r = this.r;
    // mounds & craters lists
    this.moundList = MOUNDS.map(([x, z, rx, rz, h, rot]) => ({ x, z, rx, rz, h, c: Math.cos(rot), s: Math.sin(rot), ext: Math.max(rx, rz) * 2.05 }));
    this.craterList = [];
    let guard = 0;
    while (this.craterList.length < 150 && guard++ < 4000) {
      const x = r.range(-165, 165), z = r.range(40, 1560);
      if (this.roadInfo(x, z).d < 12) continue;
      if (Math.abs(z - 372) < 30) continue;
      this.craterList.push({ x, z, r: r.range(2.4, 6.5), depth: r.range(0.6, 1.6) });
    }

    const H = this.h = new Float32Array(NX * NZ);
    this.onRoad = new Uint8Array(NX * NZ);
    this.meta = new Float32Array(NX * NZ * 3); // roadBlend, trenchMask, moundAmt
    for (let j = 0; j < NZ; j++) {
      const z = Z0 + j * CELL;
      for (let i = 0; i < NX; i++) {
        const x = X0 + i * CELL, k = j * NX + i;
        let h = this.natural(x, z);
        const ri = this.roadInfo(x, z);
        let blend = 0;
        if (ri.d < ROAD_HW + 11) {
          const rh = this.roadHeight(x, z) - 0.22;
          blend = 1 - smooth(ROAD_HW + 1.8, ROAD_HW + 11, ri.d);
          h = lerp(h, rh, blend);
        }
        H[k] = h;
        this.meta[k * 3] = blend;
        const li = this.lineInfo(x, z);
        this.meta[k * 3 + 1] = li.d < 8 ? (li.type === 'trench' ? 1 - smooth(1.2, 3.6, li.d) : -(1 - smooth(1, 4, li.d))) : 0;
        this.meta[k * 3 + 2] = h - this.base(x, z);
      }
    }
  }

  // ---------------- queries ----------------
  heightAt(x, z) {
    const fx = clamp((x - X0) / CELL, 0, NX - 1.001), fz = clamp((z - Z0) / CELL, 0, NZ - 1.001);
    const i = Math.floor(fx), j = Math.floor(fz), u = fx - i, v = fz - j;
    const H = this.h, a = H[j * NX + i], b = H[j * NX + i + 1], c = H[(j + 1) * NX + i], d = H[(j + 1) * NX + i + 1];
    if (u + v <= 1) return a + (b - a) * u + (c - a) * v;
    return d + (c - d) * (1 - u) + (b - d) * (1 - v);
  }
  /** drivable surface height: road deck when on a road */
  groundAt(x, z, ri) {
    const h = this.heightAt(x, z);
    ri = ri || this.roadInfo(x, z);
    if (ri.d < ROAD_HW + 0.8) return Math.max(h, this.roadHeight(x, z) + 0.03);
    return h;
  }
  normalAt(x, z, out = new THREE.Vector3()) {
    const e = 1.2;
    const hx = this.heightAt(x + e, z) - this.heightAt(x - e, z);
    const hz = this.heightAt(x, z + e) - this.heightAt(x, z - e);
    return out.set(-hx, 2 * e, -hz).normalize();
  }
  /** line of sight between two points (terrain only) */
  los(ax, ay, az, bx, by, bz) {
    const d = Math.hypot(bx - ax, bz - az), n = Math.ceil(d / 4);
    for (let i = 1; i < n; i++) {
      const t = i / n;
      if (this.heightAt(lerp(ax, bx, t), lerp(az, bz, t)) > lerp(ay, by, t) + 0.3) return false;
    }
    return true;
  }

  // ---------------- meshes ----------------
  buildMesh() {
    const n = NX * NZ;
    const pos = new Float32Array(n * 3), col = new Float32Array(n * 3);
    const C = hex => new THREE.Color(hex);
    const pal = {
      wet: C('#a8946b'), sand: C('#d9c38f'), dry: C('#e2cf9e'), shingle: C('#a39c8c'),
      dune: C('#9d9b62'), grass: C('#71883f'), grass2: C('#86984c'), dirt: C('#7c6443'),
      soil: C('#4f4232'), mud: C('#4b3c2b'), berm: C('#6d5a3d'), rock: C('#8b877b'), shoulder: C('#8b7652'),
      hedge: C('#55693a'), apron: C('#8f8a7c'),
    };
    const tmp = new THREE.Color(), t2 = new THREE.Color();
    for (let j = 0; j < NZ; j++) {
      const z = Z0 + j * CELL;
      for (let i = 0; i < NX; i++) {
        const x = X0 + i * CELL, k = j * NX + i, h = this.h[k];
        pos[k * 3] = x; pos[k * 3 + 1] = h; pos[k * 3 + 2] = z;
        // --- colour ---
        const beachT = smooth(-20, 60, z);
        tmp.copy(pal.wet).lerp(pal.sand, beachT).lerp(pal.dry, smooth(150, 260, z));
        tmp.lerp(pal.shingle, smooth(275, 300, z) * (1 - smooth(328, 350, z)));
        const g = fbm(x * 0.012 + 11, z * 0.012 - 4, 2) * 0.5 + 0.5;
        t2.copy(pal.grass).lerp(pal.grass2, g);
        tmp.lerp(pal.dune, smooth(328, 352, z)).lerp(t2, smooth(360, 420, z));
        // steepness / side ridges -> rock
        const ax = Math.abs(x);
        if (ax > 150) tmp.lerp(pal.rock, smooth(170, 215, ax) * 0.7);
        // mounds & craters -> dirt
        const raised = this.meta[k * 3 + 2];
        const moundAmt = z > 330 ? smooth(1.5, 5, raised) : 0;
        tmp.lerp(pal.dirt, moundAmt * 0.75);
        if (raised < -0.35 && z > 20) tmp.lerp(pal.soil, smooth(-0.35, -1.2, raised) * 0.85);
        // trench / hedge
        const tm = this.meta[k * 3 + 1];
        if (tm > 0) tmp.lerp(pal.berm, smooth(0, 0.5, tm)).lerp(pal.mud, smooth(0.6, 1, tm));
        if (tm < 0) tmp.lerp(pal.hedge, -tm);
        // road shoulders
        const rb = this.meta[k * 3];
        if (rb > 0) tmp.lerp(pal.shoulder, rb * 0.55 * (z > 330 ? 1 : 0.4));
        // wall apron
        if (z > WALL_Z - 30) tmp.lerp(pal.apron, smooth(WALL_Z - 30, WALL_Z - 6, z) * 0.55);
        // subtle per-vertex variation
        const v = 1 + (fbm(x * 0.21, z * 0.21, 1)) * 0.035;
        col[k * 3] = tmp.r * v; col[k * 3 + 1] = tmp.g * v; col[k * 3 + 2] = tmp.b * v;
      }
    }
    const idx = new Uint32Array((NX - 1) * (NZ - 1) * 6);
    let o = 0;
    for (let j = 0; j < NZ - 1; j++) for (let i = 0; i < NX - 1; i++) {
      const a = j * NX + i, b = a + 1, c = a + NX, d = c + 1;
      idx[o++] = a; idx[o++] = c; idx[o++] = b;
      idx[o++] = b; idx[o++] = c; idx[o++] = d;
    }
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    geo.setAttribute('color', new THREE.BufferAttribute(col, 3));
    geo.setIndex(new THREE.BufferAttribute(idx, 1));
    geo.computeVertexNormals();
    geo.computeBoundingSphere();
    this.geo = geo;
    const mat = new THREE.MeshStandardMaterial({ vertexColors: true, flatShading: true, roughness: 0.97, metalness: 0 });
    const mesh = new THREE.Mesh(geo, mat);
    mesh.receiveShadow = true;
    return mesh;
  }

  buildRoadMeshes() {
    const group = new THREE.Group();
    const OFF = [-ROAD_HW - 1.6, -ROAD_HW, -ROAD_HW + 0.7, -1.9, -1.2, 0, 1.2, 1.9, ROAD_HW - 0.7, ROAD_HW, ROAD_HW + 1.6];
    const cShoulder = new THREE.Color('#857150'), cEdge = new THREE.Color('#9a8763'), cRoad = new THREE.Color('#a8977a'), cRut = new THREE.Color('#8b7a5f');
    const COLS = [cShoulder, cEdge, cRoad, cRoad, cRut, cRoad, cRut, cRoad, cRoad, cEdge, cShoulder];
    const DY = [-0.16, 0.03, 0.04, 0.04, 0.035, 0.045, 0.035, 0.04, 0.04, 0.03, -0.16];
    this.roads.forEach((road, ri) => {
      const smp = road.samples.filter((_, i) => i % 2 === 0 || i === road.samples.length - 1);
      const W = OFF.length, pos = [], col = [], idx = [];
      smp.forEach((s, j) => {
        const nx = s.tz, nz = -s.tx; // left normal
        const shade = 1 + fbm(s.x * 0.15, s.z * 0.15, 1) * 0.04;
        OFF.forEach((o, i) => {
          const x = s.x + nx * o, z = s.z + nz * o;
          let y = this.roadHeight(x, z) + DY[i];
          if (i === 0 || i === W - 1) y = Math.min(y, this.heightAt(x, z) + 0.02);
          pos.push(x, y, z);
          col.push(COLS[i].r * shade, COLS[i].g * shade, COLS[i].b * shade);
        });
        if (j > 0) for (let i = 0; i < W - 1; i++) {
          const a = (j - 1) * W + i, b = a + 1, c = j * W + i, d = c + 1;
          idx.push(a, b, c, b, d, c);
        }
      });
      const geo = new THREE.BufferGeometry();
      geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
      geo.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
      geo.setIndex(idx);
      geo.computeVertexNormals();
      // make sure the ribbon faces up regardless of curve direction
      const nrm = geo.attributes.normal;
      let up = 0; for (let i = 0; i < nrm.count; i++) up += nrm.getY(i);
      if (up < 0) { for (let i = 0; i < idx.length; i += 3) { const t = idx[i + 1]; idx[i + 1] = idx[i + 2]; idx[i + 2] = t; } geo.setIndex(idx); geo.computeVertexNormals(); }
      const mat = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 1, flatShading: false, polygonOffset: true, polygonOffsetFactor: -1 - ri * 0.5, polygonOffsetUnits: -2 - ri });
      const m = new THREE.Mesh(geo, mat);
      m.receiveShadow = true;
      group.add(m);
    });
    return group;
  }

  /** Blast a crater into the ground at runtime (off-road only). */
  deform(x, z, radius, depth) {
    const ri = this.roadInfo(x, z);
    const col = this.geo.attributes.color, pos = this.geo.attributes.position;
    const i0 = Math.max(0, Math.floor((x - radius * 1.6 - X0) / CELL)), i1 = Math.min(NX - 1, Math.ceil((x + radius * 1.6 - X0) / CELL));
    const j0 = Math.max(0, Math.floor((z - radius * 1.6 - Z0) / CELL)), j1 = Math.min(NZ - 1, Math.ceil((z + radius * 1.6 - Z0) / CELL));
    const scorch = new THREE.Color('#3a3129');
    const canDig = ri.d > ROAD_HW + 5;
    for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) {
      const k = j * NX + i, vx = X0 + i * CELL, vz = Z0 + j * CELL;
      const d = Math.hypot(vx - x, vz - z) / radius;
      if (d > 1.6) continue;
      if (canDig && this.roadInfo(vx, vz).d > ROAD_HW + 2.5) {
        let dh = 0;
        if (d < 1) dh -= depth * (1 - d * d);
        dh += depth * 0.3 * Math.exp(-((d - 1) ** 2) / 0.06);
        this.h[k] += dh; pos.setY(k, this.h[k]);
      }
      const s = (1 - smooth(0.4, 1.5, d)) * 0.75;
      col.setXYZ(k, lerp(col.getX(k), scorch.r, s), lerp(col.getY(k), scorch.g, s), lerp(col.getZ(k), scorch.b, s));
    }
    pos.needsUpdate = true; col.needsUpdate = true;
  }
}

// Builds the terrain surface (and the block skirt) as indexed BufferGeometry.
// Vertex layout: position, normal, aux = (deposit, flow, hardness, cavity).

import * as THREE from 'three';

import { SimplexNoise, GradientNoise3 } from './noise.js';
import { makeDetail } from './detail.js';
export { makeDetail };
import { applyRoads, applyLakes, upsampleWaterLevel, NO_WATER } from './features.js';

// Face displacement: moves vertices horizontally along the outward face normal so that hard beds
// stand proud of the face and soft beds are recessed. Because this is applied to the mesh (not the
// heightfield) it produces genuine overhangs, ledges and alcoves that a heightmap cannot represent.
export function makeDisplacement(field, v) {
  const { resolution: N, worldSize: size } = field;
  const cell = size / (N - 1);
  const noise = new SimplexNoise((v.seed || 1) * 13 + 5);
  const overhang = v.overhang || 0;
  const buttress = v.buttress || 0;
  const ledge = v.ledgeNoise || 0;
  // Keep displacement below the cell size so the grid never folds over itself.
  const limit = cell * 0.85;
  return {
    limit,
    // returns horizontal offset (dx, dz) and a small vertical sag for a grid vertex
    at(i, j, nx, ny, nz, hardness, x, z, h) {
      const steep = Math.min(1, Math.max(0, (1 - ny - 0.25) / 0.45)); // 0 below ~40°, 1 above ~70°
      if (steep <= 0 || overhang + buttress <= 0) return [0, 0, 0];
      const hl = Math.hypot(nx, nz) || 1e-6;
      const ox = nx / hl, oz = nz / hl;
      // caprock out, soft beds in; notch the ledges with noise so they are not continuous shelves
      let bed = (hardness - 0.45) * 2;
      const notch = noise.fbm(x * 0.03 + h * 0.05, z * 0.03, 3);
      bed *= 1 - ledge * 0.6 * Math.max(0, notch);
      let d = bed * overhang;
      // large buttresses / alcoves
      d += buttress * noise.fbm(x * 0.006, z * 0.006 + h * 0.004, 3) * limit * 1.4;
      d = Math.max(-limit, Math.min(limit, d)) * steep;
      // a lip sags slightly under its own weight
      const sag = Math.min(0, -Math.max(0, d) * 0.15);
      return [ox * d, oz * d, sag];
    },
  };
}


// Catmull-Rom bicubic upsampling of every field map, so the mesh can carry more vertices than the
// simulated heightfield (erosion cost grows with N², mesh detail is cheap by comparison).
// The mesh can be subdivided well past the simulated grid; this is the vertex count per side it
// is allowed to reach, and the subdivision factor is clamped so it never does.
const MAX_MESH_SIDE = 4097;
export function meshSubdivision(field, v) {
  const want = Math.max(1, Math.round(v.meshSubdivision || 1));
  const maxK = Math.max(1, Math.floor((MAX_MESH_SIDE - 1) / (field.resolution - 1)));
  return Math.min(want, maxK);
}
function cubic(p0, p1, p2, p3, t) {
  return 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t);
}
function upsampleMap(src, N, k, smoothOnly) {
  const M = (N - 1) * k + 1;
  const out = new Float32Array(M * M);
  const at = (i, j) => src[Math.min(N - 1, Math.max(0, j)) * N + Math.min(N - 1, Math.max(0, i))];
  const col = new Float32Array(4);
  for (let J = 0; J < M; J++) {
    const gj = J / k, j = Math.min(N - 2, Math.floor(gj)), tj = gj - j;
    for (let I = 0; I < M; I++) {
      const gi = I / k, i = Math.min(N - 2, Math.floor(gi)), ti = gi - i;
      if (smoothOnly) {
        // bilinear for the auxiliary maps (no overshoot on masks)
        out[J * M + I] = at(i, j) * (1 - ti) * (1 - tj) + at(i + 1, j) * ti * (1 - tj) + at(i, j + 1) * (1 - ti) * tj + at(i + 1, j + 1) * ti * tj;
      } else {
        for (let r = -1; r <= 2; r++) col[r + 1] = cubic(at(i - 1, j + r), at(i, j + r), at(i + 1, j + r), at(i + 2, j + r), ti);
        out[J * M + I] = cubic(col[0], col[1], col[2], col[3], tj);
      }
    }
  }
  return out;
}
function featureKey(v) {
  const f = v.features || {};
  return JSON.stringify([f.roads || [], f.lakes || [], v.roadWidth, v.roadShoulder, v.roadSmoothing, v.roadCut, v.roadFill, v.lakeDepth, v.lakeWater, v.riverWater]);
}

// Refine the simulated field for the mesh: bicubic upsample (k×), then the post-erosion features
// (roads, lakes) and the merged water level. Cached on the field by (k, feature key).
export function refineField(field, v) {
  const k = meshSubdivision(field, v);
  const key = `${k}|${featureKey(v)}`;
  if (field._refined && field._refined.key === key) return field._refined.field;
  const N = field.resolution;
  const M = (N - 1) * k + 1;
  const empty = () => new Float32Array(N * N);
  const fine = k <= 1 ? {
    resolution: N, worldSize: field.worldSize,
    height: field.height.slice(), hardness: field.hardness, deposit: field.deposit, flow: field.flow, cavity: field.cavity, slope: field.slope,
    river: field.river || empty(), waterLevel: (field.waterLevel || empty().fill(NO_WATER)).slice(), lakeSim: field.lake || empty(),
    outcrop: field.outcrop || null,
    stats: field.stats, base: field,
  } : {
    resolution: M,
    worldSize: field.worldSize,
    height: upsampleMap(field.height, N, k, false),
    hardness: upsampleMap(field.hardness, N, k, true),
    deposit: upsampleMap(field.deposit, N, k, true),
    flow: upsampleMap(field.flow, N, k, true),
    cavity: upsampleMap(field.cavity, N, k, true),
    slope: upsampleMap(field.slope, N, k, true),
    river: upsampleMap(field.river || empty(), N, k, true),
    waterLevel: upsampleWaterLevel(field.waterLevel || empty().fill(NO_WATER), N, k),
    lakeSim: upsampleMap(field.lake || empty(), N, k, true),
    outcrop: field.outcrop ? upsampleMap(field.outcrop, N, k, true) : null,
    stats: field.stats,
    base: field,
  };
  if (!v.riverWater) {
    // dry the channels; simulated lakes follow the lake toggle instead
    for (let i = 0; i < fine.waterLevel.length; i++) if (!(v.lakeWater && fine.lakeSim[i] > 0.9)) fine.waterLevel[i] = NO_WATER;
  } else if (!v.lakeWater) {
    for (let i = 0; i < fine.waterLevel.length; i++) if (fine.lakeSim[i] > 0.9) fine.waterLevel[i] = NO_WATER;
  }
  const features = v.features || {};
  const { roadMask } = applyRoads(fine.height, fine.resolution, fine.worldSize, features.roads, {
    width: v.roadWidth, shoulder: v.roadShoulder, smoothing: v.roadSmoothing, cutAngle: v.roadCut, fillAngle: v.roadFill,
  });
  const { lakeMask } = applyLakes(fine.height, fine.resolution, fine.worldSize, features.lakes, { depth: v.lakeDepth, water: !!v.lakeWater }, fine.waterLevel);
  fine.road = roadMask;
  for (let i = 0; i < lakeMask.length; i++) lakeMask[i] = Math.max(lakeMask[i], fine.lakeSim[i]);
  fine.lake = lakeMask;
  // painted water follows the water switch: a dry landform (a desert of stacks, a quarry) must
  // not keep lakes from an earlier wet setting, or the flats read as holes in the rock
  fine.waterPaint = v.waterEnabled === 0 ? new Float32Array(fine.resolution * fine.resolution).fill(NO_WATER)
    : continuousWaterLevel(fine.waterLevel, fine.height, fine.resolution, fine.worldSize / (fine.resolution - 1));
  field._refined = { key, field: fine };
  return fine;
}

// Water level as a *continuous* per-vertex field for the painted-water shader. The simulation
// leaves NO_WATER (-1e6) on dry cells; interpolated across a triangle against a real level that
// makes the water line jump within the triangle and the shore turns into a cell-by-cell
// checkerboard. The level is extrapolated outwards from the wet cells, falling away with distance,
// and far from any water it simply sits well below the ground.
export function continuousWaterLevel(waterLevel, height, N, cell) {
  const total = N * N;
  const out = new Float32Array(total);
  const wet = new Uint8Array(total);
  for (let c = 0; c < total; c++) { wet[c] = waterLevel[c] > NO_WATER * 0.5 ? 1 : 0; out[c] = wet[c] ? waterLevel[c] : NO_WATER; }
  const drop = Math.max(0.75, cell * 0.35);
  let frontier = [];
  for (let c = 0; c < total; c++) if (wet[c]) frontier.push(c);
  const dist = new Uint8Array(total);
  for (let pass = 1; pass <= 10 && frontier.length; pass++) {
    const next = [];
    for (const c of frontier) {
      const ci = c % N, cj = (c - ci) / N;
      for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) {
        const ii = ci + di, jj = cj + dj;
        if (ii < 0 || jj < 0 || ii >= N || jj >= N) continue;
        const n = jj * N + ii;
        if (wet[n]) continue;
        const v = out[c] - drop * (di && dj ? 1.414 : 1);
        if (v > out[n]) { out[n] = v; if (!dist[n]) { dist[n] = pass; next.push(n); } }
      }
    }
    frontier = next;
  }
  for (let c = 0; c < total; c++) {
    if (wet[c]) continue;
    if (out[c] <= NO_WATER * 0.5) out[c] = height[c] - 40;
    else out[c] = Math.min(out[c], height[c] - 0.02 * dist[c]); // never above the ground it was extrapolated onto
  }
  return out;
}

// Fine relief added along the surface normal: rock bumps / knobs on steep faces, gentle hummocks on
// flat ground. Independent of the heightfield so it survives any resolution.
export function buildTerrainGeometry(field, v = {}, chunks = null) {
  const { resolution: N, worldSize: size, height, deposit, flow, hardness, cavity } = field;
  const road = field.road, river = field.river, lake = field.lake;
  const waterLevel = field.waterPaint || field.waterLevel;
  const cell = size / (N - 1);
  const count = N * N;
  const positions = new Float32Array(count * 3);
  const normals = new Float32Array(count * 3);
  const aux = new Float32Array(count * 4);
  const aux2 = new Float32Array(count * 4);
  const disp = makeDisplacement(field, v);
  const detail = makeDetail(field, v);
  const fadeCells = 3;
  // SDF cliff chunks: no displacement on / near their footprint (the chunk field is the authority
  // there and its border must coincide with the plain heightfield), and their quads are skipped
  const sdfFade = chunks ? chunks.fade : null;

  for (let j = 0; j < N; j++) {
    const z = (j / (N - 1) - 0.5) * size;
    const j0 = Math.max(0, j - 1), j1 = Math.min(N - 1, j + 1);
    for (let i = 0; i < N; i++) {
      const idx = j * N + i;
      const x = (i / (N - 1) - 0.5) * size;
      const i0 = Math.max(0, i - 1), i1 = Math.min(N - 1, i + 1);
      const dx = (height[j * N + i1] - height[j * N + i0]) / ((i1 - i0) * cell);
      const dz = (height[j1 * N + i] - height[j0 * N + i]) / ((j1 - j0) * cell);
      const nx = -dx, ny = 1, nz = -dz;
      const inv = 1 / Math.hypot(nx, ny, nz);
      const edge = i === 0 || j === 0 || i === N - 1 || j === N - 1;
      const chunkFade = sdfFade ? sdfFade[idx] : 1;
      let [ox, oz, sag] = edge || chunkFade <= 0 ? [0, 0, 0] : disp.at(i, j, nx * inv, ny * inv, nz * inv, hardness[idx], x, z, height[idx]);
      if (chunkFade < 1) { ox *= chunkFade; oz *= chunkFade; sag *= chunkFade; }
      let det = 0;
      if (detail.amp > 0 && !edge && chunkFade > 0) {
        const border = Math.min(i, j, N - 1 - i, N - 1 - j) / fadeCells;
        det = detail.at(x, height[idx], z, ny * inv, hardness[idx]) * Math.min(1, border) * chunkFade;
      }
      positions[idx * 3] = x + ox + nx * inv * det;
      positions[idx * 3 + 1] = height[idx] + sag + ny * inv * det;
      positions[idx * 3 + 2] = z + oz + nz * inv * det;
      normals[idx * 3] = nx * inv;
      normals[idx * 3 + 1] = ny * inv;
      normals[idx * 3 + 2] = nz * inv;
      aux[idx * 4] = deposit[idx];
      aux[idx * 4 + 1] = flow[idx];
      aux[idx * 4 + 2] = hardness[idx];
      aux[idx * 4 + 3] = cavity[idx];
      const rd = road ? road[idx] : 0;
      aux2[idx * 4] = rd;
      aux2[idx * 4 + 1] = river ? river[idx] : 0;
      aux2[idx * 4 + 2] = lake ? lake[idx] : 0;
      aux2[idx * 4 + 3] = waterLevel ? waterLevel[idx] : NO_WATER;
      if (rd > 0.5) {
        // roads are flat: strip the normal-space detail off the carriageway
        positions[idx * 3] = x + ox; positions[idx * 3 + 1] = height[idx] + sag; positions[idx * 3 + 2] = z + oz;
      }
    }
  }

  let indices = new Uint32Array((N - 1) * (N - 1) * 6);
  let k = 0;
  const active = chunks ? chunks.active : null, C = chunks ? chunks.C : 1, nc = chunks ? chunks.nc : 1;
  for (let j = 0; j < N - 1; j++) {
    for (let i = 0; i < N - 1; i++) {
      if (active && active[Math.floor(j / C) * nc + Math.floor(i / C)]) continue;
      const a = j * N + i, b = a + 1, c = a + N, d = c + 1;
      // Alternate the diagonal to avoid a directional bias on steep faces.
      if ((i + j) & 1) {
        indices[k++] = a; indices[k++] = c; indices[k++] = b;
        indices[k++] = b; indices[k++] = c; indices[k++] = d;
      } else {
        indices[k++] = a; indices[k++] = c; indices[k++] = d;
        indices[k++] = a; indices[k++] = d; indices[k++] = b;
      }
    }
  }

  if (k < indices.length) indices = indices.slice(0, k);
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  geometry.setAttribute('normal', new THREE.BufferAttribute(normals, 3));
  geometry.setAttribute('aux', new THREE.BufferAttribute(aux, 4));
  geometry.setAttribute('aux2', new THREE.BufferAttribute(aux2, 4));
  geometry.setIndex(new THREE.BufferAttribute(indices, 1));
  if ((v.overhang || 0) + (v.buttress || 0) + (v.detailRelief || 0) + (v.rockyAmount || 0) > 0) {
    // displaced faces need true mesh normals; blend with the heightfield normal to keep them smooth
    const smooth = normals.slice();
    geometry.computeVertexNormals();
    const mesh = geometry.getAttribute('normal').array;
    for (let n = 0; n < mesh.length; n += 3) {
      const x = mesh[n] * 0.75 + smooth[n] * 0.25, y = mesh[n + 1] * 0.75 + smooth[n + 1] * 0.25, z = mesh[n + 2] * 0.75 + smooth[n + 2] * 0.25;
      const l = 1 / (Math.hypot(x, y, z) || 1);
      mesh[n] = x * l; mesh[n + 1] = y * l; mesh[n + 2] = z * l;
    }
  }
  geometry.computeBoundingSphere();
  geometry.computeBoundingBox();
  return geometry;
}

// Vertical walls around the tile so the terrain reads as a cut block of ground.
export function buildSkirtGeometry(field, floorY) {
  const { resolution: N, worldSize: size, height, hardness } = field;
  const edges = [
    { count: N, at: (t) => [t, 0], normal: [0, 0, -1] },            // north  (j = 0)
    { count: N, at: (t) => [N - 1 - t, N - 1], normal: [0, 0, 1] }, // south  (j = N-1)
    { count: N, at: (t) => [0, N - 1 - t], normal: [-1, 0, 0] },    // west   (i = 0)
    { count: N, at: (t) => [N - 1, t], normal: [1, 0, 0] },         // east   (i = N-1)
  ];
  const positions = [], normals = [], aux = [], aux2 = [], indices = [];
  let base = 0;
  for (const edge of edges) {
    for (let t = 0; t < edge.count; t++) {
      const [i, j] = edge.at(t);
      const x = (i / (N - 1) - 0.5) * size;
      const z = (j / (N - 1) - 0.5) * size;
      const idx = j * N + i;
      positions.push(x, height[idx], z, x, floorY, z);
      normals.push(...edge.normal, ...edge.normal);
      aux.push(0, 0, hardness[idx], 0, 0, 0, hardness[idx], 0);
      aux2.push(0, 0, 0, NO_WATER, 0, 0, 0, NO_WATER);
    }
    for (let t = 0; t < edge.count - 1; t++) {
      const a = base + t * 2, b = a + 1, c = a + 2, d = a + 3;
      indices.push(a, b, c, b, d, c);
    }
    base += edge.count * 2;
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geometry.setAttribute('normal', new THREE.Float32BufferAttribute(normals, 3));
  geometry.setAttribute('aux', new THREE.Float32BufferAttribute(aux, 4));
  geometry.setAttribute('aux2', new THREE.Float32BufferAttribute(aux2, 4));
  geometry.setIndex(indices);
  geometry.computeBoundingSphere();
  return geometry;
}

// Bilinear sampling helpers for rock placement.
export function makeSampler(field) {
  const { resolution: N, worldSize: size } = field;
  const toGrid = (x, z) => [((x / size) + 0.5) * (N - 1), ((z / size) + 0.5) * (N - 1)];
  const bilinear = (arr, gx, gz) => {
    const i = Math.max(0, Math.min(N - 2, Math.floor(gx)));
    const j = Math.max(0, Math.min(N - 2, Math.floor(gz)));
    const u = Math.min(1, Math.max(0, gx - i)), v = Math.min(1, Math.max(0, gz - j));
    const idx = j * N + i;
    return arr[idx] * (1 - u) * (1 - v) + arr[idx + 1] * u * (1 - v) + arr[idx + N] * (1 - u) * v + arr[idx + N + 1] * u * v;
  };
  const cell = size / (N - 1);
  return {
    height: (x, z) => { const [gx, gz] = toGrid(x, z); return bilinear(field.height, gx, gz); },
    normal: (x, z) => {
      const [gx, gz] = toGrid(x, z);
      const h = (dx, dz) => bilinear(field.height, gx + dx, gz + dz);
      const nx = -(h(1, 0) - h(-1, 0)) / (2 * cell), nz = -(h(0, 1) - h(0, -1)) / (2 * cell);
      const inv = 1 / Math.hypot(nx, 1, nz);
      return [nx * inv, inv, nz * inv];
    },
    map: (name, x, z) => { const [gx, gz] = toGrid(x, z); return bilinear(field[name], gx, gz); },
    // world-space point on the rendered surface at (x, z): heightfield + face displacement + detail
    surface(x, z, v) {
      const [gx, gz] = toGrid(x, z);
      const h = bilinear(field.height, gx, gz);
      const n = this.normal(x, z);
      let px = x, py = h, pz = z;
      if (v && ((v.overhang || 0) + (v.buttress || 0)) > 0) {
        if (!this._disp || this._disp.v !== v) { this._disp = makeDisplacement(field, v); this._disp.v = v; }
        const [ox, oz, sag] = this._disp.at(0, 0, n[0], n[1], n[2], bilinear(field.hardness, gx, gz), x, z, h);
        px += ox; pz += oz; py += sag;
      }
      if (v && ((v.detailRelief || 0) + (v.rockyAmount || 0)) > 0) {
        if (!this._det || this._det.v !== v) { this._det = makeDetail(field, v); this._det.v = v; }
        const d = this._det.at(x, h, z, n[1], bilinear(field.hardness, gx, gz));
        px += n[0] * d; py += n[1] * d; pz += n[2] * d;
      }
      return [px, py, pz];
    },
    // horizontal offset the face displacement applies near (x, z)
    displaced(x, z, v) {
      if (!v || ((v.overhang || 0) + (v.buttress || 0)) <= 0) return [x, z];
      if (!this._disp || this._disp.v !== v) { this._disp = makeDisplacement(field, v); this._disp.v = v; }
      const [gx, gz] = toGrid(x, z);
      const h = bilinear(field.height, gx, gz);
      const n = this.normal(x, z);
      const [ox, oz] = this._disp.at(0, 0, n[0], n[1], n[2], bilinear(field.hardness, gx, gz), x, z, h);
      return [x + ox, z + oz];
    },
  };
}

// Water surface for rivers and lakes: grid cells whose water level stands above the ground.
export function buildWaterGeometry(field) {
  const { resolution: N, worldSize: size, height, waterLevel } = field;
  if (!waterLevel) return null;
  const index = new Int32Array(N * N).fill(-1);
  const positions = [];
  const indices = [];
  const wet = (idx) => waterLevel[idx] > NO_WATER * 0.5 && waterLevel[idx] > height[idx] - 0.5;
  // per-vertex level: own level when wet, else the highest wet neighbour (shore vertices) — the
  // sheet is then one continuous surface that follows the river's grade instead of stepped plates
  const vertexLevel = (i, j, idx) => {
    if (wet(idx)) return waterLevel[idx];
    let lvl = NO_WATER;
    for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) {
      const ii = i + di, jj = j + dj;
      if (ii < 0 || jj < 0 || ii >= N || jj >= N) continue;
      const n = jj * N + ii;
      if (wet(n) && waterLevel[n] > lvl) lvl = waterLevel[n];
    }
    return lvl;
  };
  const depths = [], foams = [];
  const cell = size / (N - 1);
  // grade of the water surface → white water on rapids and falls
  const lvlAt = (i, j, fallback) => {
    if (i < 0 || j < 0 || i >= N || j >= N) return fallback;
    const n = j * N + i;
    return wet(n) ? waterLevel[n] : fallback;
  };
  const vertex = (i, j, idx) => {
    if (index[idx] >= 0) return index[idx];
    const x = (i / (N - 1) - 0.5) * size, z = (j / (N - 1) - 0.5) * size;
    const lvl = vertexLevel(i, j, idx);
    positions.push(x, lvl, z);
    depths.push(Math.max(0, lvl - height[idx]));
    const gx = (lvlAt(i + 1, j, lvl) - lvlAt(i - 1, j, lvl)) / (2 * cell), gz = (lvlAt(i, j + 1, lvl) - lvlAt(i, j - 1, lvl)) / (2 * cell);
    const grade = Math.hypot(gx, gz);
    const t = Math.min(1, Math.max(0, (grade - 0.05) / 0.15));
    foams.push(t * t * (3 - 2 * t));
    index[idx] = positions.length / 3 - 1;
    return index[idx];
  };
  for (let j = 0; j < N - 1; j++) {
    for (let i = 0; i < N - 1; i++) {
      const a = j * N + i, b = a + 1, c = a + N, d = c + 1;
      const anyWet = wet(a) || wet(b) || wet(c) || wet(d);
      if (!anyWet) continue;
      const va = vertex(i, j, a), vb = vertex(i + 1, j, b), vc = vertex(i, j + 1, c), vd = vertex(i + 1, j + 1, d);
      // a quad whose four corners all stand above their water level is a dry bank
      if (height[a] > positions[va * 3 + 1] + 0.5 && height[b] > positions[vb * 3 + 1] + 0.5 && height[c] > positions[vc * 3 + 1] + 0.5 && height[d] > positions[vd * 3 + 1] + 0.5) continue;
      indices.push(va, vc, vb, vb, vc, vd);
    }
  }
  if (!indices.length) return null;
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geometry.setAttribute('aDepth', new THREE.Float32BufferAttribute(depths, 1));
  geometry.setAttribute('aFoam', new THREE.Float32BufferAttribute(foams, 1));
  geometry.setIndex(indices);
  geometry.computeVertexNormals();
  geometry.computeBoundingSphere();
  return geometry;
}

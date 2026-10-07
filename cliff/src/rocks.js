/*
 * Procedural rock meshes.
 *
 * A rock is born from an icosahedron, lumped with fBm, then "fractured" by
 * collapsing clusters of vertices onto their best-fit plane.  The result is an
 * angular, low-poly chunk with believable flat fracture planes - the same
 * trick used when authors kit-bash cliff faces out of rock chunks.
 *
 * No textures, no normal maps: the silhouette and the facets *are* the detail.
 */
import * as THREE from '../vendor/three.module.js';
import { mulberry32, hash1 } from './util.js';
import { makeNoise3, fbm3 } from './noise.js';

const _v = new THREE.Vector3();

/**
 * Build a small library of rock meshes.
 * @returns {{geo:THREE.BufferGeometry, radius:number, baseY:number}[]}
 */
export function makeRockLibrary({ count = 14, seed = 1 } = {}) {
  const rng = mulberry32((seed * 2654435761) >>> 0);
  const noise = makeNoise3(seed + 7717);
  const lib = [];

  for (let i = 0; i < count; i++) {
    const detail = rng() < 0.4 ? 1 : 2; // 80 or 320 faces
    // PolyhedronGeometry is already non-indexed, but stay safe across versions
    let geo = new THREE.IcosahedronGeometry(1, detail);
    if (geo.index) geo = geo.toNonIndexed();
    const pos = geo.attributes.position;

    const amp = 0.18 + rng() * 0.24;   // lumpiness
    const freq = 0.85 + rng() * 1.5;
    const sx = 0.78 + rng() * 0.5;
    const sy = 0.5 + rng() * 0.55;
    const sz = 0.78 + rng() * 0.5;

    // --- 1. lumpy displacement -------------------------------------------
    for (let k = 0; k < pos.count; k++) {
      _v.fromBufferAttribute(pos, k);
      const n1 = noise.noise3(_v.x * freq, _v.y * freq, _v.z * freq);
      const n2 = noise.noise3(_v.x * freq * 2.9 + 13.1, _v.y * freq * 2.9, _v.z * freq * 2.9 - 4.2);
      const n3 = fbm3(noise, _v.x * freq * 0.45, _v.y * freq * 0.45, _v.z * freq * 0.45, 2);
      const r = 1 + amp * n1 + amp * 0.45 * n2 + amp * 0.8 * n3;
      _v.multiplyScalar(r);
      _v.x *= sx;
      _v.y *= sy;
      _v.z *= sz;
      // flatten the underside so rocks sit on floors instead of floating
      const base = -sy * 0.9;
      if (_v.y < base) _v.y = base + (_v.y - base) * 0.22;
      pos.setXYZ(k, _v.x, _v.y, _v.z);
    }

    // --- 2. fracture into flat facets ------------------------------------
    geo.computeVertexNormals(); // per-face normals (non-indexed)
    facet(geo, 0.26 + rng() * 0.18);
    geo.computeVertexNormals();

    geo.computeBoundingSphere();
    let minY = Infinity;
    for (let k = 0; k < pos.count; k++) minY = Math.min(minY, pos.getY(k));

    lib.push({ geo, radius: geo.boundingSphere.radius, baseY: minY });
  }
  return lib;
}

/**
 * Collapse clusters of nearby vertices onto a shared plane.  Clusters are found
 * by quantising position; the plane normal is the average of the member face
 * normals (robust, unlike a Newell fit on an unordered cluster).
 */
function facet(geo, cell) {
  const pos = geo.attributes.position;
  const nrm = geo.attributes.normal;
  const n = pos.count;

  const groups = new Map();
  const inv = 1 / cell;
  for (let i = 0; i < n; i++) {
    const k =
      Math.round(pos.getX(i) * inv) + ',' +
      Math.round(pos.getY(i) * inv) + ',' +
      Math.round(pos.getZ(i) * inv);
    let g = groups.get(k);
    if (!g) { g = []; groups.set(k, g); }
    g.push(i);
  }

  for (const g of groups.values()) {
    if (g.length === 0) continue;
    let cx = 0, cy = 0, cz = 0;
    for (const i of g) { cx += pos.getX(i); cy += pos.getY(i); cz += pos.getZ(i); }
    cx /= g.length; cy /= g.length; cz /= g.length;

    let nx = 0, ny = 0, nz = 0;
    for (const i of g) { nx += nrm.getX(i); ny += nrm.getY(i); nz += nrm.getZ(i); }
    let len = Math.hypot(nx, ny, nz);
    if (len < 1e-6 || g.length < 3) {
      // fall back to the radial direction (pointy spikes stay pointy)
      nx = cx; ny = cy; nz = cz;
      len = Math.hypot(nx, ny, nz) || 1;
    }
    nx /= len; ny /= len; nz /= len;

    for (const i of g) {
      const x = pos.getX(i), y = pos.getY(i), z = pos.getZ(i);
      const d = (x - cx) * nx + (y - cy) * ny + (z - cz) * nz;
      pos.setXYZ(i, x - d * nx, y - d * ny, z - d * nz);
    }
  }
}

/**
 * Places a rock from the library into a MeshBuilder.
 * `tilt` rotates the rock away from +Y (its "up") by up to `tilt` radians.
 */
export function placeRock(lib, rng, mb, {
  x, y, z, scale, yaw, tilt = 0, tint, jitter = 0.16, jitterSeed = 0, shade = 1,
}) {
  const rock = lib[(rng() * lib.length) | 0];
  const s = scale * (0.8 + rng() * 0.45);
  const q = new THREE.Quaternion().setFromEuler(
    new THREE.Euler(tilt * (rng() - 0.5) * 2, yaw, tilt * (rng() - 0.5) * 2, 'YZX')
  );
  const m = new THREE.Matrix4().compose(
    new THREE.Vector3(x, y - rock.baseY * s * 0.55, z),
    q,
    new THREE.Vector3(s, s * (0.75 + rng() * 0.5), s)
  );
  mb.addGeometry(rock.geo, m, tint, { jitter, jitterSeed, shade });
  return s;
}

/** deterministic jitter helper reused by the scatter passes */
export function rockSeedOf(i) { return (hash1(i) * 1e6) | 0; }

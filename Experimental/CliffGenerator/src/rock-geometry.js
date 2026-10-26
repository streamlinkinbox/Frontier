// Procedural rock archetypes: a displaced icosphere, anisotropically stretched, then clipped
// by a set of cleavage planes (bedding planes near-horizontal, joint sets near-vertical) so the
// result has the flat faces and sharp arrises of real jointed rock. Normals are crease-aware:
// smooth on rounded regions, hard across cut edges.

import * as THREE from 'three';
import { GradientNoise3, mulberry32 } from './noise.js';

export const ROCK_FAMILIES = {
  // [bedding flatness, joint sharpness, roundness]
  boulder: { flatten: 1.0, planes: 5, bedBias: 0.25, roundness: 0.6, cut: 0.82 },
  block:   { flatten: 0.9, planes: 9, bedBias: 0.7, roundness: 0.15, cut: 0.7 },
  slab:    { flatten: 0.62, planes: 7, bedBias: 0.9, roundness: 0.2, cut: 0.72 },
  shard:   { flatten: 0.85, planes: 11, bedBias: 0.2, roundness: 0.05, cut: 0.66 },
};

export function buildRockGeometry({ seed = 1, family = 'block', angularity = 0.7, detail = 3 } = {}) {
  const spec = ROCK_FAMILIES[family] || ROCK_FAMILIES.block;
  const rand = mulberry32(seed * 131 + 17);
  const noise = new GradientNoise3(seed);
  const base = new THREE.IcosahedronGeometry(1, detail); // already non-indexed (flat faces)
  const pos = base.getAttribute('position');
  const count = pos.count;

  // Anisotropic stretch — bedding makes rocks wider than tall.
  const sx = 0.85 + rand() * 0.6;
  const sz = 0.75 + rand() * 0.7;
  const sy = (0.7 + rand() * 0.5) * spec.flatten;

  // Cleavage planes. Bedding planes cluster around ±Y, joints around two azimuths.
  const planeCount = Math.round(spec.planes * (0.4 + angularity * 0.8)) + 2;
  const jointA = rand() * Math.PI, jointB = jointA + Math.PI / 2 + (rand() - 0.5) * 0.6;
  const planes = [];
  for (let k = 0; k < planeCount; k++) {
    let n;
    const r = rand();
    if (r < spec.bedBias * 0.5) {
      // bedding: nearly horizontal normal pointing up or down
      const tilt = (rand() - 0.5) * 0.35;
      const az = rand() * Math.PI * 2;
      n = new THREE.Vector3(Math.sin(tilt) * Math.cos(az), rand() < 0.5 ? 1 : -1, Math.sin(tilt) * Math.sin(az));
    } else if (r < spec.bedBias * 0.5 + 0.55) {
      const az = (rand() < 0.5 ? jointA : jointB) + (rand() - 0.5) * 0.5;
      const dip = (rand() - 0.5) * 0.5;
      n = new THREE.Vector3(Math.cos(az), dip, Math.sin(az));
      if (rand() < 0.5) n.negate();
    } else {
      n = new THREE.Vector3(rand() - 0.5, rand() - 0.5, rand() - 0.5);
    }
    n.normalize();
    const d = spec.cut + rand() * (1.0 - spec.cut);
    planes.push({ n, d });
  }
  // A few open joints: narrow grooves cut across the block where it will eventually split.
  const joints = [];
  const jointCount = Math.round(rand() * 2 + angularity * 2);
  for (let k = 0; k < jointCount; k++) {
    const n = new THREE.Vector3(rand() - 0.5, (rand() - 0.5) * 0.4, rand() - 0.5).normalize();
    joints.push({ n, d: (rand() - 0.5) * 0.8, w: 0.03 + rand() * 0.04, depth: 0.04 + rand() * 0.06 });
  }

  const v = new THREE.Vector3();
  const dir = new THREE.Vector3();
  const out = new Float32Array(count * 3);
  const roundness = spec.roundness + (1 - angularity) * 0.5;
  for (let i = 0; i < count; i++) {
    v.fromBufferAttribute(pos, i);
    dir.copy(v);
    // Large-scale lumpiness then medium detail before cutting.
    const big = noise.fbm(v.x * 0.9 + 7, v.y * 0.9, v.z * 0.9, 3, 2, 0.5);
    const mid = noise.fbm(v.x * 2.6, v.y * 2.6 + 3, v.z * 2.6, 3, 2, 0.5);
    const r = 1 + big * 0.28 + mid * 0.09;
    v.multiplyScalar(r);
    v.set(v.x * sx, v.y * sy, v.z * sz);

    // Planar clipping blended by angularity → flat faces with soft/hard arrises.
    const original = v.clone();
    for (const { n, d } of planes) {
      const dist = v.dot(n) - d * Math.max(sx, sy, sz) * 0.95;
      if (dist > 0) v.addScaledVector(n, -dist);
    }
    // roundness pulls a little back towards the uncut shape (bevelled edges)
    v.lerp(original, roundness * 0.25);

    // Joint grooves and fine chipping after the cut so faces are not perfectly planar.
    for (const { n, d, w, depth } of joints) {
      const t = Math.abs(v.dot(n) - d);
      if (t < w) v.addScaledVector(dir, -depth * (1 - t / w));
    }
    const chip = noise.fbm(v.x * 7 + 21, v.y * 7, v.z * 7 - 4, 3, 2, 0.5);
    const pit = noise.fbm(v.x * 14 + 3, v.y * 14, v.z * 14, 2, 2, 0.5);
    v.addScaledVector(dir, chip * 0.045 + Math.min(0, pit) * 0.03);

    out[i * 3] = v.x; out[i * 3 + 1] = v.y; out[i * 3 + 2] = v.z;
  }

  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.BufferAttribute(out, 3));
  computeCreaseNormals(geometry, 34);
  geometry.setAttribute('aux', new THREE.BufferAttribute(new Float32Array(count * 4), 4));

  // Normalise: unit bounding radius, centre at origin, remember bottom extent.
  geometry.computeBoundingBox();
  const bb = geometry.boundingBox;
  const centre = bb.getCenter(new THREE.Vector3());
  geometry.translate(-centre.x, -centre.y, -centre.z);
  geometry.computeBoundingBox();
  const sizeV = geometry.boundingBox.getSize(new THREE.Vector3());
  const scale = 2 / Math.max(sizeV.x, sizeV.y, sizeV.z);
  geometry.scale(scale, scale, scale);
  geometry.computeBoundingBox();
  geometry.computeBoundingSphere();
  geometry.userData.bottom = geometry.boundingBox.min.y;
  geometry.userData.height = geometry.boundingBox.max.y - geometry.boundingBox.min.y;
  return geometry;
}

// Smooth normals where adjacent faces are nearly coplanar, hard normals across sharp edges.
function computeCreaseNormals(geometry, creaseDegrees) {
  const pos = geometry.getAttribute('position');
  const count = pos.count;
  const faceNormals = new Float32Array(count * 3);
  const a = new THREE.Vector3(), b = new THREE.Vector3(), c = new THREE.Vector3(), n = new THREE.Vector3();
  const buckets = new Map();
  const key = (x, y, z) => `${Math.round(x * 1e4)},${Math.round(y * 1e4)},${Math.round(z * 1e4)}`;
  for (let i = 0; i < count; i += 3) {
    a.fromBufferAttribute(pos, i); b.fromBufferAttribute(pos, i + 1); c.fromBufferAttribute(pos, i + 2);
    n.subVectors(c, b).cross(a.clone().sub(b)).normalize();
    for (let k = 0; k < 3; k++) {
      faceNormals[(i + k) * 3] = n.x; faceNormals[(i + k) * 3 + 1] = n.y; faceNormals[(i + k) * 3 + 2] = n.z;
      const kk = key(pos.getX(i + k), pos.getY(i + k), pos.getZ(i + k));
      let list = buckets.get(kk);
      if (!list) { list = []; buckets.set(kk, list); }
      list.push(i + k);
    }
  }
  const cosCrease = Math.cos((creaseDegrees * Math.PI) / 180);
  const normals = new Float32Array(count * 3);
  const acc = new THREE.Vector3(), fn = new THREE.Vector3(), other = new THREE.Vector3();
  for (const list of buckets.values()) {
    for (const i of list) {
      fn.set(faceNormals[i * 3], faceNormals[i * 3 + 1], faceNormals[i * 3 + 2]);
      acc.set(0, 0, 0);
      for (const j of list) {
        other.set(faceNormals[j * 3], faceNormals[j * 3 + 1], faceNormals[j * 3 + 2]);
        if (other.dot(fn) >= cosCrease) acc.add(other);
      }
      acc.normalize();
      normals[i * 3] = acc.x; normals[i * 3 + 1] = acc.y; normals[i * 3 + 2] = acc.z;
    }
  }
  geometry.setAttribute('normal', new THREE.BufferAttribute(normals, 3));
}

// A small library of archetypes shared by all placements. Low detail for scree, higher for blocks.
export function buildRockLibrary(seed, angularity) {
  const families = ['block', 'block', 'boulder', 'slab', 'shard', 'block', 'boulder', 'block', 'slab', 'boulder'];
  const byFamily = {};
  families.forEach((f, i) => { (byFamily[f] = byFamily[f] || []).push(i); });
  // the placer asks for a shape by family (a cap is a boulder, a face block is a slab, scree is a
  // shard), so expose the index of every archetype of a family within a detail set
  const sets = {
    large: families.map((family, i) => buildRockGeometry({ seed: seed * 10 + i, family, angularity, detail: 3 })),
    small: families.map((family, i) => buildRockGeometry({ seed: seed * 10 + i + 50, family, angularity, detail: 1 })),
    // pebbles: rounded, low-poly, for the dense gravel scatter
    pebble: families.slice(0, 6).map((family, i) => buildRockGeometry({ seed: seed * 10 + i + 90, family: 'boulder', angularity: angularity * 0.3, detail: 1 })),
  };
  return { ...sets, byFamily: { large: byFamily, small: byFamily, pebble: { boulder: [0, 1, 2, 3, 4, 5] } } };
}

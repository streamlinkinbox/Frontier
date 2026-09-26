import * as THREE from 'three';
import { WORLD, MOUNDS, TRENCHES, ROADS } from './layout.js';

// ---------------------------------------------------------------------------
// Terrain height field. Mesh and physics share this analytic function so
// walking / driving stays glued to the ground.
// ---------------------------------------------------------------------------

function distToSegment(px, pz, ax, az, bx, bz) {
  const abx = bx - ax, abz = bz - az;
  const apx = px - ax, apz = pz - az;
  const len2 = abx * abx + abz * abz;
  let t = len2 > 0 ? (apx * abx + apz * abz) / len2 : 0;
  t = Math.max(0, Math.min(1, t));
  const dx = px - (ax + abx * t), dz = pz - (az + abz * t);
  return Math.sqrt(dx * dx + dz * dz);
}

// Precompute trench segments for fast lookup.
const TRENCH_SEGS = [];
for (const tr of TRENCHES) {
  for (let i = 0; i < tr.points.length - 1; i++) {
    TRENCH_SEGS.push({
      ax: tr.points[i][0], az: tr.points[i][1],
      bx: tr.points[i + 1][0], bz: tr.points[i + 1][1],
      w: tr.width, d: tr.depth,
    });
  }
}

export function terrainHeight(x, z) {
  // gentle beach slope: sea-level sand at the shore, rising toward the wall
  let y = Math.max(-1.6, (z - WORLD.shoreZ) * 0.0175);
  if (z < WORLD.shoreZ) y = Math.max(-2.4, (z - WORLD.shoreZ) * 0.05);

  // huge earth mounds (smooth gaussian bumps)
  for (const m of MOUNDS) {
    const dx = x - m.x, dz = z - m.z;
    const d2 = dx * dx + dz * dz;
    const r2 = m.r * m.r;
    if (d2 < r2 * 4) y += m.h * Math.exp(-d2 / (r2 * 0.55));
  }

  // trenches carved into the ground with soft shoulders
  for (const s of TRENCH_SEGS) {
    const d = distToSegment(x, z, s.ax, s.az, s.bx, s.bz);
    const edge = s.w + 1.1;
    if (d < edge) {
      const t = d / edge;                    // 0 center -> 1 edge
      const k = 1 - t * t * (3 - 2 * t);     // smoothstep falloff
      y -= s.d * k;
    }
  }

  return y;
}

export function trenchDepthAt(x, z) {
  let depth = 0;
  for (const s of TRENCH_SEGS) {
    const d = distToSegment(x, z, s.ax, s.az, s.bx, s.bz);
    const edge = s.w + 1.1;
    if (d < edge) {
      const t = d / edge;
      depth = Math.max(depth, s.d * (1 - t * t * (3 - 2 * t)));
    }
  }
  return depth;
}

// ---------------------------------------------------------------------------

function roadDist(x, z) {
  let best = { d: Infinity, main: false };
  for (const r of ROADS) {
    for (let i = 0; i < r.points.length - 1; i++) {
      const d = distToSegment(x, z, r.points[i][0], r.points[i][1], r.points[i + 1][0], r.points[i + 1][1]);
      if (d < best.d) best = { d, main: r.main, halfWidth: r.halfWidth };
    }
  }
  return best;
}

// deterministic hash noise for subtle color variety
function hash(x, z) {
  const s = Math.sin(x * 127.1 + z * 311.7) * 43758.5453;
  return s - Math.floor(s);
}

const C_SAND = new THREE.Color('#d9c49c');
const C_SAND_DARK = new THREE.Color('#c4ab82');
const C_WET = new THREE.Color('#a68d68');
const C_EARTH = new THREE.Color('#8f7250');
const C_ROAD = new THREE.Color('#b5a07c');
const C_ROAD_MAIN = new THREE.Color('#a89172');
const C_SCRUB = new THREE.Color('#b3a072');

export function buildTerrain(scene) {
  const minX = -WORLD.groundMaxX, maxX = WORLD.groundMaxX;
  const minZ = WORLD.groundMinZ, maxZ = WORLD.groundMaxZ;
  const segX = 260, segZ = 260;

  const geo = new THREE.PlaneGeometry(maxX - minX, maxZ - minZ, segX, segZ);
  geo.rotateX(-Math.PI / 2);
  geo.translate((minX + maxX) / 2, 0, (minZ + maxZ) / 2);

  const pos = geo.attributes.position;
  const colors = new Float32Array(pos.count * 3);
  const col = new THREE.Color();

  for (let i = 0; i < pos.count; i++) {
    const x = pos.getX(i);
    const z = pos.getZ(i);
    const y = terrainHeight(x, z);
    pos.setY(i, y);

    // --- clean vertex-color painting ---
    col.copy(C_SAND);

    // darker, damp sand near / below the shoreline
    const shoreT = THREE.MathUtils.smoothstep(z, WORLD.shoreZ - 30, WORLD.shoreZ + 18);
    col.lerp(C_WET, 1 - shoreT);

    // subtle large-scale variation
    const n = hash(Math.round(x * 0.11), Math.round(z * 0.11));
    if (n > 0.72) col.lerp(C_SCRUB, 0.22);
    else if (n < 0.18) col.lerp(C_SAND_DARK, 0.25);

    // dug earth around trenches
    const td = trenchDepthAt(x, z);
    if (td > 0.02) col.lerp(C_EARTH, Math.min(0.85, td * 0.9 + 0.15));

    // roads painted over
    const rd = roadDist(x, z);
    if (rd.d < rd.halfWidth + 1.2) {
      const k = 1 - THREE.MathUtils.smoothstep(rd.d, rd.halfWidth - 0.4, rd.halfWidth + 1.2);
      col.lerp(rd.main ? C_ROAD_MAIN : C_ROAD, k * 0.92);
    }

    colors[i * 3] = col.r;
    colors[i * 3 + 1] = col.g;
    colors[i * 3 + 2] = col.b;
  }

  geo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
  geo.computeVertexNormals();

  const mat = new THREE.MeshStandardMaterial({
    vertexColors: true,
    flatShading: true,
    roughness: 0.96,
    metalness: 0.0,
  });

  const mesh = new THREE.Mesh(geo, mat);
  mesh.receiveShadow = true;
  mesh.name = 'terrain';
  scene.add(mesh);

  return mesh;
}

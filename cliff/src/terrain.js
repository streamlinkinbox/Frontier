/*
 * The base mesh: a heightfield plateau with a raised rim of hills around the
 * edge of the world.  The quarry is carved out of it by simply dropping every
 * triangle whose centre falls inside the pit outline (inflated a little so the
 * wall geometry always sits proud of the terrain seam).
 *
 * All relief is analytic; the only "texture" is vertex colour.
 */
import * as THREE from '../vendor/three.module.js';
import { clamp, lerp, smoothstep } from './util.js';
import { fbm2 } from './noise.js';
import { hex } from './palette.js';

/** Analytic ground height. */
export function makeGroundField(params, noise2) {
  const half = params.size / 2;
  const bowlR = params.pitRadius * 1.55;

  return {
    height(x, z) {
      const d = Math.max(Math.abs(x), Math.abs(z)) / half; // 0..1 to the rim
      const dc = Math.hypot(x, z) / bowlR;

      const hills = fbm2(noise2, x * 0.0125, z * 0.0125, 4) * 3.2;
      const hills2 = fbm2(noise2, x * 0.042 + 31.7, z * 0.042 - 11.3, 3) * 0.9;
      // shallow bowl under the quarry so benches read as level cuts
      const bowl = -3.6 * Math.exp(-dc * dc * 1.1) +
        0.75 * fbm2(noise2, x * 0.022 + 5, z * 0.022 - 9, 3);

      let h = lerp(bowl, 1.0 + hills + hills2, smoothstep(1.02, 1.62, dc));

      // rim of hills hides the edge of the world
      const rim = smoothstep(0.82, 1.0, d);
      h += rim * rim * (6 + 9 * fbm2(noise2, x * 0.009 - 7, z * 0.009 + 3, 3));

      // fine ground detail - gated off right at the quarry so seams stay clean
      const fine = 0.22 * noise2.noise2(x * 0.26, z * 0.26) + 0.10 * noise2.noise2(x * 0.9, z * 0.9);
      h += fine * smoothstep(0.42, 0.95, dc);
      return h;
    },
  };
}

/**
 * Ground colour.  `aux` may carry slope/AO/distance pre-computed from the
 * heightfield grid; without it they are derived analytically (used when the
 * quarry needs to blend its haul road into the surrounding ground).
 */
export function makeGroundColor(params, preset, noise2, field, pit) {
  const grass = hex(preset.ground.grass);
  const dirt = hex(preset.ground.dirt);
  const rock = hex(preset.ground.rock);
  const dust = hex(preset.ground.dust);
  const e = params.terrainRes;

  return function groundColor(x, z, out, aux) {
    let slope, ao, dist;
    if (aux && aux.slope !== undefined) {
      slope = aux.slope; ao = aux.ao; dist = aux.dist;
    } else {
      const h = field.height(x, z);
      const hx = field.height(x + e, z) - field.height(x - e, z);
      const hz = field.height(x, z + e) - field.height(x, z - e);
      slope = Math.hypot(hx, hz) / (2 * e);
      let s = 0;
      s += field.height(x - 2 * e, z); s += field.height(x + 2 * e, z);
      s += field.height(x, z - 2 * e); s += field.height(x, z + 2 * e);
      ao = clamp(0.5 + (h - s / 4) / 2.5, 0.25, 1);
      dist = pit.signedRadial(x, z);
    }

    const patch = fbm2(noise2, x * 0.026, z * 0.026, 3) * 0.5 + 0.5;
    const speck = noise2.noise2(x * 0.2, z * 0.2) * 0.5 + 0.5;
    out.copy(grass).lerp(dirt, clamp(patch * 0.85 + speck * 0.25 - 0.08, 0, 1));

    const rocky = clamp(
      smoothstep(1.0, 2.3, slope) * 0.85 + smoothstep(2.4, 0.35, dist) * 0.5, 0, 1);
    out.lerp(rock, rocky);
    out.lerp(dust, smoothstep(3.2, 0.25, dist) * 0.35);

    out.multiplyScalar(0.60 + 0.40 * ao);
    out.multiplyScalar(0.90 + 0.20 * (fbm2(noise2, x * 0.006, z * 0.006, 2) * 0.5 + 0.5));
    return out;
  };
}

/** Builds the plateau mesh (with a downward skirt so the world edge is hidden). */
export function buildTerrainMesh(params, field, pit, noise2, groundColor) {
  const res = params.terrainRes;
  const size = params.size;
  const half = size / 2;
  const N = Math.max(8, Math.floor(size / res));
  const stride = N + 1;
  const count = stride * stride;

  const H = new Float32Array(count);
  const positions = new Array(count * 3);
  const colors = new Array(count * 3);
  const c = new THREE.Color();

  for (let j = 0; j <= N; j++) {
    for (let i = 0; i <= N; i++) {
      const k = j * stride + i;
      const x = -half + i * res;
      const z = -half + j * res;
      const h = field.height(x, z);
      H[k] = h;
      positions[k * 3] = x;
      positions[k * 3 + 1] = h;
      positions[k * 3 + 2] = z;
    }
  }

  const hAt = (i, j) => H[clamp(j, 0, N) * stride + clamp(i, 0, N)];
  for (let j = 0; j <= N; j++) {
    for (let i = 0; i <= N; i++) {
      const k = j * stride + i;
      const x = positions[k * 3], z = positions[k * 3 + 2];
      const slope = Math.hypot(hAt(i + 1, j) - hAt(i - 1, j), hAt(i, j + 1) - hAt(i, j - 1)) / (2 * res);
      const nb = (hAt(i - 2, j) + hAt(i + 2, j) + hAt(i, j - 2) + hAt(i, j + 2)) / 4;
      const ao = clamp(0.5 + (H[k] - nb) / 2.5, 0.25, 1);
      const dist = pit.signedRadial(x, z);
      groundColor(x, z, c, { slope, ao, dist });
      colors[k * 3] = c.r; colors[k * 3 + 1] = c.g; colors[k * 3 + 2] = c.b;
    }
  }

  const indices = [];
  const eps = 0.7; // keep terrain outside the wall geometry
  for (let j = 0; j < N; j++) {
    for (let i = 0; i < N; i++) {
      const a = j * stride + i;
      const b = a + 1;
      const cc = a + stride;
      const d = cc + 1;
      const cx = -half + (i + 0.5) * res;
      const cz = -half + (j + 0.5) * res;
      if (pit.inside(cx, cz, eps)) continue;
      indices.push(a, cc, b, b, cc, d);
    }
  }

  // ---- skirt: drop the border of the world into the fog ------------------
  const skirtDepth = 70;
  const pushVert = (x, y, z, cr, cg, cb) => {
    const id = positions.length / 3;
    positions.push(x, y, z);
    colors.push(cr, cg, cb);
    return id;
  };
  const skirtEdge = (i0, j0, i1, j1) => {
    for (let s = 0; s < N; s++) {
      const t = s / N, t2 = (s + 1) / N;
      const ia = Math.round(lerp(i0, i1, t)), ja = Math.round(lerp(j0, j1, t));
      const ib = Math.round(lerp(i0, i1, t2)), jb = Math.round(lerp(j0, j1, t2));
      const ka = ja * stride + ia, kb = jb * stride + ib;
      const xa = positions[ka * 3], ya = positions[ka * 3 + 1], za = positions[ka * 3 + 2];
      const xb = positions[kb * 3], yb = positions[kb * 3 + 1], zb = positions[kb * 3 + 2];
      // inherit the ground colour so the drop-off reads as the same rock
      const va = pushVert(xa, ya - skirtDepth, za,
        colors[ka * 3] * 0.8, colors[ka * 3 + 1] * 0.8, colors[ka * 3 + 2] * 0.8);
      const vb = pushVert(xb, yb - skirtDepth, zb,
        colors[kb * 3] * 0.8, colors[kb * 3 + 1] * 0.8, colors[kb * 3 + 2] * 0.8);
      indices.push(ka, va, kb, kb, va, vb);
    }
  };
  skirtEdge(0, 0, N, 0);
  skirtEdge(N, 0, N, N);
  skirtEdge(N, N, 0, N);
  skirtEdge(0, N, 0, 0);

  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geo.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));
  geo.setIndex(indices);
  geo.computeVertexNormals();
  geo.computeBoundingSphere();
  return compact(geo);
}

/**
 * Drops vertices that ended up unreferenced (the pit hole leaves a ring of
 * them behind).  Without this they would be uploaded with zero normals.
 */
function compact(geo) {
  const pos = geo.attributes.position;
  const index = geo.index.array;
  const n = pos.count;
  const used = new Uint8Array(n);
  for (let i = 0; i < index.length; i++) used[index[i]] = 1;
  let kept = 0;
  for (let i = 0; i < n; i++) if (used[i]) kept++;
  if (kept === n) return geo;

  const remap = new Int32Array(n).fill(-1);
  const P = new Float32Array(kept * 3);
  const C = new Float32Array(kept * 3);
  let w = 0;
  for (let i = 0; i < n; i++) {
    if (!used[i]) continue;
    remap[i] = w;
    P[w * 3] = pos.getX(i); P[w * 3 + 1] = pos.getY(i); P[w * 3 + 2] = pos.getZ(i);
    const col = geo.attributes.color;
    C[w * 3] = col.getX(i); C[w * 3 + 1] = col.getY(i); C[w * 3 + 2] = col.getZ(i);
    w++;
  }
  const I = new Uint32Array(index.length);
  for (let i = 0; i < index.length; i++) I[i] = remap[index[i]];

  const out = new THREE.BufferGeometry();
  out.setAttribute('position', new THREE.BufferAttribute(P, 3));
  out.setAttribute('color', new THREE.BufferAttribute(C, 3));
  out.setIndex(new THREE.BufferAttribute(I, 1));
  out.computeVertexNormals();
  out.computeBoundingSphere();
  return out;
}

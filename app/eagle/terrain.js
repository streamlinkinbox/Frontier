// Canyon terrain: an analytic height field (so the vehicle and the eagle can
// query it cheaply anywhere) + a mesh built from it with slope-based
// triplanar rock/ground shading, a road ribbon and a guard-rail.
import * as THREE from '../vendor/three.module.js';
import { Simplex2, clamp, lerp, smoothstep } from './noise.js';
import { makeRock, makeGround, makeAsphalt } from './textures.js';

const N = new Simplex2(101);
const N2 = new Simplex2(202);

export const WORLD = {
  xMin: -760, xMax: 760,
  zMin: -300, zMax: 190,
  roadHalf: 6.0,
};

/** Road centre line. The road runs along +X; the cliff is on the +Z side. */
export function roadZ(x) { return 16 * Math.sin(x * 0.0105) + 7 * Math.sin(x * 0.0262 + 1.3); }
export function roadY(x) { return 3 + 3 * Math.sin(x * 0.0071) + 1.4 * Math.sin(x * 0.0193 + 0.7); }
export function roadDir(x, out = new THREE.Vector3()) {
  const e = 0.5;
  out.set(2 * e, roadY(x + e) - roadY(x - e), roadZ(x + e) - roadZ(x - e)).normalize();
  return out;
}

const cliffStart = (x) => 10 + 3 * N.noise(x * 0.05, 1.7);
const cliffEnd = (x) => cliffStart(x) + 24 + 9 * N.noise(x * 0.028 + 7, 3.1);
const cliffH = (x) => 82 + 18 * N.noise(x * 0.008, 9.2);

// Perch: a rock ledge jutting from the cliff face near x = 0, ~40 m above the road
export const PERCH_X = 0;
const PERCH_D = () => cliffStart(PERCH_X) + 9;
function perchLedge(x, d, h, ry) {
  const dx = (x - PERCH_X) / 10, dd = (d - PERCH_D()) / 6.5;
  const r = Math.sqrt(dx * dx + dd * dd);
  if (r > 1.6) return h;
  const w = 1 - smoothstep(0.55, 1.5, r);
  const ledgeY = ry + 40 + (1 - clamp(r, 0, 1)) * 2.5 - dd * 1.5;
  return lerp(h, Math.max(h * 0.2 + ledgeY * 0.8, ledgeY), w);
}

/** Terrain height at world (x, z). */
export function terrainHeight(x, z) {
  const rz = roadZ(x), ry = roadY(x);
  const d = z - rz; // lateral distance from road centre (+ toward the cliff)
  // rolling base
  let h = ry + 1.5 * N.fbm(x * 0.01, z * 0.01, 3) - 0.8;
  // valley side (negative d): slope down to a river bed, then far hills
  if (d < 0) {
    const s = -d;
    const drop = smoothstep(9, 130, s);
    h -= drop * drop * 48;
    h += (smoothstep(120, 260, s)) * 60 + 12 * N.fbm(x * 0.004 + 9, z * 0.004, 4) * smoothstep(60, 200, s);
    h += N.fbm(x * 0.03, z * 0.03, 4) * 2.2 * smoothstep(8, 30, s);
  } else {
    const cs = cliffStart(x), ce = cliffEnd(x), H = cliffH(x);
    let t = clamp((d - cs) / (ce - cs), 0, 1);
    // cliff profile: gentle scree at the bottom, near-vertical face, rounded rim
    const prof = t < 0.18 ? 0.35 * Math.pow(t / 0.18, 1.6) : 0.35 + 0.65 * Math.pow((t - 0.18) / 0.82, 0.55);
    const face = smoothstep(0.02, 0.98, t);
    h += H * prof;
    // rock detail: ridges (strata & buttresses) strongest on the face
    h += (N.ridged(x * 0.035, z * 0.07, 4) - 0.5) * 9 * face * (1 - t * 0.6);
    h += N.fbm(x * 0.09, z * 0.09, 3) * 2.4 * face;
    // plateau beyond the rim
    const top = smoothstep(ce - 2, ce + 30, d);
    h += top * (6 * N.fbm(x * 0.006 + 3, z * 0.006, 4) + 4 * N.fbm(x * 0.02, z * 0.02, 3));
    // talus / scree: small bumps at the foot of the cliff
    h += smoothstep(cs - 6, cs + 4, d) * (1 - smoothstep(cs + 4, cs + 14, d)) * (1.2 + N.noise(x * 0.4, z * 0.4) * 0.8);
    // perch ledge
    h = perchLedge(x, d, h, ry);
  }
  // road bed: flatten to the road profile near the centre line (with a crown & shoulders)
  const ad = Math.abs(d);
  const flat = 1 - smoothstep(WORLD.roadHalf + 1.5, WORLD.roadHalf + 8, ad);
  const roadSurface = ry - 0.02 * ad + (ad > WORLD.roadHalf ? (ad - WORLD.roadHalf) * 0.06 : 0);
  h = lerp(h, roadSurface, flat);
  return h;
}

export function terrainNormal(x, z, out = new THREE.Vector3(), e = 0.6) {
  const hl = terrainHeight(x - e, z), hr = terrainHeight(x + e, z);
  const hd = terrainHeight(x, z - e), hu = terrainHeight(x, z + e);
  out.set(hl - hr, 2 * e, hd - hu).normalize();
  return out;
}

/** March a ray against the height field. Returns hit distance or -1. */
export function raycastTerrain(origin, dir, maxLen, steps = 8) {
  let tPrev = 0, hPrev = origin.y - terrainHeight(origin.x, origin.z);
  if (hPrev <= 0) return 0;
  for (let i = 1; i <= steps; i++) {
    const t = (maxLen * i) / steps;
    const px = origin.x + dir.x * t, py = origin.y + dir.y * t, pz = origin.z + dir.z * t;
    const hv = py - terrainHeight(px, pz);
    if (hv <= 0) {
      // bisect
      let a = tPrev, b = t;
      for (let k = 0; k < 6; k++) {
        const m = (a + b) * 0.5;
        const mh = origin.y + dir.y * m - terrainHeight(origin.x + dir.x * m, origin.z + dir.z * m);
        if (mh > 0) a = m; else b = m;
      }
      return (a + b) * 0.5;
    }
    tPrev = t; hPrev = hv;
  }
  return -1;
}

/** Where the eagle sits. */
export function perchPoint() {
  const z = roadZ(PERCH_X) + PERCH_D();
  let best = null;
  for (let dx = -3; dx <= 3; dx += 0.5) for (let dz = -2; dz <= 2; dz += 0.5) {
    const h = terrainHeight(PERCH_X + dx, z + dz);
    if (!best || h > best.y) best = new THREE.Vector3(PERCH_X + dx, h, z + dz);
  }
  return best;
}

/** Approximate the cliff face location near x: the z where the terrain has climbed 25 m above the road. */
export function cliffFaceZ(x) {
  const rz = roadZ(x), ry = roadY(x);
  for (let d = 8; d < 60; d += 0.5) { if (terrainHeight(x, rz + d) > ry + 22) return rz + d; }
  return rz + 30;
}

/* ------------------------------------------------------------------------- */

function injectTriplanar(material, { rock, ground, tile = 0.08 }) {
  material.onBeforeCompile = (shader) => {
    shader.uniforms.tRock = { value: rock.map };
    shader.uniforms.tRockN = { value: rock.normalMap };
    shader.uniforms.tGround = { value: ground.map };
    shader.uniforms.tGroundN = { value: ground.normalMap };
    shader.uniforms.uTile = { value: tile };
    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', '#include <common>\n varying vec3 vWPos; varying vec3 vWNrm;')
      .replace('#include <worldpos_vertex>', '#include <worldpos_vertex>\n vWPos = (modelMatrix * vec4(transformed,1.0)).xyz; vWNrm = normalize(mat3(modelMatrix) * objectNormal);');
    shader.fragmentShader = shader.fragmentShader
      .replace('#include <common>', `#include <common>
        varying vec3 vWPos; varying vec3 vWNrm;
        uniform sampler2D tRock, tRockN, tGround, tGroundN; uniform float uTile;
        vec4 triplanar(sampler2D t, vec3 p, vec3 n, float s){
          vec3 w = pow(abs(n), vec3(4.0)); w /= (w.x + w.y + w.z);
          vec4 x = texture2D(t, p.zy * s), y = texture2D(t, p.xz * s), z = texture2D(t, p.xy * s);
          return x * w.x + y * w.y + z * w.z;
        }
        vec3 triNormal(sampler2D t, vec3 p, vec3 n, float s){
          vec3 w = pow(abs(n), vec3(4.0)); w /= (w.x + w.y + w.z);
          vec3 tx = texture2D(t, p.zy * s).xyz * 2.0 - 1.0;
          vec3 ty = texture2D(t, p.xz * s).xyz * 2.0 - 1.0;
          vec3 tz = texture2D(t, p.xy * s).xyz * 2.0 - 1.0;
          // whiteout blend
          tx = vec3(tx.xy + n.zy, abs(tx.z) * n.x);
          ty = vec3(ty.xy + n.xz, abs(ty.z) * n.y);
          tz = vec3(tz.xy + n.xy, abs(tz.z) * n.z);
          return normalize(tx.zyx * w.x + ty.xzy * w.y + tz.xyz * w.z);
        }`)
      .replace('#include <map_fragment>', `
        vec3 wn = normalize(vWNrm);
        float slope = 1.0 - wn.y;
        float rockW = smoothstep(0.22, 0.45, slope) ;
        // extra rock on high ground / cliff band, grass in the valley
        rockW = max(rockW, smoothstep(30.0, 60.0, vWPos.y) * 0.6);
        vec4 cR = triplanar(tRock, vWPos, wn, uTile);
        vec4 cR2 = triplanar(tRock, vWPos * 0.23 + 17.0, wn, uTile);
        cR.rgb = mix(cR.rgb, cR.rgb * (cR2.rgb * 1.8), 0.45);
        vec4 cG = triplanar(tGround, vWPos, wn, uTile * 1.6);
        vec4 cG2 = triplanar(tGround, vWPos * 0.17 + 3.0, wn, uTile * 1.6);
        cG.rgb = mix(cG.rgb, cG.rgb * (cG2.rgb * 1.9), 0.4);
        vec4 texelColor = mix(cG, cR, rockW);
        diffuseColor *= texelColor;`)
      .replace('#include <normal_fragment_maps>', `
        vec3 tn = mix(triNormal(tGroundN, vWPos, wn, uTile * 1.6), triNormal(tRockN, vWPos, wn, uTile), rockW);
        normal = normalize(mix(normal, (viewMatrix * vec4(tn, 0.0)).xyz, 0.9));`);
  };
  material.customProgramCacheKey = () => 'triplanar-terrain';
}

export function buildTerrain() {
  const group = new THREE.Group();
  const rock = makeRock({ seed: 11 });
  const ground = makeGround({ seed: 21, size: 512 });

  const cell = 1.6;
  const nx = Math.round((WORLD.xMax - WORLD.xMin) / cell), nz = Math.round((WORLD.zMax - WORLD.zMin) / cell);
  const geo = new THREE.PlaneGeometry(WORLD.xMax - WORLD.xMin, WORLD.zMax - WORLD.zMin, nx, nz);
  geo.rotateX(-Math.PI / 2);
  const pos = geo.attributes.position;
  const colors = new Float32Array(pos.count * 3);
  const c = new THREE.Color();
  for (let i = 0; i < pos.count; i++) {
    const x = pos.getX(i) + (WORLD.xMin + WORLD.xMax) / 2, z = pos.getZ(i) + (WORLD.zMin + WORLD.zMax) / 2;
    pos.setXYZ(i, x, terrainHeight(x, z), z);
    // vertex tint: strata darkening, valley greening, road-side dust
    const d = z - roadZ(x);
    const strata = 0.92 + 0.08 * Math.sin(terrainHeight(x, z) * 0.35 + N2.noise(x * 0.02, z * 0.02) * 2);
    const dust = 1 - 0.18 * (1 - smoothstep(6, 16, Math.abs(d)));
    const ao = 1 - 0.25 * smoothstep(0.5, 0.0, terrainNormal(x, z).y) * (1 - smoothstep(20, 60, terrainHeight(x, z) - roadY(x)));
    const tint = 0.9 + 0.2 * N2.fbm(x * 0.003, z * 0.003, 3);
    c.setRGB(strata * tint * ao * dust, strata * tint * ao * dust * (d < -10 ? 1.03 : 1), strata * tint * ao * dust * 0.98);
    colors[i * 3] = c.r; colors[i * 3 + 1] = c.g; colors[i * 3 + 2] = c.b;
  }
  geo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
  geo.computeVertexNormals();
  geo.computeBoundingSphere();

  const mat = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.94, metalness: 0.0, color: 0xffffff });
  injectTriplanar(mat, { rock, ground, tile: 0.075 });
  const mesh = new THREE.Mesh(geo, mat);
  mesh.receiveShadow = true; mesh.castShadow = true;
  mesh.name = 'terrain';
  group.add(mesh);

  // ---- Road ribbon --------------------------------------------------------
  const asphalt = makeAsphalt();
  const segs = Math.round((WORLD.xMax - WORLD.xMin) / 2);
  const rw = WORLD.roadHalf;
  const rp = new Float32Array((segs + 1) * 2 * 3), ruv = new Float32Array((segs + 1) * 2 * 2), rn = new Float32Array((segs + 1) * 2 * 3);
  const idx = [];
  const dir = new THREE.Vector3(), side = new THREE.Vector3(), up = new THREE.Vector3(0, 1, 0);
  for (let i = 0; i <= segs; i++) {
    const x = WORLD.xMin + (i / segs) * (WORLD.xMax - WORLD.xMin);
    roadDir(x, dir); side.crossVectors(up, dir).normalize(); // side points to -z? cross(up, +x) = -z... we want +z: flip
    side.negate();
    const cz = roadZ(x), cy = roadY(x) + 0.06;
    for (let s = 0; s < 2; s++) {
      const k = (i * 2 + s);
      const off = (s === 0 ? -rw : rw);
      rp[k * 3] = x + side.x * off; rp[k * 3 + 1] = cy - 0.02 * Math.abs(off) + 0.03; rp[k * 3 + 2] = cz + side.z * off;
      ruv[k * 2] = s; ruv[k * 2 + 1] = x / 24;
      rn[k * 3] = 0; rn[k * 3 + 1] = 1; rn[k * 3 + 2] = 0;
    }
    if (i < segs) { const a = i * 2; idx.push(a, a + 2, a + 1, a + 1, a + 2, a + 3); }
  }
  const rg = new THREE.BufferGeometry();
  rg.setAttribute('position', new THREE.BufferAttribute(rp, 3));
  rg.setAttribute('uv', new THREE.BufferAttribute(ruv, 2));
  rg.setAttribute('normal', new THREE.BufferAttribute(rn, 3));
  rg.setIndex(idx);
  const road = new THREE.Mesh(rg, new THREE.MeshStandardMaterial({ map: asphalt.map, normalMap: asphalt.normalMap, normalScale: new THREE.Vector2(0.6, 0.6), roughness: 0.85, metalness: 0.0, polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -2 }));
  road.receiveShadow = true;
  road.name = 'road';
  group.add(road);

  // ---- Guard rail on the valley side ---------------------------------------
  const postGeo = new THREE.BoxGeometry(0.16, 0.9, 0.12);
  const railGeo = new THREE.BoxGeometry(4.02, 0.32, 0.08);
  const postMat = new THREE.MeshStandardMaterial({ color: 0x777d80, roughness: 0.5, metalness: 0.8 });
  const railMat = new THREE.MeshStandardMaterial({ color: 0xaeb4b8, roughness: 0.35, metalness: 0.9 });
  const count = Math.floor((WORLD.xMax - WORLD.xMin) / 4);
  const posts = new THREE.InstancedMesh(postGeo, postMat, count);
  const rails = new THREE.InstancedMesh(railGeo, railMat, count);
  const m = new THREE.Matrix4(), q = new THREE.Quaternion(), p = new THREE.Vector3(), sc = new THREE.Vector3(1, 1, 1);
  const yaw = new THREE.Vector3();
  for (let i = 0; i < count; i++) {
    const x = WORLD.xMin + 2 + i * 4;
    roadDir(x, dir);
    const angle = Math.atan2(-dir.z, dir.x);
    q.setFromAxisAngle(up, angle);
    const off = -(rw + 0.9);
    p.set(x, roadY(x) + 0.5, roadZ(x) + off);
    m.compose(p, q, sc); posts.setMatrixAt(i, m);
    p.y += 0.25; p.x += Math.cos(angle) * 2; p.z -= Math.sin(angle) * 2;
    m.compose(p, q, sc); rails.setMatrixAt(i, m);
  }
  posts.castShadow = rails.castShadow = true;
  group.add(posts, rails);

  // ---- Scattered boulders & scrub -----------------------------------------
  const rockGeo = new THREE.DodecahedronGeometry(1, 1);
  const rpos = rockGeo.attributes.position;
  for (let i = 0; i < rpos.count; i++) { const v = new THREE.Vector3().fromBufferAttribute(rpos, i); v.multiplyScalar(0.8 + 0.4 * N2.noise(v.x * 2, v.y * 2)); rpos.setXYZ(i, v.x, v.y * 0.7, v.z); }
  rockGeo.computeVertexNormals();
  const rockMat = new THREE.MeshStandardMaterial({ map: rock.map, normalMap: rock.normalMap, roughness: 0.95, color: 0xc9b39a });
  rockMat.map = rock.map.clone(); rockMat.map.repeat.set(0.35, 0.35); rockMat.map.needsUpdate = true;
  const nRocks = 420;
  const rocks = new THREE.InstancedMesh(rockGeo, rockMat, nRocks);
  const rnd = (s => () => (s = (s * 16807) % 2147483647) / 2147483647)(99);
  for (let i = 0; i < nRocks; i++) {
    const x = WORLD.xMin + rnd() * (WORLD.xMax - WORLD.xMin);
    let d;
    const r = rnd();
    if (r < 0.45) d = 8 + rnd() * 9;         // cliff foot scree
    else if (r < 0.75) d = -(9 + rnd() * 90); // valley slope
    else d = 40 + rnd() * 90;                  // plateau
    const z = roadZ(x) + d;
    const s = 0.4 + rnd() * rnd() * 3.2;
    p.set(x, terrainHeight(x, z) + s * 0.25, z);
    q.setFromEuler(new THREE.Euler(rnd() * 0.6, rnd() * 6.28, rnd() * 0.6));
    m.compose(p, q, new THREE.Vector3(s * (0.8 + rnd() * 0.5), s * (0.6 + rnd() * 0.5), s * (0.8 + rnd() * 0.5)));
    rocks.setMatrixAt(i, m);
  }
  rocks.castShadow = rocks.receiveShadow = true;
  group.add(rocks);

  // dry scrub bushes (cheap crossed planes)
  const bushGeo = new THREE.BufferGeometry();
  {
    const quads = [];
    for (let k = 0; k < 3; k++) {
      const a = (k / 3) * Math.PI; const ca = Math.cos(a), sa = Math.sin(a);
      quads.push(-ca, 0, -sa, ca, 0, sa, ca, 1, sa, -ca, 0, -sa, ca, 1, sa, -ca, 1, -sa);
    }
    bushGeo.setAttribute('position', new THREE.Float32BufferAttribute(quads, 3));
    const uvs = []; for (let k = 0; k < 3; k++) uvs.push(0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1);
    bushGeo.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
    bushGeo.computeVertexNormals();
  }
  const bushTex = (() => {
    const c = document.createElement('canvas'); c.width = c.height = 128; const ctx = c.getContext('2d');
    ctx.clearRect(0, 0, 128, 128);
    for (let i = 0; i < 260; i++) {
      const x = 64 + (rnd() - 0.5) * 100, y = 128 - rnd() * rnd() * 120, l = 6 + rnd() * 14;
      ctx.strokeStyle = `rgba(${90 + rnd() * 50 | 0},${80 + rnd() * 40 | 0},${40 + rnd() * 30 | 0},${0.6 + rnd() * 0.4})`;
      ctx.lineWidth = 1 + rnd() * 1.5; ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x + (rnd() - 0.5) * 10, y - l); ctx.stroke();
    }
    const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; return t;
  })();
  const bushMat = new THREE.MeshStandardMaterial({ map: bushTex, alphaTest: 0.4, side: THREE.DoubleSide, roughness: 1 });
  const nBush = 700;
  const bushes = new THREE.InstancedMesh(bushGeo, bushMat, nBush);
  for (let i = 0; i < nBush; i++) {
    const x = WORLD.xMin + rnd() * (WORLD.xMax - WORLD.xMin);
    const d = rnd() < 0.7 ? -(8.5 + rnd() * 110) : 45 + rnd() * 100;
    const z = roadZ(x) + d;
    const s = 0.8 + rnd() * 1.6;
    p.set(x, terrainHeight(x, z) - 0.05, z);
    q.setFromAxisAngle(up, rnd() * 6.28);
    m.compose(p, q, new THREE.Vector3(s, s * (0.7 + rnd() * 0.5), s));
    bushes.setMatrixAt(i, m);
  }
  bushes.castShadow = true;
  group.add(bushes);

  return { group, mesh, road };
}

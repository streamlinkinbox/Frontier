// Giant bald eagle: articulated skeleton, ~330 individually posed & bent
// feathers (merged into a few draw calls), procedural plumage / skin / eye /
// beak, and a biomechanical pose function driven by a small parameter set.
//
// Conventions: eagle forward = +Z, up = +Y, right wing = +X (side = +1),
// left wing = -X (side = -1). All lengths are in metres (giant scale).
import * as THREE from '../vendor/three.module.js';
import { makeFeather, makePlumage, makeScales, makeEye } from './textures.js';
import { clamp, lerp, smoothstep } from './noise.js';

const DEG = Math.PI / 180;
const _q1 = new THREE.Quaternion(), _q2 = new THREE.Quaternion(), _q3 = new THREE.Quaternion(), _q4 = new THREE.Quaternion();
const _v1 = new THREE.Vector3(), _v2 = new THREE.Vector3(), _v3 = new THREE.Vector3();
const X = new THREE.Vector3(1, 0, 0), Y = new THREE.Vector3(0, 1, 0), Z = new THREE.Vector3(0, 0, 1);
const Q_BASE = new THREE.Quaternion().setFromAxisAngle(X, -Math.PI / 2); // feather +Y(shaft) -> -Z, +Z(normal) -> +Y

// Skeleton dimensions
export const DIM = {
  shoulder: new THREE.Vector3(0.7, 0.42, 0.62),
  humerus: 2.15, ulna: 2.85, manus: 1.55,
  hip: new THREE.Vector3(0.44, -0.42, -0.35),
  femur: 1.05, tibia: 1.65, tarsus: 1.35,
  tailBase: new THREE.Vector3(0, 0.0, -1.95),
  neckBase: new THREE.Vector3(0, 0.55, 1.6),
  neckSeg: 0.5,
  headR: 0.6,
};

/* ============================================================================
 * FEATHER SYSTEM — merged, CPU-deformed feather quads.
 * ========================================================================== */
class FeatherSystem {
  constructor() { this.groups = new Map(); this.feathers = []; }
  material(key, tex, opts = {}) {
    if (this.groups.has(key)) return;
    const mat = new THREE.MeshStandardMaterial({
      map: tex.map, normalMap: tex.normalMap, normalScale: new THREE.Vector2(0.9, 0.9),
      alphaTest: 0.42, side: THREE.DoubleSide, roughness: opts.roughness ?? 0.72, metalness: 0,
      color: opts.color ?? 0xffffff, transparent: false, shadowSide: THREE.DoubleSide,
    });
    this.groups.set(key, { mat, feathers: [], mesh: null, pos: null, nrm: null });
  }
  add(key, f) {
    const g = this.groups.get(key);
    const rec = Object.assign({ key, sx: 3, sy: 10, bend: 0, twist: 0, camber: 0.06, mirror: false, w: 0.5, L: 2, baseShift: 0 }, f);
    rec.index = g.feathers.length; g.feathers.push(rec); this.feathers.push(rec);
    return rec;
  }
  finalize(scene) {
    for (const [key, g] of this.groups) {
      let vCount = 0, iCount = 0;
      for (const f of g.feathers) { f.vOffset = vCount; vCount += (f.sx + 1) * (f.sy + 1); iCount += f.sx * f.sy * 6; }
      const pos = new Float32Array(vCount * 3), nrm = new Float32Array(vCount * 3), uv = new Float32Array(vCount * 2);
      const idx = new (vCount > 65535 ? Uint32Array : Uint16Array)(iCount);
      let ii = 0;
      for (const f of g.feathers) {
        const cols = f.sx + 1;
        for (let j = 0; j <= f.sy; j++) for (let i = 0; i <= f.sx; i++) {
          const k = f.vOffset + j * cols + i;
          const u = i / f.sx;
          uv[k * 2] = f.mirror ? 1 - u : u; uv[k * 2 + 1] = j / f.sy;
        }
        for (let j = 0; j < f.sy; j++) for (let i = 0; i < f.sx; i++) {
          const a = f.vOffset + j * cols + i, b = a + 1, c = a + cols, d = c + 1;
          idx[ii++] = a; idx[ii++] = c; idx[ii++] = b; idx[ii++] = b; idx[ii++] = c; idx[ii++] = d;
        }
      }
      const geo = new THREE.BufferGeometry();
      geo.setAttribute('position', new THREE.BufferAttribute(pos, 3).setUsage(THREE.DynamicDrawUsage));
      geo.setAttribute('normal', new THREE.BufferAttribute(nrm, 3).setUsage(THREE.DynamicDrawUsage));
      geo.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
      geo.setIndex(new THREE.BufferAttribute(idx, 1));
      geo.boundingSphere = new THREE.Sphere(new THREE.Vector3(), 20);
      const mesh = new THREE.Mesh(geo, g.mat);
      mesh.castShadow = true; mesh.receiveShadow = true; mesh.frustumCulled = true;
      mesh.name = 'feathers-' + key;
      g.mesh = mesh; g.pos = pos; g.nrm = nrm;
      scene.add(mesh);
    }
  }
  update(center) {
    const m = new THREE.Matrix4(), e = new THREE.Matrix3();
    const p = new THREE.Vector3(), n = new THREE.Vector3(), tx = new THREE.Vector3(), ty = new THREE.Vector3();
    for (const [, g] of this.groups) {
      const P = g.pos, Nn = g.nrm;
      for (const f of g.feathers) {
        const cols = f.sx + 1;
        m.copy(f.pivot.matrixWorld);
        const el = m.elements;
        const bend = f.bend, twist = f.twist, camber = f.camber, L = f.L, w = f.w;
        const lenScale = 1 - 0.28 * bend * bend;
        for (let j = 0; j <= f.sy; j++) {
          const t = j / f.sy;
          const tt = t * t;
          const yv = L * t * (1 - (1 - lenScale) * tt) + f.baseShift;
          const bz = bend * L * tt;
          const a = twist * t, ca = Math.cos(a), sa = Math.sin(a);
          for (let i = 0; i <= f.sx; i++) {
            const xu = (i / f.sx - 0.5);
            const xv = xu * w;
            const cz = -camber * w * (xu * xu * 4) * (0.3 + 0.7 * t);
            const lx = xv * ca, lz = bz + cz + xv * sa, ly = yv;
            const k = (f.vOffset + j * cols + i) * 3;
            P[k] = el[0] * lx + el[4] * ly + el[8] * lz + el[12];
            P[k + 1] = el[1] * lx + el[5] * ly + el[9] * lz + el[13];
            P[k + 2] = el[2] * lx + el[6] * ly + el[10] * lz + el[14];
          }
        }
        // normals by central differences on the deformed grid
        for (let j = 0; j <= f.sy; j++) for (let i = 0; i <= f.sx; i++) {
          const k = f.vOffset + j * cols + i;
          const i0 = Math.max(0, i - 1), i1 = Math.min(f.sx, i + 1), j0 = Math.max(0, j - 1), j1 = Math.min(f.sy, j + 1);
          const a = (f.vOffset + j * cols + i1) * 3, b = (f.vOffset + j * cols + i0) * 3, c = (f.vOffset + j1 * cols + i) * 3, d = (f.vOffset + j0 * cols + i) * 3;
          tx.set(P[a] - P[b], P[a + 1] - P[b + 1], P[a + 2] - P[b + 2]);
          ty.set(P[c] - P[d], P[c + 1] - P[d + 1], P[c + 2] - P[d + 2]);
          n.crossVectors(tx, ty).normalize();
          Nn[k * 3] = n.x; Nn[k * 3 + 1] = n.y; Nn[k * 3 + 2] = n.z;
        }
      }
      g.mesh.geometry.attributes.position.needsUpdate = true;
      g.mesh.geometry.attributes.normal.needsUpdate = true;
      g.mesh.geometry.boundingSphere.center.copy(center);
    }
  }
}

/* ============================================================================
 * GEOMETRY HELPERS
 * ========================================================================== */
/** Tapered capsule along +X (len), radii r0 (base) -> r1 (tip). */
function boneGeo(len, r0, r1, seg = 12, rings = 8, squash = 1) {
  const g = new THREE.CylinderGeometry(r1, r0, len, seg, rings, false);
  g.rotateZ(-Math.PI / 2); // Y -> X
  g.translate(len / 2, 0, 0);
  // round the ends a little by pulling the caps in
  const p = g.attributes.position;
  for (let i = 0; i < p.count; i++) { const x = p.getX(i); const t = x / len; const s = Math.sin(Math.PI * clamp(t, 0, 1)) * 0.15 + 0.92; p.setY(i, p.getY(i) * s * squash); p.setZ(i, p.getZ(i) * s); }
  g.computeVertexNormals();
  return g;
}

/** Sweep a circle of varying radius along a CatmullRom curve. */
function sweepGeo(points, radiusFn, segs = 24, radial = 12, scaleY = 1) {
  const curve = new THREE.CatmullRomCurve3(points);
  const frames = curve.computeFrenetFrames(segs, false);
  const pos = [], nrm = [], uv = [], idx = [];
  for (let i = 0; i <= segs; i++) {
    const t = i / segs;
    const c = curve.getPointAt(t);
    const r = radiusFn(t);
    const N = frames.normals[i], B = frames.binormals[i];
    for (let j = 0; j <= radial; j++) {
      const a = (j / radial) * Math.PI * 2;
      const cx = Math.cos(a), sy = Math.sin(a) * scaleY;
      const nx = N.x * cx + B.x * sy, ny = N.y * cx + B.y * sy, nz = N.z * cx + B.z * sy;
      pos.push(c.x + nx * r, c.y + ny * r, c.z + nz * r);
      nrm.push(nx, ny, nz); uv.push(j / radial, t);
    }
  }
  for (let i = 0; i < segs; i++) for (let j = 0; j < radial; j++) {
    const a = i * (radial + 1) + j, b = a + radial + 1;
    idx.push(a, b, a + 1, b, b + 1, a + 1);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('normal', new THREE.Float32BufferAttribute(nrm, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setIndex(idx);
  g.computeVertexNormals();
  return g;
}

/* ============================================================================
 * EAGLE MODEL
 * ========================================================================== */
export class Eagle {
  constructor(scene) {
    this.scene = scene;
    this.root = new THREE.Group(); this.root.name = 'eagle';
    scene.add(this.root);
    this.time = 0;
    this.buildMaterials();
    this.fs = new FeatherSystem();
    this.registerFeatherMaterials();
    this.buildBody();
    this.wings = { R: this.buildWing(1), L: this.buildWing(-1) };
    this.legs = { R: this.buildLeg(1), L: this.buildLeg(-1) };
    this.buildTail();
    this.fs.finalize(scene);
    // pose parameters (smoothed by the brain, consumed by pose())
    this.params = {
      flapPhase: 0, flapAmp: 0, fold: 1, spread: 0.3, dihedral: 0, sweep: 0, brake: 0, tuck: 0,
      tailFan: 0.2, tailPitch: 0, tailRoll: 0,
      legMode: { perch: 1, tucked: 0, strike: 0, carry: 0 }, toeCurl: 0.5, toeSpread: 0.4, legLoad: 0,
      headTarget: null, headLevelRoll: 0, crouch: 0, blink: 0, shrug: 0,
      bodyRoll: 0, bodyPitch: 0,
    };
    this.footAnchor = new THREE.Vector3(); // world-space point between the feet (used for grabbing)
  }

  /* ---------------- materials ---------------- */
  buildMaterials() {
    const brown = makePlumage({ seed: 3, base: [46, 32, 22], edge: [86, 64, 44], dark: [20, 13, 9] });
    const white = makePlumage({ seed: 5, white: true, scale: 0.8 });
    const scales = makeScales({ seed: 9 });
    this.tex = { brown, white, scales };
    this.mat = {
      body: new THREE.MeshPhysicalMaterial({ map: brown.map, normalMap: brown.normalMap, normalScale: new THREE.Vector2(1.2, 1.2), roughness: 0.78, sheen: 0.5, sheenRoughness: 0.6, sheenColor: new THREE.Color(0x4a3a2a), color: 0xffffff }),
      white: new THREE.MeshPhysicalMaterial({ map: white.map, normalMap: white.normalMap, normalScale: new THREE.Vector2(1.0, 1.0), roughness: 0.82, sheen: 0.4, sheenColor: new THREE.Color(0xffffff), color: 0xf5f2ea }),
      skin: new THREE.MeshStandardMaterial({ map: scales.map, normalMap: scales.normalMap, normalScale: new THREE.Vector2(1.4, 1.4), roughness: 0.55, color: 0xffffff }),
      beak: new THREE.MeshPhysicalMaterial({ color: 0xe6b422, roughness: 0.35, clearcoat: 0.6, clearcoatRoughness: 0.25, map: scales.map, normalMap: scales.normalMap, normalScale: new THREE.Vector2(0.5, 0.5), side: THREE.DoubleSide }),
      talon: new THREE.MeshPhysicalMaterial({ color: 0x151210, roughness: 0.28, clearcoat: 0.9, clearcoatRoughness: 0.15, side: THREE.DoubleSide }),
      eye: new THREE.MeshPhysicalMaterial({ map: makeEye(), roughness: 0.05, clearcoat: 1, clearcoatRoughness: 0.02 }),
      lid: new THREE.MeshStandardMaterial({ color: 0xd8c070, roughness: 0.85 }),
      mouth: new THREE.MeshStandardMaterial({ color: 0x3a1a12, roughness: 0.8 }),
    };
    for (const k of ['body', 'white']) { this.mat[k].map.repeat.set(3, 3); this.mat[k].normalMap.repeat.set(3, 3); }
    this.mat.bodyDS = this.mat.body.clone(); this.mat.bodyDS.side = THREE.DoubleSide;
  }

  registerFeatherMaterials() {
    const fs = this.fs;
    fs.material('primary', makeFeather({ seed: 1, base: [40, 30, 22], mid: [34, 25, 18], tip: [18, 13, 10], shaft: [120, 105, 85], asym: 0.45, tipShape: 'pointed', splits: 0.5 }), { roughness: 0.68 });
    fs.material('secondary', makeFeather({ seed: 2, base: [48, 35, 25], mid: [40, 29, 20], tip: [24, 17, 12], shaft: [130, 112, 90], asym: 0.28, tipShape: 'round', splits: 0.3 }), { roughness: 0.7 });
    fs.material('covert', makeFeather({ seed: 3, base: [52, 38, 27], mid: [46, 33, 23], tip: [34, 24, 17], edge: [92, 70, 48], shaft: [120, 105, 85], asym: 0.15, tipShape: 'round', downy: 0.4, splits: 0.15 }), { roughness: 0.75 });
    fs.material('under', makeFeather({ seed: 4, base: [70, 54, 40], mid: [58, 44, 32], tip: [44, 33, 24], edge: [110, 88, 62], asym: 0.12, tipShape: 'round', downy: 0.45, splits: 0.1 }), { roughness: 0.8 });
    fs.material('tail', makeFeather({ seed: 5, white: true, asym: 0.25, tipShape: 'round', splits: 0.25, shaft: [235, 225, 205], shaftDark: [200, 190, 170] }), { roughness: 0.7 });
    fs.material('ruff', makeFeather({ seed: 6, white: true, asym: 0.1, tipShape: 'pointed', downy: 0.5, splits: 0.2, shaft: [235, 225, 205], shaftDark: [200, 190, 170] }), { roughness: 0.85 });
  }

  /* ---------------- body ---------------- */
  buildBody() {
    const root = this.root;
    // torso: lathe profile along Z (front = +Z)
    const prof = [];
    const pts = [[-2.05, 0.30], [-1.9, 0.5], [-1.5, 0.74], [-1.0, 0.92], [-0.4, 1.02], [0.2, 1.05], [0.8, 1.0], [1.2, 0.9], [1.5, 0.72], [1.7, 0.5], [1.78, 0.2]];
    for (const [z, r] of pts) prof.push(new THREE.Vector2(r, z));
    prof.unshift(new THREE.Vector2(0.01, -2.08)); prof.push(new THREE.Vector2(0.01, 1.8));
    const torsoGeo = new THREE.LatheGeometry(prof, 36);
    torsoGeo.rotateX(Math.PI / 2); // Y -> Z ... check: rotX(90): (0,1,0)->(0,0,1) yes
    torsoGeo.scale(1.0, 0.92, 1);
    const torso = new THREE.Mesh(torsoGeo, this.mat.body);
    torso.castShadow = torso.receiveShadow = true;
    root.add(torso);
    this.torso = torso;

    // white "bib" transition: neck ruff feathers (white) draped over the shoulders
    const ruffPivots = [];
    for (let i = 0; i < 18; i++) {
      const a = (i / 18) * Math.PI * 2;
      const r = 0.95;
      const pv = new THREE.Object3D();
      pv.position.set(Math.sin(a) * r * 0.95, Math.cos(a) * r * 0.85 + 0.15, 1.35);
      // shaft points backward (-Z) tilted slightly away from the body, normal points radially out
      const out = new THREE.Vector3(Math.sin(a), Math.cos(a), 0).normalize();
      const ny = new THREE.Vector3(0, 0, -1).addScaledVector(out, 0.28).normalize();
      const nz = out.clone().addScaledVector(ny, -out.dot(ny)).normalize();
      const nx = new THREE.Vector3().crossVectors(ny, nz);
      pv.quaternion.setFromRotationMatrix(new THREE.Matrix4().makeBasis(nx, ny, nz));
      root.add(pv);
      this.fs.add('ruff', { pivot: pv, w: 0.42, L: 1.25 + 0.15 * Math.cos(a), mirror: i % 2 === 0, sy: 6, camber: 0.15 });
      ruffPivots.push(pv);
    }
    // scapulars (brown contour feathers on the back over the wing roots)
    for (let s = -1; s <= 1; s += 2) for (let i = 0; i < 7; i++) {
      const pv = new THREE.Object3D();
      pv.position.set(s * (0.35 + i * 0.09), 0.85 - i * 0.02, 0.9 - i * 0.32);
      _q1.setFromAxisAngle(Y, -s * (0.15 + i * 0.05));
      _q2.setFromAxisAngle(X, -0.05);
      pv.quaternion.copy(_q1).multiply(_q2).multiply(Q_BASE);
      root.add(pv);
      this.fs.add('covert', { pivot: pv, w: 0.55, L: 1.2 + i * 0.05, mirror: s > 0, sy: 6, camber: 0.2 });
    }
    // rump / undertail coverts
    for (let s = -1; s <= 1; s += 2) for (let i = 0; i < 4; i++) {
      const pv = new THREE.Object3D();
      pv.position.set(s * (0.12 + i * 0.16), -0.35 - i * 0.05, -1.4 - i * 0.12);
      _q1.setFromAxisAngle(Y, -s * (0.1 + i * 0.12)); _q2.setFromAxisAngle(X, 0.15);
      pv.quaternion.copy(_q1).multiply(_q2).multiply(Q_BASE);
      root.add(pv);
      this.fs.add('ruff', { pivot: pv, w: 0.45, L: 1.4, mirror: s > 0, sy: 6, camber: 0.1 });
    }

    // neck: 2 segments
    const nb = new THREE.Object3D(); nb.position.copy(DIM.neckBase); root.add(nb);
    const n1 = new THREE.Object3D(); nb.add(n1);
    const n2 = new THREE.Object3D(); n2.position.set(0, 0, DIM.neckSeg); n1.add(n2);
    const headPivot = new THREE.Object3D(); headPivot.position.set(0, 0, DIM.neckSeg); n2.add(headPivot);
    const neckGeo1 = boneGeo(DIM.neckSeg + 0.25, 0.5, 0.46, 16, 4); neckGeo1.rotateY(-Math.PI / 2); // +X -> +Z
    const neckM1 = new THREE.Mesh(neckGeo1, this.mat.white); neckM1.position.z = -0.1; neckM1.castShadow = true; n1.add(neckM1);
    const neckGeo2 = boneGeo(DIM.neckSeg + 0.2, 0.46, 0.42, 16, 4); neckGeo2.rotateY(-Math.PI / 2);
    const neckM2 = new THREE.Mesh(neckGeo2, this.mat.white); neckM2.position.z = -0.08; neckM2.castShadow = true; n2.add(neckM2);
    this.neck = { base: nb, n1, n2, head: headPivot };

    // head
    const R = DIM.headR;
    const headGeo = new THREE.SphereGeometry(R, 32, 24);
    headGeo.scale(0.92, 1.0, 1.12);
    // flatten the crown & elongate the face a bit (raptor skull)
    const hp = headGeo.attributes.position;
    for (let i = 0; i < hp.count; i++) {
      const x = hp.getX(i), y = hp.getY(i), z = hp.getZ(i);
      const f = smoothstep(0.1, 0.7, z / R);
      hp.setXYZ(i, x * (1 - 0.18 * f), y * (1 - 0.12 * f) - 0.06 * f, z + 0.05 * f);
    }
    headGeo.computeVertexNormals();
    const head = new THREE.Mesh(headGeo, this.mat.white);
    head.position.set(0, 0.12, 0.3);
    head.castShadow = true;
    headPivot.add(head);
    // brow ridges (supraorbital) — the fierce look
    for (const s of [-1, 1]) {
      const brow = new THREE.Mesh(new THREE.BoxGeometry(0.34, 0.09, 0.42), this.mat.white);
      brow.position.set(s * 0.33, 0.3, 0.45); brow.rotation.set(0.15, s * -0.35, s * -0.25);
      head.add(brow);
      // eye + lids
      const eye = new THREE.Mesh(new THREE.SphereGeometry(0.115, 24, 18), this.mat.eye);
      eye.position.set(s * 0.4, 0.15, 0.42);
      eye.quaternion.setFromUnitVectors(X, new THREE.Vector3(s * 1, 0.08, 0.5).normalize()); // pupil (+X of the sphere) looks outward/forward
      head.add(eye);
      const lid = new THREE.Mesh(new THREE.SphereGeometry(0.128, 20, 12, 0, Math.PI * 2, 0, Math.PI * 0.5), this.mat.lid);
      lid.position.copy(eye.position);
      lid.scale.y = 0.15;
      head.add(lid);
      if (s < 0) this.lidL = lid; else this.lidR = lid;
    }
    // beak: upper mandible (hooked) & lower
    const bk = [new THREE.Vector3(0, 0.05, 0.45), new THREE.Vector3(0, 0.1, 0.75), new THREE.Vector3(0, 0.06, 1.0), new THREE.Vector3(0, -0.06, 1.2), new THREE.Vector3(0, -0.3, 1.26), new THREE.Vector3(0, -0.46, 1.2)];
    const upper = new THREE.Mesh(sweepGeo(bk, t => lerp(0.3, 0.02, Math.pow(t, 1.15)) * (t < 0.35 ? 1 : 1 - 0.15 * (t - 0.35)), 30, 14, 0.85), this.mat.beak);
    upper.castShadow = true; head.add(upper);
    const lk = [new THREE.Vector3(0, -0.14, 0.45), new THREE.Vector3(0, -0.14, 0.75), new THREE.Vector3(0, -0.16, 0.98), new THREE.Vector3(0, -0.2, 1.08)];
    const lower = new THREE.Mesh(sweepGeo(lk, t => lerp(0.24, 0.05, Math.pow(t, 1.3)), 16, 12, 0.6), this.mat.beak);
    lower.castShadow = true; head.add(lower);
    // gape line & nostril
    const gape = new THREE.Mesh(new THREE.TorusGeometry(0.235, 0.012, 6, 24, Math.PI * 1.2), this.mat.mouth);
    gape.position.set(0, -0.12, 0.72); gape.rotation.set(0, Math.PI / 2, 0); gape.rotateZ(-Math.PI * 0.6);
    head.add(gape);
    for (const s of [-1, 1]) { const n = new THREE.Mesh(new THREE.SphereGeometry(0.035, 8, 8), this.mat.mouth); n.position.set(s * 0.16, 0.12, 0.72); head.add(n); }
    this.head = head;
  }

  /* ---------------- wing ---------------- */
  buildWing(side) {
    const root = this.root, fs = this.fs;
    const S = side;
    const shoulder = new THREE.Object3D(); shoulder.position.set(S * DIM.shoulder.x, DIM.shoulder.y, DIM.shoulder.z); root.add(shoulder);
    const elbow = new THREE.Object3D(); elbow.position.set(S * DIM.humerus, 0, 0); shoulder.add(elbow);
    const wrist = new THREE.Object3D(); wrist.position.set(S * DIM.ulna, 0, 0); elbow.add(wrist);
    const tip = new THREE.Object3D(); tip.position.set(S * DIM.manus, 0, 0); wrist.add(tip);

    // feathered bone volumes (hide the skeleton under the coverts)
    const mk = (parent, len, r0, r1, squash) => {
      const g = boneGeo(len, r0, r1, 14, 6, squash);
      if (S < 0) g.rotateY(Math.PI); // point along -X without mirroring (keeps winding)
      const m = new THREE.Mesh(g, this.mat.body); m.castShadow = true; parent.add(m); return m;
    };
    mk(shoulder, DIM.humerus + 0.2, 0.42, 0.3, 0.7).position.x = S * -0.1;
    mk(elbow, DIM.ulna + 0.15, 0.3, 0.2, 0.6);
    mk(wrist, DIM.manus + 0.1, 0.2, 0.09, 0.6);
    // propatagium (leading edge web) — thin wedge from shoulder to wrist, follows joints approximately via 2 pieces
    const web = (parent, len, depth) => {
      const g = new THREE.PlaneGeometry(len, depth, 6, 1);
      g.translate(S * len / 2, 0, depth / 2 + 0.05);
      const p = g.attributes.position; for (let i = 0; i < p.count; i++) p.setY(i, 0.04 - 0.16 * (p.getZ(i) / depth));
      g.computeVertexNormals();
      const m = new THREE.Mesh(g, this.mat.bodyDS); m.castShadow = true; parent.add(m); return m;
    };
    web(shoulder, DIM.humerus, 0.7); web(elbow, DIM.ulna * 0.9, 0.55);

    const mirror = S > 0;
    const F = { primaries: [], secondaries: [], tertials: [], gCov: [], mCov: [], lCov: [], under: [], alula: [], all: [] };
    const addF = (list, key, parent, bonePos, sweepDeg, tilt, w, L, extra = {}) => {
      const pv = new THREE.Object3D();
      pv.position.set(S * bonePos, extra.y ?? 0, extra.z ?? 0);
      parent.add(pv);
      const rec = fs.add(key, Object.assign({ pivot: pv, w, L, mirror, sy: extra.sy ?? 10, camber: extra.camber ?? 0.06 }, extra));
      rec.splayOpen = sweepDeg; rec.splayClosed = extra.closed ?? sweepDeg; rec.tilt = tilt; rec.side = S; rec.bonePos = bonePos; rec.span = extra.span ?? 0;
      rec.baseY = extra.y ?? 0; rec.baseZ = extra.z ?? 0; rec.shaftRot = 0; rec.liftBias = extra.lift ?? 0; rec.bone = parent; rec.stagger = (extra.stagger ?? 0) * DEG;
      list.push(rec); F.all.push(rec);
      return rec;
    };
    // ---- primaries (10) on the manus: P1 inner (near wrist) ... P10 outer
    const NP = 10;
    for (let i = 0; i < NP; i++) {
      const t = i / (NP - 1);
      const bonePos = 0.06 + t * (DIM.manus - 0.08);
      const len = lerp(3.7, 5.0, Math.sin(Math.PI * clamp(t * 1.25, 0, 1)) ) * (t > 0.8 ? 0.93 : 1);
      const width = t > 0.6 ? lerp(0.62, 0.36, (t - 0.6) / 0.4) : 0.62; // emarginated outer "fingers"
      const open = 22 + t * 92;   // fanned
      const closed = 6 + t * 10;   // folded / tucked: nearly parallel to the hand
      addF(F.primaries, 'primary', wrist, bonePos, open, 0.02 - t * 0.02, width, len, { closed, span: 1 + t, camber: 0.05, y: 0.0 + i * 0.004, z: -0.02, stagger: (t - 0.5) * 7 });
    }
    // ---- alula (3 small feathers at the thumb)
    for (let i = 0; i < 3; i++) addF(F.alula, 'covert', wrist, 0.05 + i * 0.1, 55 + i * 10, 0.05, 0.3, 0.9 - i * 0.12, { closed: 20, y: 0.08, z: 0.15, sy: 5 });
    // ---- secondaries (14) on the ulna: S1 near the wrist ... S14 near the elbow
    const NS = 14;
    for (let i = 0; i < NS; i++) {
      const t = i / (NS - 1);
      const bonePos = DIM.ulna - 0.1 - t * (DIM.ulna - 0.25);
      const len = lerp(3.4, 3.15, t) + 0.35 * Math.sin(Math.PI * t);
      const sweep = -2 - t * 12; // inner ones angle toward the tail
      addF(F.secondaries, 'secondary', elbow, bonePos, sweep, 0.0, 0.66, len, { closed: -82 + t * 4, span: 1 - t * 0.5, camber: 0.08, y: 0.0 + i * 0.003, z: -0.08, stagger: (0.5 - t) * 5 });
    }
    // ---- tertials (4) on the humerus
    for (let i = 0; i < 4; i++) {
      const t = i / 3;
      addF(F.tertials, 'secondary', shoulder, DIM.humerus - 0.15 - t * 0.55, -16 - t * 12, 0.02, 0.62, 3.0 - t * 0.45, { closed: 78 + t * 4, span: 0.4, camber: 0.08, y: 0.02, z: -0.1, stagger: -4 - t * 3 });
    }
    // ---- covert rows over the flight-feather bases (greater / median / lesser)
    const flight = [...F.primaries.map(f => ({ f, parent: wrist })), ...F.secondaries.map(f => ({ f, parent: elbow })), ...F.tertials.map(f => ({ f, parent: shoulder }))];
    for (const { f, parent } of flight) {
      const stg = f.stagger / DEG;
      addF(F.gCov, 'covert', parent, f.bonePos, f.splayOpen, f.tilt + 0.04, f.w * 0.95, f.L * 0.45, { closed: f.splayClosed, span: f.span, camber: 0.08, y: f.baseY + 0.06, z: f.baseZ + 0.12, sy: 6, stagger: stg });
      addF(F.mCov, 'covert', parent, f.bonePos + 0.02, f.splayOpen * 0.9, f.tilt + 0.09, f.w * 0.8, f.L * 0.28, { closed: f.splayClosed, span: f.span, camber: 0.1, y: f.baseY + 0.12, z: f.baseZ + 0.25, sy: 5, stagger: stg });
      if (parent !== wrist) addF(F.lCov, 'covert', parent, f.bonePos - 0.03, f.splayOpen * 0.8, f.tilt + 0.14, f.w * 0.65, f.L * 0.17, { closed: f.splayClosed, span: f.span, camber: 0.12, y: f.baseY + 0.18, z: f.baseZ + 0.35, sy: 4, stagger: stg });
      // underwing coverts
      addF(F.under, 'under', parent, f.bonePos + 0.01, f.splayOpen * 0.95, f.tilt - 0.06, f.w * 0.9, f.L * 0.42, { closed: f.splayClosed, span: f.span, camber: -0.04, y: f.baseY - 0.09, z: f.baseZ + 0.1, sy: 6, stagger: stg });
    }
    return { S, shoulder, elbow, wrist, tip, F };
  }

  /* ---------------- leg ---------------- */
  buildLeg(side) {
    const S = side;
    const hip = new THREE.Object3D(); hip.position.set(S * DIM.hip.x, DIM.hip.y, DIM.hip.z); this.root.add(hip);
    const knee = new THREE.Object3D(); knee.position.set(0, -DIM.femur, 0); hip.add(knee);
    const ankle = new THREE.Object3D(); ankle.position.set(0, -DIM.tibia, 0); knee.add(ankle);
    const foot = new THREE.Object3D(); foot.position.set(0, -DIM.tarsus, 0); ankle.add(foot);
    const downBone = (parent, len, r0, r1, mat, squash = 1) => {
      const g = boneGeo(len, r0, r1, 12, 4, squash); g.rotateZ(-Math.PI / 2); // +X -> -Y
      const m = new THREE.Mesh(g, mat); m.castShadow = true; parent.add(m); return m;
    };
    downBone(hip, DIM.femur + 0.3, 0.42, 0.36, this.mat.body).position.y = 0.15;   // feathered thigh
    downBone(knee, DIM.tibia + 0.2, 0.36, 0.22, this.mat.body).position.y = 0.1;  // feathered "trousers"
    downBone(ankle, DIM.tarsus + 0.05, 0.16, 0.14, this.mat.skin);                 // bare yellow tarsus
    // foot pad
    const pad = new THREE.Mesh(new THREE.SphereGeometry(0.2, 12, 10), this.mat.skin); pad.scale.set(1.2, 0.7, 1.2); pad.castShadow = true; foot.add(pad);
    // toes: 3 front (II, III, IV) + hallux (I) behind
    const toes = [];
    const mkToe = (yaw, segs, back) => {
      const rootT = new THREE.Object3D(); rootT.rotation.y = yaw; foot.add(rootT);
      const joints = [];
      let parent = rootT;
      let r = 0.13;
      for (let i = 0; i < segs.length; i++) {
        const j = new THREE.Object3D(); parent.add(j);
        const g = boneGeo(segs[i], r, r * 0.85, 10, 3); g.rotateY(-Math.PI / 2); // +X -> +Z
        const m = new THREE.Mesh(g, this.mat.skin); m.castShadow = true; j.add(m);
        const next = new THREE.Object3D(); next.position.z = segs[i]; j.add(next);
        joints.push(j); parent = next; r *= 0.85;
      }
      // talon: curved claw sweeping downward
      const tl = 0.55;
      const claw = new THREE.Mesh(sweepGeo([new THREE.Vector3(0, 0, -0.05), new THREE.Vector3(0, -0.02, tl * 0.45), new THREE.Vector3(0, -0.18, tl * 0.8), new THREE.Vector3(0, -0.45, tl * 0.92), new THREE.Vector3(0, -0.62, tl * 0.85)], t => lerp(r * 0.95, 0.008, Math.pow(t, 0.9)), 20, 10), this.mat.talon);
      claw.castShadow = true; parent.add(claw);
      toes.push({ root: rootT, joints, back, yaw });
    };
    mkToe(S * 0.55, [0.42, 0.36], false);
    mkToe(0, [0.5, 0.42, 0.36], false);
    mkToe(-S * 0.55, [0.4, 0.34], false);
    mkToe(Math.PI, [0.44], true);
    return { S, hip, knee, ankle, foot, toes };
  }

  /* ---------------- tail ---------------- */
  buildTail() {
    const tb = new THREE.Object3D(); tb.position.copy(DIM.tailBase); this.root.add(tb);
    this.tail = { base: tb, feathers: [] };
    const N = 12;
    for (let i = 0; i < N; i++) {
      const pv = new THREE.Object3D();
      const c = (i - (N - 1) / 2) / ((N - 1) / 2); // -1..1
      pv.position.set(c * 0.35, 0.02 - Math.abs(c) * 0.06, 0.05);
      tb.add(pv);
      const rec = this.fs.add('tail', { pivot: pv, w: 0.68, L: 2.75 - Math.abs(c) * 0.25, mirror: c > 0, sy: 8, camber: 0.05 });
      rec.c = c;
      this.tail.feathers.push(rec);
    }
  }

  /* ============================================================================
   * POSE — turns the parameter set into joint rotations and feather bends.
   * ========================================================================== */
  pose(dt) {
    const P = this.params;
    this.time += dt;
    const flapOn = P.flapAmp;
    // Time-warped stroke cycle: downstroke (top -> bottom) occupies 56% of the period.
    const ph = ((P.flapPhase % (Math.PI * 2)) + Math.PI * 2) % (Math.PI * 2);
    const u = ph / (Math.PI * 2);
    const w = u < 0.56 ? (u / 0.56) * 0.5 : 0.5 + ((u - 0.56) / 0.44) * 0.5;
    const phi = w * Math.PI * 2; // 0 = top of stroke, PI = bottom
    const downstroke = Math.sin(phi) > 0 ? Math.sin(phi) : 0;  // >0 while moving down
    const upstroke = Math.sin(phi) < 0 ? -Math.sin(phi) : 0;
    const lagE = 0.55, lagW = 0.95; // elbow / wrist lag behind the shoulder (rad)
    const flexE = Math.pow(0.5 - 0.5 * Math.cos(phi - Math.PI * 1.5 - lagE + Math.PI), 1.6); // peaks mid-upstroke
    const flexW = Math.pow(0.5 - 0.5 * Math.cos(phi - Math.PI * 1.5 - lagW + Math.PI), 1.8);

    const fold = P.fold, glide = 1 - fold;
    const brake = P.brake, tuck = P.tuck;
    const amp = flapOn * glide;

    // --- per wing
    for (const key of ['R', 'L']) {
      const W = this.wings[key], S = W.S;
      // --- shoulder ---
      let elev = lerp(12 * DEG, P.dihedral + 6 * DEG, glide);
      let prot = lerp(-86 * DEG, -10 * DEG, glide);          // + = forward
      let pron = lerp(55 * DEG, 4 * DEG, glide);              // + = leading edge down (folded wing drapes down the flank)
      // flapping (elevation ±, protraction forward on downstroke, pronation with stroke)
      elev += amp * (44 * DEG * Math.cos(phi) + 6 * DEG);
      prot += amp * (16 * DEG * Math.sin(phi) - 4 * DEG);
      pron += amp * (14 * DEG * Math.sin(phi));
      // tuck (stoop): wings swept back tightly against the body, slightly drooped
      elev = lerp(elev, -12 * DEG, tuck); prot = lerp(prot, -55 * DEG, tuck);
      // brake (flare): wings thrown forward and cupped, strong pronation (high AoA)
      prot = lerp(prot, 20 * DEG, brake * glide); pron = lerp(pron, 32 * DEG, brake * glide); elev = lerp(elev, 22 * DEG, brake * glide * 0.6);
      // shrug (perched wing settle)
      elev += P.shrug * 20 * DEG; prot += P.shrug * 12 * DEG;
      _q1.setFromAxisAngle(Y, -S * prot); _q2.setFromAxisAngle(Z, S * elev); _q3.setFromAxisAngle(X, pron);
      W.shoulder.quaternion.copy(_q1).multiply(_q2).multiply(_q3);

      // --- elbow --- (flexE > 0 swings the forearm forward = folding direction)
      let eFlex = lerp(168 * DEG, 20 * DEG, glide);
      eFlex += amp * flexE * 42 * DEG;
      eFlex = lerp(eFlex, 112 * DEG, tuck);
      eFlex = lerp(eFlex, 8 * DEG, brake * glide * 0.7);
      eFlex -= P.shrug * 40 * DEG;
      let eElev = lerp(2 * DEG, -2 * DEG, glide) + amp * (-10 * DEG * flexE) + tuck * 6 * DEG;
      _q1.setFromAxisAngle(Y, -S * eFlex); _q2.setFromAxisAngle(Z, S * eElev);
      W.elbow.quaternion.copy(_q1).multiply(_q2);

      // --- wrist --- (wFlex > 0 swings the hand backward = folding direction)
      let wFlex = lerp(166 * DEG, 16 * DEG, glide);
      wFlex += amp * flexW * 58 * DEG;
      wFlex = lerp(wFlex, 128 * DEG, tuck);
      wFlex = lerp(wFlex, -6 * DEG, brake * glide * 0.6);
      wFlex -= P.shrug * 30 * DEG;
      let wElev = lerp(-10 * DEG, 4 * DEG, glide) + amp * (-22 * DEG * flexW + 6 * DEG * downstroke) + brake * glide * 10 * DEG;
      let wPron = amp * (10 * DEG * Math.sin(phi - lagW)) - amp * flexW * 20 * DEG * 0 + brake * glide * 12 * DEG;
      _q1.setFromAxisAngle(Y, S * wFlex); _q2.setFromAxisAngle(Z, S * wElev); _q3.setFromAxisAngle(X, wPron);
      W.wrist.quaternion.copy(_q1).multiply(_q2).multiply(_q3);

    }

    // --- feathers: splay / tilt / shaft rotation / aerodynamic bend.
    // The "closed" direction is computed from each bone's current orientation so the
    // feathers always lie back along the body when the wing is folded or tucked.
    this.root.updateMatrixWorld(true);
    const backWorld = _v2.set(0, -0.12, -1).normalize().applyQuaternion(this.root.quaternion);
    const m3 = this._m3 || (this._m3 = new THREE.Matrix3());
    for (const key of ['R', 'L']) {
      const W = this.wings[key], S = W.S;
      const boneBack = new Map();
      for (const bone of [W.shoulder, W.elbow, W.wrist]) {
        m3.setFromMatrix4(bone.matrixWorld).invert();
        _v3.copy(backWorld).applyMatrix3(m3);
        boneBack.set(bone, { splay: Math.atan2(S * _v3.x, -_v3.z), tilt: Math.atan2(_v3.y, Math.hypot(_v3.x, _v3.z)) });
      }
      const spreadBase = clamp(P.spread * glide + brake * 0.6, 0, 1) * (1 - tuck * 0.9) * (1 - fold * 0.9) * (1 - P.shrug * 0.3);
      const loadDown = amp * downstroke;          // lift load on the downstroke bends tips up
      const loadUp = amp * upstroke;
      const glideLoad = glide * (1 - amp) * (0.5 + brake * 0.6);
      for (const f of W.F.all) {
        const isPrimary = f.bone === W.wrist;
        const bb = boneBack.get(f.bone);
        // hand feathers close partially during the flexed upstroke
        const sp = isPrimary ? spreadBase * (1 - 0.55 * flexW * amp) : spreadBase * (1 - 0.25 * flexE * amp);
        const splay = lerp(bb.splay + f.stagger, f.splayOpen * DEG, sp);
        const tilt = lerp(bb.tilt + f.stagger * 0.3, f.tilt + (isPrimary ? loadUp * 0.15 : 0), sp);
        // upstroke: primaries rotate about their shaft & separate to spill air (venetian blind effect)
        const shaftRot = isPrimary ? (loadUp * flexW * 34 * DEG + brake * 8 * DEG) : loadUp * 6 * DEG;
        _q1.setFromAxisAngle(Y, -S * splay); _q2.setFromAxisAngle(X, tilt); _q3.setFromAxisAngle(Y, S * shaftRot);
        f.pivot.quaternion.copy(_q1).multiply(_q2).multiply(Q_BASE).multiply(_q3);
        // bend: spanwise (outer feathers bend more), up on downstroke & in glide, down/back on upstroke
        const span = f.span;
        const base = isPrimary ? 0.16 * span : 0.06 * span;
        let bend = base * (glideLoad * 1.2 + loadDown * 1.9) - (isPrimary ? 0.22 : 0.06) * span * loadUp * 0.8;
        bend += brake * glide * (isPrimary ? 0.25 : 0.1) * span;
        bend *= (1 - fold * 0.7);
        f.bend = lerp(f.bend, bend, clamp(dt * 14, 0, 1));
        f.twist = isPrimary ? (loadDown * -0.12 + loadUp * 0.18) * span : 0;
      }
      // alula pops up during braking
      for (const a of W.F.alula) { a.tilt = 0.05 + brake * 0.5; }
    }

    // --- tail
    const T = this.tail;
    const fan = clamp(P.tailFan, 0, 1);
    T.base.rotation.set(P.tailPitch, 0, P.tailRoll);
    for (const f of T.feathers) {
      const splay = f.c * lerp(4 * DEG, 42 * DEG, fan);
      _q1.setFromAxisAngle(Y, -splay); _q2.setFromAxisAngle(X, 0.02 + Math.abs(f.c) * 0.04 * fan);
      f.pivot.quaternion.copy(_q1).multiply(_q2).multiply(Q_BASE);
      f.pivot.position.y = 0.02 - Math.abs(f.c) * 0.05 - (f.c > 0 ? 0.008 : 0);
      f.bend = lerp(f.bend, 0.05 + P.brake * 0.1 - flapOn * downstroke * 0.05, clamp(dt * 10, 0, 1));
    }

    // --- legs
    const LM = P.legMode;
    const wsum = Math.max(1e-3, LM.perch + LM.tucked + LM.strike + LM.carry);
    const mix4 = (a, b, c, d) => (a * LM.perch + b * LM.tucked + c * LM.strike + d * LM.carry) / wsum;
    // angles (deg): hip(+ back), knee(+ back), ankle(+ back) — see comments in the design notes
    const hipA = mix4(-60, 62, -72, -8) + P.crouch * -25 + P.legLoad * 10;
    const kneeA = mix4(105, 42, 52, 46) + P.crouch * 40 - P.legLoad * 12;
    const ankleA = mix4(-45, -8, -22, -34) + P.crouch * -15 + P.legLoad * -8;
    const curl = clamp(P.toeCurl, 0, 1), spreadT = clamp(P.toeSpread, 0, 1);
    for (const key of ['R', 'L']) {
      const Lg = this.legs[key], S = Lg.S;
      Lg.hip.rotation.set(hipA * DEG, 0, S * mix4(-6, 10, -12, -4) * DEG);
      Lg.knee.rotation.set(kneeA * DEG, 0, 0);
      Lg.ankle.rotation.set(ankleA * DEG, 0, S * mix4(0, 0, 6, 2) * DEG);
      // foot: bring toes horizontal relative to the tarsus
      Lg.foot.rotation.set(mix4(0, -80, 60, 45) * DEG, 0, 0);
      for (const toe of Lg.toes) {
        const back = toe.back;
        const sp = lerp(0.6, 1.25, spreadT);
        toe.root.rotation.y = toe.yaw * (back ? 1 : sp);
        toe.root.rotation.x = (back ? 0.12 : 0.04) + curl * 0.3;
        for (let i = 0; i < toe.joints.length; i++) {
          toe.joints[i].rotation.x = (i === 0 ? 0.05 : 0.1) + curl * (0.55 + i * 0.25) * (back ? 1.1 : 1);
        }
      }
    }

    // --- head / neck look-at with joint limits and roll stabilisation
    this.root.updateMatrixWorld(true);
    const nb = this.neck.base;
    let yaw = 0, pitch = 0;
    if (P.headTarget) {
      _v1.copy(P.headTarget);
      nb.worldToLocal(_v1);
      yaw = Math.atan2(_v1.x, _v1.z);
      pitch = Math.atan2(_v1.y, Math.hypot(_v1.x, _v1.z));
      yaw = clamp(yaw, -125 * DEG, 125 * DEG); pitch = clamp(pitch, -55 * DEG, 45 * DEG);
    }
    this._hy = lerp(this._hy ?? yaw, yaw, clamp(dt * 7, 0, 1));
    this._hp = lerp(this._hp ?? pitch, pitch, clamp(dt * 7, 0, 1));
    const hy = this._hy, hp = this._hp;
    // neck base leans forward while crouching; distribute the rotation along the chain
    this.neck.n1.rotation.set(-hp * 0.3 - P.crouch * 0.5 + lerp(-0.35, 0.15, glide), hy * 0.35, 0, 'YXZ');
    this.neck.n2.rotation.set(-hp * 0.35 + lerp(0.15, -0.05, glide), hy * 0.35, 0, 'YXZ');
    this.neck.head.rotation.set(-hp * 0.35 + lerp(0.25, -0.05, glide) - P.crouch * 0.1, hy * 0.3, -P.headLevelRoll, 'YXZ');
    // blink
    const bl = P.blink;
    this.lidL.scale.y = this.lidR.scale.y = 0.15 + bl * 0.95;

    // --- body: flap bob & pitch coupling
    const bob = amp * (0.12 * Math.cos(phi + 0.4));
    this.torsoOffset = bob;
    this.root.updateMatrixWorld(true);
    // foot anchor: midpoint between the two feet in world space
    _v1.setFromMatrixPosition(this.legs.R.foot.matrixWorld); _v2.setFromMatrixPosition(this.legs.L.foot.matrixWorld);
    this.footAnchor.addVectors(_v1, _v2).multiplyScalar(0.5);
    this.fs.update(this.root.position);
  }

  /** World-space wing tip positions (debug / FX). */
  wingTip(side, out = new THREE.Vector3()) { return out.setFromMatrixPosition(this.wings[side].tip.matrixWorld); }
}

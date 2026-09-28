import * as THREE from 'three';
import { COLOR } from './anatomy.js';

const sstep = (a, b, x) => { const t = Math.min(Math.max((x - a) / (b - a), 0, 0), 1); return t * t * (3 - 2 * t); };
const lerp = (a, b, t) => a + (b - a) * t;

// ---------------------------------------------------------------------------------------------
// Generic loft: rings[i] = array of Vector3 (same count), closed around; optional caps.
// ---------------------------------------------------------------------------------------------
export function loftGeometry(rings, { capStart = false, capEnd = false, colors = null, uvs = true } = {}) {
  const nR = rings.length, nS = rings[0].length;
  const pos = [], col = [], uv = [], idx = [];
  for (let i = 0; i < nR; i++) for (let j = 0; j <= nS; j++) {
    const p = rings[i][j % nS]; pos.push(p.x, p.y, p.z);
    if (colors) { const c = colors[i][j % nS]; col.push(c.r, c.g, c.b); }
    uv.push(j / nS, i / (nR - 1));
  }
  const W = nS + 1;
  for (let i = 0; i < nR - 1; i++) for (let j = 0; j < nS; j++) {
    const a = i * W + j, b = a + 1, c = a + W, d = c + 1;
    idx.push(a, b, c, b, d, c);   // outward when (ring tangent x loft axis) points outward
  }
  const cap = (ri, flip) => {
    const ring = rings[ri], cen = new THREE.Vector3();
    ring.forEach((p) => cen.add(p)); cen.multiplyScalar(1 / nS);
    const ci = pos.length / 3; pos.push(cen.x, cen.y, cen.z); uv.push(0.5, ri ? 1 : 0);
    if (colors) { const c = colors[ri][0]; col.push(c.r, c.g, c.b); }
    for (let j = 0; j < nS; j++) {
      const a = ri * W + j, b = a + 1;
      if (flip) idx.push(ci, b, a); else idx.push(ci, a, b);
    }
  };
  if (capStart) cap(0, true);
  if (capEnd) cap(nR - 1, false);
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  if (colors) g.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
  if (uvs) g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setIndex(idx);
  g.computeVertexNormals();
  return g;
}

// ---------------------------------------------------------------------------------------------
// Core body profile, indexed by the bind-pose z coordinate along the spine.
// a: half width, bt/bb: half height above/below the centre, dy: centre offset from the spine.
// ---------------------------------------------------------------------------------------------
const STATIONS = [
  //  z       a      bt     bb     dy
  [-0.165, 0.022, 0.016, 0.016, 0.014],
  [-0.14, 0.04, 0.028, 0.03, 0.01],
  [-0.1, 0.062, 0.042, 0.058, 0.0],
  [-0.05, 0.08, 0.052, 0.082, -0.004],
  [0.0, 0.092, 0.058, 0.098, 0.0],
  [0.06, 0.1, 0.062, 0.112, 0.002],
  [0.12, 0.097, 0.06, 0.118, 0.004],
  [0.17, 0.083, 0.054, 0.104, 0.008],
  [0.205, 0.06, 0.044, 0.08, 0.004],
  [0.235, 0.045, 0.038, 0.052, 0.0],
  [0.28, 0.038, 0.035, 0.041, 0.0],
  [0.34, 0.035, 0.034, 0.037, 0.0],
  [0.4, 0.034, 0.035, 0.035, 0.002],
  [0.425, 0.034, 0.038, 0.034, 0.005],
  [0.445, 0.031, 0.04, 0.031, 0.007],
  [0.462, 0.027, 0.036, 0.029, 0.005],
  [0.475, 0.022, 0.029, 0.026, 0.002],
  [0.484, 0.017, 0.022, 0.021, -0.001],
  [0.489, 0.011, 0.014, 0.014, -0.002],
];
function station(z) {
  const S = STATIONS;
  if (z <= S[0][0]) return S[0].slice(1);
  for (let i = 0; i < S.length - 1; i++) {
    if (z <= S[i + 1][0]) {
      const t = (z - S[i][0]) / (S[i + 1][0] - S[i][0]);
      const u = t * t * (3 - 2 * t) * 0.5 + t * 0.5; // mostly linear with slight easing
      return S[i].slice(1).map((v, k) => lerp(v, S[i + 1][k + 1], u));
    }
  }
  return S[S.length - 1].slice(1);
}

export class Body {
  constructor(rig) {
    this.rig = rig;
    this.group = new THREE.Group();
    this._spine();
    this._core();
    this._head();
  }

  // bind-space spine curve as a function of z (piecewise linear through bone origins)
  _spine() {
    const r = this.rig; const pts = [];
    const w = (b) => new THREE.Vector3().setFromMatrixPosition(r.bindWorld.get(b));
    const tail = w(r.tail);
    pts.push(new THREE.Vector3(0, tail.y + 0.004, -0.2), tail, w(r.pelvis), w(r.thorax));
    for (const n of r.neck) pts.push(w(n));
    const h = w(r.head); pts.push(h, h.clone().add(new THREE.Vector3(0, 0, 0.1)));
    this.spinePts = pts;
    // joints (z) with their bones: tail segment is behind its origin
    this.joints = [
      { z: tail.z, a: r.tail, b: r.pelvis, h: 0.035 },
      { z: w(r.thorax).z, a: r.pelvis, b: r.thorax, h: 0.05 },
      { z: w(r.neck[0]).z, a: r.thorax, b: r.neck[0], h: 0.022 },
    ];
    for (let i = 1; i < r.neck.length; i++) this.joints.push({ z: w(r.neck[i]).z, a: r.neck[i - 1], b: r.neck[i], h: 0.011 });
    this.joints.push({ z: h.z, a: r.neck[r.neck.length - 1], b: r.head, h: 0.01 });
  }
  spineAt(z) {
    const P = this.spinePts;
    for (let i = 0; i < P.length - 1; i++) if (z <= P[i + 1].z || i === P.length - 2) {
      const t = (z - P[i].z) / (P[i + 1].z - P[i].z);
      return new THREE.Vector3().lerpVectors(P[i], P[i + 1], t);
    }
  }
  // skin weights for a bind-space z: returns [[bone, w], [bone, w]]
  weightsAt(z) {
    const J = this.joints;
    if (z < J[0].z - J[0].h) return [[J[0].a, 1], [J[0].b, 0]];
    for (let i = 0; i < J.length; i++) {
      const j = J[i];
      if (z < j.z + j.h) {
        const t = sstep(j.z - j.h, j.z + j.h, z);
        if (t <= 0 && i > 0) return [[j.a, 1], [J[i - 1].a, 0]];
        return [[j.a, 1 - t], [j.b, t]];
      }
      const nxt = J[i + 1];
      if (!nxt || z < nxt.z - nxt.h) return [[j.b, 1], [j.a, 0]];
    }
    const last = J[J.length - 1];
    return [[last.b, 1], [last.a, 0]];
  }

  // plumage colour of the area around a bind-space point (used for core & feathers)
  isWhite(p) {
    // white hood: head and neck down to the base of the neck, a little lower on the throat
    const down = Math.max(0, -(p.y - this.spineAt(p.z).y)) / 0.06;
    const edge = 0.232 - 0.02 * Math.min(1, down) + 0.006 * Math.sin(p.x * 180 + p.y * 90);
    if (p.z > edge) return true;
    // white tail coverts / undertail region
    if (p.z < -0.15) return true;
    return false;
  }

  eyeCentre(s) { return new THREE.Vector3(0.0205 * s, 0.013, 0.046); }   // head space
  eyeAxis(s) { return new THREE.Vector3(0.6 * s, 0.05, 0.8).normalize(); }

  surfacePoint(z, phi) {
    const [a, bt, bb, dy] = station(z);
    const c = this.spineAt(z);
    const cs = Math.cos(phi), sn = Math.sin(phi);
    const x = a * Math.sign(cs) * Math.pow(Math.abs(cs), 2 / 2.25);
    const y = sn >= 0 ? bt * Math.pow(sn, 2 / 2.4) : -bb * Math.pow(-sn, 2 / 2.0);
    const p = new THREE.Vector3(c.x + x, c.y + dy + y, z);
    // head: brow ridge (supraorbital shelf) and eye socket
    const hz = z - this.headZ, hy = p.y - this.headY;
    if (hz > 0) {
      for (const s of [1, -1]) {
        const e = this.eyeCentre(s);
        // brow: bulge above & slightly in front of the eye
        const dB = Math.hypot((p.x - (e.x + 0.001 * s)) / 0.014, (hy - (e.y + 0.012)) / 0.008, (hz - (e.z + 0.002)) / 0.017);
        if (dB < 1) { const k = (1 - dB * dB) ** 2; p.x += 0.0045 * s * k; p.y += 0.004 * k; }
        // socket: pull the skin around the eye inward so the eye bulges out
        const dE = Math.hypot(p.x - e.x * 1.25, hy - e.y, hz - e.z) / 0.0145;
        if (dE < 1) { const k = (1 - dE * dE) ** 2; p.x -= 0.004 * s * k * (Math.sign(p.x) === s ? 1 : 0); }
      }
    }
    return p;
  }

  _core() {
    const r = this.rig;
    this.headZ = new THREE.Vector3().setFromMatrixPosition(r.bindWorld.get(r.head)).z;
    this.headY = new THREE.Vector3().setFromMatrixPosition(r.bindWorld.get(r.head)).y;
    const z0 = STATIONS[0][0], z1 = STATIONS[STATIONS.length - 1][0];
    const nR = 150, nS = 72;
    const rings = [], cols = [];
    const brown = new THREE.Color(COLOR.brown), white = new THREE.Color(COLOR.whiteShade);
    this.zAt = (i) => { const t = i / (nR - 1); return z0 + (z1 - z0) * t; };
    for (let i = 0; i < nR; i++) {
      const z = this.zAt(i);
      const ring = [], cr = [];
      for (let j = 0; j < nS; j++) {
        const phi = (j / nS) * Math.PI * 2;
        const p = this.surfacePoint(z, phi);
        ring.push(p); cr.push(this.isWhite(p) ? white : brown);
      }
      rings.push(ring); cols.push(cr);
    }
    const geo = loftGeometry(rings, { capStart: true, capEnd: true, colors: cols });
    // skinning
    const bones = r.core;
    const bi = new Map(bones.map((b, i) => [b, i]));
    const pos = geo.attributes.position;
    const si = new Uint16Array(pos.count * 4), sw = new Float32Array(pos.count * 4);
    for (let v = 0; v < pos.count; v++) {
      const ws = this.weightsAt(pos.getZ(v));
      si[v * 4] = bi.get(ws[0][0]); sw[v * 4] = ws[0][1];
      si[v * 4 + 1] = bi.get(ws[1][0]); sw[v * 4 + 1] = ws[1][1];
    }
    geo.setAttribute('skinIndex', new THREE.Uint16BufferAttribute(si, 4));
    geo.setAttribute('skinWeight', new THREE.Float32BufferAttribute(sw, 4));
    const mat = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.95, metalness: 0 });
    this.core = new THREE.SkinnedMesh(geo, mat);
    this.core.castShadow = true; this.core.receiveShadow = true;
    this.core.frustumCulled = false;
    this.skeleton = new THREE.Skeleton(bones);
    this.core.bind(this.skeleton, new THREE.Matrix4());
    this.group.add(this.core);
  }

  // ---------------------------------------------------------------------------------------------
  // Head: bill, cere, nostrils, gape, mouth, tongue, eyes, lids and nictitating membranes
  // ---------------------------------------------------------------------------------------------
  _head() {
    const r = this.rig;
    const billMat = new THREE.MeshPhysicalMaterial({ vertexColors: true, roughness: 0.32, clearcoat: 0.5, clearcoatRoughness: 0.25, metalness: 0 });
    const skinMat = new THREE.MeshStandardMaterial({ color: COLOR.cere, roughness: 0.62 });
    const bill = this._upperBill();
    const upper = new THREE.Mesh(bill, billMat); upper.castShadow = true;
    r.upperBill.add(upper);
    const lower = new THREE.Mesh(this._lowerBill(), billMat); lower.castShadow = true;
    r.jaw.add(lower);
    // tongue
    const tg = new THREE.CapsuleGeometry(0.0045, 0.03, 4, 10); tg.rotateX(Math.PI / 2); tg.scale(1, 0.55, 1);
    const tongue = new THREE.Mesh(tg, new THREE.MeshPhysicalMaterial({ color: 0x8a4a4a, roughness: 0.35, clearcoat: 0.6, clearcoatRoughness: 0.2 }));
    tongue.position.set(0, -0.0035, 0.043);
    r.jaw.add(tongue);
    // cere (waxy skin band at the base of the upper bill)
    const cere = new THREE.Mesh(this._cere(), skinMat); cere.castShadow = true;
    r.upperBill.add(cere);
    // nostrils (nares): dark oval openings in the cere
    for (const s of [1, -1]) {
      const n = new THREE.Mesh(new THREE.SphereGeometry(1, 12, 8), new THREE.MeshStandardMaterial({ color: 0x0a0605, roughness: 0.8 }));
      n.scale.set(0.0012, 0.0024, 0.0042);
      n.position.set(0.0118 * s, -0.001, 0.003);
      n.rotation.set(-0.35, 0.2 * s, 0);
      r.upperBill.add(n);
    }
    // gape flange (rictus): fleshy yellow corner of the mouth
    for (const s of [1, -1]) {
      const g = new THREE.Mesh(new THREE.SphereGeometry(1, 14, 10), skinMat);
      g.scale.set(0.004, 0.0035, 0.009);
      g.position.set(0.0135 * s, -0.013, 0.05);
      r.head.add(g);
    }
    // eyes
    this.eyes = [1, -1].map((s) => this._eye(s));
  }

  _upperBill() {
    // Bezier helpers in (z, y) of the upperBill bone frame
    const bez = (P, t) => { const u = 1 - t; return [u * u * u * P[0][0] + 3 * u * u * t * P[1][0] + 3 * u * t * t * P[2][0] + t * t * t * P[3][0], u * u * u * P[0][1] + 3 * u * u * t * P[1][1] + 3 * u * t * t * P[2][1] + t * t * t * P[3][1]]; };
    const culmen = [[-0.006, 0.0045], [0.035, 0.009], [0.068, 0.002], [0.064, -0.034]];
    const tomium = [[-0.012, -0.024], [0.03, -0.0265], [0.056, -0.026], [0.064, -0.034]];
    const nR = 60, nS = 40;
    const rings = [], cols = [];
    const base = new THREE.Color(0xe9a514), tip = new THREE.Color(0xf2d27a), inside = new THREE.Color(0x5a3a33);
    for (let i = 0; i < nR; i++) {
      const t = i / (nR - 1);
      const [zt, yt] = bez(culmen, t); const [zb, yb] = bez(tomium, Math.min(1, t * 1.0));
      const wBase = 0.0122;
      const w = wBase * Math.pow(1 - t, 0.75) * (1 + 0.1 * Math.sin(t * Math.PI)) + 0.0006;
      const ring = [], cr = [];
      for (let j = 0; j < nS; j++) {
        const a = (j / nS) * Math.PI * 2;
        // outer arch for a in [0, PI]: from right tomium up over the culmen to the left tomium;
        // palate (inner surface) for a in [PI, 2PI], slightly vaulted above the tomia
        let x, y, z, c;
        if (a <= Math.PI) {
          const th = a;
          const cs = Math.cos(th), sn = Math.sin(th);
          x = w * Math.sign(cs) * Math.pow(Math.abs(cs), 0.62);
          const k = Math.pow(sn, 0.55);
          y = lerp(yb, yt, k); z = lerp(zb, zt, k);
          c = base.clone().lerp(tip, sstep(0.45, 1.0, t)).multiplyScalar(0.92 + 0.08 * k);
        } else {
          const th = a - Math.PI; const cs = Math.cos(th), sn = Math.sin(th);
          x = -w * 0.92 * cs;
          const vault = (yt - yb) * 0.35 * sn * (1 - t);
          y = yb + vault; z = zb + (zt - zb) * 0.1 * sn;
          c = inside;
        }
        ring.push(new THREE.Vector3(x, y, z)); cr.push(c);
      }
      rings.push(ring); cols.push(cr);
    }
    return loftGeometry(rings, { capStart: true, capEnd: false, colors: cols });
  }

  _lowerBill() {
    // lower mandible in jaw space (hinge at origin): tomium y ~ -0.001, gonys below, tip ends inside the hook
    const nR = 44, nS = 32, rings = [], cols = [];
    const outer = new THREE.Color(0xe7ad2a), tipC = new THREE.Color(0xf0d488), inside = new THREE.Color(0x7a4a45);
    for (let i = 0; i < nR; i++) {
      const t = i / (nR - 1);
      const z = lerp(0.006, 0.086, t);
      const top = -0.0015 - 0.002 * t;
      const bot = lerp(-0.018, -0.0075, Math.pow(t, 0.9)) - 0.002 * Math.sin(t * Math.PI);
      const w = lerp(0.0128, 0.0035, Math.pow(t, 1.3));
      const ring = [], cr = [];
      for (let j = 0; j < nS; j++) {
        const a = (j / nS) * Math.PI * 2;
        let x, y, c;
        if (a <= Math.PI) {
          // outer U-shaped trough: from left tomium down around the gonys to the right tomium
          const cs = Math.cos(a), sn = Math.sin(a);
          x = -w * Math.sign(cs) * Math.pow(Math.abs(cs), 0.6);
          y = lerp(top, bot, Math.pow(sn, 0.6));
          c = outer.clone().lerp(tipC, sstep(0.55, 1, t));
        } else {
          // inside of the trough (floor of the mouth)
          const cs = Math.cos(a - Math.PI), sn = Math.sin(a - Math.PI);
          x = w * 0.85 * cs;
          y = top - (top - bot) * 0.45 * sn * (1 - t * 0.7);
          c = inside;
        }
        ring.push(new THREE.Vector3(x, y, z)); cr.push(c);
      }
      rings.push(ring); cols.push(cr);
    }
    return loftGeometry(rings, { capStart: true, capEnd: true, colors: cols });
  }

  _cere() {
    const nR = 10, nS = 40, rings = [];
    for (let i = 0; i < nR; i++) {
      const t = i / (nR - 1);
      const z = lerp(-0.012, 0.011, t);
      const w = 0.0132 - 0.0008 * t, yt = 0.0068 - 0.001 * t, yb = -0.0215 + 0.001 * t;
      const ring = [];
      for (let j = 0; j < nS; j++) {
        const a = (j / nS) * Math.PI;           // arch only (open underside)
        const cs = Math.cos(a), sn = Math.sin(a);
        const x = w * Math.sign(cs) * Math.pow(Math.abs(cs), 0.62);
        const y = lerp(yb, yt, Math.pow(sn, 0.55));
        // soft rolled front edge
        const lip = sstep(0.75, 1, t);
        ring.push(new THREE.Vector3(x * (1 - 0.04 * lip), y - 0.0004 * lip, z));
      }
      rings.push(ring);
    }
    // build as an open strip (arch)
    const pos = [], idx = [];
    for (const ring of rings) for (const p of ring) pos.push(p.x, p.y, p.z);
    for (let i = 0; i < nR - 1; i++) for (let j = 0; j < nS - 1; j++) {
      const a = i * nS + j, b = a + 1, c = a + nS, d = c + 1; idx.push(a, b, c, b, d, c);
    }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3)); g.setIndex(idx); g.computeVertexNormals();
    return g;
  }

  _eye(s) {
    const r = this.rig;
    const grp = new THREE.Group();
    const c = this.eyeCentre(s), ax = this.eyeAxis(s);
    grp.position.copy(c);
    grp.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), ax);
    r.head.add(grp);
    const R = 0.0105;
    // eyeball with planar-projected iris texture (front hemisphere)
    const g = new THREE.SphereGeometry(R, 40, 28);
    const pos = g.attributes.position, uv = g.attributes.uv;
    for (let i = 0; i < pos.count; i++) uv.setXY(i, pos.getX(i) / R * 0.5 + 0.5, pos.getY(i) / R * 0.5 + 0.5);
    // flatten slightly behind; bulge the cornea in front
    for (let i = 0; i < pos.count; i++) {
      const z = pos.getZ(i) / R;
      if (z > 0.55) pos.setZ(i, pos.getZ(i) + R * 0.16 * ((z - 0.55) / 0.45) ** 2);
    }
    g.computeVertexNormals();
    const eyeMat = new THREE.MeshPhysicalMaterial({ map: irisTexture(), roughness: 0.4, clearcoat: 1, clearcoatRoughness: 0.03, ior: 1.376, specularIntensity: 0.6 });
    const eye = new THREE.Mesh(g, eyeMat);
    grp.add(eye);
    // eyelid rim (bare dark-grey skin ring)
    const rim = new THREE.Mesh(new THREE.TorusGeometry(R * 0.93, R * 0.2, 10, 40), new THREE.MeshStandardMaterial({ color: 0x3a3226, roughness: 0.7 }));
    rim.position.z = R * 0.5; rim.scale.set(1, 0.92, 1);
    grp.add(rim);
    // eyelids (upper & lower) as sphere segments that can close
    const lidMat = new THREE.MeshStandardMaterial({ color: 0x4a3d2c, roughness: 0.75, side: THREE.DoubleSide });
    const upperLid = new THREE.Mesh(new THREE.SphereGeometry(R * 1.22, 32, 12, 0, Math.PI * 2, 0, Math.PI * 0.5), lidMat);
    const lowerLid = new THREE.Mesh(new THREE.SphereGeometry(R * 1.2, 32, 12, 0, Math.PI * 2, 0, Math.PI * 0.5), lidMat);
    grp.add(upperLid, lowerLid);
    // nictitating membrane: translucent third eyelid sweeping from the front (nasal) corner backwards
    const nm = new THREE.ShaderMaterial({
      transparent: true, depthWrite: false,
      uniforms: { uP: { value: 0 }, uS: { value: s } },
      vertexShader: `varying vec3 vP; void main(){ vP = position; gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.0); }`,
      fragmentShader: `uniform float uP; uniform float uS; varying vec3 vP;
        void main(){
          if (vP.z < 0.0) discard;
          // eye-local x: towards +x for the left eye is backwards (temporal) after the eye rotation;
          float u = (vP.x * uS) / ${R.toFixed(5)} * 0.5 + 0.5;   // 0 nasal .. 1 temporal
          float cover = uP * 1.15;
          float a = smoothstep(cover, cover - 0.12, u);
          if (a < 0.01) discard;
          gl_FragColor = vec4(0.82, 0.85, 0.88, 0.72 * a);
        }`,
    });
    const nict = new THREE.Mesh(new THREE.SphereGeometry(R * 1.18, 32, 20), nm);
    grp.add(nict);
    const eyeObj = { grp, eye, upperLid, lowerLid, nict, R, s };
    this.setLids(eyeObj, 0.0, 0.0);
    return eyeObj;
  }

  // open: 0 = normal open eye, 1 = closed; nict: 0..1 membrane sweep
  setLids(e, closed, nict) {
    // lids are hemispherical caps (pole +y). Upper lid: pole tilted back when open, upright when closed;
    // lower lid mirrored below. Birds close mostly with the lower lid; it rises further than the upper drops.
    const up = THREE.MathUtils.lerp(-0.92, -0.25, closed);
    const lo = THREE.MathUtils.lerp(0.88, -0.25, closed);
    e.upperLid.rotation.set(up, 0, 0);
    e.lowerLid.rotation.set(Math.PI + lo, 0, 0);
    e.nict.material.uniforms.uP.value = nict;
  }

  // Feather anchors on the core: returns list of {p, n, flow, z, len, wid, type, white, lift, ruffle}
  sampleFeathers(seed = 7) {
    let s = seed; const R = () => { s = (s * 16807) % 2147483647; return s / 2147483647; };
    const out = [];
    const z0 = STATIONS[0][0] + 0.004, z1 = STATIONS[STATIONS.length - 1][0] - 0.004;
    const sizeAt = (z, phi) => {
      const hz = z - this.headZ;
      const under = Math.sin(phi) < -0.3;
      if (hz > 0.03) return { len: 0.017 - 0.006 * sstep(0.03, 0.068, hz), wid: 0.008, type: 'lance' };
      if (hz > -0.015) return { len: 0.028, wid: 0.012, type: 'lance' };
      if (z > 0.235) return { len: 0.05 + 0.012 * (1 - sstep(0.24, 0.4, z)), wid: 0.02, type: 'lance' };
      if (z > 0.17) return { len: 0.075, wid: 0.042, type: 'contour' };
      if (z < -0.12) return { len: 0.06, wid: 0.04, type: under ? 'fluff' : 'contour' };
      if (under && z < 0.05) return { len: 0.085, wid: 0.055, type: 'fluff' };
      return { len: 0.095, wid: 0.06, type: 'contour' };
    };
    let z = z0, row = 0;
    const eyes = [1, -1].map((si) => this.eyeCentre(si).add(new THREE.Vector3(0, this.headY, this.headZ)));
    while (z < z1) {
      const { len } = sizeAt(z, 0);
      const dz = len * 0.2;
      // perimeter approx
      const [a, bt, bb] = station(z);
      const per = Math.PI * (1.5 * (a * 2) - Math.sqrt(a * 2 * (bt + bb) / 2 * 2)) + Math.PI * (bt + bb) * 0.75;
      const { wid } = sizeAt(z, 0);
      const n = Math.max(6, Math.round(per / (wid * 0.5)));
      for (let j = 0; j < n; j++) {
        const phi = ((j + (row % 2) * 0.5 + (R() - 0.5) * 0.5) / n) * Math.PI * 2;
        const zz = z + (R() - 0.5) * dz * 0.8;
        const sz = sizeAt(zz, phi);
        const p = this.surfacePoint(zz, phi);
        // normal from finite differences
        const pz = this.surfacePoint(zz + 0.002, phi), pp = this.surfacePoint(zz, phi + 0.02);
        const tz = pz.clone().sub(p), tp = pp.clone().sub(p);
        const nrm = new THREE.Vector3().crossVectors(tz, tp).normalize();
        // keep eyes, bill and cere clear
        const hz = zz - this.headZ;
        let skip = false;
        for (const e of eyes) if (p.distanceTo(e) < 0.0122) skip = true;
        if (hz > 0.061) skip = true;
        if (hz > 0.05 && p.y - this.headY < -0.006 && hz > 0.054) skip = true;
        if (skip) continue;
        // flow: backwards along the body, slightly downwards on the flanks
        const flow = tz.clone().normalize().negate();
        const side = Math.cos(phi);
        flow.addScaledVector(nrm.clone().cross(flow).normalize(), 0.0);
        flow.y -= 0.18 * Math.abs(side) * (zz < 0.2 ? 1 : 0.3);
        flow.sub(nrm.clone().multiplyScalar(flow.dot(nrm))).normalize();
        const white = this.isWhite(p);
        const jitter = 0.85 + R() * 0.3;
        out.push({ p, n: nrm, flow, z: zz, phi, len: sz.len * jitter, wid: sz.wid * (0.9 + R() * 0.2), type: sz.type, white, seed: R(),
          lift: sz.type === 'fluff' ? 0.3 : sz.type === 'lance' ? 0.16 : 0.2, ruffle: sz.type === 'lance' ? 1 : 0.5 });
      }
      z += dz; row++;
    }
    return out;
  }
}

// procedural iris: pale yellow with radial fibres, darker limbal ring and round black pupil
function irisTexture() {
  const S = 512, c = document.createElement('canvas'); c.width = c.height = S;
  const g = c.getContext('2d');
  g.fillStyle = '#1a140c'; g.fillRect(0, 0, S, S);
  const cx = S / 2, cy = S / 2, rI = S * 0.46, rP = S * 0.16;
  const grd = g.createRadialGradient(cx, cy, rP, cx, cy, rI);
  grd.addColorStop(0, '#fff6c8'); grd.addColorStop(0.45, '#f4e38c'); grd.addColorStop(0.85, '#e0c45a'); grd.addColorStop(1, '#8a6a22');
  g.fillStyle = grd; g.beginPath(); g.arc(cx, cy, rI, 0, Math.PI * 2); g.fill();
  // radial fibres
  let s = 11; const R = () => { s = (s * 16807) % 2147483647; return s / 2147483647; };
  for (let i = 0; i < 900; i++) {
    const a = R() * Math.PI * 2, r0 = rP * (1 + R() * 0.2), r1 = rI * (0.6 + R() * 0.4);
    g.strokeStyle = R() < 0.5 ? `rgba(255,250,220,${0.08 + R() * 0.12})` : `rgba(150,110,30,${0.06 + R() * 0.1})`;
    g.lineWidth = 0.6 + R() * 1.2;
    g.beginPath(); g.moveTo(cx + Math.cos(a) * r0, cy + Math.sin(a) * r0);
    const a2 = a + (R() - 0.5) * 0.08; g.lineTo(cx + Math.cos(a2) * r1, cy + Math.sin(a2) * r1); g.stroke();
  }
  // limbal ring
  g.strokeStyle = 'rgba(70,50,15,0.8)'; g.lineWidth = S * 0.018; g.beginPath(); g.arc(cx, cy, rI - S * 0.008, 0, Math.PI * 2); g.stroke();
  // pupil (round in diurnal raptors) with a soft edge
  const pg = g.createRadialGradient(cx, cy, rP * 0.85, cx, cy, rP * 1.08);
  pg.addColorStop(0, '#020202'); pg.addColorStop(1, 'rgba(2,2,2,0)');
  g.fillStyle = pg; g.beginPath(); g.arc(cx, cy, rP * 1.08, 0, Math.PI * 2); g.fill();
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; t.anisotropy = 8;
  return t;
}

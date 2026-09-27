import * as THREE from 'three';
import { Surface } from './surface.js';
import { BODY, COL } from './anatomy.js';
import { Simplex3 } from '../core/noise.js';

const TAU = Math.PI * 2;
const noise = new Simplex3(4242);
const clamp = (x, a, b) => Math.min(Math.max(x, a), b);
const smooth = (a, b, x) => { const t = clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); };
const C = (hex) => new THREE.Color(hex);
const CL = Object.fromEntries(Object.entries(COL).map(([k, v]) => [k, C(v)]));
const tmpC = new THREE.Color();

// ----------------------------------------------------------------------------------------------
// Limb podomere (legs, palps, spinnerets)
// Local frame: segment runs along +Z from its proximal joint (z=0) to the distal joint (z=len);
// +Y is dorsal, +X is lateral. u is the angle around the segment (u=0.25 dorsal, 0.75 ventral).
// ----------------------------------------------------------------------------------------------
const PROFILES = {
  coxa: (t) => 0.92 + 0.16 * Math.sin(Math.PI * t),
  troch: (t) => 0.88 + 0.18 * Math.sin(Math.PI * t),
  femur: (t) => (0.8 + 0.2 * t) * (1 + 0.09 * Math.sin(Math.PI * t)),
  patella: (t) => 0.84 + 0.24 * Math.sin(Math.PI * Math.pow(t, 0.85)),
  tibia: (t) => (0.93 + 0.07 * t) * (1 + 0.05 * Math.sin(Math.PI * t)),
  meta: (t) => 1.0 - 0.14 * t,
  tarsus: (t) => 1.0 - 0.1 * t,
  spin: (t) => 1.0 - 0.3 * t,
};

export function limbSegmentSurface(kind, len, r, { palp = false, seed = 0 } = {}) {
  const prof = PROFILES[kind] || PROFILES.tibia;
  const cap0 = r * 0.8, cap1 = r * 0.5;
  const lat = kind === 'femur' ? 0.84 : kind === 'patella' ? 0.92 : 0.95;
  const density = kind === 'tarsus' ? 40 : 33;
  const circ = TAU * r * 1.1 * (1 + lat) / 2;
  const around = Math.max(8, Math.round(circ * density)) / density;
  const bow = kind === 'femur' ? 0.07 : kind === 'tibia' ? 0.03 : kind === 'meta' ? -0.02 : 0;
  const z0 = -cap0, z1 = len + cap1;

  const fn = (u, v, s) => {
    const z = z0 + (z1 - z0) * v;
    const t = clamp(z / len, 0, 1);
    let k = 1;
    if (z < 0) k = Math.sqrt(Math.max(1 - (z / cap0) ** 2, 0.0064)) * 0.9 + 0.1 * (1 + z / cap0);
    else if (z > len) k = Math.sqrt(Math.max(1 - ((z - len) / cap1) ** 2, 0.0064));
    const th = u * TAU;
    const ct = Math.cos(th), st = Math.sin(th);
    const pr = prof(t) * r * k;
    let ry = pr, rx = pr * lat;
    if (kind === 'tarsus' && st < 0) ry *= 0.85; // flattened scopula pad
    const yo = bow * r * Math.sin(Math.PI * t) * 2;
    s.p.set(rx * ct, ry * st + yo, z);
    // --------------------------------------------------------------- colour & fur
    const dors = st;
    const n1 = noise.noise(ct * 1.3 + seed, st * 1.3, z * 3.1) * 0.5 + 0.5;
    let col = CL.velvet, len0 = 0.08, stiff = 0.75;
    switch (kind) {
      case 'coxa': col = tmpC.copy(CL.coxa).lerp(CL.blackWarm, 0.3); len0 = 0.05; break;
      case 'troch': col = tmpC.copy(CL.blackWarm); len0 = 0.055; break;
      case 'femur': col = tmpC.copy(CL.velvet); len0 = 0.08; break;
      case 'patella': {
        const orangeMask = smooth(-0.62, -0.12, dors) * smooth(0.02, 0.14, t);
        const base = tmpC.copy(CL.orangeDeep).lerp(CL.orangePale, smooth(0.42, 0.85, t));
        let dth = Math.abs(th - Math.PI / 2);
        const stripe = (1 - smooth(0.05, 0.13, dth)) * (1 - smooth(0.7, 0.95, t)) * (palp ? 0.6 : 1);
        col = new THREE.Color().copy(CL.velvet).lerp(base, orangeMask * (1 - 0.85 * stripe));
        len0 = 0.088;
        break;
      }
      case 'tibia': {
        const ring = smooth(0.8, 0.88, t) * smooth(-0.85, -0.35, dors);
        col = new THREE.Color().copy(CL.velvet).lerp(CL.ringTibia, ring * 0.92);
        len0 = 0.085;
        break;
      }
      case 'meta': {
        const ring = smooth(0.9, 0.96, t) * smooth(-0.9, -0.4, dors);
        col = new THREE.Color().copy(CL.velvet).lerp(CL.ringMeta, ring * 0.9);
        len0 = 0.078;
        break;
      }
      case 'tarsus': {
        const scop = smooth(-0.2, -0.62, dors);
        col = new THREE.Color().copy(CL.velvet).lerp(CL.scopula, scop);
        len0 = 0.058 * (1 + 0.9 * scop); stiff = 0.85;
        break;
      }
      case 'spin': col = tmpC.copy(CL.blackWarm); len0 = 0.035; break;
    }
    // articular membranes near joints: less pile
    const jointFade = smooth(-0.3, 0.06, t) * (1 - 0.35 * smooth(0.97, 1.08, z / len));
    s.hair.copy(col).multiplyScalar(0.85 + 0.3 * n1);
    s.fur.set(len0 * 1.25 * (0.35 + 0.65 * jointFade) * (0.8 + 0.4 * n1), density, stiff);
    s.comb.set(0, -0.15, 1);
    s.furUV.set(u * around, z);
  };
  return new Surface(fn, { center: new THREE.Vector3(0, 0, len / 2) });
}

export function limbGuardSpec(kind, { palp = false } = {}) {
  const pale = CL.guardPale, orange = CL.guardOrange;
  const root = C(0x2a221b);
  const lenRange = {
    coxa: [0.18, 0.34], troch: [0.2, 0.36], femur: [0.3, 0.62], patella: [0.26, 0.52], tibia: [0.3, 0.6], meta: [0.28, 0.56], tarsus: [0.2, 0.4], spin: [0.08, 0.16],
  }[kind];
  const perArea = { coxa: 34, troch: 40, femur: 72, patella: 70, tibia: 80, meta: 72, tarsus: 50, spin: 30 }[kind] * (palp ? 1.2 : 1);
  return {
    perArea,
    spec: (u, v, s, rnd) => {
      const L = lenRange[0] + (lenRange[1] - lenRange[0]) * Math.pow(rnd(), 0.8);
      const isOrange = s.hair.r > 0.25 && s.hair.g < s.hair.r * 0.7;
      const c1 = new THREE.Color().copy(isOrange ? orange : pale).multiplyScalar(0.42 + 0.45 * rnd());
      const c0 = new THREE.Color().copy(root).lerp(c1, 0.25);
      return { len: L, width: 0.006 + 0.004 * rnd(), tilt: 0.95 + 0.5 * rnd(), curl: 0.2 + 0.3 * rnd(), spread: 0.9, c0, c1 };
    },
  };
}

// ----------------------------------------------------------------------------------------------
// Prosoma: carapace (dorsal shield) + sternum, one closed surface.
// ----------------------------------------------------------------------------------------------
const CA = BODY.carapace;
const cZc = (CA.frontZ + CA.backZ) / 2, cHalfL = (CA.frontZ - CA.backZ) / 2, cHalfW = CA.width / 2;

export function carapaceOutline(th) {
  const c = Math.cos(th), s = Math.sin(th);
  const x = cHalfW * s * (1 - 0.2 * Math.pow(Math.max(0, c), 3));
  const z = cZc + cHalfL * c * (c > 0 ? 1.0 : 0.96);
  return [x, z];
}

export function carapaceHeight(x, z, rho) {
  // dome + raised cephalic region, fovea and radial striae
  let y = CA.height * 0.78 * Math.pow(Math.max(1 - rho * rho, 0), 0.62);
  y += 0.17 * Math.exp(-(((z - 0.5) / 0.55) ** 2) - (x / 0.5) ** 2);
  y -= 0.085 * Math.exp(-(((z - BODY.fovea.z) / 0.065) ** 2) - (x / 0.2) ** 2);
  const ang = Math.atan2(x, z - BODY.fovea.z);
  const striae = Math.pow(0.5 + 0.5 * Math.cos(ang * 4.0 * 2), 8);
  y -= 0.028 * striae * smooth(0.18, 0.4, rho) * (1 - smooth(0.78, 0.98, rho));
  return y;
}

export function prosomaSurface() {
  const fn = (u, v, s) => {
    const th = u * TAU;
    const phi = v * Math.PI; // 0 = dorsal pole, PI = ventral pole
    const sp = Math.sin(phi), cp = Math.cos(phi);
    const top = cp >= 0;
    const radial = Math.pow(Math.abs(sp), top ? 0.45 : 0.4);
    const [ox, oz] = carapaceOutline(th);
    let x = ox * radial, z = cZc + (oz - cZc) * radial;
    let y;
    const rho = radial;
    if (top) {
      const rim = 0.06;
      y = rim + (carapaceHeight(x, z, rho) - 0.0) * Math.pow(cp, 0.35) * 1.0;
    } else {
      // sternum: flat, narrower (coxae occupy the sides)
      const k = 0.62 + 0.38 * Math.pow(1 - Math.min(-cp * 1.05, 1), 0.5);
      x = ox * radial * (top ? 1 : k); z = cZc - 0.05 + (oz - cZc) * radial * (top ? 1 : k * 0.95);
      y = 0.06 - 0.38 * Math.pow(-cp, 0.3);
    }
    s.p.set(x, y, z);
    const n1 = noise.noise(x * 3.3, 1.1, z * 3.3);
    if (top) {
      // black centre, orange-tan/brownish-pink border with a ragged "starburst" edge
      const ang = Math.atan2(x, z - BODY.fovea.z);
      const burst = 0.025 * Math.cos(ang * 16 + 2.0 * n1) + 0.07 * n1 + 0.04 * noise.noise(ang * 2.5, 7.7, 0);
      const rimMask = smooth(0.66, 0.86, rho + burst);
      s.hair.copy(CL.velvet).lerp(CL.carapaceRim, rimMask).lerp(CL.carapaceRimPale, rimMask * smooth(0.9, 1.0, rho) * 0.5);
      s.fur.set((0.055 + 0.06 * rimMask) * (1 - 0.6 * Math.exp(-(((z - BODY.fovea.z) / 0.08) ** 2) - (x / 0.22) ** 2)), 44, 0.8);
      const dx = x, dz = z - BODY.fovea.z;
      s.comb.set(dx, -0.2, dz).normalize(); // hairs radiate from the fovea
      s.region = rimMask;
    } else {
      s.hair.copy(CL.black).multiplyScalar(0.9 + 0.2 * n1);
      s.fur.set(0.05, 50, 0.7);
      s.comb.set(x, 0, z - 0.2);
      s.region = 0;
    }
    s.furUV.set(x, z + (top ? 0 : 10));
  };
  return new Surface(fn, { center: new THREE.Vector3(0, 0, cZc) });
}

export function prosomaGuardSpec() {
  return (u, v, s, rnd) => {
    if (v > 0.5) return rnd() < 0.4 ? { len: 0.12 + 0.12 * rnd(), width: 0.008, tilt: 1.1, curl: 0.2, spread: 0.6, c0: C(0x1a1410), c1: C(0x6a5040) } : null;
    const rim = s.region;
    if (rnd() > 0.25 + rim * 0.75) return null;
    const c1 = new THREE.Color().copy(rim > 0.4 ? CL.carapaceRimPale : CL.guardPale).multiplyScalar(0.7 + 0.4 * rnd());
    return { len: 0.1 + 0.22 * rim + 0.1 * rnd(), width: 0.008 + 0.004 * rnd(), tilt: 1.05 + 0.3 * rnd(), curl: 0.25, spread: 0.5, c0: new THREE.Color(0x2a1d14), c1 };
  };
}

// ----------------------------------------------------------------------------------------------
// Opisthosoma (abdomen). Local origin at the pedicel junction; long axis -Z.
// ----------------------------------------------------------------------------------------------
export function abdomenSurface() {
  const A = BODY.abdomen;
  const L = A.length, W = A.width / 2, H = A.height / 2;
  const circ = TAU * (W + H) / 2;
  const density = 32;
  const around = Math.round(circ * density) / density;
  const fn = (u, v, s) => {
    const th = u * TAU;
    const ct = Math.cos(th), st = Math.sin(th);
    const sv = clamp(v, 0, 1);
    let r = Math.pow(Math.sin(Math.PI * Math.pow(sv, 0.92)), 0.6);
    r *= 1 + 0.08 * Math.sin(Math.PI * sv * 1.3);
    let ry = H * r, rx = W * r;
    if (st < 0) ry *= 0.84;
    const yo = 0.12 * Math.sin(Math.PI * sv) - 0.05;
    const z = -sv * L;
    s.p.set(rx * ct, ry * st + yo, z);
    const n1 = noise.noise(ct * 2 + 5, st * 2, sv * 6) * 0.5 + 0.5;
    s.hair.copy(CL.abdomenPile).multiplyScalar(0.8 + 0.4 * n1);
    // urticating patch (dorsal posterior) is slightly browner & shorter
    const patch = smooth(0.4, 0.8, st) * smooth(0.45, 0.62, sv) * (1 - smooth(0.85, 0.95, sv));
    s.hair.lerp(C(0x2a1a12), patch * 0.5);
    s.fur.set((0.1 - 0.025 * patch) * (0.6 + 0.4 * smooth(0.0, 0.15, sv)), density, 0.6);
    s.comb.set(0, -0.35 * Math.max(0, ct * ct), -1);
    s.furUV.set(u * around, z);
    s.region = patch;
  };
  return new Surface(fn, { center: new THREE.Vector3(0, 0.05, -L / 2) });
}

export function abdomenGuardSpec() {
  return (u, v, s, rnd) => {
    const dors = Math.sin(u * TAU);
    const c1 = new THREE.Color().copy(CL.abdomenHair).lerp(CL.abdomenHairTip, Math.pow(rnd(), 2) * 0.8).multiplyScalar(0.55 + 0.45 * rnd());
    const c0 = new THREE.Color().copy(C(0x2a150b)).lerp(c1, 0.2);
    const L = (0.3 + 0.55 * Math.pow(rnd(), 1.2)) * (0.55 + 0.45 * smooth(0.02, 0.2, v)) * (dors < -0.3 ? 0.7 : 1);
    return { len: L, width: 0.007 + 0.005 * rnd(), tilt: 0.95 + 0.5 * rnd(), curl: 0.25 + 0.35 * rnd(), spread: 0.9, c0, c1 };
  };
}

// ----------------------------------------------------------------------------------------------
// Chelicera (paturon). Local origin at its base; long axis +Z; fang hinge at the front-ventral end.
// side: +1 left, -1 right (inner face is flattened against its partner)
// ----------------------------------------------------------------------------------------------
export const CHEL = { length: 0.98, width: 0.42, height: 0.56 };
export function cheliceraSurface(side) {
  const { length: L, width: Wd, height: Ht } = CHEL;
  const density = 52;
  const around = Math.round(TAU * 0.25 * density) / density;
  const fn = (u, v, s) => {
    const th = u * TAU, ct = Math.cos(th), st = Math.sin(th);
    const z = -0.12 + (L + 0.12) * v;
    const t = clamp(z / L, 0, 1);
    const pe = 2.6;
    let k = Math.pow(Math.max(1 - Math.pow(Math.abs(2 * v - 1), pe), 0.0004), 1 / pe);
    k *= 0.82 + 0.18 * smooth(0.0, 0.6, v);
    // superellipse cross-section, inner face flatter
    const inner = ct * side < 0;
    const ex = inner ? 0.35 : 0.8;
    const x = Math.sign(ct) * Math.pow(Math.abs(ct), ex) * Wd / 2 * k;
    const y = Math.sign(st) * Math.pow(Math.abs(st), 0.8) * Ht / 2 * k * (1 + 0.12 * t) - 0.08 * t * t;
    s.p.set(x, y, z);
    const n1 = noise.noise(x * 6, y * 6, z * 6 + side * 3) * 0.5 + 0.5;
    // two brownish-pink longitudinal bands on the dorsal surface (B. hamorii diagnostic)
    const bx = x * side;
    const band = st > 0.2 ? Math.max(Math.exp(-(((bx - 0.08) / 0.035) ** 2)), 0.8 * Math.exp(-(((bx + 0.07) / 0.03) ** 2))) * smooth(0.1, 0.35, t) * (1 - smooth(0.75, 0.95, t)) : 0;
    s.hair.copy(C(0x201a18)).lerp(CL.chelBand, band * 0.8).multiplyScalar(0.8 + 0.4 * n1);
    s.fur.set(0.06 * (0.5 + 0.5 * smooth(0.0, 0.2, v)), density, 0.7);
    s.comb.set(0, -0.3, 1);
    s.furUV.set(u * around, z);
  };
  return new Surface(fn, { center: new THREE.Vector3(0, 0, L / 2) });
}
export function cheliceraGuardSpec() {
  return (u, v, s, rnd) => {
    const ventral = Math.sin(u * TAU) < -0.2;
    const front = v > 0.75;
    if (!ventral && !front && rnd() > 0.35) return null;
    const c1 = new THREE.Color(ventral || front ? 0xc0703e : 0x9a8070).multiplyScalar(0.7 + 0.4 * rnd());
    return { len: 0.12 + 0.28 * rnd() * (ventral ? 1.3 : 1), width: 0.008 + 0.004 * rnd(), tilt: 0.9 + 0.4 * rnd(), curl: 0.25, spread: 0.8, c0: C(0x24170f), c1 };
  };
}

// ----------------------------------------------------------------------------------------------
// Fang: sickle-shaped, extended pose points -Y (down) and curves toward -Z (back toward mouth).
// ----------------------------------------------------------------------------------------------
export function fangGeometry() {
  const segs = 28, rad = 12, R = 0.46, arc = 1.25;
  const pos = [], nor = [], col = [], idx = [];
  const base = C(0x120806), tip = C(0x6e2a14);
  for (let i = 0; i <= segs; i++) {
    const t = i / segs, a = t * arc;
    const cz = -R * (1 - Math.cos(a)), cy = -R * Math.sin(a);
    const tz = -Math.sin(a), ty = -Math.cos(a); // tangent
    const r = 0.1 * Math.pow(1 - t, 0.85) + 0.004;
    for (let j = 0; j <= rad; j++) {
      const th = (j / rad) * TAU;
      const nx = Math.cos(th), nb = Math.sin(th);
      // binormal in the yz plane perpendicular to tangent: (ny,nz) = (tz,-ty)... use b = (-tz, ty) rotated
      const by = -tz, bz = ty;
      const rx = r * 0.78; // laterally compressed
      const x = nx * rx, y = cy + by * nb * r, z = cz + bz * nb * r;
      pos.push(x, y, z);
      const n = new THREE.Vector3(nx / 0.78, by * nb, bz * nb).normalize();
      nor.push(n.x, n.y, n.z);
      const c = new THREE.Color().copy(base).lerp(tip, smooth(0.62, 1.0, t));
      col.push(c.r, c.g, c.b);
    }
  }
  for (let i = 0; i < segs; i++) for (let j = 0; j < rad; j++) {
    const a = i * (rad + 1) + j, b = a + rad + 1;
    idx.push(a, b, a + 1, b, b + 1, a + 1);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('normal', new THREE.Float32BufferAttribute(nor, 3));
  g.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
  g.setIndex(idx);
  return g;
}

// Ocular tubercle mound (small, sparse pile)
export function tubercleSurface() {
  const fn = (u, v, s) => {
    const th = u * TAU, phi = v * Math.PI * 0.62;
    const r = Math.sin(phi);
    s.p.set(Math.cos(th) * r * 0.2, Math.cos(phi) * 0.13 - 0.03, Math.sin(th) * r * 0.2);
    s.hair.copy(CL.black);
    s.fur.set(0.02 * smooth(0.3, 1, v), 60, 0.9);
    s.comb.set(0, 0, -1);
    s.furUV.set(Math.cos(th) * r * 0.2, Math.sin(th) * r * 0.2);
  };
  return new Surface(fn, { center: new THREE.Vector3(0, -0.05, 0) });
}

// eye layout on the tubercle (tubercle-local coords): [x, y, z, radius, sx, sy, sz]
export const EYES = [
  [0.055, 0.085, 0.13, 0.052, 1, 1, 1],     // AME
  [0.135, 0.035, 0.115, 0.046, 1.15, 0.75, 1], // ALE
  [0.065, 0.1, 0.02, 0.028, 1, 1, 1],       // PME
  [0.145, 0.05, -0.03, 0.036, 1.2, 0.8, 1],  // PLE
];

// Street-furniture geometry library. Every prop: base at y=0, front = local +x.
// Each prop is a list of parts {mat, geometry}; parts using 'vcol'/'leaf' carry vertex colours.
import * as THREE from 'three';
import { mergeGeometries } from 'three/examples/jsm/utils/BufferGeometryUtils.js';
import { SIGNS, signUV } from './textures.js';

const C = {
  poleDark: 0x3d4146, galv: 0x9aa0a6, black: 0x1b1c1e, white: 0xeeeeea, concrete: 0xb9b4aa, red: 0xb3171d,
  yellow: 0xe0b021, orange: 0xd2601a, green: 0x2f5a3b, wood: 0x6b4e33, bark: 0x5b4636, leafA: 0x4f7a38, leafB: 0x3e6b34, conifer: 0x2e5232,
  housing: 0x262829, lamp: 0x4a4e53, shelter: 0x5a6068,
};

class PropBuilder {
  constructor() { this.parts = {}; }
  add(mat, geo, color, m) {
    if (m) geo.applyMatrix4(m);
    if (color !== undefined && color !== null) {
      const col = new THREE.Color(color); const n = geo.attributes.position.count;
      const a = new Float32Array(n * 3); for (let i = 0; i < n; i++) { a[i * 3] = col.r; a[i * 3 + 1] = col.g; a[i * 3 + 2] = col.b; }
      geo.setAttribute('color', new THREE.BufferAttribute(a, 3));
    }
    // normalize attribute set for merging
    for (const k of Object.keys(geo.attributes)) if (!['position', 'normal', 'uv', 'color'].includes(k)) geo.deleteAttribute(k);
    if (!geo.attributes.uv) geo.setAttribute('uv', new THREE.BufferAttribute(new Float32Array(geo.attributes.position.count * 2), 2));
    if (geo.index) geo = geo.toNonIndexed();
    (this.parts[mat] ||= []).push(geo);
    return this;
  }
  box(mat, w, h, d, x, y, z, color, ry = 0, rz = 0) { return this.add(mat, new THREE.BoxGeometry(w, h, d), color, M(x, y, z, ry, rz)); }
  cyl(mat, r0, r1, h, x, y, z, color, seg = 10, rz = 0, ry = 0) { return this.add(mat, new THREE.CylinderGeometry(r1, r0, h, seg), color, M(x, y + (rz ? 0 : h / 2), z, ry, rz)); }
  // plane facing +x (in local YZ), textured from sign atlas
  sign(idx, w, h, x, y, z, ry = 0) {
    const g = new THREE.PlaneGeometry(w, h); const [u0, v0, u1, v1] = signUV(idx);
    const uv = g.attributes.uv; for (let i = 0; i < uv.count; i++) uv.setXY(i, uv.getX(i) ? u1 : u0, uv.getY(i) ? v1 : v0);
    g.applyMatrix4(new THREE.Matrix4().makeRotationY(Math.PI / 2 + ry));
    g.translate(x, y, z);
    return this.add('sign', g, null);
  }
  build() {
    const out = [];
    for (const [mat, list] of Object.entries(this.parts)) {
      const hasColor = list.some(g => g.attributes.color);
      if (hasColor) for (const g of list) if (!g.attributes.color) { const a = new Float32Array(g.attributes.position.count * 3).fill(1); g.setAttribute('color', new THREE.BufferAttribute(a, 3)); }
      if (!hasColor) for (const g of list) if (g.attributes.color) g.deleteAttribute('color');
      const geo = mergeGeometries(list, false);
      geo.computeBoundingSphere();
      out.push({ mat, geo });
    }
    return out;
  }
}
const M = (x, y, z, ry = 0, rz = 0) => new THREE.Matrix4().compose(new THREE.Vector3(x, y, z), new THREE.Quaternion().setFromEuler(new THREE.Euler(0, ry, rz, 'YZX')), new THREE.Vector3(1, 1, 1));

function lampHead(b, x, y, len, tilt = 0.06) {
  // arm along +x from pole top, luminaire at end
  b.add('vcol', new THREE.CylinderGeometry(0.045, 0.05, len, 6), C.lamp, M(x + len / 2, y + len * tilt / 2, 0, 0, Math.PI / 2 - tilt));
  b.box('vcol', 0.75, 0.12, 0.32, x + len, y + len * tilt, 0, C.lamp);
  b.box('glow', 0.6, 0.02, 0.24, x + len, y + len * tilt - 0.07, 0);
}

function tree(b, conifer = false) {
  b.cyl('vcol', 0.18, 0.12, conifer ? 2.2 : 3.2, 0, 0, 0, C.bark, 7);
  if (conifer) {
    for (let k = 0; k < 3; k++) b.add('leaf', new THREE.ConeGeometry(2.0 - k * 0.5, 2.8, 8), C.conifer, M(0, 2.6 + k * 1.6, 0, k));
  } else {
    const ico = new THREE.IcosahedronGeometry(2.4, 1);
    const p = ico.attributes.position;
    for (let i = 0; i < p.count; i++) { const x = p.getX(i), y = p.getY(i), z = p.getZ(i); const k = 1 + 0.18 * Math.sin(x * 2.1 + z * 1.3) + 0.12 * Math.cos(y * 2.7); p.setXYZ(i, x * k, y * k * 0.9, z * k); }
    ico.computeVertexNormals();
    b.add('leaf', ico, C.leafA, M(0, 5.0, 0));
    const ico2 = new THREE.IcosahedronGeometry(1.6, 1);
    b.add('leaf', ico2, C.leafB, M(0.9, 4.0, 0.5));
    b.add('leaf', new THREE.IcosahedronGeometry(1.5, 1), C.leafB, M(-0.9, 4.3, -0.6));
  }
}

const DEFS = {
  lamp: b => { b.cyl('vcol', 0.11, 0.07, 8, 0, 0, 0, C.galv, 8); b.cyl('vcol', 0.2, 0.2, 0.5, 0, 0, 0, C.galv, 8); lampHead(b, 0, 7.9, 2.0); },
  lampHigh: b => { b.cyl('vcol', 0.14, 0.08, 12, 0, 0, 0, C.galv, 8); b.cyl('vcol', 0.24, 0.24, 0.6, 0, 0, 0, C.galv, 8); lampHead(b, 0, 11.9, 2.6); },
  lampDouble: b => {
    b.cyl('vcol', 0.15, 0.09, 12, 0, 0, 0, C.galv, 8); b.cyl('vcol', 0.26, 0.26, 0.6, 0, 0, 0, C.galv, 8);
    lampHead(b, 0, 11.9, 2.4);
    const g = new PropBuilder(); lampHead(g, 0, 11.9, 2.4);
    for (const [mat, list] of Object.entries(g.parts)) for (const geo of list) { geo.applyMatrix4(new THREE.Matrix4().makeRotationY(Math.PI)); (b.parts[mat] ||= []).push(geo); }
  },
  lampPost: b => {
    b.cyl('vcol', 0.09, 0.055, 4.0, 0, 0, 0, C.black, 8); b.cyl('vcol', 0.16, 0.12, 0.7, 0, 0, 0, C.black, 8);
    b.cyl('vcol', 0.14, 0.24, 0.12, 0, 4.0, 0, C.black, 8);
    b.cyl('glow', 0.17, 0.2, 0.5, 0, 4.12, 0, null, 8);
    b.add('vcol', new THREE.ConeGeometry(0.3, 0.3, 8), C.black, M(0, 4.77, 0));
  },
  signalPole: b => { b.cyl('vcol', 0.16, 0.12, 7.2, 0, 0, 0, C.poleDark, 10); b.cyl('vcol', 0.3, 0.3, 0.35, 0, 0, 0, C.concrete, 10); b.box('vcol', 0.25, 0.4, 0.2, -0.17, 1.2, 0, C.housing); },
  signalArm: b => { b.add('vcol', new THREE.CylinderGeometry(0.07, 0.11, 1, 8), C.poleDark, M(0.5, 0, 0, 0, Math.PI / 2)); },
  signalHead: b => {
    b.box('vcol', 0.32, 1.1, 0.38, 0, -0.55, 0, C.housing);
    b.box('vcol', 0.04, 1.3, 0.62, -0.18, -0.55, 0, C.black);
    const lights = [['sigRed', -0.2], ['sigAmber', -0.55], ['sigGreen', -0.9]];
    for (const [m, y] of lights) { b.add(m, new THREE.CircleGeometry(0.12, 12), null, M(0.165, y, 0, Math.PI / 2)); b.box('vcol', 0.18, 0.03, 0.3, 0.24, y + 0.15, 0, C.black); }
  },
  pedSignal: b => { b.box('vcol', 0.28, 0.5, 0.3, 0, -0.25, 0, C.housing); b.add('sigRed', new THREE.PlaneGeometry(0.22, 0.18), null, M(0.145, -0.14, 0, Math.PI / 2)); b.add('vcol', new THREE.PlaneGeometry(0.22, 0.18), 0x2a2a2a, M(0.145, -0.36, 0, Math.PI / 2)); },
  signStop: b => { b.cyl('vcol', 0.035, 0.035, 2.5, 0, 0, 0, C.galv, 6); b.sign(SIGNS.stop, 0.76, 0.76, 0.04, 2.3, 0); },
  signYield: b => { b.cyl('vcol', 0.035, 0.035, 2.4, 0, 0, 0, C.galv, 6); b.sign(SIGNS.yield, 0.9, 0.9, 0.04, 2.2, 0); },
  signHump: b => { b.cyl('vcol', 0.035, 0.035, 2.4, 0, 0, 0, C.galv, 6); b.sign(SIGNS.hump, 0.75, 0.75, 0.04, 2.15, 0); },
  signCrossing: b => { b.cyl('vcol', 0.035, 0.035, 2.4, 0, 0, 0, C.galv, 6); b.sign(SIGNS.crossing, 0.75, 0.75, 0.04, 2.15, 0); },
  signRail: b => { b.cyl('vcol', 0.035, 0.035, 2.4, 0, 0, 0, C.galv, 6); b.sign(SIGNS.railAdvance, 0.9, 0.9, 0.04, 2.1, 0); },
  railPost: b => { b.box('vcol', 0.08, 1.1, 0.08, 0, 0.55, 0, C.galv); },
  pierCap: b => { b.box('vcol', 1, 1, 1, 0, 0.5, 0, C.concrete); },
  pierCol: b => { b.cyl('vcol', 0.85, 0.85, 1, 0, 0, 0, C.concrete, 16); },
  pierColWide: b => { b.box('vcol', 3.2, 1, 1.1, 0, 0.5, 0, C.concrete); b.cyl('vcol', 0.55, 0.55, 1, 1.6, 0, 0, C.concrete, 10); b.cyl('vcol', 0.55, 0.55, 1, -1.6, 0, 0, C.concrete, 10); },
  bollard: b => { b.cyl('vcol', 0.085, 0.08, 0.95, 0, 0, 0, C.poleDark, 10); b.cyl('vcol', 0.087, 0.087, 0.07, 0, 0.75, 0, C.white, 10); b.add('vcol', new THREE.SphereGeometry(0.08, 10, 5, 0, Math.PI * 2, 0, Math.PI / 2), C.poleDark, M(0, 0.95, 0)); },
  bollardSign: b => { b.box('vcol', 0.32, 1.0, 0.32, 0, 0.5, 0, C.white); b.box('vcol', 0.33, 0.12, 0.33, 0, 0.9, 0, C.yellow); b.sign(SIGNS.keepRight, 0.28, 0.28, 0.17, 0.62, 0); },
  // interchange nose hardware: low TTHL-style attenuator (tip toward -x, i.e. traffic hits the taper)
  crashCushion: b => {
    b.box('vcol', 3.2, 0.34, 1.9, 0.4, 0.17, 0, C.concrete);
    b.box('vcol', 1.5, 0.6, 1.6, 1.6, 0.3, 0, C.concrete);
    b.box('vcol', 0.7, 0.2, 1.3, -1.4, 0.1, 0, C.concrete);
    b.box('vcol', 1.0, 0.66, 0.1, 1.35, 0.33, 0.83, C.yellow);   // reflective end panels
    b.box('vcol', 1.0, 0.66, 0.1, 1.35, 0.33, -0.83, C.yellow);
    b.cyl('vcol', 0.32, 0.34, 1.0, 2.1, 0, 0, C.white, 12);
    b.cyl('vcol', 0.345, 0.345, 0.16, 2.1, 0.62, 0, C.orange, 12);
  },
  signKeepRight: b => { b.cyl('vcol', 0.04, 0.04, 1.1, 0, 0, 0, C.galv, 6); b.sign(SIGNS.keepRight, 0.52, 0.52, 0.04, 0.95, 0); },
  delineator: b => { b.cyl('vcol', 0.04, 0.035, 0.9, 0, 0, 0, C.white, 6); b.cyl('vcol', 0.042, 0.042, 0.08, 0, 0.7, 0, C.yellow, 6); b.cyl('vcol', 0.12, 0.12, 0.04, 0, 0, 0, C.black, 8); },
  hydrant: b => { b.cyl('vcol', 0.13, 0.13, 0.55, 0, 0, 0, C.red, 10); b.add('vcol', new THREE.SphereGeometry(0.14, 10, 6, 0, 6.29, 0, Math.PI / 2), C.red, M(0, 0.55, 0)); b.cyl('vcol', 0.05, 0.05, 0.4, 0, 0.38, 0, C.red, 6, Math.PI / 2); b.cyl('vcol', 0.17, 0.17, 0.06, 0, 0, 0, C.red, 10); },
  bin: b => { b.cyl('vcol', 0.27, 0.3, 0.95, 0, 0, 0, C.green, 12); b.cyl('vcol', 0.31, 0.31, 0.06, 0, 0.95, 0, C.black, 12); },
  bench: b => {
    for (const z of [-0.8, 0.8]) { b.box('vcol', 0.5, 0.45, 0.06, 0, 0.225, z, C.black); b.box('vcol', 0.06, 0.45, 0.06, -0.22, 0.6, z, C.black); }
    for (let k = 0; k < 3; k++) b.box('vcol', 0.12, 0.04, 1.9, 0.15 - k * 0.15, 0.46, 0, C.wood);
    for (let k = 0; k < 2; k++) b.box('vcol', 0.04, 0.11, 1.9, -0.25, 0.62 + k * 0.17, 0, C.wood, 0, 0.15);
  },
  busShelter: b => {
    // footprint ~1.8 deep (x: -0.9..0.9, front +x), 4.2 long
    for (const z of [-2.05, 2.05]) for (const x of [-0.8, 0.75]) b.box('vcol', 0.08, 2.5, 0.08, x, 1.25, z, C.shelter);
    b.box('vcol', 2.0, 0.1, 4.4, 0, 2.55, 0, C.shelter, 0, -0.06);
    b.add('glass', new THREE.PlaneGeometry(4.1, 2.0), null, M(-0.8, 1.15, 0, Math.PI / 2));
    b.add('glass', new THREE.PlaneGeometry(1.5, 2.0), null, M(0, 1.15, -2.05));
    b.sign(SIGNS.adPanel, 1.4, 1.9, 0, 1.2, 2.06, -Math.PI / 2);
    b.box('vcol', 0.08, 1.95, 1.5, 0, 1.15, 2.1, C.shelter, Math.PI / 2);
    b.box('vcol', 0.4, 0.06, 2.6, -0.55, 0.46, 0, C.wood); b.box('vcol', 0.05, 0.42, 0.05, -0.55, 0.22, -1.1, C.black); b.box('vcol', 0.05, 0.42, 0.05, -0.55, 0.22, 1.1, C.black);
    b.cyl('vcol', 0.04, 0.04, 2.9, 0.85, 0, -2.6, C.galv, 6); b.sign(SIGNS.busStop, 0.5, 0.5, 0.88, 2.55, -2.6);
  },
  treeA: b => tree(b, false),
  treeB: b => tree(b, true),
  sleeper: b => { b.box('vcol', 2.6, 0.16, 0.26, 0, 0.08, 0, 0x6a655d); },
  catenaryMast: b => { b.box('vcol', 0.28, 7.2, 0.28, 0, 3.6, 0, C.galv); b.box('vcol', 0.6, 0.4, 0.6, 0, 0.2, 0, C.concrete); },
  catenaryArm: b => {
    b.box('vcol', 1, 0.08, 0.08, 0.5, 0, 0, C.galv);
    b.add('vcol', new THREE.CylinderGeometry(0.03, 0.03, 1.02, 5), C.galv, M(0.5, -0.25, 0, 0, Math.PI / 2 + 0.25));
  },
  fencePost: b => { b.cyl('vcol', 0.035, 0.035, 2.0, 0, 0, 0, C.galv, 6); },
  crossbuck: b => {
    b.cyl('vcol', 0.06, 0.06, 4.2, 0, 0, 0, C.white, 8);
    b.sign(SIGNS.crossbuck, 1.4, 1.4, 0.07, 3.75, 0, 0); // slightly rotated planes below
    const g = b.parts.sign.pop();
    for (const r of [Math.PI / 4, -Math.PI / 4]) { const c = g.clone(); c.translate(-0.07, -3.75, 0); c.applyMatrix4(new THREE.Matrix4().makeRotationX(r)); c.translate(0.07, 3.75, 0); b.parts.sign.push(c); }
    b.box('vcol', 0.12, 0.1, 1.3, 0.05, 2.75, 0, C.black);
    b.sign(SIGNS.rxrLights, 0.45, 0.45, 0.13, 2.75, -0.38);
    b.sign(SIGNS.rxrLights, 0.45, 0.45, 0.13, 2.75, 0.38);
    b.add('vcol', new THREE.SphereGeometry(0.12, 8, 5, 0, 6.3, 0, Math.PI / 2), C.black, M(0, 4.2, 0));
  },
  gatePost: b => { b.box('vcol', 0.45, 1.25, 0.45, 0, 0.62, 0, C.white); b.box('vcol', 0.5, 0.15, 0.5, 0, 1.25, 0, C.red); b.box('vcol', 0.3, 0.3, 0.5, -0.4, 1.0, 0, C.poleDark); },
  gateArm: b => {
    const g = new THREE.BoxGeometry(1, 0.12, 0.09); g.translate(0.5, 0, 0);
    const [u0, v0, u1, v1] = signUV(SIGNS.gateStripes);
    const p = g.attributes.position, uv = g.attributes.uv;
    for (let i = 0; i < p.count; i++) uv.setXY(i, u0 + (u1 - u0) * p.getX(i), (v0 + v1) / 2);
    b.add('sign', g, null, M(0, 1.0, 0));
  },
  stationColumn: b => { b.cyl('vcol', 0.32, 0.32, 1, 0, 0, 0, 0xd8d2c6, 14); },
  subwayEntrance: b => {
    // opening ~ 2.4 wide (z) x 5 long (x); stairs descend toward +x
    b.box('vcol', 5.2, 1.05, 0.15, 0, 0.52, 1.35, C.shelter); b.box('vcol', 5.2, 1.05, 0.15, 0, 0.52, -1.35, C.shelter); b.box('vcol', 0.15, 1.05, 2.85, -2.6, 0.52, 0, C.shelter);
    b.box('vcol', 5.0, 0.02, 2.5, 0, 0.01, 0, 0x121212);
    for (let k = 0; k < 7; k++) b.box('vcol', 0.05, 0.03, 2.4, -1.8 + k * 0.6, 0.025, 0, 0x55524c);
    b.box('glass', 5.4, 0.05, 3.0, 0, 2.6, 0);
    for (const x of [-2.6, 2.6]) for (const z of [-1.35, 1.35]) b.box('vcol', 0.08, 2.6, 0.08, x, 1.3, z, C.shelter);
    b.cyl('vcol', 0.06, 0.06, 3.6, -3.2, 0, 1.8, C.galv, 6);
    b.box('vcol', 0.62, 0.62, 0.62, -3.2, 3.9, 1.8, 0xc63b2f);
    b.sign(SIGNS.metro, 0.6, 0.6, -2.885, 3.9, 1.8); b.sign(SIGNS.metro, 0.6, 0.6, -3.515, 3.9, 1.8, Math.PI);
  },
};

let LIB = null;
export function propLibrary() {
  if (LIB) return LIB;
  LIB = {};
  for (const [k, fn] of Object.entries(DEFS)) { const b = new PropBuilder(); fn(b); LIB[k] = b.build(); }
  // generic fallback
  const fb = new PropBuilder(); fb.box('vcol', 0.3, 1, 0.3, 0, 0.5, 0, 0xff00ff); LIB.__fallback = fb.build();
  return LIB;
}

// Props that should cast shadows (others are too small to matter)
export const SHADOW_PROPS = new Set(['crashCushion', 'lamp', 'lampHigh', 'lampDouble', 'lampPost', 'signalPole', 'signalArm', 'signalHead', 'treeA', 'treeB', 'busShelter', 'pierCol', 'pierColWide', 'pierCap', 'catenaryMast', 'subwayEntrance', 'crossbuck']);

import * as THREE from 'three';
import { RoofFace } from './faces';
import { clamp } from './math';

/**
 * Shared tile geometries (built once, reused by every InstancedMesh).
 * Real-Japanese-kawara module: hiragawara ≈ 270mm wide, ~225mm exposure.
 */
export interface TileGeos {
  /** concave flat tile (hiragawara 平瓦): width X, length Z, dish facing +Y */
  hira: THREE.BufferGeometry;
  /** half-round cover tile (marugawara 丸瓦): axis along Z, wide end +Z */
  maru: THREE.BufferGeometry;
  /** decorated eave cap disc (nokimarugawara 軒丸瓦): faces +Z */
  cap: THREE.BufferGeometry;
  /** S-shaped pantile (sangawara 桟瓦): width X, length Z */
  sanga: THREE.BufferGeometry;
  /** modern flat interlocking panel: width X, length Z */
  modern: THREE.BufferGeometry;
  /** half-round ridge roll (munagawara): axis along X */
  ridge: THREE.BufferGeometry;
  /** unit cylinder (Y axis) for rafters */
  rod: THREE.BufferGeometry;
  /** wenshou hip-beast body + head */
  beastBody: THREE.BufferGeometry;
  beastHead: THREE.BufferGeometry;
  set: Set<THREE.BufferGeometry>;
}

let cache: TileGeos | null = null;

export function tileGeometries(): TileGeos {
  if (cache) return cache;

  // --- hiragawara: shallow concave slab, 270 × 300mm ---
  const hira = new THREE.BoxGeometry(0.27, 0.018, 0.3, 8, 1, 1);
  {
    const pos = hira.attributes.position as THREE.BufferAttribute;
    for (let i = 0; i < pos.count; i++) {
      const x = pos.getX(i);
      const dish = -0.016 * (1 - Math.pow(clamp(x / 0.135, -1, 1), 2));
      pos.setY(i, pos.getY(i) + dish);
    }
    hira.computeVertexNormals();
  }

  // --- marugawara: tapered half-pipe, axis Z, wide (eave) end +Z ---
  const maru = new THREE.CylinderGeometry(0.058, 0.05, 0.34, 10, 1, true, Math.PI / 2, Math.PI);
  maru.rotateX(Math.PI / 2); // Y axis → Z axis, opening faces down
  maru.computeVertexNormals();

  // --- eave cap disc ---
  const cap = new THREE.CircleGeometry(0.056, 14);

  // --- sangawara: S-curve strip, 300 × 320mm ---
  const sanga = new THREE.PlaneGeometry(0.3, 0.32, 18, 1);
  {
    const pos = sanga.attributes.position as THREE.BufferAttribute;
    for (let i = 0; i < pos.count; i++) {
      const x = pos.getX(i);
      pos.setZ(i, 0.036 * Math.sin((Math.PI * 2 * x) / 0.3));
    }
    sanga.rotateX(-Math.PI / 2); // lie flat: bump → +Y, length → Z
    sanga.computeVertexNormals();
  }

  // --- modern flat panel 600 × 450mm ---
  const modern = new THREE.BoxGeometry(0.6, 0.024, 0.45);

  // --- ridge roll: half-pipe r=140mm, axis X ---
  const ridge = new THREE.CylinderGeometry(0.14, 0.14, 0.3, 14, 1, true, 0, Math.PI);
  ridge.rotateZ(Math.PI / 2);
  ridge.computeVertexNormals();

  const rod = new THREE.CylinderGeometry(1, 1, 1, 10);

  const beastBody = new THREE.BoxGeometry(0.1, 0.13, 0.1);
  const beastHead = new THREE.ConeGeometry(0.055, 0.1, 4);

  const geos: TileGeos = { hira, maru, cap, sanga, modern, ridge, rod, beastBody, beastHead, set: new Set() };
  geos.set.add(hira).add(maru).add(cap).add(sanga).add(modern).add(ridge).add(rod).add(beastBody).add(beastHead);
  cache = geos;
  return geos;
}

/** Collectors gather instance matrices per tile kind, then become InstancedMeshes. */
export interface TileCollectors {
  hira: THREE.Matrix4[];
  hiraJit: number[];
  maru: THREE.Matrix4[];
  caps: THREE.Matrix4[];
  sanga: THREE.Matrix4[];
  sangaJit: number[];
  modern: THREE.Matrix4[];
  ridge: THREE.Matrix4[];
  ridgeScale: number[]; // uniform-ish YZ scale for hip rolls (1 = main ridge)
}

export function newCollectors(): TileCollectors {
  return { hira: [], hiraJit: [], maru: [], caps: [], sanga: [], sangaJit: [], modern: [], ridge: [], ridgeScale: [] };
}

const _m = new THREE.Matrix4();
const _q = new THREE.Quaternion();
const _s = new THREE.Vector3();
const _mb = new THREE.Matrix4();

function basisQuat(f: { x: THREE.Vector3; y: THREE.Vector3; z: THREE.Vector3 }): THREE.Quaternion {
  _mb.makeBasis(f.x, f.y, f.z);
  return _q.setFromRotationMatrix(_mb);
}

function pushFrame(
  arr: THREE.Matrix4[],
  f: { pos: THREE.Vector3; x: THREE.Vector3; y: THREE.Vector3; z: THREE.Vector3 },
  normalLift: number,
  sx = 1,
  sy = 1,
  sz = 1,
): void {
  const p = f.pos.clone().addScaledVector(f.y, normalLift);
  _s.set(sx, sy, sz);
  _m.compose(p, basisQuat(f), _s);
  arr.push(_m.clone());
}

/**
 * Hongawara-buki 本瓦葺: concave hiragawara field tiles + half-round
 * marugawara cover rolls on every column joint, round caps at the eaves.
 */
export function tileFaceHongawara(
  face: RoofFace,
  c: TileCollectors,
  rng: () => number,
  withCaps: boolean,
): { rows: number; cols: number } {
  const L = face.slopeLength();
  const rows = clamp(Math.round(L / 0.225), 2, 80);
  const margin = 0.16; // hip margins covered by hip ridge rolls
  let cols = 1;
  for (let r = 0; r < rows; r++) {
    const t = (r + 0.5) / rows;
    const wFull = face.widthAt(t);
    const wAvail = wFull - margin * 2;
    cols = Math.max(1, Math.floor(wAvail / 0.27));
    const tileW = Math.min(0.27, wAvail / cols);
    const start = (wFull - cols * tileW) / 2;
    for (let i = 0; i < cols; i++) {
      const u = (start + (i + 0.5) * tileW) / wFull;
      const f = face.frame(u, t);
      pushFrame(c.hira, f, 0.016, tileW / 0.27, 1, 1);
      c.hiraJit.push((rng() - 0.5) * 0.007);
    }
    for (let j = 0; j <= cols; j++) {
      const u = (start + j * tileW) / wFull;
      const f = face.frame(clamp(u, 0.004, 0.996), t);
      pushFrame(c.maru, f, 0.046, 1, 1, 1);
      if (withCaps && r === rows - 1) {
        // round eave cap at the down-slope end of the roll
        const capPos = f.pos.clone().addScaledVector(f.y, 0.046).addScaledVector(f.z, 0.175);
        _s.set(1, 1, 1);
        _m.compose(capPos, basisQuat(f), _s);
        c.caps.push(_m.clone());
      }
    }
  }
  return { rows, cols };
}

/** Sangawara-buki 桟瓦葺: S-pantiles combining pan + roll in one tile (from ~1650s). */
export function tileFaceSangawara(face: RoofFace, c: TileCollectors, rng: () => number): void {
  const L = face.slopeLength();
  const rows = clamp(Math.round(L / 0.24), 2, 80);
  const margin = 0.14;
  for (let r = 0; r < rows; r++) {
    const t = (r + 0.5) / rows;
    const wFull = face.widthAt(t);
    const wAvail = wFull - margin * 2;
    const cols = Math.max(1, Math.floor(wAvail / 0.3));
    const tileW = Math.min(0.3, wAvail / cols);
    const start = (wFull - cols * tileW) / 2;
    for (let i = 0; i < cols; i++) {
      const u = (start + (i + 0.5) * tileW) / wFull;
      const f = face.frame(u, t);
      pushFrame(c.sanga, f, 0.02, tileW / 0.3, 1, 1);
      c.sangaJit.push((rng() - 0.5) * 0.005);
    }
  }
}

/** Modern flat interlocking panels (和モダン styling). */
export function tileFaceModern(face: RoofFace, c: TileCollectors): void {
  const L = face.slopeLength();
  const rows = clamp(Math.ceil(L / 0.44), 1, 60);
  const margin = 0.12;
  for (let r = 0; r < rows; r++) {
    const t = (r + 0.5) / rows;
    const wFull = face.widthAt(t);
    const wAvail = wFull - margin * 2;
    const cols = Math.max(1, Math.ceil(wAvail / 0.6));
    const pw = wAvail / cols;
    const start = (wFull - cols * pw) / 2;
    for (let i = 0; i < cols; i++) {
      const u = (start + (i + 0.5) * pw) / wFull;
      const f = face.frame(u, t);
      pushFrame(c.modern, f, 0.014, (pw - 0.018) / 0.6, 1, (L / rows - 0.018) / 0.45);
    }
  }
}

/** Lay ridge rolls along a sampled 3D path (main ridge or hip ridge). */
export function ridgeAlongPath(
  pts: THREE.Vector3[],
  c: TileCollectors,
  lift: number,
  rollScale: number,
  spacing = 0.26,
): void {
  // path length
  let len = 0;
  for (let i = 1; i < pts.length; i++) len += pts[i].distanceTo(pts[i - 1]);
  const n = Math.max(1, Math.round(len / spacing));
  const up = new THREE.Vector3(0, 1, 0);
  for (let i = 0; i <= n; i++) {
    const f = (i / n) * (pts.length - 1);
    const i0 = Math.floor(f);
    const i1 = Math.min(pts.length - 1, i0 + 1);
    const fr = f - i0;
    const p = pts[i0].clone().lerp(pts[i1], fr);
    const tan = pts[i1].clone().sub(pts[i0]);
    if (tan.lengthSq() < 1e-8) tan.set(1, 0, 0);
    tan.normalize();
    const y = up.clone().addScaledVector(tan, -up.dot(tan)).normalize();
    const z = new THREE.Vector3().crossVectors(tan, y).normalize();
    _mb.makeBasis(tan, y, z);
    _q.setFromRotationMatrix(_mb);
    _s.set(1, rollScale, rollScale);
    _m.compose(p.addScaledVector(y, lift), _q, _s);
    c.ridge.push(_m.clone());
    c.ridgeScale.push(rollScale);
  }
}

/** Sample a face border (u fixed) as a path. */
export function sampleFaceEdge(face: RoofFace, u: number, n = 20): THREE.Vector3[] {
  const pts: THREE.Vector3[] = [];
  const v = new THREE.Vector3();
  for (let i = 0; i <= n; i++) {
    face.edgePoint(u, i / n, v);
    pts.push(v.clone());
  }
  return pts;
}

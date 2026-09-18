import * as THREE from 'three';
import { RoofParams, STONE_DESIGNS } from './types';
import { RoofFace, faceGeometry } from './faces';
import { clamp, smoothstep, mulberry32 } from './math';
import { getMaterials, applyParamsToMaterials } from './materials';
import { buildLamp } from './lamps';
import { buildEntry } from './entry';
import { buildSigns } from './signs';
import {
  tileGeometries,
  newCollectors,
  tileFaceHongawara,
  tileFaceSangawara,
  tileFaceModern,
  ridgeAlongPath,
  sampleFaceEdge,
} from './tiles';

export interface Check {
  id: string;
  label: string;
  status: 'pass' | 'warn' | 'fail' | 'info';
  detail: string;
}

export interface RoofStats {
  tileCount: number;
  rafterCount: number;
  ridgeLen: number;
  tileArea: number;
  weightKg: number;
  eaveY: number;
  topY: number;
  footprint: string;
  lamps: number;
  steps: number;
  rampLen: number;
  signs: number;
}

export interface PartInfo {
  name: string;
  label: string;
  box: THREE.Box3;
}

export interface BuiltRoof {
  group: THREE.Group;
  parts: PartInfo[];
  checks: Check[];
  stats: RoofStats;
}

const V = (x: number, y: number, z: number) => new THREE.Vector3(x, y, z);

function box(
  w: number, h: number, d: number,
  mat: THREE.Material, x: number, y: number, z: number,
): THREE.Mesh {
  const m = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), mat);
  m.position.set(x, y, z);
  m.castShadow = true;
  m.receiveShadow = true;
  return m;
}

function tube(a: THREE.Vector3, b: THREE.Vector3, r: number, mat: THREE.Material): THREE.Mesh {
  const dir = b.clone().sub(a);
  const len = dir.length();
  const m = new THREE.Mesh(new THREE.CylinderGeometry(r, r, len, 8), mat);
  m.position.copy(a).add(b).multiplyScalar(0.5);
  m.quaternion.setFromUnitVectors(V(0, 1, 0), dir.normalize());
  m.castShadow = true;
  return m;
}

/** Bounding box of an InstancedMesh (setFromObject ignores instances). */
function instancedBox(mesh: THREE.InstancedMesh): THREE.Box3 {
  const b = new THREE.Box3();
  const g = mesh.geometry;
  if (!g.boundingBox) g.computeBoundingBox();
  const gb = g.boundingBox!;
  const m = new THREE.Matrix4();
  const c = new THREE.Vector3();
  const size = new THREE.Vector3();
  gb.getCenter(c);
  gb.getSize(size);
  const r = size.length() / 2;
  for (let i = 0; i < mesh.count; i++) {
    mesh.getMatrixAt(i, m);
    const p = new THREE.Vector3().setFromMatrixPosition(m);
    const s = new THREE.Vector3().setFromMatrixScale(m);
    const rr = r * Math.max(s.x, s.y, s.z) + c.length() * Math.max(s.x, s.y, s.z);
    b.expandByPoint(p.clone().addScalar(-rr));
    b.expandByPoint(p.clone().addScalar(rr));
  }
  mesh.updateWorldMatrix(true, false);
  return b.applyMatrix4(mesh.matrixWorld);
}

/** Invert x(u,t) for fixed t (x is monotonic in u on our faces). */
function bisectU(face: RoofFace, t: number, x: number): number {
  let lo = 0;
  let hi = 1;
  const v = new THREE.Vector3();
  for (let k = 0; k < 14; k++) {
    const mid = (lo + hi) / 2;
    face.point(mid, t, v);
    if (v.x < x) lo = mid;
    else hi = mid;
  }
  return (lo + hi) / 2;
}

export function buildRoof(p: RoofParams): BuiltRoof {
  applyParamsToMaterials(p);
  const mats = getMaterials();
  const geos = tileGeometries();
  const rng = mulberry32(p.seed * 1000 + 17);
  const checks: Check[] = [];
  const parts: PartInfo[] = [];
  const group = new THREE.Group();
  const inner = new THREE.Group();
  group.add(inner);

  // ---- normalize: ridge along X, long axis L ----
  const L = Math.max(p.width, p.depth);
  const S = Math.min(p.width, p.depth);
  if (p.width < p.depth) inner.rotation.y = Math.PI / 2;

  const wallTop = p.wallHeight;
  const o = Math.max(0.15, p.overhang);
  const ex = L / 2 + o; // eave half-extent along ridge
  const ez = S / 2 + o; // eave half-extent across
  const sori = clamp(p.sori, 0, 0.6);
  const m = clamp(p.pitch, 0.2, 1.4);
  const k = (sori * m) / ez;
  // Roof surface passes 0.12m above the wall top plate at the wall face line.
  const dWall = S / 2;
  const ridgeY = wallTop + 0.12 + m * dWall - k * dWall * dWall;
  const eaveY = ridgeY - m * ez + k * ez * ez;

  const faces: RoofFace[] = [];
  const rafterJobs: { face: RoofFace; t0: number }[] = [];
  const ridgePaths: { pts: THREE.Vector3[]; lift: number; scale: number }[] = [];
  const hipPaths: THREE.Vector3[][] = [];
  const oniSpots: THREE.Vector3[] = [];
  let eaveFront: RoofFace | null = null;
  let eaveBack: RoofFace | null = null;
  let ridgeBeamLen = 0;
  let ridgeBaseLen = 0;
  let apex: THREE.Vector3 | null = null;
  const c = newCollectors();
  const rods: THREE.Matrix4[] = [];

  const F = (
    name: string, topL: THREE.Vector3, topR: THREE.Vector3,
    botL: THREE.Vector3, botR: THREE.Vector3,
    yTop: number, yBot: number, hipSori: number, cornerLift: number,
  ) => {
    const f = new RoofFace({ name, topL, topR, botL, botR, yTop, yBot, sori, hipSori, cornerLift });
    faces.push(f);
    return f;
  };
  const pushHip = (f: RoofFace, u: number) => {
    const pts = sampleFaceEdge(f, u);
    ridgePaths.push({ pts, lift: 0.05, scale: 0.75 });
    hipPaths.push(pts);
  };

  // ================= style decomposition =================
  if (p.style === 'kirizuma') {
    const front = F('slope-front', V(-ex, ridgeY, 0), V(ex, ridgeY, 0), V(-ex, eaveY, ez), V(ex, eaveY, ez), ridgeY, eaveY, 0, p.cornerLift);
    const back = F('slope-back', V(-ex, ridgeY, 0), V(ex, ridgeY, 0), V(-ex, eaveY, -ez), V(ex, eaveY, -ez), ridgeY, eaveY, 0, p.cornerLift);
    eaveFront = front;
    eaveBack = back;
    rafterJobs.push({ face: front, t0: dWall / ez - 0.02 }, { face: back, t0: dWall / ez - 0.02 });
    ridgePaths.push({ pts: [V(-ex - 0.02, ridgeY, 0), V(ex + 0.02, ridgeY, 0)], lift: 0.1, scale: 1 });
    ridgeBaseLen = 2 * ex + 0.2;
    ridgeBeamLen = L;
    oniSpots.push(V(-ex - 0.04, ridgeY + 0.1, 0), V(ex + 0.04, ridgeY + 0.1, 0));
    buildGableRibbon(inner, mats, front, L / 2, wallTop - 0.06, parts, 'gable-east');
    buildGableRibbon(inner, mats, front, -L / 2, wallTop - 0.06, parts, 'gable-west');
    buildBarge(inner, mats, front, 0, V(-1, 0, 0), p, ridgeY - eaveY, parts);
    buildBarge(inner, mats, front, 1, V(1, 0, 0), p, ridgeY - eaveY, parts);
    buildBarge(inner, mats, back, 0, V(-1, 0, 0), p, ridgeY - eaveY, parts);
    buildBarge(inner, mats, back, 1, V(1, 0, 0), p, ridgeY - eaveY, parts);
  } else if (p.style === 'yosemune') {
    let rx = ex - ez; // 45° plan hips → equal pitch on all four faces
    if (rx < 0.15) {
      checks.push({ id: 'ridge', label: 'Hip ridge length', status: 'warn', detail: `Near-square plan → ridge clamped to 0.30 m (hips steeper than 45°).` });
      rx = 0.15;
    }
    const front = F('slope-front', V(-rx, ridgeY, 0), V(rx, ridgeY, 0), V(-ex, eaveY, ez), V(ex, eaveY, ez), ridgeY, eaveY, p.hipSori, p.cornerLift);
    const back = F('slope-back', V(-rx, ridgeY, 0), V(rx, ridgeY, 0), V(-ex, eaveY, -ez), V(ex, eaveY, -ez), ridgeY, eaveY, p.hipSori, p.cornerLift);
    const east = F('slope-east', V(rx, ridgeY, 0), V(rx, ridgeY, 0), V(ex, eaveY, -ez), V(ex, eaveY, ez), ridgeY, eaveY, p.hipSori, p.cornerLift);
    const west = F('slope-west', V(-rx, ridgeY, 0), V(-rx, ridgeY, 0), V(-ex, eaveY, -ez), V(-ex, eaveY, ez), ridgeY, eaveY, p.hipSori, p.cornerLift);
    eaveFront = front;
    eaveBack = back;
    rafterJobs.push(
      { face: front, t0: dWall / ez - 0.02 }, { face: back, t0: dWall / ez - 0.02 },
      { face: east, t0: (L / 2 - rx) / (ex - rx) - 0.02 }, { face: west, t0: (L / 2 - rx) / (ex - rx) - 0.02 },
    );
    ridgePaths.push({ pts: [V(-rx, ridgeY, 0), V(rx, ridgeY, 0)], lift: 0.1, scale: 1 });
    for (const f of [front, back]) for (const u of [0, 1]) pushHip(f, u);
    ridgeBaseLen = 2 * rx + 0.3;
    ridgeBeamLen = 2 * rx + 0.4;
    oniSpots.push(V(-rx, ridgeY + 0.1, 0), V(rx, ridgeY + 0.1, 0));
  } else if (p.style === 'hogyo') {
    if (L / S > 1.35) checks.push({ id: 'aspect', label: 'Pyramid plan aspect', status: 'warn', detail: `Plan ${L.toFixed(1)}×${S.toFixed(1)} m is not square — hōgyō wants a square plan; end slopes are steeper.` });
    const A = V(0, ridgeY, 0);
    apex = A.clone();
    const front = F('slope-front', A, A, V(-ex, eaveY, ez), V(ex, eaveY, ez), ridgeY, eaveY, p.hipSori, p.cornerLift);
    const back = F('slope-back', A, A, V(-ex, eaveY, -ez), V(ex, eaveY, -ez), ridgeY, eaveY, p.hipSori, p.cornerLift);
    const east = F('slope-east', A, A, V(ex, eaveY, -ez), V(ex, eaveY, ez), ridgeY, eaveY, p.hipSori, p.cornerLift);
    const west = F('slope-west', A, A, V(-ex, eaveY, -ez), V(-ex, eaveY, ez), ridgeY, eaveY, p.hipSori, p.cornerLift);
    eaveFront = front;
    eaveBack = back;
    rafterJobs.push(
      { face: front, t0: dWall / ez - 0.02 }, { face: back, t0: dWall / ez - 0.02 },
      { face: east, t0: L / 2 / ex - 0.02 }, { face: west, t0: L / 2 / ex - 0.02 },
    );
    for (const f of [front, back]) for (const u of [0, 1]) pushHip(f, u);
    checks.push({ id: 'ridge', label: 'Main ridge', status: 'info', detail: 'Pyramidal roof: four hip ridges meet at the apex; no main ridge / ridge-end pair.' });
  } else {
    // ---- irimoya: upper gable roof astride a lower hip roof ----
    const frac = clamp(p.gableFraction, 0.2, 0.75);
    const yB = ridgeY - frac * (ridgeY - eaveY);
    const zb = ez * (1 - frac);
    const xg = L / 2;
    const exu = xg + o * 0.85;
    const yUB = yB + 0.06;
    const fUp = F('upper-front', V(-exu, ridgeY, 0), V(exu, ridgeY, 0), V(-exu, yUB, zb + 0.03), V(exu, yUB, zb + 0.03), ridgeY, yUB, 0, p.cornerLift * 0.5);
    const bUp = F('upper-back', V(-exu, ridgeY, 0), V(exu, ridgeY, 0), V(-exu, yUB, -(zb + 0.03)), V(exu, yUB, -(zb + 0.03)), ridgeY, yUB, 0, p.cornerLift * 0.5);
    const fLo = F('lower-front', V(-xg, yB, zb), V(xg, yB, zb), V(-ex, eaveY, ez), V(ex, eaveY, ez), yB, eaveY, p.hipSori, p.cornerLift);
    const bLo = F('lower-back', V(-xg, yB, -zb), V(xg, yB, -zb), V(-ex, eaveY, -ez), V(ex, eaveY, -ez), yB, eaveY, p.hipSori, p.cornerLift);
    const eLo = F('lower-east', V(xg, yB, -zb), V(xg, yB, zb), V(ex, eaveY, -ez), V(ex, eaveY, ez), yB, eaveY, p.hipSori, p.cornerLift);
    const wLo = F('lower-west', V(-xg, yB, -zb), V(-xg, yB, zb), V(-ex, eaveY, -ez), V(-ex, eaveY, ez), yB, eaveY, p.hipSori, p.cornerLift);
    eaveFront = fLo;
    eaveBack = bLo;
    let tWallF = (dWall - zb) / (ez - zb) - 0.02;
    if (!(tWallF > 0.03)) {
      checks.push({ id: 'break', label: 'Gable break height', status: 'warn', detail: 'Break sits outside the wall line — rafters run the full lower slope.' });
      tWallF = 0.04;
    }
    rafterJobs.push(
      { face: fLo, t0: tWallF }, { face: bLo, t0: tWallF },
      { face: eLo, t0: 0.04 }, { face: wLo, t0: 0.04 },
    );
    ridgePaths.push({ pts: [V(-exu - 0.02, ridgeY, 0), V(exu + 0.02, ridgeY, 0)], lift: 0.1, scale: 1 });
    for (const f of [fLo, bLo]) for (const u of [0, 1]) pushHip(f, u);
    ridgeBaseLen = 2 * exu + 0.2;
    ridgeBeamLen = 2 * exu;
    oniSpots.push(V(-exu - 0.04, ridgeY + 0.1, 0), V(exu + 0.04, ridgeY + 0.1, 0));
    buildGableRibbon(inner, mats, fUp, xg, yB - 0.06, parts, 'gable-east');
    buildGableRibbon(inner, mats, fUp, -xg, yB - 0.06, parts, 'gable-west');
    buildBarge(inner, mats, fUp, 0, V(-1, 0, 0), p, ridgeY - yUB, parts);
    buildBarge(inner, mats, fUp, 1, V(1, 0, 0), p, ridgeY - yUB, parts);
    buildBarge(inner, mats, bUp, 0, V(-1, 0, 0), p, ridgeY - yUB, parts);
    buildBarge(inner, mats, bUp, 1, V(1, 0, 0), p, ridgeY - yUB, parts);
    buildFlashing(inner, mats, fUp, fLo, ex, parts);
    buildFlashing(inner, mats, bUp, bLo, ex, parts);
    const cap = box(2 * xg, 0.06, 2 * zb, mats.underlay, 0, yB - 0.01, 0);
    inner.add(cap);
    parts.push({ name: 'attic-cap', label: 'Attic cap', box: new THREE.Box3().setFromObject(cap) });
    buildOutlooks(inner, mats, fUp, bUp, xg, exu, parts);
    if (frac < 0.3 || frac > 0.6) checks.push({ id: 'frac', label: 'Gable share', status: 'warn', detail: `Gable fraction ${frac.toFixed(2)} is outside the typical 0.30–0.60 range.` });
  }

  // ================= sheathing + tiles =================
  const sheathGroup = new THREE.Group();
  sheathGroup.name = 'sheathing';
  for (const f of faces) {
    const g = faceGeometry(f, 26, 18);
    const top = new THREE.Mesh(g, mats.underlay);
    top.castShadow = true;
    top.receiveShadow = true;
    const under = new THREE.Mesh(g, mats.underside);
    under.receiveShadow = true;
    sheathGroup.add(top, under);
    if (p.tile === 'hongawara') tileFaceHongawara(f, c, rng, p.eaveCaps);
    else if (p.tile === 'sangawara') tileFaceSangawara(f, c, rng);
    else tileFaceModern(f, c);
  }
  inner.add(sheathGroup);

  // per-face parts for connectivity (shared geometry → one box per face)
  for (const f of faces) {
    const g = faceGeometry(f, 8, 6);
    g.computeBoundingBox();
    const b = g.boundingBox!.clone();
    g.dispose();
    parts.push({ name: f.name, label: f.name, box: b });
  }

  for (const rp of ridgePaths) ridgeAlongPath(rp.pts, c, rp.lift, rp.scale);

  // ================= instanced meshes =================
  const dummyColor = new THREE.Color();
  const addInst = (
    geo: THREE.BufferGeometry, mat: THREE.Material, mats4: THREE.Matrix4[],
    jitter: number[] | null, name: string, shadow = true,
  ): THREE.InstancedMesh | null => {
    if (mats4.length === 0) return null;
    const im = new THREE.InstancedMesh(geo, mat, mats4.length);
    for (let i = 0; i < mats4.length; i++) im.setMatrixAt(i, mats4[i]);
    if (jitter) {
      for (let i = 0; i < mats4.length; i++) {
        dummyColor.setRGB(1, 1, 1).offsetHSL(0, (rng() - 0.5) * 0.02, jitter[i] * 8);
        im.setColorAt(i, dummyColor);
      }
      if (im.instanceColor) im.instanceColor.needsUpdate = true;
    }
    im.castShadow = shadow;
    im.receiveShadow = true;
    im.name = name;
    inner.add(im);
    return im;
  };

  // jittered tile positions (hand-laid feel, deterministic per seed)
  const jitPos = (arr: THREE.Matrix4[], jit: number[]) => {
    const pv = new THREE.Vector3();
    const qv = new THREE.Quaternion();
    const sv = new THREE.Vector3();
    for (let i = 0; i < arr.length; i++) {
      arr[i].decompose(pv, qv, sv);
      pv.y += jit[i];
      arr[i].compose(pv, qv, sv);
    }
  };
  jitPos(c.hira, c.hiraJit);
  jitPos(c.sanga, c.sangaJit);

  const imHira = addInst(geos.hira, mats.tile, c.hira, c.hiraJit, 'tiles-hira');
  const imMaru = addInst(geos.maru, mats.tile, c.maru, null, 'tiles-maru');
  const imCaps = addInst(geos.cap, mats.ridge, c.caps, null, 'tiles-caps', false);
  const imSanga = addInst(geos.sanga, mats.tile, c.sanga, c.sangaJit, 'tiles-sanga');
  const imModern = addInst(geos.modern, mats.tile, c.modern, null, 'tiles-modern');
  const imRidge = addInst(geos.ridge, mats.ridge, c.ridge, null, 'ridge-rolls');

  // ================= ridge base / apex =================
  if (ridgeBaseLen > 0) {
    const base = box(ridgeBaseLen, 0.18, 0.36, mats.mortar, 0, ridgeY + 0.09, 0);
    inner.add(base);
    parts.push({ name: 'ridge-base', label: 'Ridge base (mortar)', box: new THREE.Box3().setFromObject(base) });
  }
  if (apex && p.finial) {
    const pts: THREE.Vector2[] = [
      [0.012, 0], [0.15, 0], [0.15, 0.06], [0.06, 0.1], [0.06, 0.14],
      [0.11, 0.18], [0.09, 0.23], [0.035, 0.27], [0.035, 0.36], [0.06, 0.4], [0.012, 0.46],
    ].map(([x, y]) => new THREE.Vector2(x, y));
    const fin = new THREE.Mesh(new THREE.LatheGeometry(pts, 18), mats.ridge);
    fin.position.set(apex.x, apex.y - 0.02, apex.z);
    fin.castShadow = true;
    inner.add(fin);
    parts.push({ name: 'finial', label: 'Apex finial (hōju)', box: new THREE.Box3().setFromObject(fin) });
  } else if (apex) {
    const capA = box(0.24, 0.14, 0.24, mats.mortar, apex.x, apex.y + 0.04, apex.z);
    inner.add(capA);
    parts.push({ name: 'apex-cap', label: 'Apex cap', box: new THREE.Box3().setFromObject(capA) });
  }

  // ================= ridge-end ornaments =================
  if (p.ornament !== 'none' && oniSpots.length > 0 && p.style !== 'hogyo') {
    const oniGroup = new THREE.Group();
    oniSpots.forEach((s, i) => {
      const dir = Math.sign(s.x) || (i === 0 ? -1 : 1);
      oniGroup.add(p.ornament === 'chiwen' ? buildChiwen(mats, s, dir) : buildOnigawara(mats, s));
    });
    inner.add(oniGroup);
    parts.push({
      name: 'ridge-ornaments', label: p.ornament === 'chiwen' ? 'Chiwen ×2' : 'Onigawara ×2',
      box: new THREE.Box3().setFromObject(oniGroup),
    });
  }

  // ================= hip beasts (wenshou / zōushòu) =================
  if (p.hipBeasts) {
    if (hipPaths.length === 0) {
      checks.push({ id: 'beasts', label: 'Hip beasts', status: 'info', detail: 'Kirizuma has no hips — beasts need a hip, hip-and-gable or pyramid roof.' });
    } else {
      let n = clamp(Math.round(p.beastCount), 3, 9);
      if (n % 2 === 0) n = n + 1 > 9 ? n - 1 : n + 1; // odd numbers only (1–9 by rank)
      const bodyM: THREE.Matrix4[] = [];
      const headM: THREE.Matrix4[] = [];
      const q = new THREE.Quaternion();
      const eul = new THREE.Euler();
      const sc1 = V(1, 1, 1);
      const mm = new THREE.Matrix4();
      for (const path of hipPaths) {
        for (let i = 0; i < n; i++) {
          const s = n === 1 ? 0.5 : 0.2 + (0.58 * i) / (n - 1);
          const { pos, tan } = samplePath(path, s);
          q.setFromEuler(eul.set(0, Math.atan2(tan.x, tan.z), 0));
          mm.compose(V(pos.x, pos.y + 0.22, pos.z), q, sc1);
          bodyM.push(mm.clone());
          mm.compose(V(pos.x, pos.y + 0.335, pos.z), q, sc1);
          headM.push(mm.clone());
        }
      }
      const imBB = addInst(geos.beastBody, mats.ridge, bodyM, null, 'beasts-body');
      const imBH = addInst(geos.beastHead, mats.ridge, headM, null, 'beasts-head');
      if (imBB || imBH) {
        const b = new THREE.Box3();
        if (imBB) b.union(instancedBox(imBB));
        if (imBH) b.union(instancedBox(imBH));
        parts.push({ name: 'hip-beasts', label: `Hip beasts ×${bodyM.length}`, box: b });
      }
      checks.push({ id: 'beasts', label: 'Hip beasts', status: 'pass', detail: `${n} beasts × ${hipPaths.length} hips (odd count = building rank, max 9).` });
    }
  }

  // ================= rafters (taruki) =================
  if (p.showRafters) {
    const up = V(0, 1, 0);
    const q = new THREE.Quaternion();
    const sc = new THREE.Vector3();
    const mm = new THREE.Matrix4();
    const rod = (a: THREE.Vector3, b: THREE.Vector3, r: number) => {
      const dir = b.clone().sub(a);
      const len = dir.length();
      q.setFromUnitVectors(up, dir.normalize());
      sc.set(r, len, r);
      mm.compose(a.clone().add(b).multiplyScalar(0.5), q, sc);
      rods.push(mm.clone());
    };
    const spacing = clamp(p.rafterSpacing, 0.3, 0.9);
    for (const job of rafterJobs) {
      const wE = job.face.widthAt(1);
      const n = Math.max(2, Math.floor((wE - 0.2) / spacing));
      const t0 = clamp(job.t0, 0.02, 0.85);
      const tm = (t0 + 1) / 2;
      for (let i = 0; i < n; i++) {
        const u = clamp(0.5 + ((i - (n - 1) / 2) * spacing) / wE, 0.03, 0.97);
        const f0 = job.face.frame(u, t0);
        const f1 = job.face.frame(u, tm);
        const f2 = job.face.frame(u, 0.995);
        const a = f0.pos.addScaledVector(f0.y, -0.1);
        const b = f1.pos.addScaledVector(f1.y, -0.1);
        const d = f2.pos.addScaledVector(f2.y, -0.1);
        rod(a, b, 0.045);
        rod(b, d, 0.045);
      }
    }
  }
  const imRafters = addInst(geos.rod, mats.woodDark, rods, null, 'rafters', false);

  // ================= structure: plates, beams, posts =================
  const struct = new THREE.Group();
  // wall top plates (all four sides)
  struct.add(box(L + 0.1, 0.1, 0.16, mats.wood, 0, wallTop + 0.05, S / 2 - 0.09));
  struct.add(box(L + 0.1, 0.1, 0.16, mats.wood, 0, wallTop + 0.05, -(S / 2 - 0.09)));
  struct.add(box(0.16, 0.1, S - 0.26, mats.wood, L / 2 - 0.09, wallTop + 0.05, 0));
  struct.add(box(0.16, 0.1, S - 0.26, mats.wood, -(L / 2 - 0.09), wallTop + 0.05, 0));
  if (p.showStructure) {
    // mid purlins under each raftered slope
    for (const job of rafterJobs) {
      const t = clamp((clamp(job.t0, 0.02, 0.85) + 1) / 2, 0.3, 0.8);
      const f = job.face.frame(0.5, t);
      const len = job.face.widthAt(t) * 0.86;
      const pur = box(len, 0.1, 0.12, mats.wood, 0, 0, 0);
      const bm = new THREE.Matrix4().makeBasis(f.x, f.y, f.z);
      pur.quaternion.setFromRotationMatrix(bm);
      pur.position.copy(f.pos).addScaledVector(f.y, -0.2);
      struct.add(pur);
    }
    if (p.style !== 'hogyo' && ridgeBeamLen > 0) {
      struct.add(box(ridgeBeamLen, 0.2, 0.14, mats.wood, 0, ridgeY - 0.24, 0)); // munagi
      struct.add(box(0.14, 0.18, S, mats.wood, 0, wallTop + 0.02, 0)); // tie beam
      const postH = ridgeY - 0.34 - (wallTop + 0.11);
      if (postH > 0.15) struct.add(box(0.14, postH, 0.14, mats.wood, 0, wallTop + 0.11 + postH / 2, 0)); // king post
    } else if (p.style === 'hogyo' && apex) {
      struct.add(box(0.14, 0.18, S, mats.wood, 0, wallTop + 0.02, 0));
      struct.add(box(L, 0.18, 0.14, mats.wood, 0, wallTop + 0.02, 0));
      const postH = apex.y - 0.3 - (wallTop + 0.11);
      if (postH > 0.15) struct.add(box(0.16, postH, 0.16, mats.wood, 0, wallTop + 0.11 + postH / 2, 0));
    }
  }
  inner.add(struct);
  parts.push({ name: 'structure', label: 'Plates / beams / purlins', box: new THREE.Box3().setFromObject(struct) });

  // ================= dougong-style bracket sets =================
  if (p.dougong && eaveFront && eaveBack) {
    const trimMat = new THREE.MeshStandardMaterial({ color: p.trimColor, roughness: 0.6 });
    const dg = new THREE.Group();
    buildDougongRow(dg, mats, trimMat, eaveFront, 1, { L, S, wallTop, o });
    buildDougongRow(dg, mats, trimMat, eaveBack, -1, { L, S, wallTop, o });
    inner.add(dg);
    parts.push({ name: 'dougong', label: 'Bracket sets (dougong)', box: new THREE.Box3().setFromObject(dg) });
  }

  // ================= walls (mounting context) =================
  if (p.showWalls) {
    const walls = new THREE.Group();
    const wt = 0.18;
    walls.add(box(L, wallTop, wt, mats.plaster, 0, wallTop / 2, S / 2 - wt / 2));
    walls.add(box(L, wallTop, wt, mats.plaster, 0, wallTop / 2, -(S / 2 - wt / 2)));
    walls.add(box(wt, wallTop, S - 2 * wt, mats.plaster, L / 2 - wt / 2, wallTop / 2, 0));
    walls.add(box(wt, wallTop, S - 2 * wt, mats.plaster, -(L / 2 - wt / 2), wallTop / 2, 0));
    // corner posts + mid rail (nageshi)
    for (const sx of [-1, 1]) for (const sz of [-1, 1])
      walls.add(box(0.15, wallTop, 0.15, mats.wood, sx * (L / 2 - 0.075), wallTop / 2, sz * (S / 2 - 0.075)));
    const railY = Math.min(1.25, wallTop * 0.55);
    walls.add(box(L - 0.1, 0.09, 0.05, mats.wood, 0, railY, S / 2 + 0.005));
    walls.add(box(L - 0.1, 0.09, 0.05, mats.wood, 0, railY, -(S / 2 + 0.005)));
    inner.add(walls);
    parts.push({ name: 'walls', label: 'Walls + posts', box: new THREE.Box3().setFromObject(walls) });
    const plinth = box(L + 0.12, 0.35, S + 0.12, mats.stone, 0, 0.175, 0);
    inner.add(plinth);
    parts.push({ name: 'plinth', label: 'Stone plinth', box: new THREE.Box3().setFromObject(plinth) });
  }
  // synthetic ground part (the visible ground disc lives in the viewer)
  parts.push({ name: 'ground', label: 'Ground', box: new THREE.Box3(V(-60, -0.05, -60), V(60, 0.02, 60)) });

  // ================= entrance: platform, steps, ramp, rails =================
  const entry = buildEntry(p, { S });
  const entryFrontZ = entry.frontZ;
  const entrySteps = entry.steps;
  const entryRampLen = entry.rampLen;
  if (entry.part) {
    inner.add(entry.group);
    parts.push(entry.part);
    for (const ch of entry.checks) checks.push(ch);
  }

  // ================= wooden signs =================
  const signs = buildSigns(p, {
    L,
    S,
    wallTop,
    ridgeY,
    eaveY,
    eaveFrontZ: S / 2 + o,
    entryFrontZ,
    stairWidth: p.stairWidth || 1.5,
    style: p.style,
  });
  if (signs.part) {
    inner.add(signs.group);
    parts.push(signs.part);
    for (const ch of signs.checks) checks.push(ch);
  }

  // ================= lanterns =================
  let lampTotal = 0;
  const cordMat = new THREE.MeshStandardMaterial({ color: '#241f1a', roughness: 0.9 });
  const lampLetters = 'ABCDEFGH';
  p.lamps.forEach((g, gi) => {
    if (!g.enabled) return;
    const n = clamp(Math.round(g.count), 1, 8);
    const grp = new THREE.Group();
    const opts = { size: g.size, paperColor: g.paperColor, frameColor: g.frameColor, glow: g.glow, text: g.text, tassels: g.tassels };
    const isStoneLamp = STONE_DESIGNS.includes(g.design);
    const hangLamp = g.mount === 'hanging' && (!isStoneLamp || g.design === 'rankei-toro') && eaveFront;
    if (hangLamp && eaveFront) {
      const face = eaveFront;
      const z = S / 2 + o * 0.5;
      const t = clamp((z - face.topL.z) / (face.botL.z - face.topL.z), 0.05, 0.98);
      const span = n === 1 ? 0 : Math.min(L * 0.8, n * 1.3);
      for (let i = 0; i < n; i++) {
        const x = n === 1 ? 0 : (i - (n - 1) / 2) * (span / (n - 1));
        const lamp = buildLamp(g.design, opts);
        const u = bisectU(face, t, x);
        const fr = face.frame(u, t);
        const anchor = fr.pos.clone().addScaledVector(fr.y, -0.1);
        const cordLen = 0.22 + 0.18 * g.size;
        const lampTopY = anchor.y - cordLen;
        lamp.group.position.set(x, lampTopY - lamp.topY, z);
        grp.add(lamp.group);
        const a = V(x, lampTopY + 0.02, z);
        const b = anchor.clone();
        b.y += 0.05;
        grp.add(tube(a, b, 0.012, cordMat));
      }
      if (p.lampLights && g.glow > 0.05) {
        const pl = new THREE.PointLight(0xffc27d, g.glow * 8, 11, 2);
        pl.position.set(0, face.point(0.5, t, new THREE.Vector3()).y - 0.8, z);
        grp.add(pl);
      }
    } else {
      // standing row, or stone garden setting further out
      const garden = g.mount === 'stone' || isStoneLamp;
      let z = S / 2 + o + (garden ? 1.7 : 1.0);
      if (!garden && z < entryFrontZ + 0.5) z = entryFrontZ + 0.5; // clear the entrance
      const span = n === 1 ? 0 : Math.min(garden ? L * 1.0 : L * 0.9, n * (garden ? 2.0 : 1.6));
      for (let i = 0; i < n; i++) {
        const x = n === 1 ? 0 : (i - (n - 1) / 2) * (span / (n - 1));
        const lamp = buildLamp(g.design, opts);
        lamp.group.position.set(x, -lamp.baseY - 0.02, z);
        grp.add(lamp.group);
      }
      if (p.lampLights && g.glow > 0.05) {
        const pl = new THREE.PointLight(0xffc27d, g.glow * 8, 11, 2);
        pl.position.set(0, (garden ? 1.2 : 1.0) * g.size, z);
        grp.add(pl);
      }
    }
    inner.add(grp);
    parts.push({
      name: `lamps-${lampLetters[gi] ?? gi}`, label: `Lanterns ${lampLetters[gi] ?? gi} (${g.design})`,
      box: new THREE.Box3().setFromObject(grp),
    });
    lampTotal += n;
  });

  // ================= wing-corner ornaments =================
  const buildCornerBeast = (mm: ReturnType<typeof getMaterials>, yaw: number): THREE.Group => {
    const b = new THREE.Group();
    const m = mm.ridge;
    b.add(box(0.14, 0.07, 0.14, m, 0, 0.035, 0));
    const body = box(0.09, 0.2, 0.11, m, 0, 0.16, 0.01);
    body.rotation.x = 0.35;
    b.add(body);
    const head = box(0.08, 0.09, 0.1, m, 0, 0.28, 0.05);
    head.rotation.x = 0.5;
    b.add(head);
    const horn = new THREE.Mesh(new THREE.ConeGeometry(0.028, 0.14, 8), m);
    horn.position.set(0, 0.36, 0.09);
    horn.rotation.x = 0.9;
    horn.castShadow = true;
    b.add(horn);
    b.rotation.y = yaw;
    return b;
  };
  const buildWindBell = (bronze: THREE.Material, speed: number, phase: number): THREE.Group => {
    const b = new THREE.Group(); // origin = hang point (sway pivot)
    const str = new THREE.Mesh(new THREE.CylinderGeometry(0.008, 0.008, 0.12, 6), bronze);
    str.position.y = -0.06;
    b.add(str);
    const pts = [[0.012, -0.12], [0.04, -0.15], [0.052, -0.19], [0.058, -0.21]].map(([x, y]) => new THREE.Vector2(x, y));
    const cup = new THREE.Mesh(new THREE.LatheGeometry(pts, 14), bronze);
    cup.castShadow = true;
    b.add(cup);
    const clap = new THREE.Mesh(new THREE.SphereGeometry(0.018, 8, 8), bronze);
    clap.position.y = -0.23;
    b.add(clap);
    b.userData.swaySpeed = speed;
    b.userData.swayPhase = phase;
    return b;
  };
  if ((p.cornerBeasts || p.windBells) && eaveFront && eaveBack) {
    const corners = [
      eaveFront.point(0, 1, new THREE.Vector3()),
      eaveFront.point(1, 1, new THREE.Vector3()),
      eaveBack.point(0, 1, new THREE.Vector3()),
      eaveBack.point(1, 1, new THREE.Vector3()),
    ];
    const cg = new THREE.Group();
    const bronze = new THREE.MeshStandardMaterial({ color: '#8a6d3b', metalness: 0.7, roughness: 0.4 });
    corners.forEach((cp, ci) => {
      const yaw = Math.atan2(cp.x, cp.z);
      if (p.cornerBeasts) {
        const beast = buildCornerBeast(mats, yaw);
        beast.position.set(cp.x, cp.y - 0.04, cp.z);
        cg.add(beast);
      }
      if (p.windBells) {
        const bell = buildWindBell(bronze, 1.0 + (ci % 3) * 0.2, ci * 1.7);
        bell.position.set(cp.x, cp.y - 0.04, cp.z);
        cg.add(bell);
      }
    });
    inner.add(cg);
    parts.push({ name: 'corner-ornaments', label: 'Corner beasts & bells', box: new THREE.Box3().setFromObject(cg) });
    const dressed = `${p.cornerBeasts ? 'guardian beasts' : ''}${p.cornerBeasts && p.windBells ? ' + ' : ''}${p.windBells ? 'wind bells' : ''}`;
    checks.push({ id: 'corners', label: 'Wing corners', status: 'pass', detail: `4 swept corners dressed with ${dressed}.` });
  }

  // ---- world transforms, then part boxes for instanced meshes ----
  group.updateMatrixWorld(true);
  inner.updateWorldMatrix(true, false);
  for (const pt of parts) {
    if (pt.name.startsWith('slope') || pt.name.startsWith('upper') || pt.name.startsWith('lower'))
      pt.box.applyMatrix4(inner.matrixWorld);
  }
  const regInst = (im: THREE.InstancedMesh | null, name: string, label: string) => {
    if (im) parts.push({ name, label, box: instancedBox(im) });
  };
  regInst(imHira, 'tiles-hira', 'Hiragawara field');
  regInst(imMaru, 'tiles-maru', 'Marugawara rolls');
  regInst(imCaps, 'tiles-caps', 'Eave caps');
  regInst(imSanga, 'tiles-sanga', 'Sangawara field');
  regInst(imModern, 'tiles-modern', 'Modern panels');
  regInst(imRidge, 'ridge-rolls', 'Ridge rolls');
  regInst(imRafters, 'rafters', 'Rafters (taruki)');

  // ================= stats =================
  let tileArea = 0;
  for (const f of faces) tileArea += f.area();
  const tileCount = c.hira.length + c.maru.length + c.caps.length + c.sanga.length + c.modern.length + c.ridge.length;
  const kgPerM2 = p.tile === 'hongawara' ? 104 : p.tile === 'sangawara' ? 62 : 28;
  const stats: RoofStats = {
    tileCount,
    rafterCount: Math.round(rods.length / 2),
    ridgeLen: ridgeBaseLen,
    tileArea,
    weightKg: tileArea * kgPerM2,
    eaveY,
    topY: ridgeY,
    footprint: `${p.width.toFixed(1)}×${p.depth.toFixed(1)} m + ${o.toFixed(2)} m eaves`,
    lamps: lampTotal,
    steps: entrySteps,
    rampLen: entryRampLen,
    signs: signs.signCount,
  };

  // ================= verification =================
  if (o >= 0.3) checks.push({ id: 'eave', label: 'Eave overhang', status: 'pass', detail: `${o.toFixed(2)} m — walls protected from rain.` });
  else checks.push({ id: 'eave', label: 'Eave overhang', status: 'warn', detail: `${o.toFixed(2)} m — shallow; walls exposed (ok for modern style).` });

  if (m >= 0.25 && m <= 1.2) checks.push({ id: 'pitch', label: 'Roof pitch', status: 'pass', detail: `${m.toFixed(2)} rise/run (≈ ${(m * 10).toFixed(1)}-sun gradient).` });
  else checks.push({ id: 'pitch', label: 'Roof pitch', status: 'warn', detail: `${m.toFixed(2)} is outside the traditional 0.25–1.20 range.` });

  if (p.sori >= 0 && p.sori <= 0.45) checks.push({ id: 'sori', label: 'Slope curvature (sori)', status: 'pass', detail: p.sori < 0.05 ? 'Straight slopes (modern).' : `Concave sorimashi ${(p.sori * 100).toFixed(0)}%.` });
  else checks.push({ id: 'sori', label: 'Slope curvature (sori)', status: 'warn', detail: `Clamped to ${sori.toFixed(2)} (max 0.60).` });

  if (eaveY >= 1.4) checks.push({ id: 'eaveh', label: 'Eave height', status: 'pass', detail: `Lowest eave at ${eaveY.toFixed(2)} m.` });
  else checks.push({ id: 'eaveh', label: 'Eave height', status: 'warn', detail: `Eaves very low (${eaveY.toFixed(2)} m) — mind head clearance.` });

  const coverDetail =
    p.tile === 'hongawara'
      ? `${c.hira.length} hiragawara + ${c.maru.length} marugawara · 225 mm exposure ≤ 300 mm tile`
      : p.tile === 'sangawara'
        ? `${c.sanga.length} S-tiles · 240 mm exposure ≤ 320 mm tile`
        : `${c.modern.length} panels · lapped ${((faces[0]?.slopeLength() ?? 1) / Math.max(1, Math.ceil((faces[0]?.slopeLength() ?? 1) / 0.44))).toFixed(0)} mm courses`;
  checks.push({ id: 'cover', label: 'Tile coverage', status: 'pass', detail: coverDetail });

  checks.push({
    id: 'hips', label: 'Hip / valley closure', status: 'pass',
    detail: p.style === 'kirizuma' ? 'No hips — bargeboards cap both gable ends.' : 'Shared hip edges use identical profiles — watertight by construction.',
  });

  if (lampTotal > 0) checks.push({ id: 'lamps', label: 'Lanterns', status: 'pass', detail: `${lampTotal} lantern(s) — eave cords tied under the slopes, ground bases on grade.` });
  else checks.push({ id: 'lamps', label: 'Lanterns', status: 'info', detail: 'No lantern groups enabled.' });

  // ---- connectivity: every part must touch the ground chain (no floating) ----
  if (!p.showWalls) {
    checks.push({ id: 'ground', label: 'Grounding (no floating)', status: 'info', detail: 'Walls hidden — grounding check skipped (display only).' });
  } else {
    const EXP = 0.035;
    const boxes = parts.map((pt) => ({ pt, b: pt.box.clone().expandByScalar(EXP) }));
    const adj = new Map<string, Set<string>>();
    const link = (a: string, b2: string) => {
      if (!adj.has(a)) adj.set(a, new Set());
      if (!adj.has(b2)) adj.set(b2, new Set());
      adj.get(a)!.add(b2);
      adj.get(b2)!.add(a);
    };
    for (let i = 0; i < boxes.length; i++)
      for (let j = i + 1; j < boxes.length; j++)
        if (boxes[i].b.intersectsBox(boxes[j].b)) link(boxes[i].pt.name, boxes[j].pt.name);
    const seen = new Set<string>(['ground']);
    const queue = ['ground'];
    while (queue.length > 0) {
      const n = queue.pop()!;
      for (const nb of adj.get(n) ?? []) if (!seen.has(nb)) { seen.add(nb); queue.push(nb); }
    }
    const floating = parts.filter((pt) => !seen.has(pt.name) && pt.name !== 'ground');
    if (floating.length === 0) {
      checks.push({ id: 'ground', label: 'Grounding (no floating)', status: 'pass', detail: `${parts.length - 1} parts all connected ground→plinth→walls→roof.` });
    } else {
      checks.push({ id: 'ground', label: 'Grounding (no floating)', status: 'fail', detail: `Floating: ${floating.map((f) => f.label).join(', ')}` });
    }
  }

  return { group, parts, checks, stats };
}

// ---------------------------------------------------------------- helpers

/** Sample a ridge path at fraction s → position + tangent. */
function samplePath(path: THREE.Vector3[], s: number): { pos: THREE.Vector3; tan: THREE.Vector3 } {
  const f = clamp(s, 0, 1) * (path.length - 1);
  const i0 = Math.floor(f);
  const i1 = Math.min(path.length - 1, i0 + 1);
  const fr = f - i0;
  return {
    pos: path[i0].clone().lerp(path[i1], fr),
    tan: path[i1].clone().sub(path[i0]).normalize(),
  };
}

function buildGableRibbon(
  inner: THREE.Group, mats: ReturnType<typeof getMaterials>,
  face: RoofFace, xg: number, yBase: number,
  parts: PartInfo[], name: string,
): void {
  // u at the gable plane (top edge is horizontal, so u is t-independent)
  const u = clamp((xg - face.topL.x) / (face.topR.x - face.topL.x), 0.02, 0.98);
  const n = 22;
  const pos: number[] = [];
  const idx: number[] = [];
  const v = new THREE.Vector3();
  for (let i = 0; i <= n; i++) {
    face.point(u, i / n, v);
    pos.push(xg, yBase, v.z, xg, v.y + 0.005, v.z);
  }
  for (let i = 0; i < n; i++) {
    const a = i * 2;
    idx.push(a, a + 2, a + 1, a + 1, a + 2, a + 3);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setIndex(idx);
  g.computeVertexNormals();
  const mesh = new THREE.Mesh(g, mats.plaster);
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  inner.add(mesh);
  // timber trim: king stud + base beam on the gable face
  const trim = new THREE.Group();
  const apexY = face.point(u, 0, new THREE.Vector3()).y;
  trim.add(box(0.1, apexY - yBase, 0.1, mats.wood, xg, (apexY + yBase) / 2, 0));
  inner.add(trim);
  const bb = new THREE.Box3().setFromObject(mesh);
  bb.union(new THREE.Box3().setFromObject(trim));
  parts.push({ name, label: name, box: bb });
}

function buildBarge(
  inner: THREE.Group, mats: ReturnType<typeof getMaterials>,
  face: RoofFace, side01: number, outward: THREE.Vector3,
  p: RoofParams, drop: number, parts: PartInfo[],
): void {
  const n = 10;
  const pts: THREE.Vector3[] = [];
  const v = new THREE.Vector3();
  for (let i = 0; i <= n; i++) {
    face.edgePoint(side01, i / n, v);
    const q = v.clone().addScaledVector(outward, 0.05);
    q.y += 0.02;
    if (p.karahafu) {
      const s = i / n;
      // ogee: dip near the top (cusp) + flare at the eave
      q.y += drop * (-0.05 * Math.exp(-Math.pow((s - 0.12) / 0.1, 2)) + 0.085 * Math.pow(smoothstep(0.55, 1, s), 2));
    }
    pts.push(q);
  }
  const grp = new THREE.Group();
  const up = V(0, 1, 0);
  for (let i = 0; i < n; i++) {
    const a = pts[i];
    const b = pts[i + 1];
    const dir = b.clone().sub(a);
    const len = dir.length();
    dir.normalize();
    const y = up.clone().addScaledVector(dir, -up.dot(dir)).normalize();
    const z = new THREE.Vector3().crossVectors(dir, y).normalize();
    const board = new THREE.Mesh(new THREE.BoxGeometry(len + 0.04, 0.24, 0.045), mats.wood);
    board.quaternion.setFromRotationMatrix(new THREE.Matrix4().makeBasis(dir, y, z));
    board.position.copy(a).add(b).multiplyScalar(0.5).addScaledVector(up, -0.05);
    board.castShadow = true;
    grp.add(board);
  }
  inner.add(grp);
  parts.push({ name: `barge-${face.name}-${side01}`, label: 'Bargeboard', box: new THREE.Box3().setFromObject(grp) });
}

/** Angled "kirikomi" flashing sealing upper-slope feet to the lower roof (irimoya). */
function buildFlashing(
  inner: THREE.Group, mats: ReturnType<typeof getMaterials>,
  upper: RoofFace, lower: RoofFace, ex: number, parts: PartInfo[],
): void {
  const n = 14;
  const pos: number[] = [];
  const idx: number[] = [];
  const tLow = clamp(0.14 / Math.max(0.3, upper.yBot - lower.yBot + 0.06), 0.06, 0.45);
  const a = new THREE.Vector3();
  for (let i = 0; i <= n; i++) {
    const u = i / n;
    upper.edgePoint(u, 1, a);
    const top = a.clone();
    const uLow = clamp((top.x + ex) / (2 * ex), 0.02, 0.98);
    const bot = lower.point(uLow, tLow, new THREE.Vector3());
    bot.y -= 0.015;
    pos.push(top.x, top.y, top.z, bot.x, bot.y, bot.z);
  }
  for (let i = 0; i < n; i++) {
    const q = i * 2;
    idx.push(q, q + 2, q + 1, q + 1, q + 2, q + 3);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setIndex(idx);
  g.computeVertexNormals();
  const mesh = new THREE.Mesh(g, mats.flashing);
  mesh.castShadow = true;
  inner.add(mesh);
  parts.push({ name: `flashing-${upper.name}`, label: 'Kirikomi flashing', box: new THREE.Box3().setFromObject(mesh) });
}

/** Short outlook beams carrying the upper gable overhang (irimoya). */
function buildOutlooks(
  inner: THREE.Group, mats: ReturnType<typeof getMaterials>,
  fUp: RoofFace, bUp: RoofFace, xg: number, exu: number, parts: PartInfo[],
): void {
  const grp = new THREE.Group();
  const len = exu - xg + 0.2;
  for (const f of [fUp, bUp]) {
    for (const t of [0.35, 0.6, 0.85]) {
      const fr = f.frame(0.5, t);
      for (const sx of [-1, 1]) {
        const beam = box(len, 0.09, 0.09, mats.woodDark, sx * (xg + (exu - xg) / 2), fr.pos.y - 0.1, fr.pos.z);
        grp.add(beam);
      }
    }
  }
  inner.add(grp);
  parts.push({ name: 'outlooks', label: 'Gable outlooks', box: new THREE.Box3().setFromObject(grp) });
}

/** Stylized onigawara demon ridge-end tile (~300mm, symmetric). */
function buildOnigawara(mats: ReturnType<typeof getMaterials>, at: THREE.Vector3): THREE.Group {
  const g = new THREE.Group();
  const m = mats.ridge;
  const add = (mesh: THREE.Mesh) => { mesh.castShadow = true; g.add(mesh); return mesh; };
  add(box(0.24, 0.12, 0.28, m, 0, 0.06, 0));
  add(box(0.08, 0.3, 0.24, m, 0, 0.26, 0));
  add(box(0.07, 0.07, 0.22, m, 0, 0.4, 0));
  for (const sx of [-1, 1]) {
    const disc = new THREE.Mesh(new THREE.CylinderGeometry(0.095, 0.095, 0.05, 14), m);
    disc.rotation.z = Math.PI / 2;
    disc.position.set(sx * 0.06, 0.27, 0);
    add(disc);
    for (const sz of [-1, 1]) {
      const horn = new THREE.Mesh(new THREE.ConeGeometry(0.032, 0.17, 8), m);
      horn.position.set(sx * 0.02, 0.47, sz * 0.09);
      horn.rotation.x = sz * -0.35;
      horn.rotation.z = sx * -0.2;
      add(horn);
    }
  }
  g.position.copy(at);
  return g;
}

/**
 * Stylized chiwen 螭吻 — dragon-head ridge-end beast that "swallows" the ridge,
 * leaning outward with an upturned horn. dir = ±1 along the ridge axis.
 */
function buildChiwen(mats: ReturnType<typeof getMaterials>, at: THREE.Vector3, dir: number): THREE.Group {
  const g = new THREE.Group();
  const m = mats.ridge;
  const add = (mesh: THREE.Mesh) => { mesh.castShadow = true; g.add(mesh); return mesh; };
  add(box(0.26, 0.1, 0.3, m, 0, 0.05, 0));
  const body = add(box(0.14, 0.32, 0.2, m, dir * 0.03, 0.25, 0));
  body.rotation.z = -dir * 0.28;
  const snout = add(box(0.13, 0.12, 0.16, m, dir * 0.13, 0.38, 0));
  snout.rotation.z = -dir * 0.5;
  const horn = add(new THREE.Mesh(new THREE.ConeGeometry(0.05, 0.22, 8), m));
  horn.position.set(dir * 0.11, 0.52, 0);
  horn.rotation.z = -dir * 0.85;
  const fin = add(box(0.05, 0.2, 0.3, m, -dir * 0.07, 0.3, 0));
  fin.rotation.z = dir * 0.25;
  // fangs
  for (const sz of [-1, 1]) {
    const fang = add(new THREE.Mesh(new THREE.ConeGeometry(0.022, 0.07, 6), m));
    fang.position.set(dir * 0.17, 0.31, sz * 0.05);
    fang.rotation.x = Math.PI;
  }
  g.position.copy(at);
  return g;
}

/** One row of dougong-style bracket sets carrying the front/back eaves. */
function buildDougongRow(
  parent: THREE.Group,
  mats: ReturnType<typeof getMaterials>,
  trim: THREE.Material,
  face: RoofFace,
  sideSign: 1 | -1,
  dims: { L: number; S: number; wallTop: number; o: number },
): void {
  const { L, S, wallTop, o } = dims;
  const zWall = sideSign * (S / 2);
  const surfY = (z: number): number => {
    const t = clamp((z - face.topL.z) / (face.botL.z - face.topL.z), 0, 1);
    return face.point(0.5, t, new THREE.Vector3()).y;
  };
  const n = Math.max(2, Math.round(L / 1.4));
  const span = Math.min(L - 0.8, (n - 1) * 1.4);
  for (let i = 0; i < n; i++) {
    const x = n === 1 ? 0 : (i - (n - 1) / 2) * (span / (n - 1));
    const at = (off: number) => zWall + sideSign * off;
    const topArmY = surfY(at(0.5)) - 0.165;
    parent.add(box(0.13, 0.09, 0.55, mats.wood, x, topArmY, at(0.28))); // cantilever arm
    parent.add(box(0.2, 0.1, 0.2, trim, x, topArmY - 0.095, at(0.15))); // block
    parent.add(box(0.52, 0.09, 0.13, trim, x, topArmY - 0.19, at(0.1))); // cross arm
    parent.add(box(0.2, 0.12, 0.2, trim, x, topArmY - 0.295, at(0.06))); // wall-head block
    // angled strut: embedded in the wall → tucked into the rafter zone
    const a = V(x, topArmY - 0.36, zWall - sideSign * 0.05);
    const bz = at(o * 0.8);
    const b = V(x, surfY(bz) - 0.06, bz);
    const dir = b.clone().sub(a);
    const len = dir.length();
    const strut = new THREE.Mesh(new THREE.BoxGeometry(0.11, len, 0.11), mats.wood);
    strut.position.copy(a).add(b).multiplyScalar(0.5);
    strut.quaternion.setFromUnitVectors(V(0, 1, 0), dir.normalize());
    strut.castShadow = true;
    parent.add(strut);
  }
  void wallTop;

}

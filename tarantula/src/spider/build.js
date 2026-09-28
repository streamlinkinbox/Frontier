import * as THREE from 'three';
import { mulberry32 } from '../core/noise.js';
import { BODY, LEGS, PALP } from './anatomy.js';
import {
  limbSegmentSurface, limbGuardSpec, prosomaSurface, prosomaGuardSpec, abdomenSurface, abdomenGuardSpec,
  cheliceraSurface, cheliceraGuardSpec, fangGeometry, tubercleSurface, EYES, carapaceHeight, CHEL,
} from './parts.js';
import {
  createFurBaseMaterial, createFurShellMaterial, createHairStrandMaterial, createFangMaterial, createEyeMaterial, createChitinMaterial, furUniforms,
} from './materials.js';
import { Limb } from './Limb.js';

export const MAX_SHELLS = 40;
const DEG = Math.PI / 180;
const LEG_KINDS = ['coxa', 'troch', 'femur', 'patella', 'tibia', 'meta', 'tarsus'];
const PALP_KINDS = ['coxa', 'troch', 'femur', 'patella', 'tibia', 'tarsus'];

export function createMaterials() {
  return {
    base: createFurBaseMaterial(),
    shell: createFurShellMaterial(),
    strand: createHairStrandMaterial(),
    fang: createFangMaterial(),
    eye: createEyeMaterial(),
    chitin: createChitinMaterial(),
  };
}

const identity = new THREE.Matrix4();
function makePart(surface, nu, nv, strandCount, strandSpec, mats, rnd, name) {
  const g = new THREE.Group();
  g.name = name;
  const geo = surface.build(nu, nv);
  const base = new THREE.Mesh(geo, mats.base);
  base.castShadow = true; base.receiveShadow = true; base.frustumCulled = false;
  base.name = name + ':base';
  const shells = new THREE.InstancedMesh(geo, mats.shell, MAX_SHELLS);
  for (let i = 0; i < MAX_SHELLS; i++) shells.setMatrixAt(i, identity);
  shells.count = furUniforms.uShells.value;
  shells.castShadow = false; shells.receiveShadow = true; shells.frustumCulled = false;
  shells.userData.isShell = true;
  g.add(base, shells);
  if (strandCount > 0) {
    const sg = surface.buildStrands(strandCount, rnd, strandSpec);
    const strands = new THREE.Mesh(sg, mats.strand);
    strands.receiveShadow = true; strands.frustumCulled = false;
    strands.userData.isStrands = true;
    g.add(strands);
  }
  return g;
}

export function buildTarantula(mats, { hairDensity = 1 } = {}) {
  const rnd = mulberry32(90210);
  const root = new THREE.Group(); root.name = 'TarantulaRoot';
  const body = new THREE.Group(); body.name = 'Body';
  root.add(body);

  // ------------------------------------------------------------------ prosoma
  const pro = prosomaSurface();
  const prosoma = makePart(pro, 96, 72, Math.round(900 * hairDensity), prosomaGuardSpec(), mats, rnd, 'prosoma');
  body.add(prosoma);

  // ocular tubercle + eyes
  const tub = makePart(tubercleSurface(), 32, 16, 0, null, mats, rnd, 'tubercle');
  const tubY = 0.06 + carapaceHeight(0, BODY.ocular.z, 0.35) * 0.98;
  tub.position.set(0, tubY, BODY.ocular.z);
  tub.rotation.x = -0.12;
  body.add(tub);
  const eyeGeo = new THREE.SphereGeometry(1, 24, 16);
  for (const [x, y, z, r, sx, sy, sz] of EYES) {
    for (const s of [1, -1]) {
      const e = new THREE.Mesh(eyeGeo, mats.eye);
      e.position.set(x * s, y, z);
      e.scale.set(r * sx, r * sy, r * sz);
      e.castShadow = false; e.receiveShadow = true;
      tub.add(e);
    }
  }

  // ------------------------------------------------------------------ chelicerae + fangs
  const chelicerae = [], fangs = [];
  const fangGeo = fangGeometry();
  for (const side of [1, -1]) {
    const pivot = new THREE.Group();
    pivot.position.set(0.215 * side, 0.02, 1.0);
    pivot.userData.restPitch = 0.42; // pointing forward-down
    pivot.rotation.x = pivot.userData.restPitch;
    const chel = makePart(cheliceraSurface(side), 40, 28, Math.round(260 * hairDensity), cheliceraGuardSpec(), mats, rnd, 'chelicera');
    pivot.add(chel);
    const fangPivot = new THREE.Group();
    fangPivot.position.set(-0.04 * side, -0.25, CHEL.length * 0.86);
    fangPivot.rotation.x = Math.PI / 2; // folded back into the cheliceral groove
    const fang = new THREE.Mesh(fangGeo, mats.fang);
    fang.castShadow = true; fang.receiveShadow = true; fang.frustumCulled = false;
    fangPivot.add(fang);
    pivot.add(fangPivot);
    body.add(pivot);
    chelicerae.push(pivot); fangs.push(fangPivot);
  }

  // ------------------------------------------------------------------ abdomen
  const abdPivot = new THREE.Group();
  abdPivot.position.set(0, BODY.pedicel.y, BODY.pedicel.z);
  const abdAnchor = new THREE.Group();
  abdAnchor.position.set(0, 0.06, -0.12);
  abdPivot.add(abdAnchor);
  const abdSurf = abdomenSurface();
  const abdomen = makePart(abdSurf, 96, 64, Math.round(4200 * hairDensity), abdomenGuardSpec(), mats, rnd, 'abdomen');
  abdAnchor.add(abdomen);
  // pedicel
  const ped = makePart(limbSegmentSurface('spin', 0.3, 0.22), 18, 8, 0, null, mats, rnd, 'pedicel');
  ped.rotation.y = Math.PI; ped.position.set(0, 0, 0.12);
  abdPivot.add(ped);
  // spinnerets: posterior lateral (3 podomeres, finger-like) + posterior median (small)
  const spinnerets = [];
  const A = BODY.abdomen;
  for (const side of [1, -1]) {
    let parent = new THREE.Group();
    parent.position.set(0.2 * side, -0.42, -A.length * 0.93);
    parent.rotation.set(-0.55, Math.PI + 0.28 * side, 0);
    abdAnchor.add(parent);
    const chain = [parent];
    const lens = [0.26, 0.2, 0.24], rads = [0.1, 0.085, 0.07];
    for (let i = 0; i < 3; i++) {
      const seg = makePart(limbSegmentSurface('spin', lens[i], rads[i]), 16, 10, Math.round(14 * hairDensity), limbGuardSpec('spin').spec, mats, rnd, 'pls');
      parent.add(seg);
      const next = new THREE.Group();
      next.position.set(0, 0, lens[i]);
      next.rotation.x = -0.22;
      seg.add(next);
      parent = next; chain.push(next);
    }
    spinnerets.push(chain);
    const pms = makePart(limbSegmentSurface('spin', 0.12, 0.05), 12, 6, 0, null, mats, rnd, 'pms');
    pms.position.set(0.07 * side, -0.52, -A.length * 0.9);
    pms.rotation.set(-0.6, Math.PI + 0.1 * side, 0);
    abdAnchor.add(pms);
    chain.side = side; chain.pms = pms;
  }
  body.add(abdPivot);

  // ------------------------------------------------------------------ legs + palps
  const legs = [];
  const buildLimb = (def, side, isPalp) => {
    const kinds = isPalp ? PALP_KINDS : LEG_KINDS;
    const limb = new Limb({
      lengths: def.seg, radii: def.rad,
      socket: new THREE.Vector3(def.socket[0] * side, def.socket[1], def.socket[2]),
      side, yaw0: def.yaw0 * DEG, isPalp,
      coxaPitch: isPalp ? -0.55 : -0.3,
      patTibFlex: isPalp ? 0.12 : 0.08,
    });
    for (let i = 0; i < kinds.length; i++) {
      const kind = kinds[i];
      const surf = limbSegmentSurface(kind, def.seg[i], def.rad[i], { palp: isPalp, seed: rnd() * 100 });
      const gs = limbGuardSpec(kind, { palp: isPalp });
      const count = Math.round(surf.area * gs.perArea * hairDensity);
      const nv = Math.max(10, Math.round(def.seg[i] * 16));
      const seg = makePart(surf, 28, nv, count, gs.spec, mats, rnd, `${isPalp ? 'palp' : 'leg' + def.id}${side > 0 ? 'L' : 'R'}:${kind}`);
      body.add(seg);
      limb.segments.push(seg);
    }
    return limb;
  };
  for (let li = 0; li < 4; li++) {
    for (const side of [1, -1]) {
      const limb = buildLimb(LEGS[li], side, false);
      limb.legIndex = li;
      limb.reachFrac = LEGS[li].reach;
      legs.push(limb);
    }
  }
  const palps = [1, -1].map((side) => buildLimb(PALP, side, true));

  return { root, body, prosoma, chelicerae, fangs, abdPivot, abdAnchor, spinnerets, legs, palps };
}

// Toggle the number of rendered fur shells at runtime
export function setShellCount(root, n) {
  furUniforms.uShells.value = n;
  root.traverse((o) => { if (o.userData.isShell) o.count = n; });
}

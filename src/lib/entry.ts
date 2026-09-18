import * as THREE from 'three';
import { RoofParams } from './types';
import { getMaterials } from './materials';
import type { Check, PartInfo } from './buildRoof';

export interface EntryResult {
  group: THREE.Group;
  part: PartInfo | null;
  checks: Check[];
  /** tread count (0 when no steps) */
  steps: number;
  /** ramp slope length (0 when no ramp) */
  rampLen: number;
  /** max +z extent in the inner frame (for lamp clearance) */
  frontZ: number;
}

const RUN = 0.28; // tread depth (IRC min 254 mm + nosing)
const RISER_MAX = 0.18; // target max riser (IRC max 197 mm)
const RAIL_H = 0.9; // handrail height above nosing (IRC/ADA 860–970 mm)
const RAMP_W = 1.2; // ramp clear width (ADA min 900 mm)

function emesh(geo: THREE.BufferGeometry, mat: THREE.Material, x = 0, y = 0, z = 0): THREE.Mesh {
  const m = new THREE.Mesh(geo, mat);
  m.position.set(x, y, z);
  m.castShadow = true;
  m.receiveShadow = true;
  return m;
}

/** Sloped beam between two points (stringers, handrails). */
function beam(a: THREE.Vector3, b: THREE.Vector3, w: number, d: number, mat: THREE.Material): THREE.Mesh {
  const dir = b.clone().sub(a);
  const len = dir.length();
  const m = new THREE.Mesh(new THREE.BoxGeometry(w, len, d), mat);
  m.position.copy(a).add(b).multiplyScalar(0.5);
  m.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.normalize());
  m.castShadow = true;
  m.receiveShadow = true;
  return m;
}

/**
 * Entrance: raised-floor platform + stone/timber steps and/or a sideways
 * access ramp with handrails, plus an entry door slab on the wall face.
 * Built in the inner frame (front = +z, wall face at z = S/2).
 */
export function buildEntry(p: RoofParams, dims: { S: number }): EntryResult {
  const g = new THREE.Group();
  const checks: Check[] = [];
  const { S } = dims;
  const zWall = S / 2;
  const empty: EntryResult = { group: g, part: null, checks, steps: 0, rampLen: 0, frontZ: zWall };
  if (p.entryType === 'none') return empty;

  const mats = getMaterials();
  const stone = mats.stone;
  const wood = mats.wood;
  const woodDark = mats.woodDark;
  const treadMat = p.stairMaterial === 'stone' ? stone : wood;
  const fh = THREE.MathUtils.clamp(p.floorHeight, 0.15, 0.9);
  const hasSteps = p.entryType === 'steps' || p.entryType === 'both';
  const hasRamp = p.entryType === 'ramp' || p.entryType === 'both';
  const w = THREE.MathUtils.clamp(p.stairWidth, 0.9, 2.4);

  // ---- platform (top landing; ADA wants ≥1.5 m deep at a ramp head) ----
  const platD = hasRamp ? 1.5 : 1.2;
  const platW = hasSteps && hasRamp ? w + 1.2 : hasSteps ? w + 0.6 : 2.0;
  const platCx = 0;
  const zPlatB = zWall - 0.1; // embedded in the plinth → connectivity
  const zPlatF = zPlatB + platD;
  const zPlatC = (zPlatB + zPlatF) / 2;
  const stepsCx = hasSteps && hasRamp ? platCx - platW / 2 + 0.35 + w / 2 : 0;

  if (p.stairMaterial === 'stone') {
    g.add(emesh(new THREE.BoxGeometry(platW, fh + 0.05, platD), stone, platCx, (fh - 0.05) / 2, zPlatC));
    // cap slab with nosing, top flush with fh
    g.add(emesh(new THREE.BoxGeometry(platW + 0.08, 0.07, platD + 0.08), stone, platCx, fh - 0.035, zPlatC + 0.02));
  } else {
    // recessed base + deck planks with shadow gaps
    g.add(emesh(new THREE.BoxGeometry(platW - 0.1, fh, platD - 0.1), woodDark, platCx, (fh - 0.05) / 2, zPlatC));
    const nPl = Math.max(3, Math.round(platD / 0.18));
    for (let i = 0; i < nPl; i++) {
      const pz = zPlatB + (i + 0.5) * (platD / nPl);
      g.add(emesh(new THREE.BoxGeometry(platW, 0.05, platD / nPl - 0.012), wood, platCx, fh - 0.025, pz));
    }
    g.add(emesh(new THREE.BoxGeometry(platW + 0.04, 0.09, 0.04), woodDark, platCx, fh - 0.045, zPlatF));
    for (const sx of [-1, 1])
      g.add(emesh(new THREE.BoxGeometry(0.04, 0.09, platD), woodDark, platCx + sx * platW / 2, fh - 0.045, zPlatC));
  }

  // ---- steps ----
  let n = 0;
  let riser = 0;
  let zStepF = zPlatF;
  if (hasSteps) {
    n = THREE.MathUtils.clamp(Math.ceil(fh / RISER_MAX), 1, 6);
    riser = fh / n;
    const nosing = p.stairMaterial === 'stone' ? 0.02 : 0.04;
    const treadTop = (k: number) => fh - (k - 1) * riser; // k = 1..n from the top
    const treadFront = (k: number) => zPlatF + k * RUN + nosing;
    zStepF = treadFront(n);
    if (p.stairMaterial === 'stone') {
      for (let k = 1; k <= n; k++) {
        const top = treadTop(k);
        const front = treadFront(k);
        const back = k === 1 ? zPlatF - 0.03 : zPlatF + (k - 1) * RUN;
        const h = top + 0.05;
        g.add(emesh(new THREE.BoxGeometry(w, h, front - back), stone, stepsCx, (top - 0.05) / 2, (front + back) / 2));
        // stepped side cheeks
        for (const sx of [-1, 1]) {
          const ch = top + 0.22 + 0.05;
          g.add(emesh(new THREE.BoxGeometry(0.18, ch, front - back), stone, stepsCx + sx * (w / 2 + 0.09), (top + 0.22 - 0.05) / 2, (front + back) / 2));
        }
      }
    } else {
      for (let k = 1; k <= n; k++) {
        const top = treadTop(k);
        const front = treadFront(k);
        g.add(emesh(new THREE.BoxGeometry(w, 0.07, RUN + 0.06), wood, stepsCx, top - 0.035, front - (RUN + 0.06) / 2));
        // riser board tucked under the nosing
        g.add(emesh(new THREE.BoxGeometry(w, riser + 0.02, 0.03), woodDark, stepsCx, top - riser / 2, front - 0.07));
      }
      // sloped stringers, foot buried, head into the platform
      for (const sx of [-1, 1]) {
        const a = new THREE.Vector3(stepsCx + sx * (w / 2 - 0.08), -0.03, zStepF + 0.08);
        const b = new THREE.Vector3(stepsCx + sx * (w / 2 - 0.08), fh - 0.02, zPlatF - 0.1);
        g.add(beam(a, b, 0.09, 0.3, woodDark));
      }
    }
    // step handrails: posts on every tread + sloped rail + mid rail
    if (p.entryRails) {
      const postX = (s: number) => stepsCx + s * (w / 2 - 0.07);
      for (const sx of [-1, 1]) {
        for (let k = 1; k <= n; k++) {
          const top = treadTop(k);
          const isNewel = k === 1 || k === n;
          const ps = isNewel ? 0.09 : 0.07;
          const zc = zPlatF + (k - 0.5) * RUN;
          g.add(emesh(new THREE.BoxGeometry(ps, RAIL_H + 0.1, ps), woodDark, postX(sx), top - 0.1 + (RAIL_H + 0.1) / 2, zc));
          if (isNewel) g.add(emesh(new THREE.SphereGeometry(0.06, 10, 8), wood, postX(sx), top + RAIL_H - 0.02, zc));
        }
        if (n > 1) {
          const a = new THREE.Vector3(postX(sx), treadTop(n) + RAIL_H - 0.05, zPlatF + (n - 0.5) * RUN);
          const b = new THREE.Vector3(postX(sx), treadTop(1) + RAIL_H - 0.05, zPlatF + 0.5 * RUN);
          const dir = b.clone().sub(a).normalize();
          g.add(beam(a.clone().addScaledVector(dir, -0.15), b.clone().addScaledVector(dir, 0.15), 0.1, 0.07, wood));
          const a2 = a.clone(); a2.y -= 0.42;
          const b2 = b.clone(); b2.y -= 0.42;
          g.add(beam(a2, b2, 0.06, 0.05, wood));
        }
      }
    }
  }

  // ---- access ramp (sideways along the facade, top flush with the platform) ----
  let rampLen = 0;
  if (hasRamp) {
    const s = THREE.MathUtils.clamp(p.rampSlope, 6, 12);
    const rise = fh - 0.04;
    const run = rise * s;
    const xR0 = platCx + platW / 2 - 0.05;
    const zR = zPlatC;
    const dir = new THREE.Vector3(run, -rise, 0).normalize();
    const nrm = new THREE.Vector3(-dir.y, dir.x, 0);
    const surfA = new THREE.Vector3(xR0, fh, zR);
    const surfB = new THREE.Vector3(xR0 + run, 0.04, zR);
    const mid = surfA.clone().add(surfB).multiplyScalar(0.5);
    rampLen = surfA.distanceTo(surfB);
    // slab (top flush with the platform, foot buried)
    const slab = new THREE.Mesh(new THREE.BoxGeometry(rampLen + 0.1, 0.12, RAMP_W), treadMat);
    slab.position.copy(mid).addScaledVector(nrm, -0.06);
    slab.quaternion.setFromUnitVectors(new THREE.Vector3(1, 0, 0), dir);
    slab.castShadow = slab.receiveShadow = true;
    g.add(slab);
    // support piers
    for (const f of [1 / 3, 2 / 3]) {
      const px = xR0 + run * f;
      const ys = fh - rise * f;
      const h = ys - 0.08 + 0.05;
      if (h > 0.15) g.add(emesh(new THREE.BoxGeometry(0.25, h, RAMP_W - 0.2), stone, px, (ys - 0.08 - 0.05) / 2, zR));
    }
    // edge curbs
    for (const sz of [-1, 1]) {
      const curb = new THREE.Mesh(new THREE.BoxGeometry(rampLen, 0.12, 0.08), treadMat);
      curb.position.copy(mid).addScaledVector(nrm, 0.04);
      curb.position.z = zR + sz * (RAMP_W / 2 - 0.04);
      curb.quaternion.setFromUnitVectors(new THREE.Vector3(1, 0, 0), dir);
      curb.castShadow = curb.receiveShadow = true;
      g.add(curb);
    }
    // transverse cleats (steep stone ramps + all timber ramps)
    if (s < 10 || p.stairMaterial === 'wood') {
      const count = Math.floor(rampLen / 0.35);
      for (let i = 1; i <= count; i++) {
        const f = i / (count + 1);
        const cpos = surfA.clone().lerp(surfB, f).addScaledVector(nrm, 0.015);
        const cleat = new THREE.Mesh(new THREE.BoxGeometry(0.07, 0.03, RAMP_W - 0.16), woodDark);
        cleat.position.copy(cpos);
        cleat.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), nrm);
        cleat.castShadow = false;
        cleat.receiveShadow = true;
        g.add(cleat);
      }
    }
    // ramp rails: posts along both edges + sloped rails
    if (p.entryRails) {
      const nPosts = Math.max(2, Math.round(rampLen / 1.2) + 1);
      for (const sz of [-1, 1]) {
        const tops: THREE.Vector3[] = [];
        for (let i = 0; i < nPosts; i++) {
          const f = i / (nPosts - 1);
          const base = surfA.clone().lerp(surfB, f);
          base.z = zR + sz * (RAMP_W / 2 - 0.06);
          g.add(emesh(new THREE.BoxGeometry(0.07, RAIL_H + 0.08, 0.07), woodDark, base.x, base.y - 0.08 + (RAIL_H + 0.08) / 2, base.z));
          tops.push(new THREE.Vector3(base.x, base.y + RAIL_H - 0.05, base.z));
        }
        const d = tops[tops.length - 1].clone().sub(tops[0]).normalize();
        g.add(beam(tops[0].clone().addScaledVector(d, -0.15), tops[tops.length - 1].clone().addScaledVector(d, 0.15), 0.1, 0.07, wood));
        const m0 = tops[0].clone(); m0.y -= 0.42;
        const m1 = tops[tops.length - 1].clone(); m1.y -= 0.42;
        g.add(beam(m0, m1, 0.06, 0.05, wood));
        for (const e of [tops[0], tops[tops.length - 1]])
          g.add(emesh(new THREE.SphereGeometry(0.06, 10, 8), wood, e.x, e.y + 0.03, e.z));
      }
    }
    if (run > 6) checks.push({ id: 'entry-ramp-len', label: 'Ramp length', status: 'warn', detail: `${run.toFixed(1)} m run — real barrier-free builds need a rest landing every ≤9 m / 0.76 m rise.` });
    checks.push({ id: 'entry-ramp', label: 'Access ramp', status: 'pass', detail: `1:${s} slope over ${run.toFixed(1)} m, ${RAMP_W.toFixed(1)} m wide, ${platD.toFixed(1)} m top landing.` });
  }

  // ---- entry door slab on the wall face (anticipates Phase 3 openings) ----
  if (p.showWalls) {
    const doorCx = hasSteps ? stepsCx : 0;
    const doorBase = Math.max(fh, 0.35);
    const doorH = Math.min(2.0, Math.max(1.2, p.wallHeight - doorBase - 0.25));
    const frame = woodDark;
    for (const sx of [-1, 1])
      g.add(emesh(new THREE.BoxGeometry(0.12, doorH + 0.14, 0.12), frame, doorCx + sx * 0.66, doorBase + (doorH + 0.14) / 2 - 0.05, zWall + 0.1));
    g.add(emesh(new THREE.BoxGeometry(1.44, 0.14, 0.12), frame, doorCx, doorBase + doorH + 0.02, zWall + 0.1));
    for (const sx of [-1, 1]) {
      g.add(emesh(new THREE.BoxGeometry(0.58, doorH, 0.06), wood, doorCx + sx * 0.31, doorBase + doorH / 2, zWall + 0.04));
      for (const ly of [0.35, 1.55])
        g.add(emesh(new THREE.BoxGeometry(0.5, 0.09, 0.03), frame, doorCx + sx * 0.31, doorBase + ly, zWall + 0.075));
    }
    // stone threshold step from the platform up to the door
    const thTop = doorBase + 0.02;
    g.add(emesh(new THREE.BoxGeometry(1.44, thTop - fh, 0.3), stone, doorCx, (thTop + fh) / 2, zWall + 0.15));
  }

  if (hasSteps) {
    const rmm = Math.round(riser * 1000);
    checks.push({ id: 'entry-steps', label: 'Entrance steps', status: 'pass', detail: `${n} riser${n > 1 ? 's' : ''} × ${rmm} mm, ${Math.round(RUN * 1000)} mm treads + nosing (IRC ≤197 / ≥254).` });
  }
  if (p.entryRails && (hasSteps || hasRamp))
    checks.push({ id: 'entry-rails', label: 'Handrails', status: 'pass', detail: `Both sides at ${Math.round(RAIL_H * 1000)} mm above nosing (IRC/ADA 860–970).` });
  else if (hasSteps || hasRamp)
    checks.push({ id: 'entry-rails', label: 'Handrails', status: 'info', detail: 'Rails off — code wants rails at 4+ risers / all ramps.' });

  const box3 = new THREE.Box3().setFromObject(g);
  checks.push({
    id: 'entry-ground', label: 'Entry grounding', status: box3.min.y <= 0.03 ? 'pass' : 'fail',
    detail: `Platform + flight + ramp foot on grade (minY ${(box3.min.y * 100).toFixed(0)} cm).`,
  });
  checks.push(p.showWalls
    ? { id: 'entry-wall', label: 'Entry meets wall', status: 'pass', detail: 'Platform + threshold abut the wall face; door slab set.' }
    : { id: 'entry-wall', label: 'Entry meets wall', status: 'info', detail: 'Walls hidden — entry stands free (door omitted).' });

  let frontZ = zPlatF;
  if (hasSteps) frontZ = Math.max(frontZ, zStepF);
  return {
    group: g,
    part: { name: 'entry', label: 'Entrance (steps/ramp)', box: box3 },
    checks, steps: hasSteps ? n : 0, rampLen: hasRamp ? rampLen : 0, frontZ,
  };
}

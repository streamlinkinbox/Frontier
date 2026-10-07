/*
 * Quarry geometry.
 *
 * Pipeline (the same one used when authors hand-build cliff faces):
 *
 *   1. planPit()      - an irregular, star-shaped pit outline.  Every bench is
 *                       that outline inset by benchWidth, so the terraces stay
 *                       parallel no matter how frayed the rim is.
 *   2. walls          - each bench face is a finely tessellated surface that
 *                       drops `batter` horizontally (near vertical, like a real
 *                       highwall) between two terraces.  It is displaced by
 *                       per-stratum hardness (differential erosion -> bulges
 *                       and undercuts), lumps, blocky cells and vertical joint
 *                       planes.  No textures are involved anywhere.
 *   3. benches        - annular floors between the toe of one face and the
 *                       crest of the next.
 *   4. haul road      - a spiral trench cut through the faces.  It is baked
 *                       into the wall grid itself (same topology, vertices
 *                       just moved), so every seam stays watertight.
 *   5. rock cladding  - chunks from the procedural rock library glued onto the
 *                       displaced faces, at the bench toes and on the plateau,
 *                       tinted with the strata they sit in.
 *   6. strata colour  - vertex colours keyed to a wavy "layer coordinate" so
 *                       the sedimentary bands read correctly around corners.
 */
import * as THREE from '../vendor/three.module.js';
import { clamp, lerp, smoothstep, TAU, hash1 } from './util.js';
import { hex } from './palette.js';
import { placeRock } from './rocks.js';

const V = () => new THREE.Vector3();
const C = () => new THREE.Color();
const _UP = new THREE.Vector3(0, 1, 0);

/* ------------------------------------------------------------------ */
/* pit plan                                                            */
/* ------------------------------------------------------------------ */

export function planPit(params, noise2, field, rng) {
  const { pitRadius, pitDepth, benchHeight, benchWidth, edgeFray } = params;
  const nBench = Math.max(1, Math.round(pitDepth / Math.max(1.5, benchHeight)));
  const bh = pitDepth / nBench;
  const batter = clamp(benchWidth * 0.22, 0.5, 1.4);

  const exx = 0.84 + rng() * 0.34;
  const ecc = 0.84 + rng() * 0.34;

  const R0 = (theta) => {
    const c = Math.cos(theta), s = Math.sin(theta);
    const n1 = noise2.noise2(c + 2.3, s - 1.7);
    const n2 = noise2.noise2(c * 2.6 - 5.1, s * 2.6 + 3.9);
    return pitRadius * (1 + edgeFray * (0.115 * n1 + 0.055 * n2));
  };

  const terraceR = (theta, k) => {
    const c = Math.cos(theta), s = Math.sin(theta);
    const fray = edgeFray * benchWidth * 0.13 *
      noise2.noise2(c * 3.1 + k * 7.7, s * 3.1 - k * 4.3);
    return Math.max(2.2, R0(theta) - k * benchWidth + fray);
  };

  const pointAt = (theta, k) => {
    const r = terraceR(theta, k);
    return { x: r * Math.cos(theta) * exx, z: r * Math.sin(theta) * ecc };
  };

  // rim height: average ground height around the mouth of the pit
  let yTop = 0;
  for (let i = 0; i < 12; i++) {
    const p = pointAt((i / 12) * TAU, 0);
    yTop += field.height(p.x, p.z);
  }
  yTop /= 12;

  const level = (k) => yTop - k * bh;
  const yAt = (theta, k) => {
    if (k !== 0) return level(k);
    const p = pointAt(theta, 0);
    return field.height(p.x, p.z);
  };

  // --- haul road ------------------------------------------------------
  const segments = clamp(Math.round((TAU * pitRadius) / 1.05), 140, 620);
  let road = null;
  let phi = null;
  if (params.road) {
    road = { az: (0.18 + rng() * 0.64) * TAU, width: 5.6 };
    // spiral: a fixed tangential run per bench keeps the grade sane as the
    // pit narrows
    phi = [road.az];
    for (let k = 0; k < nBench; k++) {
      const R = terraceR(phi[k], k);
      phi.push(phi[k] + clamp((road.width * 2.3) / Math.max(6, R), 0.12, 1.15));
    }
  }

  // --- sump dish at the bottom of the pit -----------------------------
  const dishTheta = rng() * TAU;
  const dish = {
    x: Math.cos(dishTheta) * pitRadius * 0.30 * exx,
    z: Math.sin(dishTheta) * pitRadius * 0.30 * ecc,
    r: pitRadius * (0.20 + rng() * 0.16),
    depth: 1.1 + rng() * 0.7,
  };

  const signedRadial = (x, z) => {
    const r = Math.hypot(x / exx, z / ecc);
    const th = Math.atan2(z / ecc, x / exx);
    return R0(th) - r;
  };

  return {
    nBench, bh, benchWidth, batter, exx, ecc, segments,
    yTop, yBot: level(nBench), level, yAt, terraceR, pointAt, signedRadial,
    inside: (x, z, eps = 0) => signedRadial(x, z) > eps,
    road, phi, dish,
    radius: pitRadius,
    center: { x: 0, z: 0 },
  };
}

/* ------------------------------------------------------------------ */
/* builder                                                             */
/* ------------------------------------------------------------------ */

export function buildQuarry(params, ctx) {
  const { noise2, noise3, rng, field, pit, rockLib, preset, mb, groundColor } = ctx;
  const stats = { benches: pit.nBench, rocks: 0 };
  const M = pit.segments;
  const nBench = pit.nBench;
  const bandH = params.bandThickness;
  const yBase = pit.yBot - 3;
  const road = pit.road;
  const batter = pit.batter;

  /* ---------------- palette ---------------- */
  const rawStrata = preset.strata.map(hex);
  const avg = C();
  for (const c of rawStrata) avg.add(c);
  avg.multiplyScalar(1 / rawStrata.length);
  const strataPal = rawStrata.map((c) => c.clone().lerp(avg, 1 - params.strataContrast));
  const tintPal = preset.rockTint.map(hex);
  const dustC = hex(preset.ground.dust);
  const dirtC = hex(preset.ground.dirt);
  const rockC = hex(preset.ground.rock);

  /* ---------------- strata ---------------- */
  function layerCoord(x, y, z) {
    const warp = 0.65 * noise2.noise2(x * 0.030 + 9.3, z * 0.030 - 4.1) +
      0.30 * noise2.noise2(x * 0.105 - 2.0, z * 0.105 + 6.0);
    return (y - yBase) / bandH + warp;
  }
  function layerHardness(idx) { return (hash1(idx * 131 + 7) - 0.5) * 2; }
  function layerColor(idx, out) {
    const n = strataPal.length;
    out.copy(strataPal[((idx % n) + n) % n]);
    if (preset.shaleEvery > 0 && (((idx % preset.shaleEvery) + preset.shaleEvery) % preset.shaleEvery) === 0) {
      out.multiplyScalar(0.52);
    }
    out.r = Math.max(0, out.r); out.g = Math.max(0, out.g); out.b = Math.max(0, out.b);
    return out;
  }

  /* ---------------- wall surface ---------------- */
  const maxD = params.benchWidth * 0.30;

  /** Undisplaced face position (also used for the road). */
  function wallRaw(theta, t, k, out) {
    const r = lerp(pit.terraceR(theta, k), pit.terraceR(theta, k) - batter, t);
    const y = lerp(pit.yAt(theta, k), pit.level(k + 1), t);
    out.set(r * Math.cos(theta) * pit.exx, y, r * Math.sin(theta) * pit.ecc);
    out.r = r;
    out.y = y;
    return out;
  }

  function wallOffset(theta, t, k) {
    const r = lerp(pit.terraceR(theta, k), pit.terraceR(theta, k) - batter, t);
    const y = lerp(pit.yAt(theta, k), pit.level(k + 1), t);
    const x = r * Math.cos(theta) * pit.exx;
    const z = r * Math.sin(theta) * pit.ecc;
    const hard = layerHardness(Math.floor(layerCoord(x, y, z)));
    const taper = 0.20 + 0.80 * Math.sin(Math.PI * clamp(t, 0, 1));
    const amp = params.roughness * params.benchWidth * 0.15;
    const bulge = hard * params.benchWidth * 0.09 * params.bulge;
    const lump = noise3.noise3(x * 0.075, y * 0.085, z * 0.075);
    const lump2 = noise3.noise3(x * 0.21 + 5, y * 0.22, z * 0.21 - 5);
    const joint = noise3.noise3(x * 0.55, y * 0.045, z * 0.55);
    const block = noise3.noise3(x * 0.30 + 11, y * 0.38, z * 0.30 - 3);
    const d = (bulge + amp * (0.55 * lump + 0.30 * lump2 + 0.24 * joint + 0.20 * block)) * taper;
    return clamp(d, -maxD * 0.7, maxD);
  }

  /* ---- haul road -------------------------------------------------------
   * roadNess() returns the normalised distance from the road centreline:
   * <=1 is on the road, >1 is off it.  The road is a spiral trench whose
   * azimuth window slides as the face is descended.                      */
  function roadNess(theta, t, k) {
    if (!road) return 99;
    const c = lerp(pit.phi[k], pit.phi[k + 1], t);
    let d = theta - c;
    while (d > Math.PI) d -= TAU;
    while (d < -Math.PI) d += TAU;
    const r = lerp(pit.terraceR(theta, k), pit.terraceR(theta, k) - batter, t);
    const hw = (road.width * 0.5) / Math.max(4, r);
    return Math.abs(d) / hw;
  }

  /** Final wall vertex: displaced rock face, or graded road where cut. */
  function wallPoint(theta, t, k, out) {
    wallRaw(theta, t, k, out);
    const nd = roadNess(theta, t, k);
    const wallMix = smoothstep(1.0, 1.28, nd);   // 1 = rock, 0 = road
    let d = 0;
    if (wallMix > 0) {
      d = wallOffset(theta, t, k) * wallMix;
      out.x += Math.cos(theta) * d;
      out.z += Math.sin(theta) * d;
    }
    const roadMix = smoothstep(1.02, 0.88, nd);
    if (roadMix > 0) {
      const crown = 0.18 * Math.max(0, 1 - nd);
      const grade = 0.10 * noise2.noise2(Math.cos(theta) * 1.7 + out.r * 0.05, Math.sin(theta) * 1.7);
      out.y += crown + grade;
    }
    out.d = d;
    out.nd = nd;
    out.roadMix = roadMix;
    return out;
  }

  const _rn = V();
  function radialNormal(theta, k, out) {
    const e = 0.004;
    const a = pit.pointAt(theta - e, k);
    const b = pit.pointAt(theta + e, k);
    const tx = b.x - a.x, tz = b.z - a.z;
    const len = Math.hypot(tx, tz) || 1;
    out.set(tz / len, 0, -tx / len);
    return out;
  }

  function wallNormal(theta, t, k, out) {
    const e = 0.006;
    const p0 = wallPoint(theta, t, k, V());
    const pa = wallPoint(theta - e, t, k, V());
    const pb = wallPoint(theta + e, t, k, V());
    const pc = wallPoint(theta, clamp(t + e, 0, 1), k, V());
    out.crossVectors(pb.sub(pa), pc.sub(p0)).normalize();
    radialNormal(theta, k, _rn);
    if (out.dot(_rn) < 0) out.negate();
    return out;
  }

  const _wc = C();
  const _rc = C();
  function wallColor(p, out) {
    wallColorRaw(p.x, p.y, p.z, p.d, out);
    if (p.roadMix > 0.01) {
      roadColor(p.x, p.y, p.z, p.roadMix, _rc);
      out.lerp(_rc, p.roadMix);
    }
    return out;
  }
  function wallColorRaw(x, y, z, d, out) {
    const lc = layerCoord(x, y, z);
    const idx = Math.floor(lc);
    const f = lc - idx;
    layerColor(idx, out);
    const wobble = 0.14 * noise2.noise2(x * 0.55 + 3, z * 0.55 - 8);
    const blend = smoothstep(0.30, 0.70, f + wobble);
    if (blend > 0.001) { layerColor(idx + 1, _wc); out.lerp(_wc, blend); }
    out.multiplyScalar(0.94 + 0.12 * noise3.noise3(x * 0.55, y * 0.55, z * 0.55));
    const streak = Math.max(0, noise3.noise3(x * 0.42, y * 0.03, z * 0.42));
    out.multiplyScalar(1 - 0.20 * streak * streak);
    out.multiplyScalar(1 - 0.17 * smoothstep(0.15, -0.55, d));
    const depth = (pit.yTop - y) / Math.max(1, params.pitDepth);
    out.multiplyScalar(1 - 0.13 * smoothstep(0.1, 1.0, depth));
    return out;
  }

  /* ---------------- bench / pit floor ----------------
   * f = 0 is the inner edge (crest of the face below), f = 1 the outer edge
   * (toe of the face above).  The pit floor is a full disc.              */
  function floorPoint(theta, f, k, out) {
    const inner = k === nBench ? 0 : pit.terraceR(theta, k);
    const outer = pit.terraceR(theta, k - 1) - batter;
    const r = lerp(inner, outer, f);
    const y = pit.level(k);
    const x = r * Math.cos(theta) * pit.exx;
    const z = r * Math.sin(theta) * pit.ecc;
    let h = y + 0.30 * noise2.noise2(x * 0.42, z * 0.42) * (0.25 + 0.75 * f);
    if (k === nBench) h += dishH(x, z);
    out.set(x, h, z);
    return out;
  }
  function dishH(x, z) {
    const d = Math.hypot(x - pit.dish.x, z - pit.dish.z);
    return -pit.dish.depth * smoothstep(pit.dish.r, 0, d);
  }

  const _fc = C();
  function floorColor(p, f, out) {
    const patch = noise2.noise2(p.x * 0.22 + 4, p.z * 0.22 - 6) * 0.5 + 0.5;
    const patch2 = noise2.noise2(p.x * 0.06 - 2, p.z * 0.06 + 8) * 0.5 + 0.5;
    out.copy(rockC).lerp(dirtC, clamp(0.30 + 0.55 * patch + 0.25 * patch2, 0, 1));
    // debris and dust collect at the toe of the face (outer edge), the crest
    // of the face below stays in shadow
    out.lerp(dustC, 0.22 + 0.18 * patch2 + 0.25 * f);
    out.multiplyScalar(0.90 + 0.14 * noise2.noise2(p.x * 0.7, p.z * 0.7));
    out.multiplyScalar(0.86 + 0.14 * f);
    return out;
  }

  /* ---------------- road colour ---------------- */
  // du: 0 at the centreline, 1 at the edge of the running surface
  function roadColor(x, y, z, du, out) {
    layerColor(Math.floor(layerCoord(x, y, z)), out);
    out.lerp(dustC, 0.40);
    const track =
      Math.exp(-Math.pow((du - 0.26) / 0.14, 2)) +
      Math.exp(-Math.pow((du - 0.74) / 0.14, 2));
    out.multiplyScalar(1 - 0.34 * track);
    out.multiplyScalar(0.95 + 0.10 * (1 - du));
    return out;
  }

  /* ---------------- generic grid emitter ----------------
   * na x nb grid; index 0 of `a` is the outer/top row, `b` wraps around.
   * normal = cross(dP/db, dP/da), flipped to agree with `ref`.        */
  function emitGrid(na, nb, point, color, ref, skip, fanFirst) {
    const P = new Array(na * nb);
    const Cc = new Array(na * nb);
    const Nn = new Array(na * nb);
    for (let ia = 0; ia < na; ia++) {
      for (let ib = 0; ib < nb; ib++) {
        const i = ia * nb + ib;
        const p = point(ia, ib);
        P[i] = p;
        Cc[i] = color(ia, ib, p);
        Nn[i] = V();
      }
    }
    const du = V(), dv = V(), nrm = V();
    for (let ia = 0; ia < na; ia++) {
      for (let ib = 0; ib < nb; ib++) {
        const i = ia * nb + ib;
        const iu1 = ia * nb + ((ib + 1) % nb);
        const iu0 = ia * nb + ((ib - 1 + nb) % nb);
        const iv1 = Math.min(na - 1, ia + 1) * nb + ib;
        const iv0 = Math.max(0, ia - 1) * nb + ib;
        du.subVectors(P[iu1], P[iu0]);
        dv.subVectors(P[iv1], P[iv0]);
        if (du.lengthSq() < 1e-12) du.set(1, 0, 0);
        if (dv.lengthSq() < 1e-12) dv.set(0, 1, 0);
        nrm.crossVectors(du, dv);
        if (nrm.lengthSq() < 1e-12) nrm.copy(ref(ia, ib));
        nrm.normalize();
        if (nrm.dot(ref(ia, ib)) < 0) nrm.negate();
        Nn[i].copy(nrm);
      }
    }
    const add = (a, b, c) =>
      mb.addTri(P[a], P[b], P[c], Nn[a], Nn[b], Nn[c], Cc[a], Cc[b], Cc[c]);
    for (let ia = 0; ia + 1 < na; ia++) {
      for (let ib = 0; ib < nb; ib++) {
        if (skip && skip(ia, ib)) continue;
        const a = ia * nb + ib;
        const b = ia * nb + ((ib + 1) % nb);
        const c = (ia + 1) * nb + ((ib + 1) % nb);
        const d = (ia + 1) * nb + ib;
        if (fanFirst && ia === 0) { add(a, c, d); continue; }
        add(a, b, d);
        add(b, c, d);
      }
    }
  }

  /* ---------------- walls ---------------- */
  for (let k = 0; k < nBench; k++) {
    const V0 = clamp(Math.round(pit.bh / 0.7), 3, 20);
    emitGrid(
      V0 + 1, M,
      (ia, ib) => wallPoint((ib / M) * TAU, ia / V0, k, V()),
      (ia, ib, p) => wallColor(p, C()),
      (ia, ib) => radialNormal((ib / M) * TAU, k, V()),
      null
    );
  }

  /* ---------------- bench + pit floors ---------------- */
  for (let k = 1; k <= nBench; k++) {
    const rings = k === nBench ? 8 : 4;
    emitGrid(
      rings + 1, M,
      (ia, ib) => floorPoint((ib / M) * TAU, ia / rings, k, V()),
      (ia, ib, p) => floorColor(p, ia / rings, C()),
      () => _UP,
      null,
      k === nBench
    );
  }

  /* ---------------- approach road on the plateau ---------------- */
  if (road) {
    const az = road.az;
    const r0 = pit.terraceR(az, 0) - 0.6;
    const len = 52;
    const steps = 16;
    const cx = Math.cos(az), cz = Math.sin(az);
    const _gc = C();
    emitGrid(
      steps + 1, 3,
      (ia, ib) => {
        const f = ia / steps;
        const u = ib / 2;
        const r = r0 + f * len;
        const lat = (u - 0.5) * road.width;
        const x = cx * r * pit.exx - cz * lat * pit.exx;
        const z = cz * r * pit.ecc + cx * lat * pit.ecc;
        const y = (ia === 0 ? pit.yAt(az, 0) : field.height(x, z)) + 0.34;
        return V().set(x, y, z);
      },
      (ia, ib, p) => {
        const f = ia / steps;
        const out = roadColor(p.x, p.y, p.z, Math.abs((ib / 2) * 2 - 1), C());
        groundColor(p.x, p.z, _gc);
        out.lerp(_gc, smoothstep(0.45, 1.0, f) * 0.92);
        return out;
      },
      () => _UP,
      null
    );
  }

  /* ---------------- rocks ---------------- */
  const tint = C();
  const pos = V();
  const nrm = V();
  const perim = TAU * params.pitRadius;

  // cladding on the faces
  for (let k = 0; k < nBench; k++) {
    const count = Math.round(params.rockDensity * 30 * (perim / (TAU * 34)) * (pit.bh / 5.5));
    for (let i = 0; i < count; i++) {
      const th = rng() * TAU;
      const t = Math.pow(rng(), 0.6);
      if (roadNess(th, t, k) < 1.06) continue;
      const p = wallPoint(th, t, k, pos);
      wallNormal(th, t, k, nrm);
      const scale = params.rockScale * (0.5 + rng() * 1.05) * (1 + 0.45 * (1 - t));
      const sink = scale * 0.30;
      layerColor(Math.floor(layerCoord(p.x, p.y, p.z)), tint);
      tint.lerp(tintPal[(rng() * tintPal.length) | 0], 0.35);
      placeRock(rockLib, rng, mb, {
        x: p.x + nrm.x * sink, y: p.y + nrm.y * sink, z: p.z + nrm.z * sink,
        scale, yaw: rng() * TAU, tilt: 0.32,
        tint, jitter: 0.18, jitterSeed: k * 977 + i,
        shade: 0.80 + 0.20 * t,
      });
      stats.rocks++;
    }
  }

  // scree at the toe of every face (outer edge of each bench)
  for (let k = 1; k <= nBench; k++) {
    const count = Math.round(params.rockDensity * 15);
    for (let i = 0; i < count; i++) {
      const th = rng() * TAU;
      if (road && Math.abs(angDiff(th, pit.phi[k - 1])) < 0.2) continue;
      const p = floorPoint(th, 0.90 + rng() * 0.10, k, pos);
      tint.copy(dirtC).lerp(rockC, 0.4 + rng() * 0.4);
      placeRock(rockLib, rng, mb, {
        x: p.x, y: p.y, z: p.z,
        scale: params.rockScale * (0.45 + rng() * 0.9), yaw: rng() * TAU, tilt: 0,
        tint, jitter: 0.2, jitterSeed: 5000 + k * 131 + i,
      });
      stats.rocks++;
    }
  }

  // rubble on the pit floor
  for (let i = 0; i < Math.round(params.rockDensity * 22); i++) {
    const p = floorPoint(rng() * TAU, 0.10 + rng() * 0.85, nBench, pos);
    tint.copy(rockC).lerp(dirtC, rng() * 0.5);
    placeRock(rockLib, rng, mb, {
      x: p.x, y: p.y, z: p.z,
      scale: params.rockScale * (0.4 + rng() * 1.3), yaw: rng() * TAU, tilt: 0,
      tint, jitter: 0.22, jitterSeed: 9000 + i,
    });
    stats.rocks++;
  }

  // boulders out on the plateau
  for (let i = 0; i < Math.round(params.rockDensity * 46); i++) {
    const th = rng() * TAU;
    const dist = params.pitRadius + 1.2 + Math.pow(rng(), 0.7) * 30;
    const x = Math.cos(th) * dist * pit.exx;
    const z = Math.sin(th) * dist * pit.ecc;
    const y = field.height(x, z);
    tint.copy(rockC).lerp(dirtC, 0.35 + rng() * 0.5);
    placeRock(rockLib, rng, mb, {
      x, y, z,
      scale: params.rockScale * (0.35 + rng() * 0.95) * (dist > params.pitRadius + 9 ? 1.5 : 1.0),
      yaw: rng() * TAU, tilt: 0,
      tint, jitter: 0.2, jitterSeed: 13000 + i,
    });
    stats.rocks++;
  }

  // kerb stones beside the haul road where it meets each bench
  if (road) {
    for (let k = 1; k <= nBench; k++) {
      for (const s of [-1, 1]) {
        const p = floorPoint(pit.phi[k - 1] + s * 0.05, 0.93, k, pos);
        tint.copy(rockC).lerp(dustC, 0.5);
        placeRock(rockLib, rng, mb, {
          x: p.x, y: p.y, z: p.z,
          scale: params.rockScale * (0.32 + rng() * 0.45), yaw: rng() * TAU, tilt: 0,
          tint, jitter: 0.2, jitterSeed: 17000 + k * 37 + (s + 1),
        });
        stats.rocks++;
      }
    }
  }

  return {
    stats,
    puddle: params.puddle
      ? {
          x: pit.dish.x, z: pit.dish.z,
          y: pit.level(nBench) - pit.dish.depth * 0.62 + 0.06,
          r: pit.dish.r * 0.74,
        }
      : null,
  };
}

function angDiff(a, b) {
  let d = a - b;
  while (d > Math.PI) d -= TAU;
  while (d < -Math.PI) d += TAU;
  return d;
}

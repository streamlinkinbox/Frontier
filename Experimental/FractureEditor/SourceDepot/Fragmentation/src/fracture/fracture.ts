/**
 * Stress-driven, energy-budgeted fragmentation.
 *
 * This is deliberately NOT a Voronoi / cell-fracture: Voronoi gives uniformly
 * sized blobby cells that read as "shattered rock" for every material.
 * Instead we solve a cheap analytic impact stress field and propagate cracks
 * through it under a Griffith energy budget:
 *
 *   1. A Hertzian cone crack is nucleated under the impact (glass, rock,
 *      concrete) - the cone plug is the first thing that leaves the object.
 *   2. Cracks open PERPENDICULAR to the local maximum principal tensile
 *      stress, which for an impact gives hoop tension near the site (radial
 *      star cracks) and radial tension further out (concentric ring cracks).
 *   3. Every new crack surface costs 2*Gc*A joules. A piece can only split
 *      while it still holds enough kinetic/strain energy, so cracks ARREST on
 *      their own: fine comminution at the impact site, big intact chunks far
 *      away. Fragment-size distribution comes out Weibull-ish for free, which
 *      is what real fragmentation data looks like.
 *   4. Material character comes from anisotropy (wood grain, rock bedding),
 *      toughness (ductile plastic tears into a few big flaps) and stored
 *      energy (tempered glass dices itself with no extra impact energy).
 */
import { Vector3 } from 'three';
import { clip, Piece, type Plane } from '../core/convex';
import { fbm, perp, rng, type Rand } from '../core/math';
import type { MaterialDef } from './materials';

export interface Impact {
  point: Vector3;   // object space
  dir: Vector3;     // unit, into the surface
  energy: number;   // joules actually delivered
  radius: number;   // contact radius (m)
}

export interface Fragment {
  piece: Piece;
  centroid: Vector3;
  volume: number;
  /** 0..1 how close to the impact this fragment was born (drives dust, speed) */
  proximity: number;
  /** energy left in the fragment -> ejection speed */
  kinetic: number;
}

interface Work { piece: Piece; energy: number; gen: number; vol: number; c: Vector3; rad: number }

const sat = (x: number) => (x < 0 ? 0 : x > 1 ? 1 : x);

/** Max principal tensile direction => crack plane normal. */
function crackNormal(mat: MaterialDef, c: Vector3, imp: Impact, objRadius: number, rand: Rand): Vector3 {
  const R = c.clone().sub(imp.point);
  const axial = imp.dir.clone().multiplyScalar(R.dot(imp.dir));
  const radial = R.clone().sub(axial);
  const r = radial.length();
  if (r < 1e-5) radial.copy(perp(imp.dir)).multiplyScalar(1e-3);
  radial.normalize();
  const hoop = imp.dir.clone().cross(radial).normalize();

  let n: Vector3;
  switch (mat.mode) {
    case 'plate-brittle': {
      if (mat.storedEnergy > 0) {
        // tempered: the tensile core is in residual equilibrium everywhere ->
        // isotropic bifurcating dicing, not a star pattern
        n = new Vector3(rand() * 2 - 1, rand() * 2 - 1, rand() * 2 - 1).normalize();
        n.addScaledVector(imp.dir, -n.dot(imp.dir) * 0.85).normalize(); // cracks run through thickness
      } else {
        // near field: hoop tension -> radial star cracks
        // far field: radial tension -> concentric rings (+ Wallner wander)
        const ring = sat((r / (objRadius * 0.45)) ** 1.4) * (0.35 + mat.ringBias);
        n = rand() < ring ? radial.clone() : hoop.clone();
        n.addScaledVector(imp.dir, -n.dot(imp.dir) * 0.9).normalize();
      }
      break;
    }
    case 'bulk-brittle': {
      const ring = sat((r / (objRadius * 0.6)) ** 1.5) * mat.ringBias;
      n = rand() < ring ? radial.clone() : hoop.clone();
      if (rand() < 0.28) { // secondary shear fragmentation of the comminuted zone
        n.lerp(new Vector3(rand() * 2 - 1, rand() * 2 - 1, rand() * 2 - 1).normalize(), 0.8).normalize();
      }
      if (mat.beddingWeak > 0 && mat.anisoDir && rand() < 0.35 + mat.beddingWeak * 0.4) {
        n.lerp(mat.anisoDir, 0.85).normalize(); // split along a bedding plane
      }
      break;
    }
    case 'fibrous': {
      const g = mat.anisoDir!;
      // splitting along the grain: crack plane CONTAINS the grain direction
      const t = new Vector3(rand() * 2 - 1, rand() * 2 - 1, rand() * 2 - 1);
      n = g.clone().cross(t).normalize();
      if (n.lengthSq() < 0.5) n.copy(perp(g));
      // occasional cross-grain rupture where bending is highest
      const bend = sat(1 - r / (objRadius * 0.5));
      if (rand() < 0.10 + 0.25 * bend) n.copy(g).addScaledVector(hoop, 0.25).normalize();
      break;
    }
    default: { // ductile: crack follows the plastic zone, heavily curved
      n = hoop.clone().lerp(radial, rand() * 0.5).normalize();
      break;
    }
  }
  // crack path wander
  const j = mat.jitter;
  n.x += (rand() - 0.5) * j; n.y += (rand() - 0.5) * j; n.z += (rand() - 0.5) * j;
  return n.normalize();
}

/** Direction-dependent toughness: grain, bedding planes, flaw scatter. */
function toughness(mat: MaterialDef, n: Vector3, rand: Rand) {
  let g = mat.Gc;
  if (mat.mode === 'fibrous' && mat.anisoDir) {
    const across = Math.abs(n.dot(mat.anisoDir)); // n ∥ grain  => cutting fibres
    g *= 1 + (mat.anisoRatio - 1) * across ** 2;
  }
  if (mat.beddingWeak > 0 && mat.anisoDir) {
    const align = Math.abs(n.dot(mat.anisoDir));
    g *= 1 - mat.beddingWeak * align ** 4;
  }
  // Weibull-ish flaw scatter
  return g * (1 - mat.weibull + 2 * mat.weibull * rand());
}

function capArea(piece: Piece): number {
  let best = 0;
  for (const f of piece.faces) {
    if (!f.interior) continue;
    let a = 0; const V = piece.verts;
    for (let i = 1; i < f.idx.length - 1; i++)
      a += V[f.idx[i]].clone().sub(V[f.idx[0]]).cross(V[f.idx[i + 1]].clone().sub(V[f.idx[0]])).length() / 2;
    best = Math.max(best, a);
  }
  return best;
}

function mkWork(piece: Piece, energy: number, gen: number, c?: Vector3, vol?: number): Work {
  const cc = c ?? piece.centroid();
  return { piece, energy, gen, vol: vol ?? piece.volume(), c: cc, rad: piece.radius(cc) };
}

/** Punch a Hertzian cone crack under the impact (pyramidal approximation). */
function coneCrack(p: Piece, imp: Impact, half: number, sides: number, rand: Rand, out: Work[], energy: number) {
  const apex = imp.point.clone().addScaledVector(imp.dir, -imp.radius * 0.35);
  const a = imp.dir.clone().normalize();
  const u0 = perp(a);
  const u1 = a.clone().cross(u0);
  let inside: Piece | null = p;
  for (let i = 0; i < sides && inside; i++) {
    const ang = (i / sides) * Math.PI * 2 + rand() * 0.2;
    const w = Math.cos(half) * (1 + (rand() - 0.5) * 0.18);
    const u = u0.clone().multiplyScalar(Math.cos(ang)).addScaledVector(u1, Math.sin(ang));
    const n = u.multiplyScalar(w).addScaledVector(a, -Math.sin(half)).normalize();
    const pl: Plane = { n, d: n.dot(apex) };
    const r = clip(inside, pl, 900 + i);
    if (r.front) out.push(mkWork(r.front, energy * 0.14, 1));
    inside = r.back ?? inside;
    if (!r.back) break;
  }
  return inside;
}

export interface FractureResult {
  fragments: Fragment[];
  ms: number;
  crackArea: number;
}

export function fracture(
  root: Piece, mat: MaterialDef, imp: Impact, budget = 220, seed = 1,
): FractureResult {
  const t0 = performance.now();
  const rand = rng(seed);
  const objC = root.centroid();
  const objR = root.radius(objC);
  const totalVol = root.volume();

  // energy available = impact energy + released residual strain energy
  let E = imp.energy + mat.storedEnergy * totalVol;

  const work: Work[] = [];
  const done: Piece[] = [];
  let crackArea = 0;

  let start: Piece | null = root;
  if (mat.hertzCone > 0 && E > 4) {
    const coneE = E * mat.coneEnergy;
    start = coneCrack(root, imp, mat.hertzCone, 5 + ((rand() * 3) | 0), rand, work, coneE);
    E -= coneE * 0.5;
  }
  if (start) work.push(mkWork(start, E, 0));

  // Energy-greedy front: always advance the crack in the most loaded piece.
  const guard = budget * 6;
  let iter = 0;
  while (work.length && done.length + work.length < budget && iter++ < guard) {
    let bi = 0, bs = -Infinity;
    for (let i = 0; i < work.length; i++) {
      const w = work[i];
      const s = w.energy / (1 + w.gen * 0.35);
      if (s > bs) { bs = s; bi = i; }
    }
    const w = work.splice(bi, 1)[0];
    const piece = w.piece;
    if (w.vol < mat.minFragment || w.energy <= 0) { done.push(piece); continue; }

    const c = w.c;
    const rad = w.rad;
    const n = crackNormal(mat, c, imp, objR, rand);

    // the crack nucleates at the worst flaw, not at the centre of mass
    const flaw = c.clone()
      .addScaledVector(perp(n), (rand() - 0.5) * rad * 0.7)
      .addScaledVector(n.clone().cross(perp(n)), (rand() - 0.5) * rad * 0.7)
      .addScaledVector(n, (rand() - 0.5) * rad * 0.55);
    // bias toward the impact axis: stress concentrates there
    const toImp = imp.point.clone().sub(flaw);
    flaw.addScaledVector(toImp, 0.12 * sat(1 - toImp.length() / (objR + 1e-6)));

    const pl: Plane = { n, d: n.dot(flaw) };
    const res = clip(piece, pl, (rand() * 1e6) | 0);
    if (!res.front || !res.back) { done.push(piece); continue; }

    const A = capArea(res.front);
    const cost = 2 * toughness(mat, n, rand) * A;
    if (cost > w.energy) { done.push(piece); continue; } // crack arrests

    crackArea += A;
    const left = w.energy - cost;
    const vf = res.front.volume(), vb = res.back.volume();
    const cf = res.front.centroid(), cb = res.back.centroid();
    // energy follows mass, but the piece nearer the impact keeps more of it
    const lam = objR * 0.5;
    const wf = vf * Math.exp(-cf.distanceTo(imp.point) / lam);
    const wb = vb * Math.exp(-cb.distanceTo(imp.point) / lam);
    const s = wf + wb + 1e-12;
    work.push(mkWork(res.front, left * (wf / s), w.gen + 1, cf, vf));
    work.push(mkWork(res.back, left * (wb / s), w.gen + 1, cb, vb));
  }
  for (const w of work) done.push(w.piece);

  const fragments: Fragment[] = done.map(p => {
    const c = p.centroid();
    const d = c.distanceTo(imp.point);
    return {
      piece: p, centroid: c, volume: p.volume(),
      proximity: sat(1 - d / (objR * 1.2)),
      kinetic: 0,
    };
  });

  return { fragments, ms: performance.now() - t0, crackArea };
}

/** Noise field shared by every crack surface of a given object. */
export function surfaceNoise(mat: MaterialDef) {
  const oct = mat.surface.octaves;
  if (mat.mode === 'plate-brittle') {
    // conchoidal ripples + Wallner lines: smooth, banded
    return (x: number, y: number, z: number) =>
      0.35 * fbm(x, y, z, 2) + 0.65 * Math.sin(fbm(x * 0.35, y * 0.35, z * 0.35, 2) * 9.0);
  }
  if (mat.mode === 'fibrous') {
    // fibre pull-out: sharp, stretched along the grain
    return (x: number, y: number, z: number) => {
      const n = fbm(x, y, z, oct);
      return Math.sign(n) * Math.abs(n) ** 0.65;
    };
  }
  if (mat.mode === 'ductile') {
    // necked, stretched tear lip
    return (x: number, y: number, z: number) => Math.tanh(fbm(x, y, z, 2) * 2.2);
  }
  // aggregate / crystalline: blocky, high contrast
  return (x: number, y: number, z: number) => {
    const n = fbm(x, y, z, oct);
    return n * 0.7 + 0.3 * Math.sign(n) * Math.abs(n) ** 0.4;
  };
}

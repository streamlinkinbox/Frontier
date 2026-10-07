/*
 * Top-level generation entry point: given a parameter set, produce the meshes.
 * Deterministic - the same params always give the same cliff.
 */
import * as THREE from '../vendor/three.module.js';
import { makeNoise2, makeNoise3 } from './noise.js';
import { mulberry32, hashString } from './util.js';
import { makeRockLibrary } from './rocks.js';
import { makeGroundField, makeGroundColor, buildTerrainMesh } from './terrain.js';
import { planPit, buildQuarry } from './quarry.js';
import { MeshBuilder } from './meshbuilder.js';
import { PRESETS } from './palette.js';

export function generateCliff(params) {
  const t0 = (typeof performance !== 'undefined' ? performance.now() : Date.now());
  const preset = PRESETS[params.preset] || PRESETS.limestone;

  // normalise + guard the parameters
  const p = { ...params };
  p.pitRadius = Math.max(10, p.pitRadius);
  p.pitDepth = Math.max(4, p.pitDepth);
  p.benchHeight = Math.max(2, p.benchHeight);
  p.benchWidth = Math.max(1.5, p.benchWidth);
  p.rockDensity = Math.max(0, p.rockDensity);
  p.rockScale = Math.max(0.05, p.rockScale);
  const nBenchApprox = Math.max(1, Math.round(p.pitDepth / p.benchHeight));
  // never let the benches eat each other: grow the bench height instead
  const maxBenches = Math.max(1, Math.floor((p.pitRadius * 0.8) / p.benchWidth));
  if (nBenchApprox > maxBenches) {
    p.benchHeight = Math.max(p.benchHeight, p.pitDepth / maxBenches);
  }
  p.benchWidth = Math.min(p.benchWidth, (p.pitRadius * 0.82) / nBenchApprox);

  const seed = hashString(String(p.seed));
  const rng = mulberry32(seed);
  const noise2 = makeNoise2(seed);
  const noise3 = makeNoise3(seed + 12345);

  const field = makeGroundField(p, noise2);
  const pit = planPit(p, noise2, field, rng);
  const groundColor = makeGroundColor(p, preset, noise2, field, pit);
  const rockLib = makeRockLibrary({ count: 14, seed });

  const mb = new MeshBuilder();
  const quarry = buildQuarry(p, {
    noise2, noise3, rng, field, pit, rockLib, preset, mb, groundColor,
  });

  const terrainGeo = buildTerrainMesh(p, field, pit, noise2, groundColor);
  const cliffGeo = mb.build();

  const t1 = (typeof performance !== 'undefined' ? performance.now() : Date.now());
  const terrainTris = terrainGeo.index ? terrainGeo.index.count / 3 : 0;
  return {
    params: p,
    preset,
    pit,
    terrainGeo,
    cliffGeo,
    puddle: quarry.puddle,
    stats: {
      benches: pit.nBench,
      benchHeight: pit.bh,
      rocks: quarry.stats.rocks,
      terrainTris,
      cliffTris: cliffGeo.attributes.position.count / 3,
      totalTris: terrainTris + cliffGeo.attributes.position.count / 3,
      ms: Math.round(t1 - t0),
    },
  };
}

/** Framing helper for the camera. */
export function framingFor(result) {
  const { pit } = result;
  const y = pit.yTop - pit.nBench * pit.bh * 0.42;
  return {
    target: new THREE.Vector3(0, y, 0),
    distance: pit.radius * 2.6 + pit.nBench * pit.bh * 1.4,
    height: pit.yTop,
    depth: pit.nBench * pit.bh,
  };
}

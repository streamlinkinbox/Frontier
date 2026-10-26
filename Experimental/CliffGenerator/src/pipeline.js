// End-to-end heightfield pipeline. Pure JS, no DOM — runs in the worker and under Node for checks.
//
//   synthesizeBase → outcrops → applyStrata (+hardness) → thermal pre-settle → fluvial incision
//   (stream power) → hydraulic droplet erosion → thermal → simulated rivers
//   → derived maps (deposit, flow, cavity, slope) packed for the vertex attributes.

import { synthesizeBase, applyStrata, computeCavity, computeSlopeMap } from './heightfield.js';
import { ruggedPushPull } from './rugged.js';
import { hydraulicErosion, thermalErosion, blurField } from './erosion.js';
import { carveRivers, NO_WATER } from './features.js';
import { simulateRivers } from './hydrology.js';
import { addOutcrops } from './outcrops.js';
import { fluvialErosion, fillShallowPits, rillErosion, bankErosion } from './fluvial.js';
import { smoothstep } from './noise.js';

export function generateTerrain(params, progress = () => {}) {
  const N = params.resolution;
  const cell = params.worldSize / (N - 1);
  const t0 = now();

  progress({ phase: 'Synthesising base relief', fraction: 0 });
  const height = synthesizeBase(params, (f) => progress({ phase: 'Synthesising base relief', fraction: f }));

  // rugged outcrops: the steep faces of the base shape are pushed and pulled sideways (blocky
  // buttresses and recesses) before anything else is layered on them
  const rugged = (params.ruggedOn == null ? 1 : params.ruggedOn) ? {
    amount: params.ruggedAmount, scale: params.ruggedScale, blockiness: params.ruggedBlockiness, ledges: params.ruggedLedges,
    slopeMin: params.ruggedSlope, slopeMax: params.ruggedSlope + 22, bands: params.ruggedBands, seed: params.seed,
  } : null;
  if (rugged && rugged.amount > 0) {
    progress({ phase: 'Rugged outcrops', fraction: 0 });
    ruggedPushPull(height, N, params.worldSize, rugged);
  }

  // boulder outcrops are part of the landform: added before strata and erosion
  const outcrop = addOutcrops(height, params);

  progress({ phase: 'Layering strata', fraction: 0 });
  const hardness = applyStrata(height, params, (f) => progress({ phase: 'Layering strata', fraction: f }), outcrop);
  for (let i = 0; i < N * N; i++) if (outcrop[i] > 0) hardness[i] = Math.max(hardness[i], 0.55 + 0.4 * outcrop[i]);

  const erosionParams = { ...params, heightScale: Math.max(1, params.mountainHeight) };

  progress({ phase: 'Thermal settling', fraction: 0 });
  thermalErosion(height, hardness, { ...erosionParams, thermalIterations: Math.ceil(params.thermalIterations * 0.3) },
    (f) => progress({ phase: 'Thermal settling', fraction: f }));

  // Cliff protection: the steep rock faces keep their shape through the erosion stages (the
  // massive beds of a sea cliff or quarry wall erode far slower than the soil-covered ground),
  // so the plateau above can be eroded into a real landscape — valleys, rills, streams — that
  // simply hangs at the crest, while the wall stays a wall. The pre-erosion surface is kept and
  // blended back in on a feathered mask of the steep cells.
  const protect = Math.min(1, Math.max(0, params.cliffProtect || 0));
  let protectMask = null, preErosion = null;
  if (protect > 0) {
    preErosion = height.slice();
    const sl = computeSlopeMap(height, N, cell);
    const steepMask = new Float32Array(N * N);
    const g0 = Math.tan((params.cliffProtectAngle == null ? 38 : params.cliffProtectAngle) * Math.PI / 180);
    for (let i = 0; i < N * N; i++) steepMask[i] = smoothstep(g0, g0 * 1.6, sl[i]);
    // grow by a cell so the lip and the foot are covered too, then feather
    const grown = new Float32Array(N * N);
    for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) {
      let mx = 0;
      for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) {
        const ii = Math.min(N - 1, Math.max(0, i + di)), jj = Math.min(N - 1, Math.max(0, j + dj));
        const q = steepMask[jj * N + ii]; if (q > mx) mx = q;
      }
      grown[j * N + i] = mx;
    }
    protectMask = blurField(grown, N, 2);
  }
  const restoreCliffs = () => {
    if (!protectMask) return;
    for (let i = 0; i < N * N; i++) { const a = protectMask[i] * protect; if (a > 0) height[i] += (preErosion[i] - height[i]) * a; }
  };

  // Fluvial incision: stream-power erosion re-shapes the relief into a drainage network of
  // branching valleys before the fine droplet erosion and the river simulation run on it.
  let fluvial = null;
  if (params.fluvialStrength > 0 && params.fluvialIterations > 0) {
    progress({ phase: 'Fluvial incision', fraction: 0 });
    fluvial = fluvialErosion(height, hardness, N, params.worldSize, {
      strength: params.fluvialStrength, iterations: params.fluvialIterations, concavity: params.fluvialConcavity,
      uplift: params.fluvialUplift, diffusion: params.fluvialDiffusion, basinFill: params.fluvialFill, deposition: params.fluvialDeposition,
      seaLevel: params.waterEnabled ? params.seaLevel : -Infinity,
    }, (f) => progress({ phase: 'Fluvial incision', fraction: f }));
  }

  // Drawn rivers: carve the channel first so the slopes drain into it, and seed the erosion with it.
  const rivers = (params.features && params.features.rivers) || [];
  const riverOpts = {
    width: params.riverWidth, depth: params.riverDepth, bankAngle: params.riverBank, maxBank: params.riverMaxBank, meander: params.riverMeander,
    water: true, waterDepth: params.riverWaterDepth,
  };
  let riverResult = { riverMask: new Float32Array(N * N), waterLevel: new Float32Array(N * N).fill(NO_WATER), sources: [] };
  if (rivers.length) {
    progress({ phase: 'Carving rivers', fraction: 0 });
    riverResult = carveRivers(height, N, params.worldSize, rivers, riverOpts, params.seed);
    erosionParams.sources = riverResult.sources;
    erosionParams.sourceFraction = 0.6 * params.riverErosion;
    erosionParams.sourceWater = 1.5 + 4 * params.riverErosion;
  }

  restoreCliffs();
  progress({ phase: 'Hydraulic erosion', fraction: 0 });
  const { flow, delta } = hydraulicErosion(height, hardness, erosionParams,
    (f) => progress({ phase: 'Hydraulic erosion', fraction: f }));

  progress({ phase: 'Scree slumping', fraction: 0 });
  const slumped = thermalErosion(height, hardness, erosionParams,
    (f) => progress({ phase: 'Scree slumping', fraction: f }));
  restoreCliffs();

  // Erosion smears the lateral structure, so part of the push–pull is applied again on the eroded
  // faces (a second, finer pass) — the hardness and outcrop maps ride along with the faces.
  if (rugged && rugged.amount > 0 && params.ruggedAfter > 0) {
    progress({ phase: 'Rugged outcrops', fraction: 0.5 });
    ruggedPushPull(height, N, params.worldSize, { ...rugged, amount: rugged.amount * params.ruggedAfter, scale: rugged.scale * 0.6, seed: params.seed + 3 }, [hardness, outcrop]);
  }

  // Re-cut the beds: erosion rounds the terraces off, so the strata are applied once more (lighter)
  // on the eroded surface — the ledges stay crisp where the slopes are steep.
  if (params.strataRecut > 0 && params.strataStrength > 0) {
    progress({ phase: 'Re-cutting strata', fraction: 0 });
    const recut = applyStrata(height, { ...params, strataStrength: params.strataStrength * params.strataRecut },
      (f) => progress({ phase: 'Re-cutting strata', fraction: f }), outcrop);
    for (let i = 0; i < N * N; i++) { hardness[i] = recut[i]; if (outcrop[i] > 0) hardness[i] = Math.max(hardness[i], 0.55 + 0.4 * outcrop[i]); }
  }

  // Rills: fine converging flow lines on the slopes (sharp, after the droplets); their drainage
  // area becomes the wet-line flow map.
  let rills = null;
  if (params.rillStrength > 0) {
    progress({ phase: 'Cutting rills', fraction: 0 });
    const preRill = protectMask ? height.slice() : null;
    rills = rillErosion(height, hardness, N, params.worldSize, {
      strength: params.rillStrength, iterations: params.rillSteps, seaLevel: params.waterEnabled ? params.seaLevel : -Infinity, seed: params.seed,
    }, (f) => progress({ phase: 'Cutting rills', fraction: f }));
    // protected faces keep the (re-cut) wall; the rills stay on the ground above and below
    if (preRill) for (let i = 0; i < N * N; i++) { const a = protectMask[i] * protect; if (a > 0) height[i] += (preRill[i] - height[i]) * a; }
  }

  // droplet fans dam the valley floors into chains of shallow pits — silt them up so the drainage
  // stays integrated (deep basins remain as lakes)
  if ((fluvial || rills) && params.fluvialPits > 0) fillShallowPits(height, N, params.waterEnabled ? params.seaLevel : -Infinity, params.fluvialPits, 0.2, cell);

  if (rivers.length) {
    // Restore the bed to its profile after erosion/slumping and take the final mask + water level.
    progress({ phase: 'Settling river beds', fraction: 0 });
    riverResult = carveRivers(height, N, params.worldSize, rivers, riverOpts, params.seed);
  }

  // Simulated drainage: the drawn rivers are guides (already carved + injected as flow); the
  // network itself comes from the eroded surface.
  let hydro = null;
  const lake = new Float32Array(N * N);
  // alluvium (0..1): fluvial sediment thickness relative to the "shows at" thickness — floodplains,
  // fans and silted basins; the rivers braid on it and the surface paints it as silt / sand
  const alluvium = new Float32Array(N * N);
  if (fluvial && fluvial.sediment) {
    const sedScale = Math.max(0.5, params.fluvialSedimentShow == null ? 2.5 : params.fluvialSedimentShow);
    const sed = blurField(fluvial.sediment, N, 1);
    for (let i = 0; i < N * N; i++) { const t = Math.min(1, sed[i] / sedScale); alluvium[i] = t * t * (3 - 2 * t); }
  }
  if (params.riverSim) {
    progress({ phase: 'Simulating rivers', fraction: 0 });
    hydro = simulateRivers(height, N, params.worldSize, {
      catchment: params.riverCatchment, widthScale: params.riverWidthScale, maxWidth: params.riverMaxWidth, depthScale: params.riverDepthScale,
      bankAngle: params.riverBank, maxBank: params.riverMaxBank, waterDepth: params.riverWaterFrac, braiding: params.riverBraiding, drySlope: params.riverDrySlope, dryBig: params.riverDryBig,
      lakes: params.riverLakes, lakeFill: params.riverLakeFill, lakeMaxArea: (params.riverLakeMax == null ? 8 : params.riverLakeMax) / 100, lakeMinArea: Math.round((params.riverLakeMin || 0.01) * 1e6 / (cell * cell)),
      seaLevel: params.waterEnabled ? params.seaLevel : -Infinity, sources: riverResult.sources, guideFlow: params.riverGuideFlow,
      alluvium, floodplain: params.riverFloodplain,
    }, params.seed);
    for (let i = 0; i < N * N; i++) {
      riverResult.riverMask[i] = Math.max(riverResult.riverMask[i], hydro.riverMask[i]);
      riverResult.waterLevel[i] = Math.max(riverResult.waterLevel[i], hydro.waterLevel[i]);
      lake[i] = hydro.lakeMask[i];
    }
    if (params.riverBankErosion > 0) {
      progress({ phase: 'Gullying the banks', fraction: 0 });
      bankErosion(height, hardness, N, params.worldSize, {
        mask: hydro.bankMask, floor: hydro.waterLevel, strength: params.riverBankErosion, iterations: params.riverBankSteps,
        seaLevel: params.waterEnabled ? params.seaLevel : -Infinity, seed: params.seed,
      }, (f) => progress({ phase: 'Gullying the banks', fraction: f }));
    }
  }

  progress({ phase: 'Deriving shading maps', fraction: 0 });

  // Deposit: hydraulic sediment + thermally slumped material → scree/gravel aprons.
  const depositRaw = new Float32Array(N * N);
  for (let i = 0; i < N * N; i++) depositRaw[i] = Math.max(0, delta[i]) + slumped[i] * 0.6;
  const deposit = normalisePercentile(blurField(depositRaw, N, 2), 0.985);

  // Flow accumulation on a log scale → wet streaks, darkened gully floors.
  const flowRaw = new Float32Array(N * N);
  const fluvialFlow = new Float32Array(N * N);
  for (let i = 0; i < N * N; i++) flowRaw[i] = Math.log1p(flow[i]);
  const accMap = rills ? rills.acc : fluvial ? fluvial.acc : null;
  if (accMap) {
    // stream-power drainage area → flow lines and valley floors read as wet even where droplets were sparse
    let hiAcc = 1; for (let i = 0; i < N * N; i++) if (accMap[i] > hiAcc) hiAcc = accMap[i];
    const lh = Math.log1p(hiAcc);
    for (let i = 0; i < N * N; i++) { const t = Math.log1p(accMap[i] - 1) / lh; fluvialFlow[i] = Math.max(0, Math.min(1, (t - 0.3) / 0.7)); }
  }
  const flowNorm = normalisePercentile(blurField(flowRaw, N, 1), 0.995);
  if (accMap) for (let i = 0; i < N * N; i++) flowNorm[i] = Math.max(flowNorm[i], fluvialFlow[i] * fluvialFlow[i] * 0.8);
  if (hydro) {
    // drainage network → wet gully floors; channels and lake shores → gravel / silt deposits
    for (let i = 0; i < N * N; i++) {
      const f = hydro.flow[i];
      flowNorm[i] = Math.max(flowNorm[i], f * f * 0.95);
      deposit[i] = Math.max(deposit[i], riverResult.riverMask[i] * 0.8, lake[i] * 0.5);
    }
  }

  // fluvial sediment: fans, valley fills and basin floors are alluvium — fine sediment (the
  // silt / sand channel the surface shares with lake beds) with some gravel in it
  for (let i = 0; i < N * N; i++) {
    const a = alluvium[i];
    if (a <= 0) continue;
    lake[i] = Math.max(lake[i], a * 0.75);
    deposit[i] = Math.max(deposit[i], a * 0.45);
  }

  // core-stones shed their debris: no scree skin on the boulders themselves
  for (let i = 0; i < N * N; i++) if (outcrop[i] > 0) deposit[i] *= 1 - 0.85 * outcrop[i];

  const cavityRaw = computeCavity(height, N, cell);
  const cavity = new Float32Array(N * N);
  for (let i = 0; i < N * N; i++) cavity[i] = Math.max(-1, Math.min(1, cavityRaw[i] * 1.5));

  const slope = computeSlopeMap(height, N, cell);

  let min = Infinity, max = -Infinity;
  for (let i = 0; i < N * N; i++) {
    if (height[i] < min) min = height[i];
    if (height[i] > max) max = height[i];
  }

  progress({ phase: 'Done', fraction: 1 });
  return {
    resolution: N,
    worldSize: params.worldSize,
    height, hardness, deposit, flow: flowNorm, cavity, slope,
    river: riverResult.riverMask, waterLevel: riverResult.waterLevel, lake, outcrop,
    stats: { min, max, elapsedMs: now() - t0, rivers: hydro ? hydro.stats : null },
  };
}

function normalisePercentile(field, pct) {
  const sorted = Float32Array.from(field).sort();
  const hi = sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * pct))] || 1e-6;
  const out = new Float32Array(field.length);
  for (let i = 0; i < field.length; i++) out[i] = Math.min(1, field[i] / hi);
  return out;
}

function now() {
  return typeof performance !== 'undefined' ? performance.now() : Date.now();
}

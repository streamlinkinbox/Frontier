// Base heightfield synthesis: domain-warped ridged multifractal mountains, continental
// relief mask, optional canyon incision, mesa plateaus and — the cliff-maker — layered
// strata terracing with per-layer hardness (hard caprock → vertical face, soft layer → slope).
//
// Heights are in metres. The grid covers worldSize × worldSize metres, resolution N × N.

import { SimplexNoise, hash2, smoothstep, lerp, clamp01 } from './noise.js';
import { makeBedTable, bedAt, bedJitter, bedHardness } from './strata-model.js';

export function synthesizeBase(params, progress = () => {}) {
  const N = params.resolution;
  const size = params.worldSize;
  const noise = new SimplexNoise(params.seed);
  const warpNoise = new SimplexNoise(params.seed * 7 + 13);
  const maskNoise = new SimplexNoise(params.seed * 3 + 101);
  const height = new Float32Array(N * N);

  const freq = params.baseFrequency;
  const warpAmp = params.warpStrength * 0.35;
  const invN = 1 / (N - 1);

  // Canyon centreline wanders with low-frequency noise so the gorge meanders.
  const canyonNoise = new SimplexNoise(params.seed * 11 + 7);
  const cliffH = params.cliffHeight || 0;
  const cliffEdgeW = 0.5 * (1 - 0.97 * Math.min(1, Math.max(0, params.cliffSharpness == null ? 0.8 : params.cliffSharpness)));
  const cliffNoise = new SimplexNoise(params.seed * 13 + 29);
  const cliffStacks = params.cliffStacks || 0;

  // Transverse dunes: asymmetric ridges across the wind, in fields.
  const duneAmp = params.duneAmount || 0;
  const duneNoise = new SimplexNoise(params.seed * 17 + 23);
  const duneDir = ((params.duneDirection || 0) * Math.PI) / 180;
  const duneCos = Math.cos(duneDir), duneSin = Math.sin(duneDir);
  const duneLambda = Math.max(10, params.duneWavelength || 140);
  const crest = Math.min(0.95, Math.max(0.5, params.duneAsymmetry == null ? 0.68 : params.duneAsymmetry));
  const duneCover = params.duneCoverage == null ? 0.6 : params.duneCoverage;

  // Rock stacks (Gaea "Stacks"): tiered towers of rock — each tier a thresholded blob of a
  // warped mask, every tier smaller than the one below, with its own thickness, edge and offset.
  const stacks = (params.stackHeight || 0) > 0 ? makeStacks(params) : null;

  for (let j = 0; j < N; j++) {
    const v = j * invN;
    for (let i = 0; i < N; i++) {
      const u = i * invN;

      // Domain warp — bends ridgelines so they stop looking like noise contours.
      const wx = warpNoise.fbm(u * freq * 0.8 + 3.1, v * freq * 0.8 + 1.7, 4, 2, 0.5);
      const wy = warpNoise.fbm(u * freq * 0.8 - 5.3, v * freq * 0.8 + 8.9, 4, 2, 0.5);
      const px = u * freq + wx * warpAmp;
      const py = v * freq + wy * warpAmp;

      // Ridged mountains with a touch of fbm so that lowlands have gentle rolling relief.
      let r = noise.ridged(px, py, 9, 2.07, 0.5, params.ridgeSharpness);
      r = Math.pow(r, params.peakPower);
      const rolling = 0.5 + 0.5 * noise.fbm(px * 2.3 + 40, py * 2.3 - 12, 5, 2, 0.55);

      // Continental mask: where mountains rise vs. plains / sea floor.
      const m = 0.5 + 0.5 * maskNoise.fbm(u * params.reliefFrequency + 0.3, v * params.reliefFrequency - 0.6, 4, 2, 0.5);
      const mask = smoothstep(0.5 - params.reliefContrast * 0.5, 0.5 + params.reliefContrast * 0.5, m);

      let h = (r * mask + rolling * 0.08 * (1 - mask * 0.5)) * params.mountainHeight;
      h += (mask - 0.5) * params.mountainHeight * 0.25;
      h += params.baseElevation;

      // Escarpment: a near-vertical cliff line along the edge of the massif (coastal cliffs, quarry
      // walls). The continental mask is thresholded very sharply and the ground on the high side
      // is lifted onto a bench, so the relief drops by `cliffHeight` within a cell or two; the
      // line is pushed and pulled in plan by the rugged stage that follows (headlands, coves).
      if (cliffH > 0) {
        // the cliff line follows a smoother version of the mask (long headlands and bays, not the
        // fractal fringe of the full mask) with a little fine wobble; the rugged stage adds the
        // blocky push–pull afterwards
        const mEdge = 0.5 + 0.5 * maskNoise.fbm(u * params.reliefFrequency + 0.3, v * params.reliefFrequency - 0.6, 2, 2, 0.5);
        const me = mEdge + 0.03 * cliffNoise.fbm(u * 7 + 4, v * 7 - 2, 3);
        let edge = smoothstep(0.5 - cliffEdgeW, 0.5 + cliffEdgeW, me);
        // stacks: pillars of the former cliff left standing just off the line (the sea has cut
        // the arch behind them away) — blobs of a fine noise inside a narrow band seaward of the edge
        if (cliffStacks > 0 && me < 0.5) {
          const band = smoothstep(0.5 - 0.06, 0.5 - 0.03, me) * (1 - smoothstep(0.5 - 0.018, 0.5 - 0.004, me));
          const blob = 0.5 + 0.5 * cliffNoise.fbm(u * 22 + 9, v * 22 + 3, 2, 2, 0.5);
          const stack = smoothstep(0.9 - 0.12 * cliffStacks, 0.94 - 0.12 * cliffStacks, blob) * band;
          edge = Math.max(edge, stack * (0.8 + 0.2 * blob));
        }
        h += cliffH * edge;
      }

      // Mesa plateau — soft minimum clamps peaks into flat tops.
      if (params.plateauStrength > 0) {
        const cap = params.plateauHeight;
        const k = lerp(220, 8, params.plateauStrength);
        const soft = cap - softplus(cap - h, k) + softplus(h - cap, k) * (1 - params.plateauStrength);
        h = lerp(h, soft, params.plateauStrength);
      }

      // Canyon incision — a meandering gorge cut through everything.
      if (params.canyonDepth > 0) {
        const cx = 0.5 + 0.22 * canyonNoise.fbm(v * 1.6 + 2.0, 0.37, 3, 2, 0.5)
          + 0.04 * canyonNoise.fbm(v * 6.0 + 9.0, 1.21, 3, 2, 0.5);
        const d = Math.abs(u - cx) * size;
        const halfWidth = params.canyonWidth * 0.5;
        const profile = 1 - smoothstep(halfWidth * 0.25, halfWidth, d);
        const floorNoise = 1 + 0.15 * noise.fbm(u * 20, v * 20, 3);
        h -= params.canyonDepth * profile * floorNoise;
      }

      if (stacks) h += stacks.at(u, v);

      if (duneAmp > 0) {
        const xw = (u - 0.5) * size, zw = (v - 0.5) * size;
        // along-wind coordinate, bent by low-frequency noise so crests curve (barchanoid ridges)
        const bend = duneNoise.fbm(u * 3 + 11, v * 3 - 4, 3) * 0.6 + duneNoise.fbm(u * 9 + 2, v * 9 + 6, 2) * 0.12;
        const sCoord = (xw * duneCos + zw * duneSin) / duneLambda + bend;
        const fr = sCoord - Math.floor(sCoord);
        // long windward slope up to the crest, short slip face down (at the angle of repose it is the
        // thermal pass that finishes the slip face)
        const profile = duneProfile(fr, crest, duneLambda, duneAmp);
        // dune fields come and go
        const fieldMask = smoothstep(0.5 - duneCover * 0.5, 0.5 + duneCover * 0.35, 0.5 + 0.5 * duneNoise.fbm(u * 2.2 + 31, v * 2.2 + 7, 3));
        // compound dunes: a smaller set of dunes climbing the windward slope of the big ones
        const s2 = sCoord * 2.6 + bend * 2.5 + duneNoise.fbm(u * 6 + 17, v * 6 - 9, 2) * 0.8;
        const fr2 = s2 - Math.floor(s2);
        const minor = (fr2 < 0.7 ? Math.pow(fr2 / 0.7, 1.5) : 1 - smoothstep(0.7, 1, fr2)) * 0.07 * smoothstep(0.15, 0.6, fr) * (fr < crest ? 1 : 0.1);
        const local = 0.75 + 0.25 * duneNoise.fbm(u * 5 + 3, v * 5 + 9, 2);
        h += duneAmp * (profile + minor) * fieldMask * local;
      }

      height[j * N + i] = h;
    }
    if ((j & 31) === 0) progress(j / N);
  }
  return height;
}

// Transverse dune cross-section on [0,1): gently concave windward slope steepening to a sharp
// brink, a straight slip face at the angle of repose, a short toe and an interdune flat.
function duneProfile(fr, crest, lambda, amp) {
  if (fr < crest) return Math.pow(fr / crest, 1.7); // steepens all the way to the brink
  // slip face: drop at ~33° with a rounded toe; if the wavelength leaves room the floor stays flat
  const run = (fr - crest) * lambda;
  const slipLen = amp / Math.tan((33 * Math.PI) / 180);
  const t = Math.min(1, run / Math.max(1e-3, slipLen));
  const a = 0.25;
  const y = t <= 1 - a ? 1 - t - a / 2 : (1 - t) * (1 - t) / (2 * a);
  return y / (1 - a / 2);
}

// ---- rock stacks -------------------------------------------------------------------------------
// A stack field is a *scatter*, not a grid: cluster cells each hold one to three features, and a
// feature is an elongated, part-square blob with its own size, height, taper and lean. Sizes are
// drawn from a heavy-tailed distribution, so a few anchor towers stand among many small ones and
// neighbours touch and merge into irregular compounds — which is what keeps the lattice of the
// scatter from reading through. The mask is the smooth union of those blobs; a tier is a contour of
// it, so every tier sits inside the one below. The pile walks in plan from its own foot to its own
// crest (two mask samples, blended per tier) and each tier is wobbled, so the steps are uneven.
function makeStacks(params) {
  const size = params.worldSize;
  const amp = params.stackHeight;
  const levels = Math.max(1, Math.min(16, Math.round(params.stackLevels == null ? 6 : params.stackLevels)));
  const scale = Math.max(10, params.stackScale || 220);
  const taper = params.stackTaper == null ? 0.5 : params.stackTaper;
  const sharp = params.stackSharpness == null ? 0.8 : params.stackSharpness;
  const chaos = params.stackChaos == null ? 0.5 : params.stackChaos;
  const spires = params.stackSpires == null ? 0 : params.stackSpires;
  const cover = params.stackCoverage == null ? 0.5 : params.stackCoverage;
  const pedestal = params.stackPedestal == null ? 0.4 : params.stackPedestal;
  const sizeVar = params.stackSizeVar == null ? 0.6 : params.stackSizeVar;
  const elong = params.stackElongation == null ? 0.4 : params.stackElongation;
  const grouping = params.stackCluster == null ? 0.5 : params.stackCluster;
  const seed = params.seed | 0;
  const n2 = new SimplexNoise(seed * 29 + 17);
  // tier table: thickness (its own bed table), threshold, edge width and a plan step of every tier
  const tTop = 0.85 * taper;
  const tiers = [];
  let sum = 0;
  for (let k = 0; k < levels; k++) {
    const thick = 0.45 + 1.1 * hash2(k, 1, seed + 5);
    sum += thick;
    const ang = hash2(k, 2, seed + 5) * Math.PI * 2;
    tiers.push({
      thick,
      t: levels > 1 ? (tTop * k) / (levels - 1) : 0,
      w: lerp(0.16, 0.015, sharp) * (0.6 + 0.8 * hash2(k, 3, seed + 5)),
      ox: Math.cos(ang) * chaos * 0.045, oz: Math.sin(ang) * chaos * 0.045,
      ph: k * 7.3,
    });
  }
  for (const tier of tiers) tier.h = (tier.thick / sum) * amp;
  const f = size / scale; // one unit = the stack size
  const cs = lerp(1.0, 0.52, spires)                 // feature spacing, in stack units
    , rMul = lerp(1.0, 0.78, spires)
    , kS = 0.07;                                      // smooth-union softness
  const out = new Float64Array(5);                   // mask, height factor, taper, lean x, lean z
  function blobs(px, pz) {
    const cx = Math.floor(px / cs), cz = Math.floor(pz / cs);
    let acc = 0, wsum = 0, hs = 0, tf = 0, lx = 0, lz = 0;
    for (let dz = -2; dz <= 2; dz++) for (let dx = -2; dx <= 2; dx++) {
      const ix = cx + dx, iz = cz + dz;
      if (hash2(ix, iz, seed + 43) > 0.84 - 0.16 * grouping) continue; // some cells are bare ground
      const h0 = hash2(ix * 3, iz * 5 + 2, seed + 61);
      // clustering: a cell holds one tower, or a tight group of them, or nothing — never a row
      const n = grouping > 0.02 ? (h0 < 0.3 + 0.34 * grouping ? 3 : h0 < 0.56 + 0.2 * grouping ? 2 : 1) : (h0 < 0.8 ? 1 : 0);
      // anchor, jittered inside its cell (never on the lattice)
      const ax0 = (ix + 0.16 + 0.68 * hash2(ix, iz, seed + 31)) * cs;
      const az0 = (iz + 0.16 + 0.68 * hash2(ix, iz, seed + 37)) * cs;
      for (let i = 0; i < n; i++) {
        const hh = hash2(ix * 7 + i * 5, iz * 13 - i * 3, seed + 71);
        // the rest of a group leans against the first one, so towers touch but stay recognisable
        const off = i === 0 ? 0 : cs * (0.2 + 0.2 * hh);
        const oa = hash2(ix + i * 11, iz - i * 7, seed + 83) * Math.PI * 2;
        const fx = ax0 + Math.cos(oa) * off, fz = az0 + Math.sin(oa) * off;
        const ddx = px - fx, ddz = pz - fz;
        const far = cs * 1.6 + off;
        if (ddx * ddx + ddz * ddz > far * far) continue; // cheap reject before shaping
        const hA = hash2(ix + i, iz * 3 + 1, seed + 41), hB = hash2(ix * 5 - i, iz + i * 2, seed + 97);
        const hC = hash2(ix - i * 4, iz * 11 + i, seed + 103), hD = hash2(ix * 3 + i, iz + i * 7, seed + 109);
        // heavy-tailed size: most stacks are small, a few are the anchors of their district
        const R = cs * rMul * (0.2 + 0.15 * hA) * (0.8 + 0.85 * Math.pow(hB, 1 + 2.4 * sizeVar)) * (i === 0 ? 1 : 0.7);
        if (R < 0.03) continue;
        const ca = Math.cos(hC * Math.PI), sa = Math.sin(hC * Math.PI); // its own orientation
        const asp = 1 + elong * 2.2 * hD;                                // elongation / ridge-like
        const sq = 0.12 + 0.62 * hB;                                     // round blob → squarish mesa
        const ax = (ddx * ca + ddz * sa) / (R * asp), az = (-ddx * sa + ddz * ca) / R;
        const ex = Math.abs(ax) < 1e-6 ? 1e-6 : Math.abs(ax), ez = Math.abs(az);
        const e = (1 - sq) * Math.sqrt(ex * ex + ez * ez) + sq * (ex > ez ? ex : ez);
        const val = 1 - e;
        if (val < -3.2 * kS) continue;
        const wgt = Math.exp(val / kS);
        acc += wgt; wsum += wgt;
        // what the dominant features look like: taller / shorter, pointier, leaning one way
        hs += wgt * (0.52 + 0.82 * Math.pow(hA, 1 + 1.7 * sizeVar));
        tf += wgt * (0.5 + 1.0 * hD);
        lx += wgt * Math.cos(hB * Math.PI * 2) * chaos * 0.14 * (0.4 + hC);
        lz += wgt * Math.sin(hB * Math.PI * 2) * chaos * 0.14 * (0.4 + hC);
      }
    }
    // a compound of touching towers has a higher union than a lone one — cap it, so groups grow
    // sideways instead of piling up into a single massif
    out[0] = acc > 0 ? Math.min(1.12, kS * Math.log(acc)) : -2;
    if (wsum <= 0) { out[1] = 1; out[2] = 1; out[3] = 0; out[4] = 0; return out; }
    out[1] = hs / wsum; out[2] = tf / wsum; out[3] = lx / wsum; out[4] = lz / wsum;
    return out;
  }
  function sample(px, pz) {
    // the outlines are pulled around at two scales, so no stack is a disc or a clean ellipse
    const w1 = 0.15 + 0.2 * chaos;
    const wx = n2.fbm(px * 1.1 + 1.3, pz * 1.1 + 5.1, 3) * w1 + n2.fbm(px * 3.6 - 2.2, pz * 3.6 + 7.7, 2) * w1 * 0.5;
    const wz = n2.fbm(px * 1.1 - 7.7, pz * 1.1 + 2.9, 3) * w1 + n2.fbm(px * 3.6 + 4.4, pz * 3.6 - 6.1, 2) * w1 * 0.5;
    return blobs(px + wx, pz + wz);
  }
  function at(u, v) {
    // stack fields come and go
    const edge = 0.5 + (0.5 - cover) * 0.8;
    const c = cover >= 1 ? 1 : smoothstep(edge - 0.08, edge + 0.08, 0.5 + 0.5 * n2.fbm(u * 1.4 + 23, v * 1.4 - 11, 3));
    if (c <= 0.001) return 0;
    const px = u * f, pz = v * f;
    // outside the field the stacks shrink and lose their upper tiers (the mask sinks) rather
    // than being squashed flat
    const sink = (1 - c) * 0.7;
    const o0 = sample(px, pz);
    const m0 = o0[0] - sink;
    if (m0 < -0.6) return 0;
    const hs = Math.max(0.18, Math.min(1.5, o0[1])), tf = o0[2], lx = o0[3], lz = o0[4];
    // the mask at the top of the pile, walked in the stack's own lean direction: the tiers then
    // climb towards it instead of stacking dead vertical
    let m1 = m0;
    if (chaos > 0.01) m1 = sample(px + lx * 2.2, pz + lz * 2.2)[0] - sink;
    let h = 0, below = 1;
    for (let k = 0; k < levels; k++) {
      const tier = tiers[k];
      const s = levels > 1 ? k / (levels - 1) : 0;
      // this tier's own outline: the walk plus a wobble, so the tiers do not share one shape
      const mk = m0 + (m1 - m0) * s + chaos * 0.05 * n2.fbm(px * 6 + tier.ph, pz * 6 - tier.ph, 2) + tier.ox * (1 - s) - tier.oz * s;
      // a short stack simply has fewer tiers; a tall one has more (the thresholds ride on its own
      // height and taper instead of the table's)
      const t = tier.t * tf / hs;
      const sk = smoothstep(t - tier.w, t + tier.w, mk) * below;
      // the tier top is not dead flat: it rises a little towards the centre
      h += hs * tier.h * sk * (1 + 0.05 * Math.max(0, mk - t));
      below = sk; // a tier can only stand on the one below it
      if (k === 0 && pedestal > 0) {
        // talus pedestal: a ramp from the ground up to the foot of the lowest tier, bigger under
        // the big stacks
        const ramp = smoothstep(-0.55, -tier.w, m0);
        h += amp * 0.22 * pedestal * Math.pow(ramp, 1.6) * (1 - sk) * (0.45 + 0.75 * Math.min(1.4, hs));
      }
    }
    return h;
  }
  return { at };
}

function softplus(x, k) {
  const kx = k * x;
  if (kx > 30) return x;
  if (kx < -30) return 0;
  return Math.log1p(Math.exp(kx)) / k;
}

// Strata terracing with geological dip. Also produces the hardness field that the erosion
// passes use so that resistant layers survive as cliff bands while soft layers wear to slopes.
export function applyStrata(height, params, progress = () => {}, outcrop = null) {
  const N = params.resolution;
  const size = params.worldSize;
  const hardness = new Float32Array(N * N);
  const band = Math.max(2, params.strataBand);
  const strength = params.strataStrength;
  const invN = 1 / (N - 1);
  const cell = size / (N - 1);

  // Dip plane: tilt strata by dipAngle towards dipDirection.
  const dipRad = (params.strataDip * Math.PI) / 180;
  const dirRad = (params.strataDipDirection * Math.PI) / 180;
  const gx = Math.tan(dipRad) * Math.cos(dirRad);
  const gz = Math.tan(dipRad) * Math.sin(dirRad);

  const detail = new SimplexNoise(params.seed * 5 + 77);
  // beds only show where the ground is steep enough to expose them (≈ 17° → full at 38°); gentle
  // slopes keep their soil / scree profile instead of a contour-line staircase
  const slopeMaskLo = 0.3, slopeMaskHi = 0.78;
  const table = makeBedTable(params);
  const lateral = params.strataLateral == null ? 0.18 : params.strataLateral;
  const bed = {};

  // Precompute slope of the un-terraced field so terracing targets steep ground.
  const slope = new Float32Array(N * N);
  for (let j = 0; j < N; j++) {
    const j0 = Math.max(0, j - 1), j1 = Math.min(N - 1, j + 1);
    for (let i = 0; i < N; i++) {
      const i0 = Math.max(0, i - 1), i1 = Math.min(N - 1, i + 1);
      const dx = (height[j * N + i1] - height[j * N + i0]) / ((i1 - i0) * cell);
      const dz = (height[j1 * N + i] - height[j0 * N + i]) / ((j1 - j0) * cell);
      slope[j * N + i] = Math.sqrt(dx * dx + dz * dz);
    }
  }

  for (let j = 0; j < N; j++) {
    const z = j * invN * size;
    for (let i = 0; i < N; i++) {
      const x = i * invN * size;
      const idx = j * N + i;
      const h = height[idx];

      // Work in the tilted frame so bands dip.
      const tilt = gx * x + gz * z;
      // Lateral thickness jitter keeps layers from looking machine-cut (same expression as the
      // shader and the 3-D chunks).
      const jitter = bedJitter(x, z, lateral);
      bedAt(table, (h + tilt) / jitter, bed);
      const f = bed.f, bi = bed.index;

      // Bed hardness from the stratigraphic column + intra-layer variation (lenses, joints).
      let hard = bedHardness(bed);
      hard = clamp01(hard + 0.12 * detail.fbm(x * 0.006 + bi * 3.7, z * 0.006, 3));
      hard = lerp(0.35, hard, params.hardnessContrast);

      // Terrace profile: gain curve with per-layer steepness; thin beds step less.
      const k = lerp(1.2, 9, hard);
      const fk = Math.pow(f, k);
      const fp = fk / (fk + Math.pow(1 - f, k));
      const thin = smoothstep(0.08, 0.3, bed.thick / band);
      const terraced = (table.tops[bi] - bed.thick + lerp(f, fp, thin) * bed.thick) * jitter - tilt;

      const sm = smoothstep(slopeMaskLo, slopeMaskHi, slope[idx]);
      // hard beds snap almost fully (vertical riser, flat bench); soft beds keep more of the slope;
      // and a bed's expression varies along strike (benches pinch out, ledges come and go) so the
      // hillside is not a machine-cut staircase
      const expr = 0.55 + 0.45 * (0.5 + 0.5 * detail.fbm(x * 0.0045 + bi * 1.9, z * 0.0045 - bi * 0.7, 2));
      let amount = Math.min(0.95, strength * sm * lerp(0.5, 1.1, hard) * expr);
      if (outcrop && outcrop[idx] > 0) amount *= 1 - outcrop[idx]; // massive core-stones are not bedded
      height[idx] = lerp(h, terraced, amount);
      hardness[idx] = hard;
    }
    if ((j & 31) === 0) progress(j / N);
  }
  return hardness;
}

export function computeSlopeMap(height, N, cell) {
  const out = new Float32Array(N * N);
  for (let j = 0; j < N; j++) {
    const j0 = Math.max(0, j - 1), j1 = Math.min(N - 1, j + 1);
    for (let i = 0; i < N; i++) {
      const i0 = Math.max(0, i - 1), i1 = Math.min(N - 1, i + 1);
      const dx = (height[j * N + i1] - height[j * N + i0]) / ((i1 - i0) * cell);
      const dz = (height[j1 * N + i] - height[j0 * N + i]) / ((j1 - j0) * cell);
      out[j * N + i] = Math.sqrt(dx * dx + dz * dz);
    }
  }
  return out;
}

// Cavity / convexity from a smoothed Laplacian; negative in gullies, positive on crests.
export function computeCavity(height, N, cell) {
  const out = new Float32Array(N * N);
  const radius = 2;
  for (let j = 0; j < N; j++) {
    for (let i = 0; i < N; i++) {
      const c = height[j * N + i];
      let sum = 0, count = 0;
      for (let dj = -radius; dj <= radius; dj++) {
        const jj = Math.min(N - 1, Math.max(0, j + dj));
        for (let di = -radius; di <= radius; di++) {
          if (di === 0 && dj === 0) continue;
          const ii = Math.min(N - 1, Math.max(0, i + di));
          sum += height[jj * N + ii];
          count++;
        }
      }
      // Positive = convex (ridge), negative = concave (gully). Normalised by cell size.
      out[j * N + i] = (c - sum / count) / (cell * radius);
    }
  }
  return out;
}

// Where rocks come from, and where they end up.
//
// A real landscape is never an even scatter of stones. Bedrock shows through only where soil cannot
// cling; a cliff sheds its debris, which piles up at the foot and fines away down the apron; a
// stream keeps a coarse lag only where it has the power to move it and drops bars where it slows;
// a resistant bed armours a hill top as a cap or a tor; the sea lays a cord of cobbles along its
// shore; and everything else is buried. So the placer reads those processes off the simulated
// fields — slope, hardness, deposit, flow, river, water level — and puts a rock only where its
// origin says one belongs. Each category has its own size law, shape family, embedment, orientation
// and colour, and each one is dialled in the "Rock placement" card.
//
// Every candidate point picks its *dominant* category (the strongest signal at that spot), so the
// categories compete instead of stacking up on top of each other.

import * as THREE from 'three';
import { mulberry32, smoothstep, clamp01, lerp, hash2 } from './noise.js';
import { makeSampler } from './terrain-geometry.js';

const MAX_INSTANCES = 20000;
const MAX_PEBBLES = 40000;
const NO_WATER = -1e6;

// ---- derived fields ------------------------------------------------------------------------------
// One pass over the grid, cached on the field. Cheap filters and a single steepest-descent sweep,
// so this stays in the tens of milliseconds even at 1024².
export function rockFields(field, params = {}) {
  const ck = `${params.waterEnabled ? 1 : 0}|${params.seaLevel}|${params.riverWater ? 1 : 0}|${params.lakeWater ? 1 : 0}`;
  if (field._rockF && field._rockFKey === ck) return field._rockF;
  const N = field.resolution, size = field.worldSize;
  const cell = size / (N - 1), total = N * N;
  const h = field.height, sl = field.slope, dp = field.deposit, hd = field.hardness;
  const flow = field.flow || null, river = field.river || null, wlR = field.waterLevel || null;
  // the water surface the shore is measured against: the rivers / lakes of the simulation and, if
  // the sea is on, the sea level itself
  const sea = params.waterEnabled ? (params.seaLevel || 0) : NO_WATER * 0.5;
  let wl = wlR;
  if (sea > NO_WATER * 0.4) {
    wl = new Float32Array(total);
    for (let i = 0; i < total; i++) wl[i] = Math.max(wlR ? wlR[i] : NO_WATER, h[i] < sea ? sea : NO_WATER);
  }

  const face = new Float32Array(total);   // bare rock on a steep, soil-free, hard slope
  const soil = new Float32Array(total);   // fine cover that hides rock
  const crest = new Float32Array(total); // relief above the surrounding ground (m, normalised)
  const apron = new Float32Array(total); // debris arriving from the face above
  const aSize = new Float32Array(total); // characteristic block size there (m)
  const aDist = new Float32Array(total); // cells travelled from the source
  const power = new Float32Array(total); // stream power proxy inside the channel
  const brake = new Float32Array(total); // where the flow decelerates → bars, fans
  const shore = new Float32Array(total); // in the swash band of standing water

  // separable min-filter for the local base level (relief), then the face/soil masks
  const tmp = new Float32Array(total);
  const rad = Math.max(5, Math.round(N / 28));
  for (let j = 0; j < N; j++) {
    for (let i = 0; i < N; i++) {
      let m = h[j * N + i];
      for (let d = -rad; d <= 0; d++) { const ii = i + d; if (ii >= 0) m = Math.min(m, h[j * N + ii]); }
      for (let d = 1; d <= rad; d++) { const ii = i + d; if (ii < N) m = Math.min(m, h[j * N + ii]); }
      tmp[j * N + i] = m;
    }
  }
  let reliefMax = 1e-3;
  for (let j = 0; j < N; j++) {
    for (let i = 0; i < N; i++) {
      let m = tmp[j * N + i];
      for (let d = -rad; d <= rad; d++) { const jj = j + d; if (jj >= 0 && jj < N) m = Math.min(m, tmp[jj * N + i]); }
      const r = Math.max(0, h[j * N + i] - m);
      crest[j * N + i] = r;
      if (r > reliefMax) reliefMax = r;
    }
  }
  for (let i = 0; i < total; i++) crest[i] = clamp01(crest[i] / Math.max(8, reliefMax * 0.55));

  for (let i = 0; i < total; i++) {
    const slope = sl[i] || 0;
    const soft = dp ? clamp01(dp[i] * 1.6) : 0;
    soil[i] = smoothstep(0.02, 0.4, soft) * (1 - 0.8 * smoothstep(0.55, 1.6, slope));
    const hard = hd ? clamp01(hd[i]) : 0.6;
    face[i] = smoothstep(0.85, 2.1, slope) * (0.35 + 0.65 * hard) * (1 - 0.75 * soil[i]);
    const s = Math.sqrt(slope * slope) ;
    power[i] = flow ? Math.sqrt(clamp01(flow[i])) * Math.min(4, s) : 0;
    if (wl) {
      const w = wl[i];
      if (w > NO_WATER * 0.5) {
        const dw = w - h[i]; // > 0: under water
        shore[i] = (1 - smoothstep(0.25, 1.1, slope)) * smoothstep(3.0, 0.4, Math.abs(dw - 0.3));
      }
    }
  }

  // where the flow slows down: compare the slope with the steepest upslope neighbour
  if (flow && sl) {
    for (let j = 1; j < N - 1; j++) {
      for (let i = 1; i < N - 1; i++) {
        const idx = j * N + i;
        const f = clamp01(flow[idx]);
        if (f < 0.05) continue;
        let up = 0;
        for (let d = 0; d < 4; d++) {
          const a = idx + (d === 0 ? -1 : d === 1 ? 1 : d === 2 ? -N : N);
          if (h[a] > h[idx] && sl[a] > up) up = sl[a];
        }
        brake[idx] = smoothstep(0.05, 0.9, up - sl[idx]) * smoothstep(0.05, 0.45, f);
        if (river) brake[idx] *= 0.35 + 0.65 * clamp01(river[idx] * 1.5);
      }
    }
  }

  // ---- the talus sweep: each face cell supplies the drop of its own step, and debris is pushed
  // down the fall line, losing most of it at the foot and breaking smaller as it travels
  const supply = new Float32Array(total);
  for (let j = 1; j < N - 1; j++) {
    for (let i = 1; i < N - 1; i++) {
      const idx = j * N + i;
      const fa = face[idx];
      if (fa < 0.12) continue;
      let drop = 0;
      for (let d = 0; d < 4; d++) {
        const a = idx + (d === 0 ? -1 : d === 1 ? 1 : d === 2 ? -N : N);
        if (h[a] < h[idx]) drop = Math.max(drop, h[idx] - h[a]);
      }
      supply[idx] = drop * fa;
      apron[idx] = supply[idx];
      aSize[idx] = 1.2 + Math.min(26, drop * 1.6);
    }
  }
  // bucket sort by height (descending) so the sweep is a single pass
  const lo = field.stats ? field.stats.min : 0, hi = field.stats ? field.stats.max : 1;
  const bins = Math.min(4096, Math.max(256, N * 2));
  const scale = (bins - 1) / Math.max(1e-3, hi - lo);
  const counts = new Int32Array(bins + 1);
  const binOf = new Int32Array(total);
  for (let i = 0; i < total; i++) {
    const b = Math.max(0, Math.min(bins - 1, ((h[i] - lo) * scale) | 0));
    binOf[i] = b; counts[b + 1]++;
  }
  for (let b = 0; b < bins; b++) counts[b + 1] += counts[b];
  const order = new Int32Array(total);
  const fill = counts.slice();
  for (let i = 0; i < total; i++) order[fill[binOf[i]]++] = i;
  for (let b = 0; b < bins; b++) {
    const s0 = counts[b], s1 = counts[b + 1];
    for (let a = s0; a < s1; a++) {
      const idx = order[a];
      const a0 = apron[idx];
      if (a0 < 0.04) continue;
      const carry0 = a0 * 0.86, carry1 = a0 * 0.6;
      // push to the lowest neighbour
      let best = -1, bh = h[idx];
      const j = (idx / N) | 0, i = idx - j * N;
      if (i > 0 && h[idx - 1] < bh) { bh = h[idx - 1]; best = idx - 1; }
      if (i < N - 1 && h[idx + 1] < bh) { bh = h[idx + 1]; best = idx + 1; }
      if (j > 0 && h[idx - N] < bh) { bh = h[idx - N]; best = idx - N; }
      if (j < N - 1 && h[idx + N] < bh) { bh = h[idx + N]; best = idx + N; }
      if (best < 0) continue;
      if (carry0 > apron[best]) {
        apron[best] = carry0;
        aSize[best] = aSize[idx] * 0.9;
        aDist[best] = aDist[idx] + 1;
      }
      // and a lesser share to the second-lowest neighbour, so the skirt spreads sideways as it
      // goes down instead of running in a single line down the fall line
      let second = -1, sh = Infinity;
      if (i > 0 && h[idx - 1] < h[idx] && h[idx - 1] < bh) { /* nothing */ }
      for (let d = 0; d < 4; d++) {
        const nb = idx + (d === 0 ? -1 : d === 1 ? 1 : d === 2 ? -N : N);
        const jj = (nb / N) | 0, ii = nb - jj * N;
        if (ii <= 0 || ii >= N - 1 || jj <= 0 || jj >= N - 1) continue;
        if (h[nb] >= h[idx] || nb === best) continue;
        if (h[nb] < sh) { sh = h[nb]; second = nb; }
      }
      if (second >= 0 && carry1 > apron[second]) {
        apron[second] = carry1;
        aSize[second] = aSize[idx] * 0.9;
        aDist[second] = aDist[idx] + 1;
      }
    }
  }
  for (let i = 0; i < total; i++) apron[i] *= 1 - 0.7 * face[i]; // the skirt lies below the face
  // every signal is scaled by its own 95th percentile, so the categories compete on equal terms
  // (otherwise a field with a wide range, e.g. the apron on a big cliff, wins everywhere)
  const norm = (arr) => {
    const step = Math.max(1, (total / 90000) | 0);
    const sample = [];
    let mx = 1e-6;
    for (let i = 0; i < total; i += step) if (arr[i] > 0) { sample.push(arr[i]); if (arr[i] > mx) mx = arr[i]; }
    if (sample.length < 8) return 1;
    sample.sort((a, b) => a - b);
    const p95 = sample[Math.min(sample.length - 1, Math.floor(sample.length * 0.95))] || mx * 0.2;
    return 1 / Math.max(1e-5, p95);
  };
  const kFace = norm(face), kApron = norm(apron), kPow = norm(power), kBrake = norm(brake), kCrest = norm(crest), kShore = norm(shore);
  for (let i = 0; i < total; i++) {
    face[i] = clamp01(face[i] * kFace);
    apron[i] = clamp01(apron[i] * kApron);
    power[i] = clamp01(power[i] * kPow);
    brake[i] = clamp01(brake[i] * kBrake);
    crest[i] = clamp01(crest[i] * kCrest);
    shore[i] = clamp01(shore[i] * kShore);
  }
  void soil;

  const f = { N, cell, size, face, soil, crest, apron, aSize, aDist, power, brake, shore, water: wl };
  // bilinear taps for the placer (same convention as the terrain sampler, on this grid)
  const bil = (arr, gx, gz) => {
    const i = Math.max(0, Math.min(N - 2, gx | 0)), j = Math.max(0, Math.min(N - 2, (gz | 0)));
    const u = Math.min(1, Math.max(0, gx - i)), v = Math.min(1, Math.max(0, gz - j));
    const idx = j * N + i;
    return arr[idx] * (1 - u) * (1 - v) + arr[idx + 1] * u * (1 - v) + arr[idx + N] * (1 - u) * v + arr[idx + N + 1] * u * v;
  };
  f.at = (arr, x, z) => bil(arr, (x / size + 0.5) * (N - 1), (z / size + 0.5) * (N - 1));
  field._rockF = f;
  field._rockFKey = ck;
  return f;
}

// ---- the placer ----------------------------------------------------------------------------------
export function placeRocks(field, params, library) {
  const rf = rockFields(field, params);
  const sampler = makeSampler(field);
  const size = field.worldSize;
  const rand = mulberry32((params.rockSeed | 0) * 7919 + 1);

  const density = Math.max(0, params.rockDensity || 0);
  const w = {
    face: Math.max(0, params.rockInPlace == null ? 0.8 : params.rockInPlace),
    talus: Math.max(0, params.rockTalus == null ? 0.85 : params.rockTalus),
    channel: Math.max(0, params.rockChannel == null ? 0.7 : params.rockChannel),
    bar: Math.max(0, params.rockBars == null ? 0.6 : params.rockBars),
    cap: Math.max(0, params.rockCaps == null ? 0.5 : params.rockCaps),
    erratic: Math.max(0, params.rockErratics == null ? 0.22 : params.rockErratics),
    shore: Math.max(0, params.rockShore == null ? 0.55 : params.rockShore),
  };
  const soilHide = params.rockSoilHide == null ? 0.7 : clamp01(params.rockSoilHide);
  const spread = params.rockSizeSpread == null ? 0.7 : clamp01(params.rockSizeSpread);
  const clustering = params.rockClustering == null ? 0.5 : clamp01(params.rockClustering);
  const clusterScale = Math.max(10, params.rockClusterScale || 80);
  const tiltFollow = params.rockTilt == null ? 0.7 : params.rockTilt;
  const embed = params.rockEmbed == null ? 0.45 : params.rockEmbed;
  const angularity = params.rockAngularity == null ? 0.7 : params.rockAngularity;
  const sizeMin = Math.max(0.05, params.rockSizeMin || 0.8);
  const sizeMax = Math.max(sizeMin + 0.1, params.rockSizeMax || 9);
  const sdfW = field.sdfWeight || null;
  const riverM = field.river || null;
  const wlM = field.waterLevel || null;
  const veg = clamp01(params.vegetation || 0), dry = clamp01(params.dryness || 0);

  // heavy-tailed size: most clasts are small, a few are the ones you notice
  const tail = (k) => Math.pow(rand(), 1 + 3.2 * spread * k);

  const up = new THREE.Vector3(0, 1, 0);
  const q = new THREE.Quaternion(), qYaw = new THREE.Quaternion(), qTilt = new THREE.Quaternion(), qTumble = new THREE.Quaternion();
  const m = new THREE.Matrix4(), p = new THREE.Vector3(), s = new THREE.Vector3(), nrm = new THREE.Vector3(), axis = new THREE.Vector3(), target = new THREE.Vector3();

  const placements = [];
  const cats = { face: 0, talus: 0, channel: 0, bar: 0, cap: 0, erratic: 0, shore: 0, pebble: 0 };
  if (density <= 0 || !params.rocksEnabled) { placements.stats = cats; return placements; }

  // The candidate grid is fixed and fine; 'Density' decides how many of the geologically eligible
  // sites are used, so lowering it thins everything out instead of only the weak signals.
  const steps = Math.max(16, Math.min(260, Math.round(88 + 70 * density)));
  const rate = 0.1 + 1.2 * density;
  const spacing = size / steps;
  const pick = (set, family) => {
    const list = library.byFamily && library.byFamily[set] && library.byFamily[set][family];
    if (!list || !list.length) { const arr = library[set]; return { index: (rand() * arr.length) | 0, geometry: arr[0] }; }
    const index = list[(rand() * list.length) | 0];
    return { index, geometry: library[set][index] };
  };
  // a little push along the joint lattice, so boulders line up in rows instead of a random litter
  const jointShift = (x, z, k) => {
    const g = Math.max(6, (params.rockSizeMax || 9) * 2.4);
    const hx = hash2(Math.floor(x / g), Math.floor(z / g), (params.rockSeed | 0) + 5) - 0.5;
    const hz = hash2(Math.floor(x / g), Math.floor(z / g), (params.rockSeed | 0) + 9) - 0.5;
    return [x + hx * g * 0.5 * k, z + hz * g * 0.5 * k];
  };

  for (let j = 0; j < steps && placements.length < MAX_INSTANCES; j++) {
    for (let i = 0; i < steps && placements.length < MAX_INSTANCES; i++) {
      let x = (i + 0.5 + (rand() - 0.5) * 0.95) * spacing - size * 0.5;
      let z = (j + 0.5 + (rand() - 0.5) * 0.95) * spacing - size * 0.5;
      if (Math.abs(x) > size * 0.495 || Math.abs(z) > size * 0.495) continue;
      const hh = sampler.height(x, z);
      if (params.waterEnabled && hh < (params.seaLevel || 0) - 1) continue;
      if (field.road && sampler.map('road', x, z) > 0.2) continue;
      let wet = 0;
      if (wlM) { const wl = sampler.map('waterLevel', x, z); if (wl > NO_WATER * 0.5) { if (wl > hh + 0.4) continue; wet = 1 - smoothstep(0, 1.2, wl - hh); } }

      // ---- the signals at this point
      const faceV = rf.at(rf.face, x, z), soilV = rf.at(rf.soil, x, z), crestV = rf.at(rf.crest, x, z);
      const apronV = rf.at(rf.apron, x, z), aSizeV = rf.at(rf.aSize, x, z), aDistV = rf.at(rf.aDist, x, z);
      const powV = rf.at(rf.power, x, z), brakeV = rf.at(rf.brake, x, z), shoreV = rf.at(rf.shore, x, z);
      const riverV = riverM ? smoothstep(0.25, 0.75, sampler.map('river', x, z)) : 0;
      const hardV = field.hardness ? clamp01(sampler.map('hardness', x, z)) : 0.6;
      const slopeV = sampler.map('slope', x, z);
      // the true-3D chunks own their faces: no in-situ blocks on them, but their feet still get talus
      const onChunk = sdfW ? smoothstep(0.35, 0.7, sampler.map('sdfWeight', x, z)) : 0;
      // small-scale patchiness, so nothing covers the map evenly
      const pmask = hash2(Math.floor(x / clusterScale), Math.floor(z / clusterScale), (params.rockSeed | 0) + 3);
      const patch = clustering <= 0 ? 1 : smoothstep(clustering * 0.55, clustering * 0.55 + 0.3, 0.35 + 0.65 * pmask);

      const wf = w.face * faceV * (0.35 + 0.85 * hardV) * (1 - onChunk) * (1 - smoothstep(2.4, 3.8, slopeV));
      const wt = w.talus * apronV * 1.7 * (1 - 0.55 * soilV);
      const wc = w.channel * (0.4 + 0.8 * powV) * riverV * (1 - 0.65 * brakeV);
      const wb = w.bar * brakeV * (1 - 0.5 * riverV * powV);
      // a resistant cap on the high ground, or a bare rock *pavement* where soil could never settle
      const pave = (1 - smoothstep(0.3, 1.0, slopeV)) * (1 - soilV) * smoothstep(0.42, 0.78, hardV) * (1 - 0.7 * apronV) * (0.3 + 0.7 * Math.pow(crestV, 0.7));
      const wp = w.cap * Math.max(Math.pow(crestV, 1.35) * (0.35 + 0.9 * hardV) * (1 - smoothstep(0.45, 1.15, slopeV)), 0.22 * pave);
      const we = 0;
      const ws = w.shore * shoreV;
      let kind = 'face', wv = wf;
      if (wt > wv) { wv = wt; kind = 'talus'; }
      if (wc > wv) { wv = wc; kind = 'channel'; }
      if (wb > wv) { wv = wb; kind = 'bar'; }
      if (wp > wv) { wv = wp; kind = 'cap'; }
      if (we > wv) { wv = we; kind = 'erratic'; }
      if (ws > wv) { wv = ws; kind = 'shore'; }
      if (wv <= 0.02) continue;
      const accept = rate * wv * patch * (1 - soilHide * soilV * (kind === 'face' || kind === 'talus' ? 0.6 : 1));
      if (rand() > Math.min(1, accept * 1.35)) continue;

      // ---- size / shape law per category: radius = how big, thin = how flat it lies, wide = how
      // far it spreads in plan. A block that broke off in place keeps its bedding; a clast that has
      // been rolled lies flat and round; a pavement of slabs is thin, wide and half buried.
      let radius, family, embedK, tiltK, tumble, thin = 0.78, wide = 1;
      const isPave = kind === 'cap' && crestV < 0.45;
      if (kind === 'face') {
        radius = sizeMin * 0.7 + sizeMax * (0.14 + 0.66 * Math.pow(rand(), 1 + 2.2 * spread)) * (0.45 + 0.75 * faceV);
        family = rand() < 0.5 + 0.4 * angularity ? 'block' : 'slab';
        embedK = 0.66 + 0.26 * embed; tiltK = 0.3; tumble = 0.16; thin = 0.72;
      } else if (kind === 'talus') {
        const fine = Math.pow(0.96, Math.min(20, aDistV));   // the biggest blocks stay nearest the cliff
        radius = (0.5 + aSizeV * 0.55) * (0.35 + 1.6 * Math.pow(rand(), 1 + 2.6 * spread)) * fine;
        family = aSizeV > 8 && rand() < 0.45 + 0.3 * hardV ? 'slab' : (rand() < 0.3 ? 'shard' : 'block');
        embedK = 0.34 + 0.4 * embed; tiltK = 0.8; tumble = 0.55; thin = 0.7; wide = 1.1;
      } else if (kind === 'channel') {
        radius = 0.35 + 2.3 * Math.pow(clamp01(powV), 1 + 1.5 * spread) + 1.5 * Math.pow(rand(), 2.2);
        radius = Math.min(sizeMax * 1.2, Math.max(sizeMin * 0.35, radius));
        family = 'boulder'; embedK = 0.52 + 0.24 * embed; tiltK = 0.85; tumble = 0.16; thin = 0.68; wide = 1.15;
      } else if (kind === 'bar') {
        radius = sizeMin * 0.3 + 2 * Math.pow(rand(), 1 + 2.4 * spread) * (0.45 + 0.9 * brakeV);
        family = 'boulder'; embedK = 0.46 + 0.2 * embed; tiltK = 0.9; tumble = 0.2; thin = 0.6; wide = 1.25;
      } else if (isPave) {
        radius = sizeMin * (0.8 + 1.7 * Math.pow(rand(), 1 + 2 * spread));
        family = 'slab'; embedK = 0.8; tiltK = 0.12; tumble = 0.1; thin = 0.26; wide = 1.7;
      } else if (kind === 'cap') {
        radius = sizeMax * (0.26 + 0.72 * Math.pow(rand(), 1 + 1.7 * spread)) * (0.5 + 0.8 * crestV);
        family = rand() < 0.5 ? 'boulder' : 'block'; embedK = 0.5 + 0.3 * embed; tiltK = 0.55; tumble = 0.28; thin = 0.76;
      } else if (kind === 'erratic') {
        radius = sizeMax * (0.3 + 0.7 * Math.pow(rand(), 1.6 + 2 * spread));
        family = 'boulder'; embedK = 0.58 + 0.3 * embed; tiltK = 0.5; tumble = 0.3; thin = 0.74;
      } else {                                        // shore
        radius = sizeMin * 0.3 + 1.5 * Math.pow(rand(), 1 + 1.8 * spread);
        family = 'boulder'; embedK = 0.5 + 0.24 * embed; tiltK = 0.9; tumble = 0.18; thin = 0.62; wide = 1.2;
      }
      // a nudge along the joint lattice, so the pieces of one bed line up in rows
      [x, z] = jointShift(x, z, 0.5);
      const set = radius > sizeMax * 0.62 ? 'large' : 'small';
      const { index, geometry } = pick(set, family);
      const n = sampler.normal(x, z);
      nrm.set(n[0], n[1], n[2]);
      const [px, py, pz] = sampler.surface(x, z, params);
      // a clast that was deposited lies across the slope it came to rest on; a block that broke off
      // in place keeps the bedding it always had. rockTilt is how far that goes.
      target.copy(up).lerp(nrm, clamp01(tiltFollow * tiltK)).normalize();
      qTilt.setFromUnitVectors(up, target);
      qYaw.setFromAxisAngle(up, rand() * Math.PI * 2);
      axis.set(rand() - 0.5, rand() - 0.5, rand() - 0.5).normalize();
      // small clasts are the ones a flood, or one more bounce, can turn over
      qTumble.setFromAxisAngle(axis, (rand() - 0.5) * tumble * (1 + 0.4 * Math.min(2, sizeMax / Math.max(0.3, radius))));
      q.copy(qTilt).multiply(qYaw).multiply(qTumble);
      s.set(radius * wide * (0.85 + rand() * 0.35), radius * thin * (0.8 + rand() * 0.45), radius * wide * (0.85 + rand() * 0.35));
      const sink = geometry.userData.height * s.y * 0.5 * embedK;
      p.set(px, py, pz).addScaledVector(nrm, -sink);
      m.compose(p, q, s);

      // colour: the local rock tone, weathered where it has been sitting in soil or spray
      let tone = 0.84 + rand() * 0.26;
      let warm = (rand() - 0.5) * 0.08 + dry * 0.06;
      if (kind === 'channel' || kind === 'shore' || wet > 0.05) { const k = 0.78 - 0.16 * wet; tone *= k; warm -= 0.03; }
      if (kind === 'erratic' || kind === 'cap') { tone *= 1 - 0.12 * veg; warm -= 0.05 * veg; }
      placements.push({ set, index, kind, matrix: m.clone(), color: [tone + warm, tone, tone - warm] });
      cats[kind]++;
    }
  }

  // ---- erratics: solitary wanderers on the flats — the far tail of an apron, or a boulder that has
  // simply been sitting there since the ice. Rare on purpose: their own sparse grid.
  if (w.erratic > 0.001) {
    const eSteps = Math.max(6, Math.round(9 + 40 * w.erratic * (0.5 + 0.8 * density)));
    const eSpacing = size / eSteps;
    for (let j = 0; j < eSteps && placements.length < MAX_INSTANCES; j++) {
      for (let i = 0; i < eSteps && placements.length < MAX_INSTANCES; i++) {
        if (rand() > 0.55) continue;
        const x = (i + 0.25 + 0.5 * rand()) * eSpacing - size * 0.5;
        const z = (j + 0.25 + 0.5 * rand()) * eSpacing - size * 0.5;
        if (Math.abs(x) > size * 0.49 || Math.abs(z) > size * 0.49) continue;
        const slopeV = sampler.map('slope', x, z);
        if (slopeV > 0.75) continue;
        const apronV = rf.at(rf.apron, x, z), soilV = rf.at(rf.soil, x, z), crestV = rf.at(rf.crest, x, z);
        const fringe = smoothstep(0.02, 0.2, apronV) * (1 - smoothstep(0.25, 0.8, apronV));
        const lonely = 0.25 * (1 - apronV) * (1 - crestV * 1.4) * (1 - soilV * soilHide);
        if (rand() > Math.min(1, (fringe + lonely) * (0.55 + 0.6 * rate) * 1.7)) continue;
        const hh = sampler.height(x, z);
        if (params.waterEnabled && hh < (params.seaLevel || 0) - 1) continue;
        if (wlM) { const wl = sampler.map('waterLevel', x, z); if (wl > NO_WATER * 0.5 && wl > hh + 0.3) continue; }
        if (field.road && sampler.map('road', x, z) > 0.2) continue;
        const radius = sizeMax * (0.32 + 0.68 * Math.pow(rand(), 1.4 + 2.4 * spread));
        const n = sampler.normal(x, z);
        nrm.set(n[0], n[1], n[2]);
        const [px, py, pz] = sampler.surface(x, z, params);
        const { index, geometry } = pick('large', 'boulder');
        target.copy(up).lerp(nrm, 0.45).normalize();
        qTilt.setFromUnitVectors(up, target);
        qYaw.setFromAxisAngle(up, rand() * Math.PI * 2);
        axis.set(rand() - 0.5, rand() - 0.5, rand() - 0.5).normalize();
        qTumble.setFromAxisAngle(axis, (rand() - 0.5) * 0.55);
        q.copy(qTilt).multiply(qYaw).multiply(qTumble);
        s.set(radius * (0.8 + rand() * 0.4), radius * (0.55 + rand() * 0.3), radius * (0.8 + rand() * 0.4));
        p.set(px, py, pz).addScaledVector(nrm, -geometry.userData.height * s.y * 0.5 * (0.55 + 0.35 * embed));
        m.compose(p, q, s);
        let tone = 0.8 + rand() * 0.22;
        tone *= 1 - 0.14 * veg;                       // weathered, mossed where things grow
        placements.push({ set: 'large', index, kind: 'erratic', matrix: m.clone(), color: [tone - 0.02 * veg, tone + 0.02, tone - 0.06] });
        cats.erratic++;
      }
    }
  }

  // ---- pebbles: the fine tail of the same story (scree, bars, shore cords, channel margins) -----
  const pebbleDensity = params.pebblesOn ? Math.max(0, params.pebbleDensity || 0) : 0;
  if (pebbleDensity > 0 && library.pebble) {
    const pSteps = Math.max(24, Math.round(220 + 520 * pebbleDensity));
    const pSpacing = size / pSteps;
    const pMax = Math.max(0.08, params.pebbleSize || 0.5);
    let count = 0;
    for (let j = 0; j < pSteps && count < MAX_PEBBLES; j++) {
      for (let i = 0; i < pSteps && count < MAX_PEBBLES; i++) {
        const x = (i + 0.5 + (rand() - 0.5) * 0.95) * pSpacing - size * 0.5;
        const z = (j + 0.5 + (rand() - 0.5) * 0.95) * pSpacing - size * 0.5;
        if (Math.abs(x) > size * 0.495 || Math.abs(z) > size * 0.495) continue;
        const apronV = rf.at(rf.apron, x, z), brakeV = rf.at(rf.brake, x, z), shoreV = rf.at(rf.shore, x, z);
        const depV = field.deposit ? clamp01(sampler.map('deposit', x, z)) : 0;
        const slopeV = sampler.map('slope', x, z);
        const riverV = riverM ? smoothstep(0.15, 0.6, sampler.map('river', x, z)) : 0;
        // scree is gravel-sized too; so are bars and shores; a channel margin carries its own wash
        const where = Math.max(apronV, 0.85 * brakeV, 0.7 * shoreV, riverV * 0.55, smoothstep(0.2, 0.7, depV) * 0.4 * (1 - smoothstep(0.9, 1.9, slopeV)));
        if (where <= 0.14) continue;
        const h = sampler.height(x, z);
        if (params.waterEnabled && h < (params.seaLevel || 0) - 0.5) continue;
        if (wlM) { const wl = sampler.map('waterLevel', x, z); if (wl > NO_WATER * 0.5 && wl > h + 0.8) continue; }
        if (field.road && sampler.map('road', x, z) > 0.2) continue;
        if (slopeV > 1.35) continue;
        if (rand() > pebbleDensity * Math.pow(where, 1.25) * 1.55) continue;
        const radius = lerp(0.08, pMax, Math.pow(rand(), 1.4));
        const n = sampler.normal(x, z);
        nrm.set(n[0], n[1], n[2]);
        const [px, py, pz] = sampler.surface(x, z, params);
        const index = (rand() * library.pebble.length) | 0;
        const geometry = library.pebble[index];
        target.copy(up).lerp(nrm, 0.82).normalize();
        qTilt.setFromUnitVectors(up, target);
        qYaw.setFromAxisAngle(up, rand() * Math.PI * 2);
        axis.set(rand() - 0.5, rand() - 0.5, rand() - 0.5).normalize();
        qTumble.setFromAxisAngle(axis, (rand() - 0.5) * 1.2);
        q.copy(qTilt).multiply(qYaw).multiply(qTumble);
        s.set(radius * (0.8 + rand() * 0.4), radius * (0.55 + rand() * 0.4), radius * (0.8 + rand() * 0.4));
        p.set(px, py, pz).addScaledVector(nrm, -geometry.userData.height * s.y * 0.5 * 0.55);
        m.compose(p, q, s);
        const tone = 0.76 + rand() * 0.42, wm = (rand() - 0.5) * 0.1;
        placements.push({ set: 'pebble', index, kind: 'pebble', matrix: m.clone(), color: [tone + wm, tone, tone - wm] });
        count++;
      }
    }
    cats.pebble = count;
  }
  placements.stats = cats;
  return placements;
}

export function buildRockMeshes(placements, library, material) {
  const groupsByKey = new Map();
  for (const pl of placements) {
    const key = `${pl.set}:${pl.index}`;
    let list = groupsByKey.get(key);
    if (!list) { list = []; groupsByKey.set(key, list); }
    list.push(pl);
  }
  const meshes = [];
  const color = new THREE.Color();
  for (const [key, items] of groupsByKey) {
    const [set, index] = key.split(':');
    const geometry = library[set][Number(index)];
    const mesh = new THREE.InstancedMesh(geometry, material, items.length);
    items.forEach((pl, i) => { mesh.setMatrixAt(i, pl.matrix); mesh.setColorAt(i, color.setRGB(...pl.color)); });
    mesh.instanceMatrix.needsUpdate = true;
    mesh.instanceColor.needsUpdate = true;
    mesh.castShadow = true;
    mesh.receiveShadow = true;
    mesh.frustumCulled = false;
    mesh.name = `rocks-${key}`;
    meshes.push(mesh);
  }
  return meshes;
}

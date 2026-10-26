// Headless checks for the hybrid SDF cliff chunks: the original heightfield/chunk selection stays
// in charge, and the dense volume inside each steep chunk must (a) weather differentially,
// (b) detach joint-bounded blocks, (c) stay exactly y - h where W = 0, (d) agree on faces shared
// between neighbouring chunks, and (e) produce finite marching-cubes geometry end to end.
import { defaults, presets } from '../src/params.js';
import { generateTerrain } from '../src/pipeline.js';
import { selectChunks, packChunkJobs, makeChunkContext, buildChunkField, buildChunkGeometry } from '../src/sdf-chunks.js';
import { CliffVolume, weatherCliffVolume, detachJointBlocks } from '../src/sdf-cliff-volume.js';

let passed = 0;
function check(name, cond, detail = '') {
  if (!cond) throw new Error(`FAIL: ${name}${detail ? ' — ' + detail : ''}`);
  passed++;
  console.log(`  ok ${name}${detail ? ' — ' + detail : ''}`);
}
const sstep = (t) => { t = Math.min(1, Math.max(0, t)); return t * t * (3 - 2 * t); };
const finite = (a) => { for (let i = 0; i < a.length; i++) if (!Number.isFinite(a[i])) return false; return true; };

// ---------------------------------------------------------- unit: differential weathering
{
  const vol = new CliffVolume(20, 18, 20, 1, [0, 0, 0]);
  for (let iz = 0; iz < 20; iz++) for (let ix = 0; ix < 20; ix++) {
    const c = vol.columnIndex(ix, iz);
    vol.weight[c] = 1; vol.steep[c] = 1;
    for (let iy = 0; iy < 18; iy++) {
      const idx = vol.index(ix, iy, iz);
      vol.sdf[idx] = ix - 10;                       // vertical planar face at x = 10
      vol.susceptibility[idx] = iy < 9 ? 255 : 0;   // soft lower bed, inert upper bed
    }
  }
  const stats = weatherCliffVolume(vol, { iterations: 3, step: 0.2, band: 3 });
  const soft = vol.sdf[vol.index(10, 4, 10)], hard = vol.sdf[vol.index(10, 15, 10)];
  check('weathering moves the susceptible bed only', soft > 0.3 && hard === 0,
    `soft sdf ${soft.toFixed(2)}, hard sdf ${hard.toFixed(2)}, delta ${stats.totalDelta.toFixed(1)}`);
}

// ---------------------------------------------------------- unit: joint-block detachment
{
  const vol = new CliffVolume(26, 22, 26, 1, [0, 0, 0]);
  for (let iz = 0; iz < 26; iz++) for (let ix = 0; ix < 26; ix++) {
    const c = vol.columnIndex(ix, iz);
    vol.weight[c] = 1; vol.steep[c] = 1;
    for (let iy = 0; iy < 22; iy++) {
      const idx = vol.index(ix, iy, iz);
      vol.sdf[idx] = ix - (iy >= 8 ? 13 : 7);       // hard lip overhanging an undercut foot
      vol.hardness[idx] = 235; vol.joint[idx] = 255; vol.bedThick[idx] = 4;
    }
  }
  const solidBefore = vol.sdf.reduce((n, s) => n + (s < 0 ? 1 : 0), 0);
  const stats = detachJointBlocks(vol, {
    seed: 5, strength: 1, loss: 1, blockSize: 5, depth: 2, maxBlocks: 12,
    interior: { minX: 0, maxX: 25, minZ: 0, maxZ: 25 },
  });
  const solidAfter = vol.sdf.reduce((n, s) => n + (s < 0 ? 1 : 0), 0);
  check('unsupported jointed blocks detach as boxes', stats.detached > 0 && solidAfter < solidBefore,
    `${stats.detached} detached from ${stats.candidates} candidates, solid ${solidBefore} → ${solidAfter}`);
}

// ---------------------------------------------------------- synthetic escarpment through the real chunk path
const N = 81, size = 320;
const zeros = () => new Float32Array(N * N);
const height = zeros(), deposit = zeros();
for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) {
  const centre = 40 + 3 * Math.sin(j * 0.18);
  const t = sstep((i - (centre - 3.5)) / 7);
  height[j * N + i] = 24 + 58 * (1 - t) + 2 * Math.sin(j * 0.21) + 1.5 * Math.cos(i * 0.11);
  deposit[j * N + i] = 0.15 * t;
}
const fine = {
  resolution: N, worldSize: size, height, hardness: zeros().fill(0.5), deposit,
  flow: zeros(), cavity: zeros(), road: zeros(), river: zeros(), lake: zeros(),
  waterLevel: zeros().fill(-1e6), waterPaint: zeros().fill(-1e6), outcrop: zeros(),
};
const v = {
  ...defaults, seed: 428, resolution: N, worldSize: size,
  strataBand: 9, strataVariation: 0.65, strataPackaging: 0.65, strataHardShare: 0.3, hardnessContrast: 0.9,
  sdfAngle: 42, sdfBlend: 2, sdfChunk: 16, sdfVoxel: 2, sdfUndercut: 12, sdfBedContrast: 1.2,
  sdfPockets: 14, sdfJoints: 1.1, sdfPits: 0.35, sdfRough: 0.35, sdfPushPull: 1.5,
  sdfBlocks: 2.5, sdfBlockSize: 7, sdfBlockLoss: 0.35, sdfVertical: 0, sdfLean: 0,
  detailRelief: 0, rockyAmount: 0, cragAmount: 0,
};
const chunks = selectChunks(fine, v);
check('steep cells select chunks (heightfield elsewhere)', chunks.list.length >= 3, `${chunks.list.length} chunks, ${chunks.candidates} candidates`);
let borderW = 0;
for (let i = 0; i < N; i++) borderW = Math.max(borderW, chunks.W[i], chunks.W[(N - 1) * N + i], chunks.W[i * N], chunks.W[i * N + N - 1]);
check('chunk weight is zero on the map border', borderW === 0);

const { jobs, meta } = packChunkJobs(fine, chunks, v);
check('jobs carry halo padding and full map slices', jobs[0].pad >= 6
  && jobs[0].maps.height.length === (jobs[0].cw + 1 + 2 * jobs[0].pad) * (jobs[0].ch + 1 + 2 * jobs[0].pad),
  `pad ${jobs[0].pad} cells, k ${meta.k}`);
{
  // at sub-metre heightfield cells the ~6 m halo spans more than six cells; padding must follow
  const n2 = 65, size2 = 32, h2 = new Float32Array(n2 * n2);
  for (let j = 0; j < n2; j++) for (let i = 0; i < n2; i++) h2[j * n2 + i] = 12 - 6 * sstep((i - 30) / 4);
  const fine2 = { resolution: n2, worldSize: size2, height: h2 };
  const v2 = { ...v, resolution: n2, worldSize: size2, sdfVoxel: 1 };
  const packed2 = packChunkJobs(fine2, selectChunks(fine2, v2), v2);
  check('fine cells widen map padding to cover the physical halo', packed2.jobs.length > 0 && packed2.jobs[0].pad >= 13,
    `cell 0.5 m, pad ${packed2.jobs[0]?.pad} cells`);
}

const ctx = makeChunkContext(v);
const fields = new Map();
let multiCrossing = 0, exactW0 = 0, worstW0 = 0, detachedTotal = 0;
for (const job of jobs) {
  const f = buildChunkField(job, meta, v, ctx);
  fields.set(job.ci + ',' + job.cj, f);
  detachedTotal += f.stats.blocksDetached;
  const sx = f.nx + 1, sy = f.ny + 1;
  for (let iz = 0; iz <= f.nz; iz++) for (let ix = 0; ix <= f.nx; ix++) {
    const vc = (iz + f.halo) * f.volume.nx + (ix + f.halo);
    const w = f.volume.weight[vc], h = f.volume.height[vc];
    let crossings = 0, prev = f.vals[(iz * sy) * sx + ix];
    for (let iy = 0; iy <= f.ny; iy++) {
      const val = f.vals[(iz * sy + iy) * sx + ix];
      if (w <= 1e-6) { exactW0++; worstW0 = Math.max(worstW0, Math.abs(val - (f.yLo + iy * f.vox - h))); }
      if (iy > 0 && ((prev >= 0 && val < 0) || (prev < 0 && val >= 0))) crossings++;
      prev = val;
    }
    if (crossings > 2) multiCrossing++;
  }
}
check('W = 0 field is exactly y - h (heightfield seam)', exactW0 > 100 && worstW0 < 1e-4,
  `${exactW0} nodes, worst error ${worstW0.toExponential(1)}`);
check('weathered chunks are multi-valued (real undercuts)', multiCrossing > 0,
  `${multiCrossing} columns with >2 crossings, ${detachedTotal} blocks detached in-chunk`);

// neighbouring chunks must agree on their shared face
{
  let worst = -1, compared = 0;
  for (const job of jobs) {
    const a = fields.get(job.ci + ',' + job.cj), b = fields.get(job.ci + ',' + (job.cj + 1));
    if (!a || !b) continue;
    const dy = Math.round((b.yLo - a.yLo) / a.vox);
    const sxA = a.nx + 1, syA = a.ny + 1, sxB = b.nx + 1, syB = b.ny + 1;
    for (let ix = 0; ix <= a.nx; ix++) for (let iy = 0; iy <= a.ny; iy++) {
      const iyB = iy - dy;
      if (iyB < 0 || iyB > b.ny) continue;
      const va = a.vals[(a.nz * syA + iy) * sxA + ix];
      const vb = b.vals[(0 * syB + iyB) * sxB + ix];
      worst = Math.max(worst, Math.abs(va - vb)); compared++;
    }
  }
  check('neighbouring chunk fields agree on shared faces', compared > 100 && worst < 1e-3,
    `${compared} nodes compared, worst difference ${worst.toExponential(1)}`);
}

// determinism + the no-weathering field must not invent surfaces
{
  const job = jobs[0];
  const again = buildChunkField(job, meta, v, makeChunkContext(v));
  const first = fields.get(job.ci + ',' + job.cj);
  let worst = 0;
  for (let i = 0; i < first.vals.length; i++) worst = Math.max(worst, Math.abs(first.vals[i] - again.vals[i]));
  check('chunk field is deterministic', worst === 0, `worst difference ${worst}`);

  const vOff = { ...v, sdfUndercut: 0, sdfJoints: 0, sdfPits: 0, sdfPushPull: 0, sdfRough: 0, sdfBlocks: 0 };
  const fOff = buildChunkField(job, meta, vOff, makeChunkContext(vOff));
  const sx = fOff.nx + 1, sy = fOff.ny + 1;
  let signFlips = 0;
  for (let iz = 0; iz <= fOff.nz; iz++) for (let ix = 0; ix <= fOff.nx; ix++) {
    const h = fOff.volume.height[(iz + fOff.halo) * fOff.volume.nx + (ix + fOff.halo)];
    for (let iy = 0; iy <= fOff.ny; iy++) {
      const plain = fOff.yLo + iy * fOff.vox - h;
      if (Math.abs(plain) < fOff.vox * 0.6) continue;
      const val = fOff.vals[(iz * sy + iy) * sx + ix];
      if ((plain < 0) !== (val < 0)) signFlips++;
    }
  }
  check('with weathering off the field keeps the heightfield sign', signFlips === 0, `${signFlips} flips`);
}

// geometry out of the worker entry point
{
  const g = buildChunkGeometry(jobs[0], meta, v, makeChunkContext(v));
  const count = g.positions.length / 3;
  let maxIndex = 0;
  for (let i = 0; i < g.index.length; i++) if (g.index[i] > maxIndex) maxIndex = g.index[i];
  check('marching-cubes geometry is finite and indexed in range',
    g.triangles > 0 && finite(g.positions) && finite(g.normals) && finite(g.aux) && finite(g.aux2) && maxIndex < count,
    `${g.triangles} triangles, ${count} vertices, volume stats ${JSON.stringify(g.sdfVolume)}`);
}

// ---------------------------------------------------------- real pipeline field, first chunk end to end
{
  const params = { ...defaults, ...presets['Alpine granite'], resolution: 128, worldSize: 1024 };
  params.droplets = Math.round(params.droplets * (128 * 128) / (512 * 512));
  const result = generateTerrain(params, () => {});
  const realFine = { ...result, road: new Float32Array(128 * 128), waterPaint: result.waterLevel };
  const vReal = { ...params, sdfVoxel: 1, sdfMaxChunks: 24, sdfAngle: Math.min(params.sdfAngle || 58, 55) };
  const realChunks = selectChunks(realFine, vReal);
  check('real terrain selects SDF chunks', realChunks.list.length > 0, `${realChunks.list.length} chunks`);
  const real = packChunkJobs(realFine, realChunks, vReal);
  const g = buildChunkGeometry(real.jobs[0], real.meta, vReal, makeChunkContext(vReal));
  check('real terrain chunk meshes finite geometry', g.triangles > 0 && finite(g.positions) && finite(g.normals),
    `${g.triangles} triangles, voxel ${real.meta.cell.toFixed(1)} m, volume stats ${JSON.stringify(g.sdfVolume)}`);
}

console.log(`\n${passed} passed, 0 failed`);

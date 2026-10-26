// Node checks for the pure (non-DOM) core: geology, SDF volume, erosion, mesher.
// Run: node scripts/check.mjs [from environment-editor/]

import { SdfVolume, buildEscarpmentScene } from '../src/volume.js';
import { Geology, fillGeology } from '../src/geology.js';
import { differentialWeathering, thermalLoose, hydraulicDroplets, computeDrainage, updateFlowMaps } from '../src/erosion.js';
import { buildChunkMesh, buildAllChunks } from '../src/mesher.js';

let passed = 0, failed = 0;
function check(name, cond, detail = '') {
  if (cond) { passed++; console.log(`  ok  ${name}${detail ? ` — ${detail}` : ''}`); }
  else { failed++; console.error(`FAIL  ${name}${detail ? ` — ${detail}` : ''}`); }
}
const finiteField = (a) => { for (let i = 0; i < a.length; i++) if (!Number.isFinite(a[i])) return false; return true; };

const params = {
  seed: 428, bedScale: 1, dip: 4, dipDirection: 35, lateral: 0.55, fold: 2.5,
  faultThrow: 0, faultPos: -18, jointWidth: 0.35, jointSpacing: 1,
  plateauHeight: 40, plainHeight: 12, cliffX: -10, cliffWidth: 4.8, cave: 1, caveRadius: 2.4, fallenBlocks: 6,
};

console.log('Geology');
const geology = new Geology(params);
check('bed stack has many beds', geology.beds.length >= 20, `${geology.beds.length} beds`);
const thicknesses = geology.beds.map((b) => b.thickness);
check('bed thicknesses are not periodic/uniform', new Set(thicknesses.map((t) => t.toFixed(2))).size > geology.beds.length * 0.7);
check('hardness spans soft shale to hard sandstone/limestone',
  Math.min(...geology.beds.map((b) => b.hardness)) < 0.35 && Math.max(...geology.beds.map((b) => b.hardness)) > 0.8);
{
  // lateral pinch-out: some bed must change local thickness across the patch
  const a = geology.columnBoundaries(-40, -30, new Float64Array(geology.beds.length));
  const b = geology.columnBoundaries(35, 25, new Float64Array(geology.beds.length));
  let varies = false;
  for (let i = 1; i < geology.beds.length; i++) {
    const ta = a[i] - a[i - 1], tb = b[i] - b[i - 1];
    if (Math.abs(ta - tb) > 0.25) varies = true;
  }
  check('beds pinch/swell laterally (no infinite stripes)', varies);
}
const soft = geology.findSoftBed(-12, -6, 20);
check('a soft cave-host bed exists near the escarpment', !!soft && soft.bed.hardness <= 0.42, soft ? `${soft.bed.type} @ y≈${soft.centreY.toFixed(1)}` : '');

console.log('Volume + opening scene');
const t0 = Date.now();
const volume = new SdfVolume();
volume.geology = geology;
await fillGeology(volume, geology);
await buildEscarpmentScene(volume, geology, params);
console.log(`  (scene built in ${((Date.now() - t0) / 1000).toFixed(1)} s)`);
check('SDF field finite', finiteField(volume.sdf));
check('patch has solid rock', volume.solidVolume() > 50000, `${Math.round(volume.solidVolume())} m³`);
const cave = volume.landmarks.cave;
check('cave landmark recorded', !!cave);
if (cave) {
  const [cx, cy, cz] = cave.chamber;
  check('cave chamber is air inside the mountain', volume.sampleSdf(cx, cy, cz) > 0.8, `sdf=${volume.sampleSdf(cx, cy, cz).toFixed(2)}`);
  check('rock above the chamber is solid (a roof exists)', volume.sampleSdf(cx, cy + 7, cz) < -0.8, `sdf=${volume.sampleSdf(cx, cy + 7, cz).toFixed(2)}`);
  const crossings = volume.columnCrossings(cx, cz);
  check('chamber column has >1 surface (impossible for a heightfield)', crossings.length >= 3, `${crossings.length} crossings`);
  const [ex, ey, ez] = cave.entrance;
  const rayDir = (() => { const d = [cx - (ex + 12), cy - ey, cz - ez]; const l = Math.hypot(...d); return d.map((v) => v / l); })();
  const hit = volume.raycast([ex + 12, ey, ez], rayDir);
  check('sphere-traced ray finds the cave mouth wall', !!hit && hit.distance > 2, hit ? `hit @ ${hit.distance.toFixed(1)} m` : 'no hit');
  if (cave.bedIndex >= 0) check('cave is hosted in a soft bed', geology.beds[cave.bedIndex].hardness <= 0.42, geology.beds[cave.bedIndex].type);
}
{
  const before = volume.solidVolume();
  volume.brushAdd([30, volume.topHeight(30, 30) + 1, 30], 3, 1, 0.5);
  const afterAdd = volume.solidVolume();
  volume.brushRemove([30, volume.topHeight(30, 30), 30], 3, 1, 0.5);
  const afterCarve = volume.solidVolume();
  check('brush Add increases rock volume', afterAdd > before + 5, `+${(afterAdd - before).toFixed(0)} m³`);
  check('brush Carve decreases rock volume', afterCarve < afterAdd - 5, `${(afterCarve - afterAdd).toFixed(0)} m³`);
  check('edits mark dirty chunks', volume.dirty.size > 0, `${volume.dirty.size} chunks`);
}

console.log('Differential weathering (controlled cliff)');
{
  const v = new SdfVolume({ nx: 33, ny: 25, nz: 33, spacing: 1, origin: [-16, 0, -16], chunk: 16 });
  for (let iz = 0; iz < v.nz; iz++) for (let iy = 0; iy < v.ny; iy++) for (let ix = 0; ix < v.nx; ix++) {
    const idx = v.index(ix, iy, iz);
    v.sdf[idx] = v.nodeX(ix); // vertical face at x=0, solid x<0
    const y = v.nodeY(iy);
    v.hardness[idx] = (y >= 10 && y <= 13) ? 25 : 242;  // one soft bed
    v.joint[idx] = 0;
  }
  const faceX = (yy) => {
    for (let ix = v.nx - 2; ix >= 0; ix--) {
      const a = v.sdf[v.index(ix + 1, yy, 16)], b = v.sdf[v.index(ix, yy, 16)];
      if (a >= 0 && b < 0) return v.nodeX(ix + 1) + (v.nodeX(ix) - v.nodeX(ix + 1)) * (a / (a - b));
    }
    return -999;
  };
  const softBefore = faceX(11), hardBefore = faceX(5);
  await differentialWeathering(v, { iterations: 10, rate: 0.2, jointInfluence: 0 });
  const softRetreat = softBefore - faceX(11), hardRetreat = hardBefore - faceX(5);
  check('soft bed recedes (undercut forms)', softRetreat > 0.25, `${softRetreat.toFixed(2)} m`);
  check('hard bed resists (cornice remains)', hardRetreat < softRetreat * 0.3, `${hardRetreat.toFixed(2)} m vs soft ${softRetreat.toFixed(2)} m`);
  check('field still finite after weathering + redistance', finiteField(v.sdf));
}

console.log('Joint slots in hard rock + box CSG (block fall shapes)');
{
  const v = new SdfVolume({ nx: 33, ny: 25, nz: 33, spacing: 1, origin: [-16, 0, -16], chunk: 16 });
  for (let iz = 0; iz < v.nz; iz++) for (let iy = 0; iy < v.ny; iy++) for (let ix = 0; ix < v.nx; ix++) {
    const idx = v.index(ix, iy, iz);
    v.sdf[idx] = v.nodeX(ix);
    v.hardness[idx] = 242; // uniformly hard
    v.joint[idx] = (iz >= 15 && iz <= 17) ? 255 : 0; // one vertical joint band
  }
  const faceX = (zz) => {
    for (let ix = v.nx - 2; ix >= 0; ix--) {
      const a = v.sdf[v.index(ix + 1, 10, zz)], b = v.sdf[v.index(ix, 10, zz)];
      if (a >= 0 && b < 0) return v.nodeX(ix + 1) + (v.nodeX(ix) - v.nodeX(ix + 1)) * (a / (a - b));
    }
    return -999;
  };
  const jointBefore = faceX(16), plainBefore = faceX(6);
  await differentialWeathering(v, { iterations: 10, rate: 0.2, jointInfluence: 1.4 });
  const jointRetreat = jointBefore - faceX(16), plainRetreat = plainBefore - faceX(6);
  check('joint opens a slot even in hard rock', jointRetreat > 0.12, `${jointRetreat.toFixed(2)} m`);
  check('unjointed hard rock barely retreats', plainRetreat < jointRetreat * 0.35, `${plainRetreat.toFixed(2)} m vs joint ${jointRetreat.toFixed(2)} m`);

  const axes = [[0, 0, 1], [0, 1, 0], [1, 0, 0]];
  const volBefore = v.solidVolume();
  v.brushRemoveBox([-0.5, 10, -8], [2, 1.5, 1], axes, 0.1);
  const volRemoved = v.solidVolume();
  v.brushAddBox([4, 10, 8], [2, 1.5, 1], axes, 0.1, { sediment: 0.7, bed: 3 });
  const volAdded = v.solidVolume();
  check('box removal subtracts a block (not a sphere)', volRemoved < volBefore - 4, `${(volRemoved - volBefore).toFixed(0)} m³`);
  check('box addition deposits a block with source bed', volAdded > volRemoved + 4 && v.nearestBed(4, 10, 8) === 3, `+${(volAdded - volRemoved).toFixed(0)} m³`);
}

console.log('Thermal (loose material)');
{
  const v = new SdfVolume({ nx: 33, ny: 25, nz: 33, spacing: 1, origin: [-16, 0, -16], chunk: 16 });
  for (let iz = 0; iz < v.nz; iz++) for (let ix = 0; ix < v.nx; ix++) {
    const d = Math.hypot(v.nodeX(ix), v.nodeZ(iz));
    const h = 4 + Math.max(0, 9 - d * 1.8); // steep loose cone
    for (let iy = 0; iy < v.ny; iy++) {
      const idx = v.index(ix, iy, iz);
      v.sdf[idx] = (v.nodeY(iy) - h) / 1.4;
      if (Math.abs(v.sdf[idx]) < 1.6) v.sediment[idx] = 1;
    }
  }
  v.redistance();
  const res = await thermalLoose(v, { iterations: 8, reposeDeg: 34 });
  check('loose cone slumps', res.transfers > 20, `${res.transfers} transfers`);
  check('field finite after thermal', finiteField(v.sdf));
}

console.log('Droplets + drainage (full patch)');
{
  const d = await hydraulicDroplets(volume, { droplets: 300, seed: 5 });
  check('surface droplets erode and deposit', d.eroded > 0 && d.deposited > 0, `eroded ${d.eroded.toFixed(2)} deposited ${d.deposited.toFixed(2)}`);
  check('field finite after droplets', finiteField(volume.sdf));
  const dr = computeDrainage(volume);
  let maxA = 0, downhill = true;
  for (let i = 0; i < dr.area.length; i++) {
    if (dr.area[i] > maxA) maxA = dr.area[i];
    const r = dr.receiver[i];
    if (r >= 0 && dr.filled[r] > dr.filled[i] + 1e-6) downhill = false;
  }
  check('drainage accumulates to real channels', maxA > 400, `max ${Math.round(maxA)} m²`);
  check('every receiver is downhill on the filled surface', downhill);
  const { maxArea } = updateFlowMaps(volume, dr);
  let flowOk = true;
  for (let i = 0; i < volume.flow.length; i++) if (!(volume.flow[i] >= 0 && volume.flow[i] <= 1.0001)) flowOk = false;
  check('flow map normalised for water/vegetation phases', flowOk && maxArea === maxA);
}

console.log('Mesher');
{
  volume.markAllDirty();
  const { meshes, triangles } = buildAllChunks(volume);
  check('mesh has substantial geometry', triangles > 15000, `${triangles} tris`);
  let normalsOk = true, finiteOk = true, checked = 0;
  for (const mesh of meshes.values()) {
    const d = mesh.data;
    for (let i = 0; i < d.length; i++) if (!Number.isFinite(d[i])) finiteOk = false;
    for (let vtx = 0; vtx < mesh.vertexCount; vtx += 97) {
      const o = vtx * 13;
      const l = Math.hypot(d[o + 3], d[o + 4], d[o + 5]);
      if (Math.abs(l - 1) > 0.02) normalsOk = false;
      checked++;
    }
  }
  check('all vertex data finite', finiteOk);
  check('normals are unit SDF gradients', normalsOk, `${checked} sampled`);
  // chunk seam: shared border plane must contain coincident vertices from both neighbours
  let seamShared = 0, seamChecked = false;
  for (let cy = 0; cy < volume.chunksY && !seamChecked; cy++) for (let cz = 0; cz < volume.chunksZ && !seamChecked; cz++) {
    const left = buildChunkMesh(volume, 2, cy, cz), right = buildChunkMesh(volume, 3, cy, cz);
    const planeX = volume.origin[0] + 3 * volume.chunk * volume.spacing;
    const key = (d, o) => `${d[o].toFixed(3)},${d[o + 1].toFixed(3)},${d[o + 2].toFixed(3)}`;
    const onPlane = (mesh) => {
      const s = new Set();
      for (let vtx = 0; vtx < mesh.vertexCount; vtx++) { const o = vtx * 13; if (Math.abs(mesh.data[o] - planeX) < 1e-4) s.add(key(mesh.data, o)); }
      return s;
    };
    const L = onPlane(left), R = onPlane(right);
    if (L.size > 20) {
      seamChecked = true;
      for (const k of R) if (L.has(k)) seamShared++;
    }
  }
  check('neighbouring chunks share coincident seam vertices', seamChecked && seamShared > 10, `${seamShared} shared`);
}

console.log('Fluid bed export (Phase-2 contract)');
{
  const bed = volume.extractBedForFluid();
  check('bed is 96×96 cells matching FluidSim.setBed length', bed.nx === 96 && bed.ny === 96 && bed.heights.length === 96 * 96);
  check('bed heights finite', finiteField(bed.heights));
}

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed ? 1 : 0);

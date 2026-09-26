// Headless validation of the mesh core: run `npm run check`.
// Verifies all-quad topology, shared-vertex connectivity (ONE mesh),
// face orientations, and junction stitching on synthetic scenes.

import { buildNetworkMesh } from '../src/core/generate.js';
import { connectedComponents, degenerateQuads, edgeValence, uniqueVertexCount } from '../src/core/topology.js';
import { toOBJColored } from '../src/core/obj.js';

let failures = 0;

function check(label, cond, detail = '') {
  const ok = !!cond;
  console.log(`  ${ok ? 'PASS' : 'FAIL'}  ${label}${detail ? ` — ${detail}` : ''}`);
  if (!ok) failures++;
}

function faceNormals(mesh) {
  const { positions: P, quads } = mesh;
  const sums = new Map();
  for (const q of quads) {
    const [a, b, c, d] = q;
    // normal from the quad's two diagonals (robust for warped quads)
    const ax = P[c * 3] - P[a * 3], ay = P[c * 3 + 1] - P[a * 3 + 1], az = P[c * 3 + 2] - P[a * 3 + 2];
    const bx = P[d * 3] - P[b * 3], by = P[d * 3 + 1] - P[b * 3 + 1], bz = P[d * 3 + 2] - P[b * 3 + 2];
    let nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
    const l = Math.hypot(nx, ny, nz) || 1;
    nx /= l; ny /= l; nz /= l;
    const tag = mesh.quadTags[quads.indexOf(q)] ?? 'wall';
    if (!sums.has(tag)) sums.set(tag, { x: 0, y: 0, z: 0, n: 0 });
    const s = sums.get(tag);
    s.x += nx; s.y += ny; s.z += nz; s.n++;
  }
  return sums;
}

function analyse(name, mesh, expect = {}) {
  console.log(`\n== ${name} ==`);
  const { stats } = mesh;
  console.log(`  verts=${stats.vertices} quads=${stats.quads} arms=${stats.arms} junctions=${stats.junctions}/${stats.junctionsTotal} overpasses=${stats.overpasses}`);
  for (const js of stats.jStats) {
    console.log(`    junction ${js.id}: ${js.built ? `built, k=${js.k}` : `SKIPPED (${js.reason})`}`);
  }

  const comps = connectedComponents(stats.vertices, mesh.quads);
  const deg = degenerateQuads(mesh.positions, mesh.quads);
  const uniq = uniqueVertexCount(mesh.positions, stats.vertices);
  const val = edgeValence(mesh.quads);
  const defects = Object.entries(val).filter(([k]) => Number(k) >= 3).reduce((a, [, n]) => a + n, 0);
  console.log(`  components=${comps} degenerate=${deg} uniquePositions=${uniq}/${stats.vertices}`);
  console.log(`  edge valence: ${JSON.stringify(val)} defects(>=3)=${defects}`);

  if (expect.junctions !== undefined) check('junction count', stats.junctions === expect.junctions, `${stats.junctions} vs ${expect.junctions}`);
  if (expect.k !== undefined) {
    const ks = stats.jStats.filter((j) => j.built).map((j) => j.k).sort().join(',');
    check('junction k', ks === expect.k, `${ks} vs ${expect.k}`);
  }
  if (expect.components !== undefined) check('connected components', comps === expect.components, `${comps} vs ${expect.components}`);
  check('no degenerate quads', deg === 0, `${deg}`);
  check('seams share vertices (no duplicate positions)', uniq === stats.vertices, `${uniq}/${stats.vertices}`);
  check('crack-free (no edge shared by 3+ quads)', defects === 0, `defects=${defects}`);
  if (expect.portals !== undefined) {
    // each open portal is a boundary loop of P edges (P = 2A + 2W - 4 = 20 for defaults)
    check('portal borders', (val[1] || 0) === expect.portals * 20, `${val[1] || 0} vs ${expect.portals * 20}`);
  }

  // orientation per tag
  const sums = faceNormals(mesh);
  for (const [tag, s] of sums) {
    const y = s.y / s.n;
    if (tag === 'road' || tag === 'floor') check(`${tag} faces up`, y > 0.7, `avg ny=${y.toFixed(3)}`);
    else if (tag === 'ceil') check(`ceil faces down`, y < -0.5, `avg ny=${y.toFixed(3)}`);
    else check(`${tag} faces horizontal`, Math.abs(y) < 0.6, `avg ny=${y.toFixed(3)}`);
  }

  // OBJ round trip
  const obj = toOBJColored(mesh.positions, mesh.colors, mesh.quads);
  const vLines = obj.split('\n').filter((l) => l.startsWith('v ')).length;
  const fLines = obj.split('\n').filter((l) => l.startsWith('f ')).length;
  const quadsOnly = obj.split('\n').filter((l) => l.startsWith('f ')).every((l) => l.trim().split(/\s+/).length === 5);
  check('OBJ vertex count', vLines === stats.vertices, `${vLines}`);
  check('OBJ faces are quads', fLines === stats.quads && quadsOnly, `${fLines}`);
}

// ---- scenes ----------------------------------------------------------------

const base = {
  stationSpacing: 3,
  junctionDist: 11,
  halfW: 4.2,
  tiers: 3,
  roadPts: 5,
};

// A) X crossing, same height -> one junction, one mesh
{
  const splines = [
    { id: 0, points: [{ x: -60, y: 0, z: -8 }, { x: -20, y: 2, z: -3 }, { x: 20, y: 0, z: 3 }, { x: 60, y: 3, z: 8 }] },
    { id: 1, points: [{ x: -8, y: 1, z: -60 }, { x: -2, y: 0, z: -18 }, { x: 4, y: 2, z: 22 }, { x: 10, y: 1, z: 60 }] },
  ];
  analyse('X crossing (junction)', buildNetworkMesh(splines, base), {
    junctions: 1,
    k: '4',
    components: 1,
    portals: 4,
  });
}

// B) Y junction: branch endpoint lands on the trunk
{
  const splines = [
    { id: 0, points: [{ x: -70, y: 0, z: 0 }, { x: -25, y: 1, z: 4 }, { x: 25, y: 2, z: -4 }, { x: 70, y: 0, z: 2 }] },
    { id: 1, points: [{ x: 2, y: 2, z: 2 }, { x: 6, y: 4, z: 22 }, { x: 18, y: 6, z: 48 }, { x: 30, y: 7, z: 70 }] },
  ];
  analyse('Y junction', buildNetworkMesh(splines, base), {
    junctions: 1,
    k: '3',
    components: 1,
    portals: 3,
  });
}

// C) Overpass: crossing at very different heights -> two tubes, no merge
{
  const splines = [
    { id: 0, points: [{ x: -60, y: 0, z: -6 }, { x: -15, y: 0, z: 0 }, { x: 15, y: 0, z: 0 }, { x: 60, y: 0, z: 6 }] },
    { id: 1, points: [{ x: -6, y: 14, z: -60 }, { x: 0, y: 14, z: -15 }, { x: 0, y: 14, z: 15 }, { x: 6, y: 14, z: 60 }] },
  ];
  analyse('Overpass (no merge)', buildNetworkMesh(splines, base), {
    junctions: 0,
    components: 2,
    portals: 4,
  });
}

// D) Hills: single spline, big elevation changes, varying tunnel height
{
  const splines = [
    {
      id: 0,
      points: [
        { x: -80, y: 0, z: -20 }, { x: -40, y: 12, z: 10 }, { x: -5, y: -4, z: -12 },
        { x: 30, y: 10, z: 14 }, { x: 70, y: -2, z: -8 },
      ],
    },
  ];
  analyse('Hills (height varies)', buildNetworkMesh(splines, base), {
    junctions: 0,
    components: 1,
    portals: 2,
  });
}

// E) T junction at 90 degrees
{
  const splines = [
    { id: 0, points: [{ x: -70, y: 0, z: 0 }, { x: -20, y: 0, z: 0 }, { x: 20, y: 0, z: 0 }, { x: 70, y: 0, z: 0 }] },
    { id: 1, points: [{ x: 0, y: 0, z: 0 }, { x: 0, y: 3, z: 25 }, { x: 4, y: 6, z: 55 }] },
  ];
  analyse('T junction', buildNetworkMesh(splines, base), {
    junctions: 1,
    k: '3',
    components: 1,
    portals: 3,
  });
}

console.log(failures === 0 ? '\nALL CHECKS PASSED' : `\n${failures} CHECK(S) FAILED`);
process.exit(failures === 0 ? 0 : 1);

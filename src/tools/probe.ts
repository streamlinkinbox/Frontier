/**
 * Geometry probe: builds a few networks headlessly and asserts the invariants the
 * real app depends on — every junction surface is pure quads, the ring closes,
 * kerbs are continuous, and no vertex is NaN.
 *
 *   node tools/run-ts.mjs src/tools/probe.ts
 */
import { makeDoc, makeEdge, makeNode, type RoadDoc } from '../core/model';
import { buildScene } from '../core/build';

function net(): { doc: RoadDoc; centre: string } {
  const doc = makeDoc('probe');
  const c = makeNode(0, 0);
  doc.nodes.push(c);
  const ring: Array<[number, number]> = [];
  const n = 8;
  for (let i = 0; i < n; i++) {
    const a = (i / n) * Math.PI * 2;
    const r = 60;
    const node = makeNode(Math.cos(a) * r, Math.sin(a) * r);
    doc.nodes.push(node);
    ring.push([node.x, node.z]);
    doc.edges.push(makeEdge(c.id, node.id, i % 2 === 0 ? 'arterial' : 'street'));
  }
  // connect the ring so there are degree-2 corridors too
  for (let i = 0; i < n; i++) {
    const a = doc.nodes[1 + i];
    const b = doc.nodes[1 + ((i + 1) % n)];
    doc.edges.push(makeEdge(a.id, b.id, 'local'));
  }
  return { doc, centre: c.id };
}

function yNetwork(): RoadDoc {
  const doc = makeDoc('y');
  const hub = makeNode(0, 0);
  doc.nodes.push(hub);
  const angles = [10, 148, 250];
  for (const deg of angles) {
    const a = (deg / 180) * Math.PI;
    const node = makeNode(Math.cos(a) * 40, Math.sin(a) * 40);
    doc.nodes.push(node);
    doc.edges.push(makeEdge(hub.id, node.id, 'street'));
  }
  return doc;
}

function tightPair(): RoadDoc {
  // two roads meeting at a very shallow angle: the worst case for kerb returns
  const doc = makeDoc('tight');
  const hub = makeNode(0, 0);
  doc.nodes.push(hub);
  for (const deg of [0, 18, 200]) {
    const a = (deg / 180) * Math.PI;
    const node = makeNode(Math.cos(a) * 40, Math.sin(a) * 40);
    doc.nodes.push(node);
    doc.edges.push(makeEdge(hub.id, node.id, 'street'));
  }
  return doc;
}

function straight(): RoadDoc {
  const doc = makeDoc('straight');
  const nodes = [makeNode(0, 0), makeNode(0, 80), makeNode(0, 160)];
  doc.nodes.push(...nodes);
  doc.edges.push(makeEdge(nodes[0].id, nodes[1].id, 'avenue'));
  doc.edges.push(makeEdge(nodes[1].id, nodes[2].id, 'avenue'));
  return doc;
}

interface Report {
  name: string;
  verts: number;
  quads: number;
  tris: number;
  junctions: number;
  ms: number;
  nan: number;
  degenerate: number;
  warns: string[];
}

function check(name: string, doc: RoadDoc, quality: 'high'): Report {
  const t0 = Date.now();
  const res = buildScene(doc, quality);
  const b = res.builder;
  let nan = 0;
  let degenerate = 0;
  for (const v of b.verts) {
    if (!v.p.every(Number.isFinite) || !v.n.every(Number.isFinite)) nan++;
  }
  for (let i = 0; i + 3 < b.quads.length; i += 4) {
    const [a, c, d, e] = [b.quads[i], b.quads[i + 1], b.quads[i + 2], b.quads[i + 3]];
    if (a === c || c === d || d === e || a === e) degenerate++;
  }
  const warns = res.diagnostics.filter((d) => d.level === 'warn').map((d) => d.message);
  const rep: Report = {
    name,
    verts: b.vertexCount,
    quads: b.quadCount,
    tris: b.triCount,
    junctions: res.junctions.length,
    ms: Date.now() - t0,
    nan,
    degenerate,
    warns,
  };
  for (const j of res.junctions) {
    if (j.warnings.length) rep.warns.push(...j.warnings);
  }
  return rep;
}

const reports: Report[] = [];
reports.push(check('straight', straight(), 'high'));
reports.push(check('y', yNetwork(), 'high'));
reports.push(check('tight', tightPair(), 'high'));
reports.push(check('ring', net().doc, 'high'));

for (const r of reports) {
  console.log(
    `${r.name.padEnd(10)} verts=${String(r.verts).padStart(7)} quads=${String(r.quads).padStart(7)} tris=${String(r.tris).padStart(7)} junctions=${r.junctions} ms=${r.ms} nan=${r.nan} degen=${r.degenerate}`,
  );
  for (const w of new Set(r.warns)) console.log(`    ! ${w}`);
}
const bad = reports.filter((r) => r.nan > 0 || r.degenerate > 0 || r.quads === 0);
if (bad.length) {
  throw new Error(`FAIL: ${bad.map((b) => b.name).join(', ')}`);
}
console.log('OK');

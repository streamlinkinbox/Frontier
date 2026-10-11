/**
 * Visual probe: builds several networks and renders top-down plus perspective
 * PNGs into tools/.cache so geometry can actually be looked at.
 *
 *   node tools/run-ts.mjs src/tools/visual.ts
 */
import { mkdirSync } from 'node:fs';
import { makeDoc, makeEdge, makeNode, type RoadDoc } from '../core/model';
import { buildScene } from '../core/build';
import { Framebuffer, drawMarker, drawText, rasterMesh, writePng, type Camera } from './raster';

const OUT = 'tools/.cache';
mkdirSync(OUT, { recursive: true });

function cross(cls: 'street' | 'arterial' | 'avenue', armLen = 70, angleDeg = 90): RoadDoc {
  const doc = makeDoc('cross');
  const hub = makeNode(0, 0);
  doc.nodes.push(hub);
  const angles = [0, angleDeg, 180, 180 + angleDeg];
  for (let i = 0; i < angles.length; i++) {
    const a = (angles[i] / 180) * Math.PI;
    const n = makeNode(Math.cos(a) * armLen, Math.sin(a) * armLen);
    doc.nodes.push(n);
    doc.edges.push(makeEdge(hub.id, n.id, i % 2 === 0 ? cls : cls));
  }
  return doc;
}

function tee(): RoadDoc {
  const doc = makeDoc('tee');
  const hub = makeNode(0, 0);
  doc.nodes.push(hub);
  const arms: Array<[number, string]> = [
    [0, 'avenue'],
    [180, 'avenue'],
    [90, 'street'],
  ];
  for (const [deg, cls] of arms) {
    const a = (deg / 180) * Math.PI;
    const n = makeNode(Math.cos(a) * 60, Math.sin(a) * 60);
    doc.nodes.push(n);
    doc.edges.push(makeEdge(hub.id, n.id, cls as 'avenue'));
  }
  return doc;
}

function interchange(): RoadDoc {
  const doc = makeDoc('interchange');
  // a highway and a slip road joining at a shallow angle, plus a diamond
  const hub = makeNode(0, 0);
  doc.nodes.push(hub);
  const arms: Array<[number, string, number]> = [
    [0, 'motorway', 120],
    [180, 'motorway', 120],
    [25, 'ramp', 90],
    [205, 'ramp', 90],
  ];
  for (const [deg, cls, len] of arms) {
    const a = (deg / 180) * Math.PI;
    const n = makeNode(Math.cos(a) * len, Math.sin(a) * len);
    doc.nodes.push(n);
    doc.edges.push(makeEdge(hub.id, n.id, cls as 'motorway'));
  }
  return doc;
}

function network(): RoadDoc {
  const doc = makeDoc('grid');
  const n = 4;
  const step = 70;
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j++) {
      const node = makeNode((i - (n - 1) / 2) * step, (j - (n - 1) / 2) * step);
      doc.nodes.push(node);
    }
  }
  const at = (i: number, j: number) => doc.nodes[j * n + i];
  for (let j = 0; j < n; j++) {
    for (let i = 0; i + 1 < n; i++) {
      doc.edges.push(makeEdge(at(i, j).id, at(i + 1, j).id, j === 1 ? 'avenue' : 'street'));
    }
  }
  for (let i = 0; i < n; i++) {
    for (let j = 0; j + 1 < n; j++) {
      doc.edges.push(makeEdge(at(i, j).id, at(i, j + 1).id, i === 2 ? 'boulevard' : 'street'));
    }
  }
  return doc;
}

interface Shot {
  doc: RoadDoc;
  label: string;
}

const only = process.env.SHOT;
const allShots: Shot[] = [
  { doc: cross('street', 60, 90), label: 'X 90' },
  { doc: cross('avenue', 60, 180 - 62), label: 'X 62' },
  { doc: cross('avenue', 60, 40), label: 'X 40' },
  { doc: tee(), label: 'T' },
  { doc: interchange(), label: 'HWY' },
  { doc: network(), label: 'GRID' },
];
const shots = only ? allShots.filter((s) => s.label.toLowerCase() === only.toLowerCase()) : allShots;

const stages: Array<'corridors' | 'junctions' | 'all' | 'junctiononly'> = process.env.STAGES
  ? ['corridors', 'junctions', 'junctiononly', 'all']
  : ['all'];

for (const shot of shots) {
 for (const stage of stages) {
  const res = buildScene(shot.doc, 'high', { stopAfter: stage === 'junctiononly' ? 'all' : stage, junctionOnly: stage === 'junctiononly' });
  const b = res.builder;
  const w = 1024;
  const h = 768;
  const camTop: Camera = {
    eye: [0, 120, 0.001],
    target: [0, 0, 0],
    up: [0, 0, 1],
    fov: 1,
    orthoHeight: 170,
    projection: 'ortho',
    width: w,
    height: h,
  };
  const fb = new Framebuffer(w, h);
  rasterMesh(fb, b, camTop, { shade: 'material', wireframe: false, light: 0.9 });
  rasterMesh(fb, b, camTop, { shade: 'material', wireframe: true, light: 0.9 });
  for (const node of shot.doc.nodes) drawMarker(fb, camTop, [node.x, node.y, node.z], [1, 0.3, 0.25], 2);
  for (const j of res.junctions) drawMarker(fb, camTop, [j.centre[0], j.centreY + 0.1, j.centre[1]], [0.2, 0.9, 0.6], 2);
  drawText(fb, 12, 12, `${shot.label} V${b.vertexCount} Q${b.quadCount} J${res.junctions.length}`, [0.9, 0.9, 0.95]);
  const slug = shot.label.toLowerCase().replace(/[^a-z0-9]+/g, '-') + (stage === 'all' ? '' : '-' + stage);
  writePng(fb, `${OUT}/top-${slug}.png`);

  // low perspective looking across the junction from the south-east
  const cam: Camera = {
    eye: [46, 26, 46],
    target: [0, 1, 6],
    up: [0, 1, 0],
    fov: (42 * Math.PI) / 180,
    orthoHeight: 100,
    projection: 'perspective',
    width: w,
    height: h,
  };
  const fb2 = new Framebuffer(w, h);
  fb2.clear(0.58, 0.65, 0.74);
  rasterMesh(fb2, b, cam, { shade: 'material', wireframe: false, light: 1 });
  writePng(fb2, `${OUT}/persp-${slug}.png`);

  // close-up of the junction: top-down ortho, shaded + wireframe
  const first = res.junctions[0];
  if (first) {
    const span = 44;
    const cam3: Camera = {
      eye: [first.centre[0], 90, first.centre[1] + 0.001],
      target: [first.centre[0], 0, first.centre[1]],
      up: [0, 0, 1],
      fov: 1,
      orthoHeight: span,
      projection: 'ortho',
      width: w,
      height: h,
    };
    const fb3 = new Framebuffer(w, h);
    const wire = process.env.WIRE === '1';
    const only2 = process.env.LAYER;
    const filter =
      only2 === 'road'
        ? (k: string) => k === 'asphalt' || k === 'asphalt-worn' || k === 'asphalt-fresh' || k === 'paint'
        : only2 === 'pav'
          ? (k: string) => k === 'concrete' || k === 'kerb' || k === 'slabs' || k === 'paver-brick' || k === 'paver-stone' || k === 'grass' || k === 'soil' || k === 'gravel' || k === 'tactile' || k === 'cobble'
          : undefined;
    rasterMesh(fb3, b, cam3, { shade: 'material', wireframe: wire, light: 0.95, debugBackfaces: process.env.DEBUG_BACKFACES === '1', matFilter: filter });
    for (const a of first.arms) {
      const p0 = a.arm.corridor.path.positionAt(a.corridorStation((a.stationLeft + a.stationRight) / 2));
      drawMarker(fb3, cam3, [p0[0], 0.4, p0[2]], [0.95, 0.6, 0.2], 4, String(a.index));
    }
    drawMarker(fb3, cam3, [first.centre[0], 0.5, first.centre[1]], [0.2, 0.95, 0.6], 4);
    for (const c of first.corners) drawText(fb3, 10, 10 + c.index * 8, `C${c.index} R${c.radius.toFixed(2)} T${c.tangencyA.toFixed(1)}/${c.tangencyB.toFixed(1)} ${c.kind}`, [0.9, 0.9, 0.95]);
    for (const a of first.arms) drawText(fb3, 10, 10 + (first.corners.length + a.index) * 8, `A${a.index} M${((a.stationLeft + a.stationRight) / 2).toFixed(2)}`, [0.95, 0.75, 0.4]);
    writePng(fb3, `${OUT}/junc-${slug}.png`);
  }
  const warns = [...new Set([...res.diagnostics.filter((d) => d.level === 'warn').map((d) => d.message), ...res.junctions.flatMap((j) => j.warnings)])];
  console.log(`${shot.label.padEnd(6)} verts=${b.vertexCount} quads=${b.quadCount} tris=${b.triCount} junctions=${res.junctions.length} ms=${res.stats.milliseconds}${warns.length ? ' warns=' + warns.length : ''}`);
  for (const w2 of warns) console.log(`   ! ${w2}`);
 }
}
console.log('images in', OUT);

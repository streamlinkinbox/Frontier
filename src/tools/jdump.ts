/**
 * Junction solver dump: prints the numbers behind one junction so geometry
 * problems can be diagnosed without guessing.
 *
 *   node tools/run-ts.mjs src/tools/jdump.ts
 */
import { makeDoc, makeEdge, makeNode, compileProfile, type RoadDoc } from '../core/index';
import { resolveHandles, buildCorridors, buildArms } from '../core/spline';
import { solveJunction } from '../core/junction';
import { v2dist, v2sub } from '../core/math';
import type { QualityProfile } from '../core/model';

function x90(): RoadDoc {
  const doc = makeDoc('x');
  const hub = makeNode(0, 0);
  doc.nodes.push(hub);
  for (const deg of [0, 90, 180, 270]) {
    const a = (deg / 180) * Math.PI;
    const n = makeNode(Math.cos(a) * 70, Math.sin(a) * 70);
    doc.nodes.push(n);
    doc.edges.push(makeEdge(hub.id, n.id, 'street'));
  }
  return doc;
}

function tee(): RoadDoc {
  const d = makeDoc('tee');
  const hub = makeNode(0, 0);
  d.nodes.push(hub);
  for (const [deg, cls] of [[0, 'avenue'], [180, 'avenue'], [90, 'street']] as Array<[number, string]>) {
    const a = (deg / 180) * Math.PI;
    const n = makeNode(Math.cos(a) * 60, Math.sin(a) * 60);
    d.nodes.push(n);
    d.edges.push(makeEdge(hub.id, n.id, cls as 'avenue'));
  }
  return d;
}

const which = process.argv[process.argv.length - 1];
const doc = which === 'tee' ? tee() : x90();
const diags: never[] = [];
const { handles } = resolveHandles(doc, diags as never);
const { corridors } = buildCorridors(doc, 'high' as QualityProfile, diags as never, handles);
const arms = buildArms(corridors);
const cpByEdge = new Map(doc.edges.map((e) => [e.id, compileProfile(e.spec)]));
const hub = doc.nodes[0];
const hubArms = arms.get(hub.id) ?? [];
console.log('arms at hub:', hubArms.length);
for (const a of hubArms) {
  const cp = cpByEdge.get(a.edge.id)!;
  console.log(
    `  arm ${a.end.padEnd(5)} edge ${a.edge.id} cls ${a.edge.spec.class.padEnd(8)} flip ${a.flip} dir [${a.dirOut.map((v) => v.toFixed(3)).join(', ')}] nodeStation ${a.nodeStation.toFixed(2)} len ${a.corridor.length.toFixed(1)} kerbL ${cp.kerb.left.toFixed(2)} kerbR ${cp.kerb.right.toFixed(2)} bounds [${cp.bounds.left.toFixed(2)}, ${cp.bounds.right.toFixed(2)}]`,
  );
}
{
  const cp = cpByEdge.get(hubArms[0].edge.id)!;
  console.log('zones:');
  cp.zones.forEach((z, i) => console.log(`  #${i} ${z.kind.padEnd(12)} [${z.from.toFixed(2)}, ${z.to.toFixed(2)}] y ${z.yFrom.toFixed(2)}->${z.yTo.toFixed(2)} dens ${z.density} vert ${z.vertical} mat ${z.material}`));
  console.log('kerb', JSON.stringify(cp.kerb), 'bounds', JSON.stringify(cp.bounds), 'median', JSON.stringify(cp.median));
}
for (const c of corridors) {
  const p0 = c.path.positionAt(0);
  const p1 = c.path.positionAt(c.length);
  const mid = c.path.positionAt(c.length / 2);
  console.log(
    `corridor ${c.id} len ${c.length.toFixed(2)} start ${c.start.nodeId.slice(-4)} end ${c.end.nodeId.slice(-4)} nodeStations [${c.nodeStations.map((v) => v.toFixed(2))}] p0 (${p0[0].toFixed(2)},${p0[2].toFixed(2)}) p1 (${p1[0].toFixed(2)},${p1[2].toFixed(2)}) mid (${mid[0].toFixed(2)},${mid[2].toFixed(2)}) edges ${c.edges.map((e) => e.id.slice(-4)).join('+')}`,
  );
}
const solve = solveJunction(0, hubArms, 'high' as QualityProfile, cpByEdge);
console.log('sorted arms:');
solve.arms.forEach((a, i) => {
  const co = a.arm.corridor;
  if (i === 0) {
    const pp = (s: number) => co.path.positionAt(s);
    console.log(`  [corridor probe] id ${co.id} pathLen ${co.path.length.toFixed(3)} len ${co.length.toFixed(3)} p0 (${pp(0)[0].toFixed(2)},${pp(0)[2].toFixed(2)}) p8.55 (${pp(8.55)[0].toFixed(2)},${pp(8.55)[2].toFixed(2)}) p70 (${pp(70)[0].toFixed(2)},${pp(70)[2].toFixed(2)}) pieces n/a nodes ${co.nodes.map((n) => n.id.slice(-4)).join(',')} nodeStations ${co.nodeStations.map((v) => v.toFixed(2)).join(',')}`);
  }
  const sStart = a.corridorStation(a.stationLeft);
  const sEnd = a.corridorStation(a.stationRight);
  const pL = a.arm.corridor.path.positionAt(sStart);
  const pR = a.arm.corridor.path.positionAt(sEnd);
  console.log(`  arm[${i}] corridor ${a.arm.corridor.id.slice(-5)} len ${a.arm.corridor.length.toFixed(1)} end=${a.end} nodeStation ${a.nodeStation.toFixed(2)} stationLeft ${a.stationLeft.toFixed(2)}->corridorStation ${sStart.toFixed(2)} pos (${pL[0].toFixed(2)},${pL[2].toFixed(2)})  stationRight ${a.stationRight.toFixed(2)}->${sEnd.toFixed(2)} pos (${pR[0].toFixed(2)},${pR[2].toFixed(2)})`);
});
solve.arms.forEach((a, i) => console.log(`  [${i}] bearing ${((a.bearing * 180) / Math.PI).toFixed(1)}° outDir [${a.outDir.map((v) => v.toFixed(2))}] rightDir [${a.rightDir.map((v) => v.toFixed(2))}] kerb {l ${a.kerb.left.toFixed(2)}, r ${a.kerb.right.toFixed(2)}} stL ${a.stationLeft.toFixed(2)} stR ${a.stationRight.toFixed(2)} nodeStation ${a.nodeStation.toFixed(2)} pos [${a.arm.pos[0].toFixed(1)}, ${a.arm.pos[1].toFixed(1)}] axisPoint ${(a.arm.nodeStation).toFixed(1)}`,));
console.log('floor per arm:', solve.arms.map((a) => a.floor.toFixed(2)).join(' '));
console.log('mouth stations L/R:', solve.arms.map((a) => `${a.stationLeft.toFixed(2)}/${a.stationRight.toFixed(2)}`).join(' '));
for (const c of solve.corners) {
  console.log(
    `corner ${c.index} A${c.armA}->B${c.armB} kind=${c.kind} r=${c.radius.toFixed(2)} tangA=${c.tangencyA.toFixed(2)} tangB=${c.tangencyB.toFixed(2)} mid=${c.midPoints.length} bandsA=${c.bandsA.length} bandsB=${c.bandsB.length} feasible=${c.feasible} ${c.note}`,
  );
  console.log('   bandA offsets:', c.bandsA.map((b) => `${b.offset.toFixed(2)}@${b.y.toFixed(2)}:${b.kind}`).join(' '));
  console.log('   bandB offsets:', c.bandsB.map((b) => `${b.offset.toFixed(2)}@${b.y.toFixed(2)}:${b.kind}`).join(' '));
  const A = solve.arms[c.armA];
  const B = solve.arms[c.armB];
  const endA = (a: typeof A, which: 'start' | 'end') => {
    const st = which === 'start' ? a.stationRight : a.stationLeft;
    const pos = a.arm.corridor.path.positionAt(a.corridorStation(st));
    const off = which === 'start' ? a.kerb.right : a.kerb.left;
    const right = [a.outDir[1], -a.outDir[0]];
    return [pos[0] + right[0] * off, pos[2] + right[1] * off];
  };
  console.log('   start', endA(A, 'start').map((v) => v.toFixed(2)).join(','), 'end', endA(B, 'end').map((v) => v.toFixed(2)).join(','));
  console.log('   V', c.V ? c.V.map((v) => v.toFixed(2)).join(',') : 'none', 'centre', c.centre ? c.centre.map((v) => v.toFixed(2)).join(',') : 'none', 'sweep', ((c.sweep * 180) / Math.PI).toFixed(1),
    'V-dist-node', c.V ? v2dist(c.V, [A.arm.pos[0], A.arm.pos[1]]).toFixed(2) : '-');
  if (c.V && c.centre) {
    const bis = v2sub(c.centre, c.V);
    const toNode = v2sub([A.arm.pos[0], A.arm.pos[1]], c.V);
    console.log('   corner outward dot(centre-V, node-V) =', (bis[0] * toNode[0] + bis[1] * toNode[1]).toFixed(3), '(negative = corner points at the junction = wrong side)');
  }
  if (c.midPoints.length) {
    console.log('   first arc pt', c.midPoints.find((m) => m.arc)?.p.map((v) => v.toFixed(2)).join(','), 'last arc pt', [...c.midPoints].reverse().find((m) => m.arc)?.p.map((v) => v.toFixed(2)).join(','));
    console.log('   chain endpoints', c.midPoints[0].p.map((v) => v.toFixed(2)).join(','), c.midPoints[c.midPoints.length - 1].p.map((v) => v.toFixed(2)).join(','));
  }
}
console.log('ring points:', solve.ring.length);
console.log('mouth offsets per arm:', solve.arms.map((a) => a.mouthOffsets.map((o) => o.toFixed(2)).join(',')).join(' | '));
console.log('centre', solve.centre.map((v) => v.toFixed(2)), 'area', solve.area.toFixed(1));
for (const w of solve.warnings) console.log('  !', w);

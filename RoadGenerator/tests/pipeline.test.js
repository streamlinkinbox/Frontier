// End-to-end pipeline tests: presets → roads → markings → furniture → rails → structures → traffic.
import test from "node:test";
import assert from "node:assert/strict";
import { generateCity, PRESETS } from "../src/core/city.js";
import { buildRoadNetwork, auditMesh } from "../src/core/build.js";
import { buildMarkings } from "../src/core/markings.js";
import { furnish } from "../src/core/furnish.js";
import { buildRails } from "../src/core/rail.js";
import { buildStructures, classifyRoads, findCrossings } from "../src/core/structures.js";
import { Traffic, MAX_VEHICLES } from "../src/core/traffic.js";
import { prop, VEHICLE_KINDS, BOLLARD_STYLES } from "../src/core/props.js";
import { defaultStacks, bakeMaterial } from "../src/core/textures.js";
import { Network } from "../src/core/network.js";

function pipeline(preset) {
  const net = generateCity({ preset });
  const plan = buildRoadNetwork(net, { step: 2 });
  const mk = buildMarkings(plan), F = furnish(plan), rails = buildRails(net, plan), st = buildStructures(plan, {}, rails.lines);
  return { net, plan, mk, F, rails, st };
}

for (const p of PRESETS) {
  test(`preset ${p.id} builds a clean all-quad network`, () => {
    const { plan, mk, F, rails, st } = pipeline(p.id);
    const a = auditMesh(plan.mesh);
    assert.ok(a.faces > 1000, "has geometry");
    assert.equal(a.nonManifoldEdges, 0, "no non-manifold edges");
    assert.equal(a.degenerate, 0, "no degenerate faces");
    assert.ok(a.quadRatio > 0.97, `quad ratio ${a.quadRatio}`);
    for (const mb of [mk, F.solid, F.paint, rails.solid, st.solid, st.ground]) { const b = auditMesh(mb); assert.equal(b.nonManifoldEdges, 0, mb.owner + " manifold"); }
    assert.ok(plan.diagnostics.every((d) => !/ear-clip/i.test(d.message)), "no ear-clip fallbacks");
  });
}

test("interchange: ramps attach as 3-arm acute junctions with gore markings and the mainline becomes a viaduct over the avenue", () => {
  const { net, plan, mk, st } = pipeline("diamond");
  const noses = [...plan.nodes.values()].filter((N) => N.kind === "junction" && N.arms.length === 3 && N.arms.some((a) => a.R.road.profile.id === "ramp"));
  assert.equal(noses.length, 4);
  const crossings = findCrossings(plan);
  assert.ok(crossings.length >= 2, "carriageways cross the avenue");
  const segs = classifyRoads(plan, crossings);
  const bridges = [...segs.values()].flat().filter((s) => s.kind === "bridge");
  assert.ok(bridges.length >= 2, "bridge spans classified");
  assert.ok(st.instances.size > 0, "piers / parapet props emitted");
  assert.ok(mk.faces.length > 100);
});

test("railway level crossing and subway tunnel", () => {
  const { net, rails, st } = pipeline("demo");
  assert.ok(rails.crossings.length >= 1, "level crossing detected");
  assert.ok([...rails.instances.keys()].some((k) => k.startsWith("crossbuck")), "crossbucks placed");
  assert.ok([...rails.instances.keys()].some((k) => k.startsWith("boom")), "booms placed");
  const tunnel = rails.lines.find((l) => l.tunnel);
  assert.ok(tunnel, "subway line analysed");
  const xray = st.ground.faces.filter((f) => f.g === "ground-xray").length;
  assert.ok(xray > 0, "ground over the tunnel is tagged for X-ray");
});

test("traffic respects the 25 vehicle cap, moves, obeys signals and never strands vehicles", () => {
  const { plan, F, rails } = pipeline("grid");
  const T = new Traffic(plan, F, rails.lines, { vehicles: 99 });
  assert.equal(T.vehicles.length, MAX_VEHICLES);
  assert.ok(T.signals.nodes.length > 0, "signalised junctions exist");
  const start = T.vehicles.map((v) => v.edge.pts[0].slice());
  for (let i = 0; i < 60 * 90; i++) T.update(1 / 60);
  const stuck = T.vehicles.filter((v) => v.waiting > 60);
  assert.equal(stuck.length, 0, "no vehicle waits more than a minute");
  assert.ok(T.graph.edges.every((e) => e.kind !== "lane" || e.next.length > 0), "every lane has an exit");
  const poses = T.poses();
  assert.equal(Object.values(poses).reduce((a, p) => a + p.length / 16, 0), MAX_VEHICLES);
  assert.ok(T.trainPoses().length > 0, "trains running");
  const L = T.signals.lenses();
  assert.ok(L.red.length + L.green.length + L.amber.length > 0);
  assert.ok(start.length);
});

test("props build with positive orientation and all vehicle / bollard variants exist", () => {
  for (const k of VEHICLE_KINDS) assert.ok(prop("carBody", k).faces.length > 100, k);
  for (const b of BOLLARD_STYLES) assert.ok(prop("bollard", b).faces.length > 10, b);
  for (const n of ["streetlight", "trafficLight", "billboard", "guardrailPanel", "wbeamSection", "jerseyBarrier", "trafficCone", "signPost", "trainCar"]) assert.ok(prop(n).faces.length > 10, n);
  assert.equal(auditMesh(prop("jerseyBarrier")).boundaryEdges, 0, "jersey barrier is a closed shell");
});

test("every material referenced by the geometry has a texture stack", () => {
  const stacks = defaultStacks();
  const { plan, mk, F, rails, st } = pipeline("demo");
  const used = new Set();
  for (const mb of [plan.mesh, mk, F.solid, F.paint, rails.solid, st.solid, st.ground]) for (const m of mb.materials) used.add(m);
  for (const g of F.instances.values()) for (const m of prop(g.prop, g.variant).materials) used.add(m);
  for (const m of used) assert.ok(stacks[m], `missing stack for ${m}`);
  const b = bakeMaterial(stacks.asphalt, 32);
  assert.equal(b.albedo.length, 32 * 32 * 4);
});

test("network JSON round-trip preserves roads, rails and node moves rebuild cleanly", () => {
  const net = generateCity({ preset: "demo" });
  const j = JSON.parse(JSON.stringify(net.toJSON()));
  const net2 = Network.fromJSON(j);
  assert.equal(net2.roads.size, net.roads.size); assert.equal(net2.rails.size, net.rails.size);
  const n = [...net2.nodes.values()].find((x) => x.name === "Main cross");
  n.p[0] += 14; n.p[2] -= 9; n.p[1] = 7.5; // move + raise the whole intersection
  const plan = buildRoadNetwork(net2, { step: 2 });
  const a = auditMesh(plan.mesh);
  assert.equal(a.nonManifoldEdges, 0); assert.ok(a.quadRatio > 0.97);
});

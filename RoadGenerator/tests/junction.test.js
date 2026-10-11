import { test } from "node:test";
import assert from "node:assert/strict";
import { Network } from "../src/core/network.js";
import { buildRoadNetwork, auditMesh } from "../src/core/build.js";
import { solveSpokes } from "../src/core/junction.js";

function star(arms, profiles, len = 160, radius = 9) {
  const net = new Network();
  const c = net.addNode([0, 0, 0], { radius, name: "centre" });
  arms.forEach((ang, i) => {
    const n = net.addNode([Math.cos(ang) * len, 0, Math.sin(ang) * len], { radius });
    net.addRoad(c, n, { profile: profiles[i % profiles.length], name: `arm${i}` });
  });
  return net;
}
const d2r = (d) => (d * Math.PI) / 180;

function check(net, label, opts = {}) {
  const plan = buildRoadNetwork(net, { step: 2 });
  const a = auditMesh(plan.mesh);
  assert.equal(a.nan, 0, `${label}: NaNs`);
  assert.equal(a.nonManifoldEdges, 0, `${label}: non-manifold edges`);
  assert.equal(a.degenerate, 0, `${label}: degenerate faces`);
  if (!opts.allowTris) assert.equal(a.tris, 0, `${label}: triangles present (${a.tris})`);
  const errs = plan.diagnostics.filter((d) => d.level === "error");
  if (!opts.allowErrors) assert.equal(errs.length, 0, `${label}: ${errs.map((e) => e.message).join("; ")}`);
  // surface normals of carriageway must point up
  const mb = plan.mesh; mb.computeNormals();
  let down = 0;
  const asphalt = mb.materials.indexOf("asphalt");
  for (const f of mb.faces) if (f.m === asphalt) { let ny = 0; for (const v of f.i) ny += mb.normals[v * 3 + 1]; if (ny < 0) down++; }
  assert.equal(down, 0, `${label}: ${down} asphalt faces facing down`);
  return { plan, audit: a };
}

test("4-way cross, equal profiles → all quads, cross centre", () => {
  const { plan, audit } = check(star([0, 90, 180, 270].map(d2r), ["local"]), "cross");
  assert.ok(audit.quadRatio === 1);
  assert.equal(plan.junctions[0].centre.kind, "cross");
});
test("4-way avenue × local → refined, all quads", () => {
  const { plan } = check(star([0, 90, 180, 270].map(d2r), ["avenue", "local"]), "avenue×local");
  assert.equal(plan.junctions[0].centre.kind, "cross");
});
test("T junction mixed profiles → midpoint centre, all quads", () => {
  const { plan } = check(star([0, 90, 180].map(d2r), ["avenue", "local", "avenue"]), "T");
  assert.equal(plan.junctions[0].centre.kind, "midpoint");
});
test("Y junction 120° → all quads", () => {
  const { plan } = check(star([0, 120, 240].map(d2r), ["collector"]), "Y");
  assert.equal(plan.junctions[0].centre.quads, true);
});
test("acute merge 25° stays clean", () => {
  const { plan } = check(star([0, 25, 180].map(d2r), ["collector"], 260), "acute");
  const j = plan.junctions[0];
  const acute = j.corners.find((c) => c.theta < 0.5);
  assert.ok(acute, "acute corner found");
  assert.ok(acute.A.cut > 20, `acute arm cut ${acute.A.cut}`);
});
test("5-way and 6-way", () => {
  check(star([0, 70, 150, 220, 300].map(d2r), ["local", "collector"], 200), "5-way");
  check(star([0, 60, 120, 180, 240, 300].map(d2r), ["local"], 200), "6-way");
});
test("skewed cross 60°/120°", () => {
  check(star([0, 60, 180, 240].map(d2r), ["avenue", "local"], 220), "skew");
});
test("joint weld and transition", () => {
  const net = new Network();
  const a = net.addNode([0, 0, 0]), b = net.addNode([100, 0, 0]), c = net.addNode([200, 0, 30]);
  net.addRoad(a, b, { profile: "local" }); net.addRoad(b, c, { profile: "local" });
  const { plan } = check(net, "weld");
  assert.equal([...plan.nodes.values()].find((n) => n.node === b).junction.type, "bend");
  const net2 = new Network();
  const a2 = net2.addNode([0, 0, 0]), b2 = net2.addNode([100, 0, 0]), c2 = net2.addNode([200, 0, 30]);
  net2.addRoad(a2, b2, { profile: "local" }); net2.addRoad(b2, c2, { profile: "avenue" });
  const r = check(net2, "transition");
  assert.equal([...r.plan.nodes.values()].find((n) => n.node === b2).junction.type, "bend");
});
test("curved approach grid", () => {
  const net = new Network();
  const g = {};
  for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) g[`${i}${j}`] = net.addNode([i * 120 + (j % 2) * 15, 0, j * 120 + (i % 2) * 10], { radius: 8 });
  for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) {
    if (i < 2) net.addRoad(g[`${i}${j}`], g[`${i + 1}${j}`], { profile: j === 1 ? "avenue" : "local", pts: [[i * 120 + 60, 0, j * 120 + 20 * (i - 0.5)]] });
    if (j < 2) net.addRoad(g[`${i}${j}`], g[`${i}${j + 1}`], { profile: i === 1 ? "collector" : "local" });
  }
  const { audit } = check(net, "grid");
  assert.ok(audit.faces > 2000);
});
test("spoke solver", () => {
  assert.deepEqual(solveSpokes([10, 10, 10]), [5, 5, 5]);
  assert.deepEqual(solveSpokes([16, 10, 16]), [5, 11, 5]);
  assert.equal(solveSpokes([4, 4, 20]), null);
  const s = solveSpokes([8, 10, 8, 12, 10]);
  assert.ok(s && s.every((v) => v > 0));
});

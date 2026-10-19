import test from "node:test";
import assert from "node:assert/strict";
import * as THREE from "three";
import {
  newProject,
  addRoad,
  validateProject,
  terrainHeight,
  moveNode,
} from "../src/model.js";
import {
  installBridgeNetworks,
  auditBridgeNetworks,
  BRIDGE_NETWORKS,
} from "../src/bridge-networks.js";
import {
  networkDeck,
  deckVolume,
  networkPiers,
  buildBridgeNetworks,
} from "../src/bridge-structures.js";
import { auditMesh, roadY, generateRoadMesh } from "../src/geometry.js";

const p = newProject();
addRoad(p, { x: -100, z: 0 }, { x: 0, z: 0 }, "country");
const before = structuredClone(p),
  prepared = installBridgeNetworks(p);

test("three networks are appended atomically, connected to the original road and idempotent", () => {
  assert.equal(p.bridgeNetworkVersion, 1);
  assert.equal(p.bridgeNetworks.length, 3);
  for (const original of before.edges)
    assert.deepEqual(
      p.edges.find((e) => e.id === original.id),
      original,
    );
  for (const original of before.nodes)
    assert.deepEqual(
      p.nodes.find((n) => n.id === original.id),
      original,
    );
  const reachable = new Set([before.nodes[0].id]);
  let size;
  do {
    size = reachable.size;
    for (const e of p.edges)
      if (reachable.has(e.a) || reachable.has(e.b)) {
        reachable.add(e.a);
        reachable.add(e.b);
      }
  } while (size !== reachable.size);
  assert.ok(
    p.edges
      .filter((e) => e.bridgeNetwork)
      .every((e) => reachable.has(e.a) && reachable.has(e.b)),
  );
  const snapshot = JSON.stringify(p);
  assert.equal(installBridgeNetworks(p).added, false);
  assert.equal(JSON.stringify(p), snapshot);
  for (const n of BRIDGE_NETWORKS)
    assert.ok(
      p.edges.some((e) => e.bridgeNetwork === n.id && e.structure === "bridge"),
    );
  assert.deepEqual(auditMesh(prepared.result, p).issues, []);
  assert.equal(prepared.streetAudit.ok, true);
});
test("shared deck mouths are continuous, gradients gentle, and crossing envelopes clear", () => {
  const report = auditBridgeNetworks(p);
  assert.equal(report.ok, true);
  assert.ok(report.maxGrade < 0.06);
  assert.ok(report.crossings.length >= 7);
  assert.ok(report.crossings.every((c) => c.clearance >= 5.5));
  for (const node of p.nodes) {
    const incident = p.edges.filter((e) => e.a === node.id || e.b === node.id);
    if (!incident.some((e) => e.bridgeNetwork)) continue;
    const heights = incident.map((e) => roadY(p, e, e.a === node.id ? 0 : 1));
    assert.ok(Math.max(...heights) - Math.min(...heights) < 0.06);
  }
  const low = structuredClone(p);
  for (const n of low.nodes) if (n.elevation !== undefined) n.elevation -= 5;
  assert.equal(auditBridgeNetworks(low).ok, false);
  const steep = structuredClone(p),
    high = steep.nodes.find((n) => n.elevation > 5);
  high.elevation += 30;
  assert.ok(auditBridgeNetworks(steep).issues.some((i) => i.includes("grade")));
});
test("deck geometry includes continuous junction slabs and finite closed volume attributes", () => {
  const deck = networkDeck(prepared.result);
  assert.ok(deck.mesh.groups.some((g) => g.startsWith("junction:")));
  assert.deepEqual(
    auditMesh({ mesh: deck.mesh, roads: [], junctions: [] }, newProject())
      .issues,
    [],
  );
  const geometry = deckVolume(deck.mesh),
    edges = new Map();
  for (const attribute of ["position", "normal", "uv"])
    assert.ok(
      Array.from(geometry.getAttribute(attribute).array).every(Number.isFinite),
    );
  assert.equal(
    geometry.getAttribute("uv").count,
    geometry.getAttribute("position").count,
  );
  const ids = geometry.index.array;
  for (let i = 0; i < ids.length; i += 3)
    for (let j = 0; j < 3; j++) {
      const key = [ids[i + j], ids[i + ((j + 1) % 3)]]
        .sort((a, b) => a - b)
        .join(":");
      edges.set(key, (edges.get(key) || 0) + 1);
    }
  assert.ok(
    [...edges.values()].every((count) => count === 2),
    "closed indexed slab shell",
  );
  geometry.dispose();
});
test("piers stay outside lower carriageways, including the landscaped island supports", () => {
  const piers = networkPiers(p, prepared.result);
  assert.ok(piers.length > 50);
  const batches = [];
  const fake = {
    p,
    result: prepared.result,
    batch: (g) => batches.push(g),
    strip() {},
    box() {},
    tree() {},
    arrow() {},
  };
  buildBridgeNetworks(fake);
  assert.ok(fake.networkIslandDeck);
  assert.ok(fake.networkIslandPiers.length >= 4);
  assert.deepEqual(
    auditMesh(
      { mesh: fake.networkIslandDeck, roads: [], junctions: [] },
      newProject(),
    ).issues,
    [],
  );
  for (const pier of [...piers, ...fake.networkIslandPiers])
    for (const road of prepared.result.roads) {
      const s = Math.max(
        0,
        Math.min(
          road.length,
          (pier.x - road.a.x) * road.dx + (pier.z - road.a.z) * road.dz,
        ),
      );
      if (roadY(p, road.e, s / road.length) >= pier.top - 0.5) continue;
      assert.ok(
        Math.hypot(
          pier.x - road.a.x - s * road.dx,
          pier.z - road.a.z - s * road.dz,
        ) >
          road.w / 2 + 3,
      );
    }
  for (const g of batches) {
    assert.ok(g.getAttribute("uv"));
    assert.ok(Array.from(g.attributes.position.array).every(Number.isFinite));
    g.dispose();
  }
});
test("stacked endpoints remain separate graph nodes; moving an elevated node retains its height", () => {
  const q = newProject();
  addRoad(q, { x: -60, z: 0 }, { x: 0, z: 0 }, "country");
  const ids = addRoad(
    q,
    { x: 0, z: 0, elevation: 10 },
    { x: 60, z: 0, elevation: 10 },
    "local",
    { structure: "bridge", elevationMode: "linear", sidewalk: 0 },
  );
  assert.equal(q.nodes.filter((n) => n.x === 0 && n.z === 0).length, 2);
  validateProject(q);
  const e = q.edges.find((e) => e.id === ids[0]);
  assert.equal(roadY(q, e, 0), 10);
  assert.equal(roadY(q, e, 1), 10);
  const moved = moveNode(q, e.b, { x: 70, z: 5 });
  assert.equal(moved.ok, true);
  assert.equal(q.nodes.find((n) => n.x === 70).elevation, 10);
});
test("malformed metadata and unsafe street profiles are rejected without partial installation", () => {
  const bad = structuredClone(p);
  bad.nodes[0].elevation = Infinity;
  assert.throws(() => validateProject(bad), /node/);
  const badCurve = structuredClone(p);
  badCurve.edges.find((e) => e.networkCurve).networkCurveRadius = NaN;
  assert.throws(() => validateProject(badCurve), /curve/);
  const badProfile = structuredClone(p);
  badProfile.edges.find((e) => e.bridgeNetwork === "loop").sidewalk = 3;
  assert.throws(() => validateProject(badProfile), /shoulders/);
  const invalid = newProject();
  addRoad(invalid, { x: 0, z: 0 }, { x: 60, z: 0 });
  invalid.nodes[1].x = 2;
  const snapshot = structuredClone(invalid);
  assert.throws(() => installBridgeNetworks(invalid));
  assert.deepEqual(invalid, snapshot);
});

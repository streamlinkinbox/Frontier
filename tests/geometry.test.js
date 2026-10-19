import test from "node:test";
import assert from "node:assert/strict";
import {
  newProject,
  seedProject,
  addRoad,
  moveNode,
  cleanup,
  nearestSnap,
  validateProject,
  terrainHeight,
} from "../src/model.js";
import { generateRoadMesh, auditMesh, exportOBJ } from "../src/geometry.js";
const point = (x, z) => ({ x, z });
function healthy(p) {
  const result = generateRoadMesh(p),
    audit = auditMesh(result, p);
  assert.equal(audit.degenerate, 0);
  assert.equal(audit.duplicate, 0);
  assert.equal(audit.nonmanifold, 0);
  assert.equal(audit.crossings, 0);
  assert.equal(audit.seamErrors, 0);
  assert.equal(audit.nonConvex, 0);
  assert.equal(audit.selfIntersections, 0);
  assert.ok(
    result.mesh.faces.every((f) => f.length === 4 && new Set(f).size === 4),
  );
  return result;
}

test("starter county has quad-only, welded, nondegenerate road surfaces", () => {
  const p = seedProject(),
    r = healthy(p);
  assert.deepEqual(auditMesh(r, p).issues, []);
  assert.ok(p.edges.length > 70);
  assert.ok(r.junctions.some((j) => j.degree === 3));
  assert.ok(r.junctions.some((j) => j.degree === 4));
});
test("a plus crossing splits both centerlines and produces one shared four-way junction", () => {
  const p = newProject();
  addRoad(p, point(-40, 0), point(40, 0));
  addRoad(p, point(0, -40), point(0, 40));
  assert.equal(p.edges.length, 4);
  assert.equal(p.nodes.length, 5);
  const r = healthy(p);
  assert.equal(r.junctions.length, 1);
  assert.equal(r.junctions[0].degree, 4);
});
test("a T crossing has exactly three arms without a hidden overlapping through-road", () => {
  const p = newProject();
  addRoad(p, point(-40, 0), point(40, 0));
  addRoad(p, point(0, 40), point(0, 0));
  assert.equal(p.edges.length, 3);
  assert.equal(healthy(p).junctions[0].degree, 3);
});
test("a Y junction supports mixed widths", () => {
  const p = newProject();
  for (const [angle, preset] of [
    [-90, "local"],
    [30, "avenue"],
    [150, "country"],
  ]) {
    const a = (angle * Math.PI) / 180;
    addRoad(p, point(0, 0), point(70 * Math.cos(a), 70 * Math.sin(a)), preset);
  }
  assert.equal(healthy(p).junctions[0].degree, 3);
});
test("straight two-arm transitions contain no collinear degenerate quads", () => {
  const p = newProject();
  addRoad(p, point(-80, 0), point(0, 0), "avenue");
  addRoad(p, point(0, 0), point(80, 0), "local");
  healthy(p);
});
test("bridge crossings do not create at-grade junctions", () => {
  const p = newProject();
  addRoad(p, point(-40, 0), point(40, 0));
  addRoad(p, point(0, -40), point(0, 40), "country", { structure: "bridge" });
  assert.equal(p.edges.length, 2);
  assert.equal(generateRoadMesh(p).junctions.length, 0);
  healthy(p);
});
test("duplicate, reversed, partial coincident and tiny edges are refused", () => {
  const p = newProject();
  addRoad(p, point(-40, 0), point(40, 0));
  for (const [a, b] of [
    [point(-40, 0), point(40, 0)],
    [point(40, 0), point(-40, 0)],
    [point(-10, 0), point(60, 0)],
    [point(0, 10), point(0, 12)],
  ])
    assert.deepEqual(addRoad(p, a, b), []);
  assert.equal(p.edges.length, 1);
});
test("snap handles endpoint and interior projections", () => {
  const p = newProject();
  addRoad(p, point(0, 0), point(60, 0));
  assert.deepEqual(nearestSnap(p, point(2, 3)), point(0, 0));
  assert.deepEqual(nearestSnap(p, point(30, 4)), point(30, 0));
  assert.deepEqual(nearestSnap(p, point(30.2, 4.4), false), point(30.2, 4.4));
});
test("moving a shared node rebuilds all connected roads and preserves names", () => {
  const p = newProject();
  addRoad(p, point(-40, 0), point(0, 0), "local", { name: "West" });
  addRoad(p, point(0, 0), point(40, 0), "country", { name: "East" });
  const id = p.nodes.find((n) => n.x === 0).id;
  assert.ok(moveNode(p, id, point(0, 12)).ok);
  assert.equal(p.edges.length, 2);
  assert.deepEqual(
    p.edges.map((e) => e.name),
    ["West", "East"],
  );
  healthy(p);
});
test("invalid node moves are atomic", () => {
  const p = newProject();
  addRoad(p, point(0, 0), point(40, 0));
  const before = JSON.stringify(p);
  assert.equal(moveNode(p, p.nodes[0].id, point(39, 0)).ok, false);
  assert.equal(JSON.stringify(p), before);
});
test("terrain edits change road elevation while preserving surface connectivity", () => {
  const p = seedProject(),
    before = generateRoadMesh(p).mesh.vertices.map((v) => v[1]);
  p.brushes.push({ x: 0, z: 0, radius: 24, amount: 1.5 });
  const after = healthy(p).mesh.vertices.map((v) => v[1]);
  assert.ok(after.some((y, i) => Math.abs(y - before[i]) > 0.1));
  assert.ok(terrainHeight(0, 0, p.brushes) > terrainHeight(0, 0));
});
test("OBJ exports one quad face for every surface face, with valid welded indices", () => {
  const p = seedProject(),
    r = healthy(p),
    obj = exportOBJ(r, p),
    faces = obj.split("\n").filter((l) => l.startsWith("f "));
  assert.equal(faces.length, r.mesh.faces.length);
  for (const f of faces) {
    const ids = f
      .split(" ")
      .slice(1)
      .map((token) => Number(token.split("/")[0]));
    assert.equal(ids.length, 4);
    assert.ok(ids.every((i) => i > 0 && i <= r.mesh.vertices.length));
  }
});
test("cleanup removes orphan nodes after a deletion", () => {
  const p = newProject();
  addRoad(p, point(0, 0), point(40, 0));
  p.edges = [];
  cleanup(p);
  assert.equal(p.nodes.length, 0);
});
test("project import rejects malformed data and preserves valid projects", () => {
  const p = seedProject();
  assert.equal(validateProject(p), p);
  assert.throws(() => validateProject({ nodes: [] }));
  const bad = structuredClone(p);
  bad.edges[0].laneWidth = -2;
  assert.throws(() => validateProject(bad));
  const bad2 = structuredClone(p);
  bad2.nodes[0].x = Infinity;
  assert.throws(() => validateProject(bad2));
});

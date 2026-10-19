import test from "node:test";
import assert from "node:assert/strict";
import { landscapedProject } from "../src/starter.js";
import {
  newProject,
  addRoad,
  moveNode,
  dist,
  validateProject,
  segmentIntersection,
} from "../src/model.js";
import { addSampleCity } from "../src/sample-city.js";
import {
  installTransportSamples,
  repairSampleCity,
} from "../src/transport-samples.js";
import {
  railCrossings,
  exitPoints,
  addRailExit,
  corridorPoints,
  crossingIssues,
} from "../src/transport-geometry.js";
import { parkingBays, plantingStations } from "../src/landscape-layout.js";
import { auditMesh, roadY, generateRoadMesh } from "../src/geometry.js";
import { sweep, buildTransportRail } from "../src/transport-world.js";
const p = landscapedProject(),
  original = structuredClone(p);
addSampleCity(p);
const before = structuredClone(p),
  built = installTransportSamples(p);
test("transport samples are connected, audited and idempotent; existing Westhaven survives", () => {
  assert.equal(p.edges.length, 145);
  assert.deepEqual(auditMesh(built.result, p).issues, []);
  assert.equal(built.streetAudit.ok, true);
  assert.equal(built.streetAudit.overlapPairs, 0);
  assert.equal(built.streetAudit.seamErrors, 0);
  for (const e of original.edges)
    assert.deepEqual(
      p.edges.find((x) => x.id === e.id),
      e,
    );
  const snapshot = JSON.stringify(p);
  assert.equal(installTransportSamples(p).added, false);
  assert.equal(JSON.stringify(p), snapshot);
  const reached = new Set([p.nodes[0].id]);
  for (let i = 0; i < p.nodes.length; i++)
    for (const e of p.edges)
      if (reached.has(e.a) || reached.has(e.b)) {
        reached.add(e.a);
        reached.add(e.b);
      }
  assert.ok(p.edges.every((e) => reached.has(e.a) && reached.has(e.b)));
});
test("parallel parking has independent 2.2m bays, .9m door zones and 2m cycle lanes", () => {
  const r = built.result.roads.find((r) => r.e.preset === "parking"),
    l = r.layout,
    bays = parkingBays(r);
  assert.equal(l.parking, 2.2);
  assert.equal(l.buffer, 0.9);
  assert.equal(l.cycle, 2);
  assert.equal(l.width, 16.4);
  assert.ok(bays.length > 10);
  assert.deepEqual(plantingStations(l, r.landscape), []);
  for (const b of bays) {
    assert.ok(b.s0 >= r.from * r.length + 10);
    assert.ok(b.s1 <= r.to * r.length - 10);
    assert.ok(Math.abs(b.s1 - b.s0 - 6.4) < 1e-8);
    assert.ok(Math.abs(b.u) - b.width / 2 >= l.outer - 1e-8);
    assert.ok(Math.abs(b.u) + b.width / 2 <= l.outer + l.parking + 1e-8);
  }
  assert.ok(built.result.mesh.surfaceKinds.includes("parking"));
});
test("two real grade-separated bridge routes clear the underlying roads and do not create at-grade nodes", () => {
  const bridges = built.result.roads.filter(
    (r) => r.e.transportSample && r.e.structure === "bridge",
  );
  assert.equal(bridges.length, 2);
  for (const bridge of bridges) {
    const under = built.result.roads.find(
      (r) =>
        r.e.structure !== "bridge" &&
        ![r.e.a, r.e.b].some((id) => [bridge.e.a, bridge.e.b].includes(id)) &&
        segmentIntersection(bridge.a, bridge.b, r.a, r.b),
    );
    assert.ok(under);
    const hit = segmentIntersection(bridge.a, bridge.b, under.a, under.b);
    assert.ok(roadY(p, bridge.e, hit.t) - 1.2 - roadY(p, under.e, hit.u) > 5.5);
    assert.ok(!p.nodes.some((n) => dist(n, hit) < 0.01));
  }
});
test("two level crossings are near perpendicular with partitioned deck surfaces", () => {
  const crossings = railCrossings(p);
  assert.equal(crossings.length, 2);
  assert.deepEqual(crossingIssues(p), []);
  for (const c of crossings) {
    assert.ok(c.angle > 75);
    assert.ok(Math.min(c.sRoad, c.roadLength - c.sRoad) > 18);
    assert.equal(p.edges.find((e) => e.id === c.roadId).sidewalk, 0);
  }
  assert.ok(built.result.mesh.surfaceKinds.includes("crossingDeck"));
});
test("rail and monorail leave the open street on opposite smooth radius-controlled alignments", () => {
  const rail = p.corridors.find((c) => c.id === "eastbank-rail-exit"),
    mono = p.corridors.find((c) => c.id === "eastbank-monorail-exit"),
    port = p.nodes.find((n) => n.id === rail.sourceNode);
  assert.equal(rail.gauge, 1.435);
  assert.equal(rail.trackSpacing, 3.3);
  assert.deepEqual(rail.points[0], { x: port.x, z: port.z });
  assert.equal(rail.exitSide, -mono.exitSide);
  assert.ok(mono.lead > 0);
  const pts = exitPoints({ x: -10, z: 0 }, { x: 0, z: 0 }, -1, 80, 100);
  for (const v of pts.slice(0, -1))
    assert.ok(Math.abs(Math.hypot(v.x, v.z + 80) - 80) < 1e-8);
  assert.ok(Math.abs(pts.at(-1).x - 80) < 1e-8);
  assert.ok(Math.abs(pts.at(-1).z + 180) < 1e-8);
});
test("attached departures follow moved road endpoints rather than being orphaned", () => {
  const p = newProject(),
    [id] = addRoad(p, { x: 0, z: 0 }, { x: 200, z: 0 }, "tram"),
    c = addRailExit(p, id),
    port = c.sourceNode;
  const result = moveNode(p, port, { x: 220, z: 30 });
  assert.equal(result.ok, true);
  const moved = p.corridors[0];
  assert.notEqual(moved.sourceEdge, id);
  assert.deepEqual(corridorPoints(p, moved)[0], { x: 220, z: 30 });
  assert.deepEqual(moved.points, corridorPoints(p, moved));
  validateProject(p);
});
test("tiny residual road stubs are rejected atomically by node moves", () => {
  const p = newProject();
  addRoad(p, { x: 0, z: 0 }, { x: 100, z: 0 }, "local");
  addRoad(p, { x: 50, z: -50 }, { x: 50, z: 50 }, "local");
  const node = p.nodes.find((n) => n.x === 100),
    before = JSON.stringify(p);
  assert.equal(moveNode(p, node.id, { x: 55, z: 5 }).ok, false);
  assert.equal(JSON.stringify(p), before);
});
test("rail exits reject invalid geometry without changing the graph", () => {
  const p = newProject(),
    [id] = addRoad(p, { x: 0, z: 0 }, { x: 100, z: 0 }, "tram"),
    before = JSON.stringify(p);
  for (const args of [
    { radius: NaN },
    { radius: 4 },
    { side: 0 },
    { run: Infinity },
    { kind: "air" },
  ])
    assert.throws(() => addRailExit(p, id, args));
  assert.equal(JSON.stringify(p), before);
});
test("unsafe urban, shallow-angle and junction-adjacent crossings are reported explicitly", () => {
  const p = newProject();
  addRoad(p, { x: -100, z: 0 }, { x: 100, z: 0 }, "local");
  p.corridors.push({
    id: "test",
    kind: "rail",
    elevation: 0.12,
    points: [
      { x: 0, z: -50 },
      { x: 0, z: 50 },
    ],
  });
  assert.match(crossingIssues(p)[0], /lowered footway/);
  p.edges[0].sidewalk = 0;
  p.corridors[0].points = [
    { x: -90, z: -10 },
    { x: 90, z: 10 },
  ];
  assert.match(crossingIssues(p)[0], /oblique/);
  p.corridors[0].points = [
    { x: 95, z: -50 },
    { x: 95, z: 50 },
  ];
  assert.match(crossingIssues(p)[0], /too close/);
});
test("swept bridge/rail beams have outward faces, finite UVs and no collapsed triangles", () => {
  const geometries = [];
  sweep(
    { batch: (g) => geometries.push(g) },
    [
      { x: 0, y: 2, z: 0 },
      { x: 5, y: 2, z: 0 },
      { x: 10, y: 2, z: 2 },
    ],
    0,
    0.9,
    1.3,
    "concrete",
  );
  for (const g of geometries) {
    const pos = g.attributes.position,
      normal = g.attributes.normal;
    assert.equal(g.attributes.uv.count, pos.count);
    for (let i = 0; i < pos.count; i++)
      assert.ok([pos.getX(i), pos.getY(i), pos.getZ(i)].every(Number.isFinite));
    let top = 0;
    for (let i = 0; i < pos.count; i += 3) {
      const a = [pos.getX(i), pos.getY(i), pos.getZ(i)],
        b = [pos.getX(i + 1), pos.getY(i + 1), pos.getZ(i + 1)],
        c = [pos.getX(i + 2), pos.getY(i + 2), pos.getZ(i + 2)],
        u = b.map((v, k) => v - a[k]),
        v = c.map((v, k) => v - a[k]);
      assert.ok(
        Math.hypot(
          u[1] * v[2] - u[2] * v[1],
          u[2] * v[0] - u[0] * v[2],
          u[0] * v[1] - u[1] * v[0],
        ) > 1e-6,
      );
      if ([a, b, c].every((v) => Math.abs(v[1] - 3.3) < 1e-5)) {
        assert.ok(normal.getY(i) > 0.7);
        top++;
      }
    }
    assert.ok(top > 0);
    g.dispose();
  }
});
test("rendered ground departure shares the street rail endpoint height and gauge", () => {
  const geometries = [],
    c = p.corridors.find((c) => c.id === "eastbank-rail-exit"),
    project = { ...p, corridors: [c] },
    mock = {
      p: project,
      result: built.result,
      batch: (g, mat) => {
        if (mat === "rail") geometries.push(g);
        else g.dispose();
      },
      box() {},
      strip() {},
    };
  buildTransportRail(mock);
  assert.equal(geometries.length, 4);
  const e = p.edges.find((e) => e.id === c.sourceEdge),
    b = p.nodes.find((n) => n.id === c.sourceNode),
    y = roadY(p, e, 1) + 0.003;
  for (const g of geometries) {
    const a = g.attributes.position;
    assert.ok(Math.abs(a.getX(0) - b.x) < 1e-4);
    assert.ok(Math.abs(a.getY(0) - y) < 1e-5);
    g.dispose();
  }
});
test("import rejects non-finite bridge and curve parameters", () => {
  const copy = structuredClone(p);
  copy.edges.find((e) => e.bridgeRise).bridgeRise = NaN;
  assert.throws(() => validateProject(copy), /bridge rise/);
  const other = structuredClone(p);
  other.corridors.find((c) => c.turnRadius).turnRadius = 0;
  assert.throws(() => validateProject(other), /alignment parameters/);
});
test("repair replaces damaged tagged samples, preserves custom roads and terrain, and validates before assignment", () => {
  const damaged = structuredClone(before),
    e = damaged.edges.find((e) => e.name.includes("Campus Cycleway")),
    a = damaged.nodes.find((n) => n.id === e.a),
    b = damaged.nodes.find((n) => n.id === e.b),
    l = dist(a, b),
    pt = { x: a.x + ((b.x - a.x) * 2) / l, z: a.z + ((b.z - a.z) * 2) / l };
  addRoad(damaged, pt, { x: pt.x, z: pt.z - 90 }, "local", {
    district: "eastbank",
    name: "Broken generated edit",
  });
  const [custom] = addRoad(
      damaged,
      { x: -600, z: -400 },
      { x: -500, z: -400 },
      "country",
      { name: "Custom road" },
    ),
    saved = structuredClone(damaged.edges.find((e) => e.id === custom));
  damaged.brushes.push({ x: -550, z: -400, radius: 12, amount: 0.3 });
  assert.ok(auditMesh(generateRoadMesh(damaged), damaged).short > 0);
  const fixed = repairSampleCity(damaged);
  assert.equal(fixed.repaired, true);
  assert.equal(fixed.streetAudit.ok, true);
  assert.deepEqual(
    damaged.edges.find((e) => e.id === custom),
    saved,
  );
  assert.equal(damaged.brushes.at(-1).amount, 0.3);
  assert.ok(!damaged.edges.some((e) => e.name === "Broken generated edit"));
  for (const e of original.edges)
    assert.deepEqual(
      damaged.edges.find((x) => x.id === e.id),
      e,
    );
  const impossible = structuredClone(before);
  impossible.edges.find((e) => e.name.includes("connector")).sampleKey =
    "removed";
  const snapshot = JSON.stringify(impossible);
  assert.throws(() => repairSampleCity(impossible), /connector is missing/);
  assert.equal(JSON.stringify(impossible), snapshot);
});

test("rail attachments survive automatic splitting of their parent street", () => {
  const p = newProject(),
    [id] = addRoad(p, { x: 0, z: 0 }, { x: 200, z: 0 }, "tram"),
    c = addRailExit(p, id),
    points = structuredClone(c.points);
  addRoad(p, { x: 100, z: -100 }, { x: 100, z: 100 }, "country");
  assert.notEqual(c.sourceEdge, id);
  assert.ok(
    p.edges.some(
      (e) => e.id === c.sourceEdge && [e.a, e.b].includes(c.sourceNode),
    ),
  );
  assert.deepEqual(corridorPoints(p, c), points);
  validateProject(p);
});
test("bridge deck slabs cover the whole footway or rural shoulder, not just traffic lanes", async () => {
  const { buildBridge } = await import("../src/transport-world.js"),
    { gutterWidth } = await import("../src/streetscape.js");
  for (const r of built.result.roads.filter(
    (r) => r.e.structure === "bridge",
  )) {
    const parts = [],
      mock = {
        p,
        result: built.result,
        batch: (g, mat) => parts.push({ g, mat }),
        box() {},
        strip() {},
      },
      at = (t, u = 0, h = 0) => [
        r.a.x + r.dx * r.length * t + r.nx * u,
        roadY(p, r.e, t) + h,
        r.a.z + r.dz * r.length * t + r.nz * u,
      ];
    buildBridge(mock, r, at);
    const pos = parts[0].g.attributes.position,
      u = [];
    for (let i = 0; i < pos.count; i++)
      u.push((pos.getX(i) - r.a.x) * r.nx + (pos.getZ(i) - r.a.z) * r.nz);
    const needed =
      r.w / 2 + (r.e.sidewalk > 0 ? r.e.sidewalk + gutterWidth(r.e) : 0.9);
    assert.ok(Math.max(...u) > needed + 0.29);
    assert.ok(Math.min(...u) < -needed - 0.29);
    parts.forEach(({ g }) => g.dispose());
  }
});

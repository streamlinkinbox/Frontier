import test from "node:test";
import assert from "node:assert/strict";
import {
  newProject,
  seedProject,
  addRoad,
  validateProject,
  roadWidth,
  presetById,
} from "../src/model.js";
import { generateRoadMesh, auditMesh, exportOBJ } from "../src/geometry.js";
import { generateStreetscape, auditStreetscape } from "../src/streetscape.js";
import {
  streetLayout,
  surfaceAt,
  plantingStations,
  islandFactor,
} from "../src/landscape-layout.js";
import {
  landscapedProject,
  applyLandscape,
  upgradeStarterRoundabout,
  canInstallLandscapeExamples,
  installLandscapeExamples,
} from "../src/starter.js";
function fixture(mode) {
  const p = newProject();
  for (const [x, z, preset] of [
    [150, 13, mode],
    [-150, -13, mode],
    [0, 120, "local"],
  ])
    addRoad(p, { x: 0, z: 0 }, { x, z }, preset);
  return { p, r: generateRoadMesh(p) };
}
function clean(p, r) {
  assert.deepEqual(auditMesh(r, p).issues, []);
  assert.equal(auditStreetscape(generateStreetscape(p, r)).ok, true);
}
const area = (q) =>
  Math.abs(
    q.reduce(
      (sum, p, i) => sum + p[0] * q[(i + 1) % 4][2] - q[(i + 1) % 4][0] * p[2],
      0,
    ) / 2,
  );
for (const [mode, width] of [
  ["garden", 15.8],
  ["cycle", 12.4],
  ["tram", 19.6],
]) {
  test(`${mode} allocates distinct traffic, cycling and reserved widths`, () => {
    const e = presetById(mode),
      l = streetLayout(e);
    assert.ok(Math.abs(roadWidth(e) - width) < 1e-8);
    assert.equal(l.cycle, 2);
    assert.equal(l.buffer, 1.2);
    assert.ok(
      Math.abs(l.bands.reduce((sum, b) => sum + b.hi - b.lo, 0) - width) < 1e-8,
    );
  });
  test(`${mode} and conventional junction mouths weld without zero-area subdivision slivers`, () => {
    const { p, r } = fixture(mode);
    clean(p, r);
    for (const road of r.roads.filter((r) => r.e.landscape)) {
      let sum = 0;
      r.mesh.faces.forEach((f, i) => {
        if (r.mesh.groups[i] === road.e.id)
          sum += area(f.map((v) => r.mesh.vertices[v]));
      });
      assert.ok(
        Math.abs(sum - road.w * road.length * (road.to - road.from)) < 1e-5,
        "exactly one partitioned surface, no hidden asphalt beneath reservations",
      );
    }
  });
  test(`${mode} keeps planting out of crossings and vehicle lanes`, () => {
    const { r } = fixture(mode);
    for (const road of r.roads.filter((r) => r.landscape)) {
      for (const plant of plantingStations(road.layout, road.landscape)) {
        assert.equal(islandFactor(road.landscape, plant.s), 1);
        assert.ok(
          road.landscape.crossings.every((c) => Math.abs(c - plant.s) >= 6),
        );
        assert.ok(
          ["planting", "trackgrass"].includes(
            surfaceAt(road.layout, road.landscape, plant.s, plant.u).kind,
          ),
        );
      }
      for (const c of road.landscape.crossings)
        for (const b of road.layout.bands.filter((b) =>
          ["median", "buffer", "tram"].includes(b.kind),
        ))
          assert.equal(
            surfaceAt(road.layout, road.landscape, c, (b.lo + b.hi) / 2).height,
            0,
          );
    }
  });
}
test("tram rail bed stays flush through crossings and shrubs stay clear of the tracks", () => {
  const { r } = fixture("tram");
  for (const road of r.roads.filter((r) => r.landscape)) {
    for (const s of road.landscape.stations)
      for (const center of [-1.65, 1.65])
        for (const off of [-0.7175, 0.7175])
          assert.equal(
            surfaceAt(road.layout, road.landscape, s, center + off).height,
            0,
          );
    for (const plant of plantingStations(road.layout, road.landscape))
      assert.ok(Math.abs(plant.u) - plant.size > 2.3675);
  }
});
test("new starter has a connected default ring and all three landscape examples", () => {
  const p = landscapedProject(),
    r = generateRoadMesh(p);
  clean(p, r);
  assert.equal(p.edges.filter((e) => e.roundaboutRole === "ring").length, 12);
  for (const mode of ["garden", "cycle", "tram"])
    assert.ok(p.edges.some((e) => e.landscape === mode));
  const visited = new Set([p.edges.find((e) => e.roundaboutRole === "ring").a]);
  let changed = true;
  while (changed) {
    changed = false;
    for (const e of p.edges)
      if (visited.has(e.a) !== visited.has(e.b)) {
        visited.add(e.a);
        visited.add(e.b);
        changed = true;
      }
  }
  assert.ok(
    visited.has("n2"),
    "roundabout connects to the original county graph",
  );
  assert.deepEqual(validateProject(JSON.parse(JSON.stringify(p))), p);
});
test("landscape presets preserve road identity and name", () => {
  const p = seedProject(),
    e = p.edges.find((e) => e.id === "r15"),
    identity = [e.id, e.a, e.b, e.name];
  applyLandscape(e, "tram");
  assert.deepEqual([e.id, e.a, e.b, e.name], identity);
  applyLandscape(e, "standard");
  assert.equal(e.landscape, "standard");
  assert.equal(e.preset, "local");
});
test("imports reject unsupported landscape combinations and prototype names", () => {
  const { p } = fixture("garden");
  for (const patch of [
    { landscape: "constructor" },
    { landscape: null },
    { lanes: 3 },
    { structure: "bridge" },
    { sidewalk: 0 },
    { landscape: "cycle", bike: false },
  ]) {
    const bad = structuredClone(p);
    Object.assign(bad.edges[0], patch);
    assert.throws(() => validateProject(bad), /landscape|Landscaped/);
  }
});
test("roundabout ring cannot accidentally receive a divided-street section", () => {
  const p = landscapedProject(),
    e = p.edges.find((e) => e.roundaboutRole === "ring"),
    snapshot = JSON.stringify(e);
  assert.throws(() => applyLandscape(e, "tram"), /circulating ring/);
  assert.equal(JSON.stringify(e), snapshot);
});
test("older Westhaven saves gain a checked roundabout without overwriting edited streets", () => {
  const p = seedProject();
  Object.assign(
    p.edges.find((e) => e.id === "r89"),
    { paving: "brick", name: "My retained street" },
  );
  const before = JSON.stringify(p),
    next = upgradeStarterRoundabout(p);
  assert.ok(next);
  assert.equal(JSON.stringify(p), before);
  assert.deepEqual(
    next.edges.find((e) => e.id === "r89"),
    p.edges.find((e) => e.id === "r89"),
  );
  assert.equal(
    next.edges.filter((e) => e.roundaboutRole === "ring").length,
    12,
  );
  assert.equal(upgradeStarterRoundabout(next), null);
});
test("occupied default placement is not forced into an existing save", () => {
  const p = seedProject();
  addRoad(p, { x: -255, z: 100 }, { x: -205, z: 100 }, "local");
  const before = JSON.stringify(p);
  assert.equal(upgradeStarterRoundabout(p), null);
  assert.equal(JSON.stringify(p), before);
});
test("road OBJ retains quad faces, UVs and the partitioned surface identities", () => {
  const { p, r } = fixture("tram"),
    obj = exportOBJ(r, p);
  for (const mat of [
    "asphalt",
    "cycle",
    "planting",
    "trackgrass",
    "refuge",
    "curb",
  ])
    assert.match(obj, new RegExp(`usemtl ${mat}\\n`));
  assert.ok(
    obj
      .split("\n")
      .filter((l) => l.startsWith("f "))
      .every((l) => l.split(" ").length === 5),
  );
  assert.match(obj, /^vt /m);
});

test("installing examples preserves the existing graph and unrelated edits", () => {
  const p = seedProject(),
    nodes = JSON.stringify(p.nodes);
  p.edges.find((e) => e.id === "r89").paving = "brick";
  installLandscapeExamples(p);
  assert.equal(JSON.stringify(p.nodes), nodes);
  assert.equal(p.edges.length, 82);
  assert.equal(p.edges.find((e) => e.id === "r89").paving, "brick");
  clean(p, generateRoadMesh(p));
});
test("example installer refuses remapped Harbor blocks without changing anything", () => {
  const p = seedProject();
  p.nodes.find((n) => n.id === "n14").x += 4;
  const before = JSON.stringify(p);
  assert.equal(canInstallLandscapeExamples(p), false);
  assert.throws(() => installLandscapeExamples(p), /moved/);
  assert.equal(JSON.stringify(p), before);
});
test("formed drainage and landscaped cross-sections coexist without broken mouth seams", () => {
  const { p } = fixture("tram");
  Object.assign(p.edges[0], { drainage: "weep-channel", curbStyle: "spoon" });
  clean(p, generateRoadMesh(p));
});

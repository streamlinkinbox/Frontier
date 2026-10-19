import test from "node:test";
import assert from "node:assert/strict";
import * as THREE from "three";
import { newProject, addRoad, validateProject } from "../src/model.js";
import { generateRoadMesh, roadY } from "../src/geometry.js";
import {
  STREET_DEFAULTS,
  STREET_PRESETS,
  generateStreetscape,
  auditStreetscape,
  exportStreetscapeOBJ,
  rampHeight,
  gutterWidth,
  tactilePrototype,
  quadArea,
} from "../src/streetscape.js";
function fixture(config = {}) {
  const p = newProject();
  for (const end of [
    { x: 40, z: 0 },
    { x: -40, z: 0 },
    { x: 0, z: 40 },
  ])
    addRoad(p, { x: 0, z: 0 }, end, "local", {
      ...STREET_DEFAULTS,
      sidewalk: 2.4,
      ...config,
    });
  return { p, result: generateRoadMesh(p) };
}
for (const preset of STREET_PRESETS)
  test(`${preset.name}: non-degenerate quad street surfaces`, () => {
    const config = Object.fromEntries(
      Object.keys(STREET_DEFAULTS).map((k) => [
        k,
        preset[k] ?? STREET_DEFAULTS[k],
      ]),
    );
    const { p, result } = fixture(config),
      street = generateStreetscape(p, result),
      audit = auditStreetscape(street);
    assert.equal(audit.degenerate, 0);
    assert.equal(audit.duplicates, 0);
    assert.equal(audit.steepRamps, 0);
    assert.ok(audit.ramps > 0);
    assert.ok(
      street.mesh.faces.every((f) => f.length === 4 && new Set(f).size === 4),
    );
  });
test("dropped curb replaces the raised profile and meets the roadway with a 6 mm lip", () => {
  const { p, result } = fixture({ drainage: "none" }),
    s = generateStreetscape(p, result),
    ramp = s.ramps[0],
    e = p.edges.find((e) => e.id === ramp.road),
    g = gutterWidth(e);
  assert.ok(
    Math.abs(rampHeight(e, ramp.s, g, [ramp.s], e.curbHeight) - 0.006) < 1e-10,
  );
  assert.equal(
    rampHeight(e, ramp.s, g + e.sidewalk, [ramp.s], e.curbHeight),
    e.curbHeight,
  );
  assert.equal(
    rampHeight(e, ramp.s + 3, g, [ramp.s], e.curbHeight),
    e.curbHeight,
  );
  assert.ok(rampHeight(e, ramp.s + 1.7, g, [ramp.s], e.curbHeight) > 0.006);
});
test("narrow sidewalks report excessive ramp slope instead of claiming compliance", () => {
  const { p, result } = fixture({ sidewalk: 1.8 });
  assert.ok(auditStreetscape(generateStreetscape(p, result)).steepRamps > 0);
});
test("disabling ramps removes tactile domes but leaves a continuous curb", () => {
  const { p, result } = fixture({ rampStyle: "none" }),
    s = generateStreetscape(p, result);
  assert.equal(s.ramps.length, 0);
  assert.equal(Object.values(s.instances).flat().length, 0);
  assert.equal(auditStreetscape(s).degenerate, 0);
});
test("perforated channel is real open geometry, not a painted solid slab", () => {
  const { p, result } = fixture({ drainage: "channel", tactile: "none" }),
    s = generateStreetscape(p, result),
    drain = s.drains.find((d) => d.side === 1),
    r = result.roads.find((r) => r.e.id === drain.road);
  const mesh = new THREE.Mesh(
    s.mesh.geometry("drainmetal"),
    new THREE.MeshBasicMaterial({ side: THREE.DoubleSide }),
  );
  mesh.updateMatrixWorld();
  const hit = (along, u) => {
    const t = along / r.length,
      x = r.a.x + r.dx * along + r.nx * (r.w / 2 + u),
      z = r.a.z + r.dz * along + r.nz * (r.w / 2 + u);
    return new THREE.Raycaster(
      new THREE.Vector3(x, roadY(p, r.e, t) + 1, z),
      new THREE.Vector3(0, -1, 0),
      0,
      2,
    ).intersectObject(mesh);
  };
  assert.equal(
    hit(drain.start + 0.07, 0.08).length,
    0,
    "slot must remain open",
  );
  assert.ok(
    hit(drain.start + 0.02, 0.08).length > 0,
    "frame should be a real surface",
  );
  mesh.geometry.dispose();
  mesh.material.dispose();
});
test("curb inlet leaves the front throat open and includes a recessed back wall", () => {
  const { p, result } = fixture({ drainage: "inlet", tactile: "none" }),
    s = generateStreetscape(p, result),
    drain = s.drains.find((d) => d.side === 1),
    r = result.roads.find((r) => r.e.id === drain.road),
    along = (drain.start + drain.end) / 2,
    g = gutterWidth(r.e);
  const origin = new THREE.Vector3(
      r.a.x + r.dx * along + r.nx * (r.w / 2 + g - 0.1),
      roadY(p, r.e, along / r.length) + 0.05,
      r.a.z + r.dz * along + r.nz * (r.w / 2 + g - 0.1),
    ),
    ray = new THREE.Raycaster(origin, new THREE.Vector3(r.nx, 0, r.nz), 0, 0.5);
  const walls = new THREE.Mesh(
    s.mesh.geometry("drainvoid"),
    new THREE.MeshBasicMaterial({ side: THREE.DoubleSide }),
  );
  walls.updateMatrixWorld();
  const hits = ray.intersectObject(walls);
  assert.ok(hits.some((hit) => hit.distance > 0.3 && hit.distance < 0.5));
  walls.geometry.dispose();
  walls.material.dispose();
});
test("streetscape OBJ includes all-quad tactile dome instances with valid indices", () => {
  const { p, result } = fixture({ drainage: "none" }),
    s = generateStreetscape(p, {
      ...result,
      roads: result.roads.slice(0, 1),
      junctions: result.junctions,
    }),
    obj = exportStreetscapeOBJ(s),
    lines = obj.split("\n"),
    vertices = lines.filter((l) => l.startsWith("v ")).length,
    faces = lines.filter((l) => l.startsWith("f "));
  assert.ok(faces.length > s.mesh.faces.length);
  for (const f of faces) {
    const ids = f
      .split(" ")
      .slice(1)
      .map((token) => Number(token.split("/")[0]));
    assert.equal(ids.length, 4);
    assert.ok(ids.every((i) => i >= 1 && i <= vertices));
  }
  const proto = tactilePrototype();
  assert.ok(
    proto.faces.every(
      (f) =>
        new Set(f).size === 4 && quadArea(f.map((i) => proto.vertices[i])) > 0,
    ),
  );
});
test("project import preserves new fields and rejects invalid curb dimensions / enums", () => {
  const { p } = fixture({ curbStyle: "rolled", drainage: "none" });
  assert.equal(
    validateProject(JSON.parse(JSON.stringify(p))).edges[0].curbStyle,
    "rolled",
  );
  p.edges[0].curbHeight = -1;
  assert.throws(() => validateProject(p));
  p.edges[0].curbHeight = 0.15;
  p.edges[0].drainage = "bad-value";
  assert.throws(() => validateProject(p));
});

import test from "node:test";
import assert from "node:assert/strict";
import { landscapedProject } from "../src/starter.js";
import { PRESETS, validateProject } from "../src/model.js";
import { STREET_PRESETS } from "../src/streetscape.js";
import { auditMesh } from "../src/geometry.js";
import { addSampleCity, mapBounds, CITY_ID } from "../src/sample-city.js";
import { tramJunctionLinks } from "../src/tram-network.js";
const p = landscapedProject();
p.edges.find((e) => e.id === "r89").name = "Keep my edits";
const before = structuredClone(p),
  built = addSampleCity(p),
  city = p.edges.filter((e) => e.district === CITY_ID);
test("second city is appended to the same project without replacing previous edits", () => {
  assert.equal(built.added, true);
  assert.equal(p.edges.length, 136);
  for (const e of before.edges)
    assert.deepEqual(
      p.edges.find((n) => n.id === e.id),
      e,
    );
  for (const n of before.nodes)
    assert.deepEqual(
      p.nodes.find((x) => x.id === n.id),
      n,
    );
  assert.equal(p.name, before.name);
  assert.equal(p.sampleCityVersion, 1);
});
test("all twelve road presets and nine curb samples are physically instantiated", () => {
  for (const preset of PRESETS)
    assert.ok(
      city.some((e) => e.preset === preset.id),
      preset.id,
    );
  for (const profile of STREET_PRESETS) {
    const e = city.find((e) => e.curbSample === profile.id);
    assert.ok(e, profile.id);
    for (const key of ["curbStyle", "rampStyle", "paving", "drainage"])
      assert.equal(e[key], profile[key], `${profile.id}/${key}`);
  }
  assert.equal(city.filter((e) => e.landscape === "tram").length, 3);
  assert.equal(city.filter((e) => e.tramVehicle).length, 1);
  assert.equal(
    p.corridors.filter((c) => c.id.startsWith("eastbank-")).length,
    2,
  );
});
test("whole two-city road and paving audits remain clean", () => {
  assert.deepEqual(auditMesh(built.result, p).issues, []);
  assert.equal(built.streetAudit.ok, true);
  assert.equal(built.streetAudit.missingCorners, 0);
  assert.equal(built.streetAudit.seamErrors, 0);
  assert.equal(built.streetAudit.overlapPairs, 0);
});
test("cities are graph-connected rather than separate floating displays", () => {
  const visited = new Set([before.nodes[0].id]);
  let change = true;
  while (change) {
    change = false;
    for (const e of p.edges)
      if (visited.has(e.a) !== visited.has(e.b)) {
        visited.add(e.a);
        visited.add(e.b);
        change = true;
      }
  }
  assert.ok(city.every((e) => visited.has(e.a) && visited.has(e.b)));
});
test("four physical rails continue through both straight-through tram junctions", () => {
  const links = tramJunctionLinks(built.result);
  assert.equal(links.length, 2);
  for (const link of links) {
    assert.equal(link.paths.length, 4);
    for (const path of link.paths) {
      assert.ok(path.length > 20);
      assert.ok(path.flat().every(Number.isFinite));
      const j = built.result.junctions.find((j) => j.id === link.node);
      for (const endpoint of [path[0], path.at(-1)]) {
        assert.ok(
          j.arms
            .filter((a) => a.e.landscape === "tram")
            .some(
              (a) =>
                Math.abs(
                  (endpoint[0] - (a.left[0] + a.right[0]) / 2) * a.dx +
                    (endpoint[2] - (a.left[2] + a.right[2]) / 2) * a.dz,
                ) < 1e-6,
            ),
        );
      }
    }
  }
});
test("non-collinear tram approaches do not acquire fabricated switches", () => {
  const result = {
    ...built.result,
    junctions: built.result.junctions.map((j) => {
      let changed = false;
      return {
        ...j,
        arms: j.arms.map((a) => {
          if (a.e.landscape !== "tram" || changed) return a;
          changed = true;
          const angle = Math.atan2(a.dz, a.dx) + 0.2;
          return { ...a, dx: Math.cos(angle), dz: Math.sin(angle) };
        }),
      };
    }),
  };
  assert.equal(tramJunctionLinks(result).length, 0);
});
test("reload/import is valid and installation is idempotent", () => {
  const saved = JSON.stringify(p);
  assert.deepEqual(validateProject(JSON.parse(saved)), p);
  assert.deepEqual(addSampleCity(p), { added: false });
  assert.equal(JSON.stringify(p), saved);
});
test("adaptive bounds contain both cities, their sidewalks and railway samples", () => {
  const b = mapBounds(p);
  assert.ok(b.width > 1400);
  for (const n of [...p.nodes, ...p.corridors.flatMap((c) => c.points)])
    assert.ok(n.x > b.minX && n.x < b.maxX && n.z > b.minZ && n.z < b.maxZ);
});
test("invalid city metadata is rejected during import", () => {
  for (const patch of [
    { sampleCityVersion: 2 },
    { cityOrigin: { x: NaN, z: 0 } },
    { cityOrigin: null },
  ])
    assert.throws(() => validateProject({ ...p, ...patch }), /sample-city/);
});

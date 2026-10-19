import test from "node:test";
import assert from "node:assert/strict";
import * as THREE from "three";
import {
  newProject,
  addRoad,
  addRoundabout,
  validateProject,
} from "../src/model.js";
import { generateRoadMesh, auditMesh, exportOBJ } from "../src/geometry.js";
import {
  generateStreetscape,
  auditStreetscape,
  STREET_PRESETS,
  STREET_DEFAULTS,
  exportStreetscapeOBJ,
} from "../src/streetscape.js";
const presets = STREET_PRESETS.filter((p) =>
  ["brick-channel", "weep-channel", "slot-channel"].includes(p.id),
);
const settings = (p) =>
  Object.fromEntries(
    Object.keys(STREET_DEFAULTS).map((k) => [k, p[k] ?? STREET_DEFAULTS[k]]),
  );
for (const radius of [24, 32, 50])
  for (const entries of [3, 4])
    test(`${radius} m / ${entries}-entry roundabout is one connected quad network with an open island`, () => {
      const p = newProject(),
        f = addRoundabout(
          p,
          { x: 0, z: 0 },
          { radius, entries, rotation: 23, clockwise: entries === 4 },
        );
      assert.equal(f.ports.length, entries);
      assert.equal(p.edges.length, 12 + entries);
      assert.equal(
        p.edges.filter((e) => e.roundaboutRole === "ring").length,
        12,
      );
      assert.equal(
        p.nodes.filter(
          (n) =>
            p.edges.filter((e) => e.a === n.id || e.b === n.id).length === 3,
        ).length,
        entries,
      );
      const r = generateRoadMesh(p),
        s = generateStreetscape(p, r);
      assert.deepEqual(auditMesh(r, p).issues, []);
      assert.equal(auditStreetscape(s).ok, true);
      const object = new THREE.Mesh(
        r.mesh.geometry(),
        new THREE.MeshBasicMaterial({ side: THREE.DoubleSide }),
      );
      object.updateMatrixWorld();
      assert.equal(
        new THREE.Raycaster(
          new THREE.Vector3(0, 50, 0),
          new THREE.Vector3(0, -1, 0),
        ).intersectObject(object).length,
        0,
      );
      object.geometry.dispose();
      object.material.dispose();
      assert.equal(
        exportOBJ(r, p)
          .split("\n")
          .filter((l) => l.startsWith("f ")).length,
        r.mesh.faces.length,
      );
      assert.equal(
        validateProject(JSON.parse(JSON.stringify(p))).edges[0].circulation,
        entries === 4 ? "clockwise" : "counterclockwise",
      );
    });
test("roundabout protection and invalid parameters leave the existing graph untouched", () => {
  const p = newProject();
  addRoad(p, { x: -50, z: 0 }, { x: 50, z: 0 });
  const snapshot = JSON.stringify(p);
  assert.throws(() => addRoundabout(p, { x: 0, z: 0 }), /clear space/);
  assert.equal(JSON.stringify(p), snapshot);
  assert.throws(() => addRoundabout(p, { x: 200, z: 0 }, { radius: 4 }));
  assert.equal(JSON.stringify(p), snapshot);
  const q = newProject();
  addRoad(q, { x: 60, z: 0 }, { x: 100, z: 0 });
  const before = JSON.stringify(q);
  assert.throws(() => addRoundabout(q, { x: 0, z: 0 }), /entry overlaps/);
  assert.equal(JSON.stringify(q), before);
  const valid = newProject();
  addRoundabout(valid, { x: 0, z: 0 });
  const bad = structuredClone(valid);
  delete bad.edges[0].roundabout;
  assert.throws(() => validateProject(bad), /roundabout metadata/);
  bad.edges[0].roundabout = "test";
  bad.edges[0].roundaboutRadius = Infinity;
  assert.throws(() => validateProject(bad), /roundabout metadata/);
});
test("a roundabout entry accepts a snapped continuation without duplicate roads", () => {
  const p = newProject(),
    f = addRoundabout(p, { x: 0, z: 0 }),
    port = f.ports[0];
  addRoad(p, port, { x: port.x + 40, z: port.z }, "access");
  const r = generateRoadMesh(p);
  assert.deepEqual(auditMesh(r, p).issues, []);
  assert.equal(auditStreetscape(generateStreetscape(p, r)).ok, true);
  const node = p.nodes.find(
    (n) => Math.hypot(n.x - port.x, n.z - port.z) < 1e-8,
  );
  assert.equal(
    p.edges.filter((e) => e.a === node.id || e.b === node.id).length,
    2,
  );
});
for (const preset of presets)
  test(`${preset.name}: real openings, depressed channel, clean quads and UV-bearing export`, () => {
    const p = newProject();
    addRoad(p, { x: 0, z: 0 }, { x: 75, z: 25 }, "local", settings(preset));
    const r = generateRoadMesh(p),
      s = generateStreetscape(p, r),
      audit = auditStreetscape(s);
    assert.equal(audit.ok, true);
    assert.equal(s.drains.length, 2);
    const d = s.drains[0],
      expected = { "brick-channel": 6, "weep-channel": 4, "slot-channel": 1 }[
        preset.id
      ];
    assert.equal(d.openings.length, expected);
    const objects = [...new Set(s.mesh.groups)]
      .filter((g) => g !== "drainvoid")
      .map((g) => {
        const o = new THREE.Mesh(
          s.mesh.geometry(g),
          new THREE.MeshBasicMaterial({ side: THREE.DoubleSide }),
        );
        o.updateMatrixWorld();
        return o;
      });
    const voids = new THREE.Mesh(
      s.mesh.geometry("drainvoid"),
      new THREE.MeshBasicMaterial({ side: THREE.DoubleSide }),
    );
    voids.updateMatrixWorld();
    for (const h of d.openings) {
      assert.ok(h.bed < -0.025);
      const [x, y, z] = h.position,
        ray = new THREE.Raycaster(
          new THREE.Vector3(x, y + 1, z),
          new THREE.Vector3(0, -1, 0),
          0,
          2,
        );
      assert.equal(
        ray.intersectObjects(objects).length,
        0,
        "a surface must not seal the hole",
      );
      const hit = ray.intersectObject(voids);
      assert.ok(
        hit.length && hit[0].point.y < y - 0.015,
        "hole opens into a lower recessed collector",
      );
    }
    for (const object of [...objects, voids]) {
      object.geometry.dispose();
      object.material.dispose();
    }
    const obj = exportStreetscapeOBJ(s);
    assert.match(obj, /^vt /m);
    assert.ok(
      obj
        .split("\n")
        .filter((l) => l.startsWith("f "))
        .every((l) => l.split(" ").length === 5),
    );
    const restored = validateProject(JSON.parse(JSON.stringify(p)));
    assert.equal(restored.edges[0].drainage, preset.drainage);
  });
test("three different channel profiles blend at a shared junction without folded or overlapping pavement", () => {
  const p = newProject();
  presets.forEach((q, i) => {
    const a = (i * Math.PI * 2) / 3;
    addRoad(
      p,
      { x: 0, z: 0 },
      { x: 80 * Math.cos(a), z: 80 * Math.sin(a) },
      "local",
      settings(q),
    );
  });
  const r = generateRoadMesh(p);
  assert.deepEqual(auditMesh(r, p).issues, []);
  assert.equal(auditStreetscape(generateStreetscape(p, r)).ok, true);
});
test("short approaches keep their formed bed but do not invent a drain collector", () => {
  const p = newProject();
  addRoad(p, { x: 0, z: 0 }, { x: 10, z: 0 }, "service");
  const s = generateStreetscape(p, generateRoadMesh(p));
  assert.equal(s.drains.length, 0);
  assert.ok(s.mesh.groups.includes("broomconcrete"));
  assert.equal(auditStreetscape(s).ok, true);
});

import test from "node:test";
import assert from "node:assert/strict";
import {
  newProject,
  addRoad,
  seedProject,
  upgradePedestrianSpace,
} from "../src/model.js";
import { generateRoadMesh, auditMesh } from "../src/geometry.js";
import {
  generateStreetscape,
  auditStreetscape,
  exportStreetscapeOBJ,
} from "../src/streetscape.js";
import { auditPavementPanels } from "../src/pavement-audit.js";
const assertSafe = (street) => {
  const report = auditStreetscape(street);
  assert.deepEqual(
    {
      folded: report.foldedFaces,
      inverted: report.invertedFaces,
      overlaps: report.overlapPairs,
      clearance: report.clearanceFailures,
      degenerate: report.degenerate,
    },
    { folded: 0, inverted: 0, overlaps: 0, clearance: 0, degenerate: 0 },
    JSON.stringify(report.issues),
  );
  assert.equal(report.ok, true);
};
test("all reported Westhaven street corners pass actual surface and overlap checks", () => {
  const p = seedProject(),
    r = generateRoadMesh(p),
    s = generateStreetscape(p, r);
  assertSafe(s);
  assert.deepEqual(auditMesh(r, p).issues, []);
  for (const name of [
    "Southbank Lane",
    "Willow Lane",
    "Oak Street",
    "Parkside Drive",
    "Foundry Street",
  ])
    assert.ok(p.edges.some((e) => e.name === name));
  assert.ok(
    p.edges.filter((e) => e.preset === "local").every((e) => e.sidewalk >= 2.4),
  );
});
for (const angle of [8, 15, 30, 48, 75, 90, 120, 165, 179, 181, 210, 270, 345])
  test(`curved pavement remains ordered through a ${angle}° mixed-width corner`, () => {
    const p = newProject(),
      a = (angle * Math.PI) / 180;
    addRoad(p, { x: 0, z: 0 }, { x: 200, z: 0 }, "local", {
      sidewalk: 1.8,
      drainage: "none",
    });
    addRoad(
      p,
      { x: 0, z: 0 },
      { x: 200 * Math.cos(a), z: 200 * Math.sin(a) },
      "local",
      { sidewalk: 4.2, drainage: "channel", curbStyle: "rolled" },
    );
    const r = generateRoadMesh(p);
    assertSafe(generateStreetscape(p, r));
  });
test("mixed curb styles and widths meet at exact road-mouth coordinates", () => {
  const p = newProject();
  for (const [angle, width, curbStyle, drainage] of [
    [0, 6, "rolled", "none"],
    [120, 1.2, "granite", "channel"],
    [240, 3.4, "concrete", "inlet"],
  ]) {
    const a = (angle * Math.PI) / 180;
    addRoad(
      p,
      { x: 0, z: 0 },
      { x: 100 * Math.cos(a), z: 100 * Math.sin(a) },
      "local",
      { sidewalk: width, curbStyle, drainage },
    );
  }
  const r = generateRoadMesh(p),
    street = generateStreetscape(p, r);
  assertSafe(street);
  for (const j of r.junctions)
    for (let i = 0; i < j.arms.length; i++) {
      const a = j.arms[i],
        b = j.arms[(i + 1) % j.arms.length],
        c = j.corners[i];
      assert.deepEqual(c.points[0], a.left);
      assert.deepEqual(c.points.at(-1), b.right);
      assert.deepEqual(c.normals[0], [-a.dz, a.dx]);
      assert.deepEqual(c.normals.at(-1), [b.dz, -b.dx]);
    }
});
test("overlap checker distinguishes a real overlap from a shared seam", () => {
  const q = [
      [0, 0, 0],
      [0, 0, 2],
      [2, 0, 2],
      [2, 0, 0],
    ],
    panel = (dx) => ({
      owner: `panel${dx}`,
      points: q.map((p) => [p[0] + dx, p[1], p[2]]),
    });
  assert.equal(
    auditPavementPanels({ panels: [panel(0), panel(1)] }).overlapPairs,
    1,
  );
  assert.equal(
    auditPavementPanels({ panels: [panel(0), panel(2)] }).overlapPairs,
    0,
  );
  const bad = {
    points: [
      [0, 0, 0],
      [2, 0, 2],
      [0, 0, 2],
      [2, 0, 0],
    ],
    owner: "bowtie",
  };
  assert.equal(auditPavementPanels({ panels: [bad] }).foldedPanels, 1);
  assert.equal(
    auditPavementPanels({ panels: [{ ...panel(0), points: [...q].reverse() }] })
      .invertedPanels,
    1,
  );
});
test("old collapsing inward-offset construction is caught by the new audit", () => {
  const rows = Array.from({ length: 8 }, (_, i) => {
    const a = (i * Math.PI) / 14;
    return [
      [0.8 * Math.cos(a), 0, 0.8 * Math.sin(a)],
      [(0.8 - 1.8) * Math.cos(a), 0, (0.8 - 1.8) * Math.sin(a)],
    ];
  });
  const panels = rows
    .slice(1)
    .map((r, i) => ({
      owner: "legacy-fold",
      points: [rows[i][0], r[0], r[1], rows[i][1]],
    }));
  assert.equal(auditPavementPanels({ panels }).ok, false);
});
test("an impossible short acute junction is flagged and cannot be exported as clean quads", () => {
  const p = newProject();
  for (const angle of [0, 8]) {
    const a = (angle * Math.PI) / 180;
    addRoad(
      p,
      { x: 0, z: 0 },
      { x: 20 * Math.cos(a), z: 20 * Math.sin(a) },
      "local",
      { sidewalk: 6 },
    );
  }
  const s = generateStreetscape(p, generateRoadMesh(p)),
    report = auditStreetscape(s);
  assert.equal(report.ok, false);
  assert.ok(report.clearanceFailures > 0);
  assert.throws(() => exportStreetscapeOBJ(s), /export blocked/);
});
test("legacy width upgrade is idempotent and preserves custom widths", () => {
  const p = seedProject();
  delete p.pavementLayoutVersion;
  const a = p.edges.find((e) => e.preset === "local"),
    b = p.edges.filter((e) => e.preset === "local")[1];
  a.sidewalk = 1.8;
  b.sidewalk = 3.1;
  assert.equal(upgradePedestrianSpace(p), true);
  assert.equal(a.sidewalk, 2.4);
  assert.equal(b.sidewalk, 3.1);
  assert.equal(upgradePedestrianSpace(p), false);
});

test("zero-net-area bowtie top faces are still detected as folded", () => {
  const street = {
    mesh: {
      vertices: [
        [0, 0, 0],
        [2, 0, 2],
        [0, 0, 2],
        [2, 0, 0],
      ],
      faces: [[0, 1, 2, 3]],
      groups: ["paving"],
    },
    ramps: [],
    drains: [],
  };
  assert.equal(auditStreetscape(street).foldedFaces, 1);
  assert.equal(auditStreetscape(street).ok, false);
});

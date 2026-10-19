import test from "node:test";
import assert from "node:assert/strict";
import { newProject, addRoad, seedProject } from "../src/model.js";
import { generateRoadMesh, auditMesh } from "../src/geometry.js";
import {
  generateStreetscape,
  auditStreetscape,
  exportStreetscapeOBJ,
} from "../src/streetscape.js";
function fixture(arms) {
  const p = newProject();
  for (const [angle, preset, options] of arms) {
    const a = (angle * Math.PI) / 180;
    addRoad(
      p,
      { x: 0, z: 0 },
      { x: 100 * Math.cos(a), z: 100 * Math.sin(a) },
      preset,
      options,
    );
  }
  const r = generateRoadMesh(p);
  return { p, r, s: generateStreetscape(p, r) };
}
function check({ p, r, s }) {
  assert.deepEqual(auditMesh(r, p).issues, []);
  const a = auditStreetscape(s);
  assert.equal(a.ok, true, JSON.stringify(a));
  assert.equal(a.missingCorners, 0);
  assert.equal(a.seamErrors, 0);
  assert.ok(s.completedCorners.every((c) => c.faceEnd > c.faceStart));
  for (const c of s.completedCorners) {
    const materials = new Set(
      s.mesh.groups
        .slice(c.faceStart, c.faceEnd)
        .filter((m) => ["paving", "brick"].includes(m)),
    );
    assert.ok(materials.size <= 1, "one paving material per complete corner");
  }
}
test("every starter corner has a generated return and both mouths are welded", () => {
  const p = seedProject(),
    r = generateRoadMesh(p),
    s = generateStreetscape(p, r);
  check({ p, r, s });
  assert.equal(s.expectedCorners.length, 156);
  assert.equal(s.seams.length, 312);
  assert.equal(
    s.completedCorners.filter((c) => c.mode === "graded-termination").length,
    8,
  );
  assert.equal(auditStreetscape(s).steepTransitions, 0);
});
for (const [name, arms] of [
  [
    "rural elbow",
    [
      [0, "country"],
      [110, "country"],
    ],
  ],
  [
    "rural four-way",
    [
      [0, "country"],
      [90, "country"],
      [180, "country"],
      [270, "country"],
    ],
  ],
  [
    "urban/rural elbow",
    [
      [0, "local"],
      [100, "country"],
    ],
  ],
  [
    "mixed Y junction",
    [
      [0, "local", { paving: "brick" }],
      [120, "country"],
      [240, "local", { paving: "slab" }],
    ],
  ],
  [
    "channel to country",
    [
      [0, "local", { curbStyle: "spoon", drainage: "weep-channel" }],
      [100, "country"],
    ],
  ],
])
  test(`${name} has continuous, non-overlapping corner ribbons`, () =>
    check(fixture(arms)));
test("paving changes align with an existing crossing ramp or a hard mouth joint", () => {
  const ramp = fixture([
    [0, "local", { paving: "brick" }],
    [120, "local"],
    [240, "local"],
  ]);
  check(ramp);
  assert.ok(ramp.s.transitions.some((t) => t.mode === "crossing-ramp"));
  const cut = fixture([
    [0, "local", { paving: "brick" }],
    [100, "local"],
  ]);
  check(cut);
  assert.ok(cut.s.transitions.some((t) => t.mode === "hard-joint"));
});
test("corner material ownership does not depend on edge iteration order", () => {
  const f = fixture([
    [0, "local", { paving: "brick" }],
    [120, "local"],
    [240, "country"],
  ]);
  const owners = f.s.completedCorners.map((c) => [c.id, c.owner, c.paving]);
  f.p.edges.reverse();
  const reversed = generateStreetscape(f.p, generateRoadMesh(f.p));
  assert.deepEqual(
    reversed.completedCorners.map((c) => [c.id, c.owner, c.paving]),
    owners,
  );
});
test("missing returns and mismatched seams are rejected, not hidden by a zero-area-only audit", () => {
  const { s } = fixture([
    [0, "local"],
    [90, "country"],
  ]);
  assert.equal(
    auditStreetscape({ ...s, completedCorners: s.completedCorners.slice(1) })
      .missingCorners,
    1,
  );
  const bad = {
    ...s,
    seams: s.seams.map((x, i) => (i ? x : { ...x, roadVertices: [] })),
  };
  assert.equal(auditStreetscape(bad).seamErrors, 1);
  assert.throws(() => exportStreetscapeOBJ(bad), /blocked/);
});
test("a selected-road export checks included seams while retaining its external boundaries", () => {
  const { p, r } = fixture([
    [0, "country"],
    [90, "local"],
    [180, "country"],
  ]);
  const s = generateStreetscape(p, { ...r, roads: r.roads.slice(0, 1) });
  assert.equal(auditStreetscape(s).ok, true);
  assert.ok(s.seams.some((s) => s.external));
  assert.match(exportStreetscapeOBJ(s), /^g shoulder$/m);
});

test("a carried corner material retains its tile phase at the road mouth", () => {
  const { s } = fixture([
    [0, "local", { paving: "brick" }],
    [120, "local"],
    [240, "local"],
  ]);
  const values = new Map();
  for (let i = 0; i < s.mesh.faces.length; i++) {
    const mat = s.mesh.groups[i];
    if (!["paving", "brick"].includes(mat)) continue;
    const period = mat === "brick" ? 1.92 : 1.2;
    s.mesh.faces[i].forEach((v, k) => {
      const key = `${v}:${mat}`,
        u = s.mesh.uvs[i][k][0] / period;
      if (!values.has(key)) values.set(key, []);
      values.get(key).push(u);
    });
  }
  for (const seam of s.seams)
    for (const v of seam.vertices)
      for (const mat of ["paving", "brick"]) {
        const a = values.get(`${v}:${mat}`) ?? [];
        for (const u of a)
          assert.ok(
            Math.abs(u - a[0] - Math.round(u - a[0])) < 1e-5,
            `tile phase at ${seam.mouth}`,
          );
      }
});
test("detailed gutter subdivisions also weld to both paved and shoulder mouths", () => {
  const { s } = fixture([
    [0, "local", { drainage: "weep-channel", curbStyle: "spoon" }],
    [120, "country"],
    [240, "local", { drainage: "brick-channel", curbStyle: "brick-roll" }],
  ]);
  const counts = new Map();
  for (const face of s.mesh.faces)
    for (let i = 0; i < 4; i++) {
      const a = face[i],
        b = face[(i + 1) % 4],
        key = [a, b].sort((a, b) => a - b).join(":");
      counts.set(key, (counts.get(key) ?? 0) + 1);
    }
  const open = [...counts]
    .filter(([, count]) => count === 1)
    .map(([key]) => key.split(":").map((i) => s.mesh.vertices[i]));
  for (const seam of s.seams) {
    const [a, b] = seam.vertices.slice(0, 2).map((v) => s.mesh.vertices[v]),
      dx = b[0] - a[0],
      dz = b[2] - a[2],
      len = dx * dx + dz * dz;
    const inBand = (p) => {
      const x = p[0] - a[0],
        z = p[2] - a[2],
        t = (x * dx + z * dz) / len;
      return t >= -1e-6 && t <= 1 + 1e-6 && Math.abs(x * dz - z * dx) < 1e-7;
    };
    assert.equal(
      open.filter((edge) => edge.every(inBand)).length,
      0,
      `unwelded gutter edge at ${seam.mouth}`,
    );
  }
});

test("taller curb terminations budget their full rise to the flush shoulder", () => {
  const f = fixture([
    [0, "local", { curbHeight: 0.24 }],
    [100, "country"],
  ]);
  check(f);
  assert.equal(auditStreetscape(f.s).steepTransitions, 0);
  assert.ok(f.s.completedCorners.every((c) => c.grade <= 1 / 12 + 1e-6));
});

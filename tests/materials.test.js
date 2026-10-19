import test from "node:test";
import assert from "node:assert/strict";
import { newProject, addRoad, seedProject } from "../src/model.js";
import { generateRoadMesh, exportOBJ } from "../src/geometry.js";
import {
  generateStreetscape,
  exportStreetscapeOBJ,
  auditStreetscape,
} from "../src/streetscape.js";
import { ribbonUVRows, normalizedFaceUV, tileSize } from "../src/surface-uv.js";
const area = (uv) =>
  uv.reduce((a, p, i) => {
    const q = uv[(i + 1) % uv.length];
    return a + p[0] * q[1] - q[0] * p[1];
  }, 0) / 2;
function fixture(options = {}) {
  const p = newProject();
  for (const angle of [25, 115, 225]) {
    const a = (angle * Math.PI) / 180;
    addRoad(
      p,
      { x: 0, z: 0 },
      { x: 70 * Math.cos(a), z: 70 * Math.sin(a) },
      "local",
      { ...options },
    );
  }
  const road = generateRoadMesh(p);
  return { p, road, street: generateStreetscape(p, road) };
}
test("UVs are intrinsic to the ribbon, independent of world rotation/translation", () => {
  const rows = Array.from({ length: 9 }, (_, i) =>
    Array.from({ length: 10 }, (_, j) => [
      i * 0.73,
      j < 3 ? j * 0.04 : 0.15,
      j * 0.25,
    ]),
  );
  const moved = rows.map((row) =>
    row.map(([x, y, z]) => [
      80 + x * Math.cos(0.71) - z * Math.sin(0.71),
      y + 12,
      -31 + x * Math.sin(0.71) + z * Math.cos(0.71),
    ]),
  );
  const a = ribbonUVRows(rows),
    b = ribbonUVRows(moved);
  for (let i = 0; i < a.length; i++)
    for (let k = 0; k < a[i].length; k++)
      for (let d = 0; d < 2; d++)
        assert.ok(Math.abs(a[i][k][d] - b[i][k][d]) < 1e-10);
});
test("corner slab joints follow the arc and have whole repeats at both mouths", () => {
  const angles = [0, 0.12, 0.31, 0.75, 1.2, Math.PI / 2];
  const rows = angles.map((a) =>
    Array.from({ length: 10 }, (_, j) => [
      (8 - j * 0.25) * Math.cos(a),
      0.15,
      (8 - j * 0.25) * Math.sin(a),
    ]),
  );
  const uv = ribbonUVRows(rows);
  assert.equal(uv[0][5][0], 0);
  assert.ok(
    Math.abs(
      uv.at(-1)[5][0] / tileSize("paving") -
        Math.round(uv.at(-1)[5][0] / tileSize("paving")),
    ) < 1e-10,
  );
  for (let i = 1; i < uv.length; i++) {
    assert.ok(uv[i][5][0] > uv[i - 1][5][0]);
    for (let k = 0; k < 10; k++) {
      assert.equal(uv[i][k][0], uv[i][5][0]);
      assert.ok(Math.abs(uv[i][k][1] - (k - 5) * 0.25) < 1e-10);
    }
  }
});
test("mixed-direction roads carry along-road/across-road UVs", () => {
  const { road } = fixture();
  for (const r of road.roads) {
    const i = road.mesh.groups.indexOf(r.e.id),
      f = road.mesh.faces[i];
    for (let j = 0; j < 4; j++) {
      const p = road.mesh.vertices[f[j]],
        uv = road.mesh.uvs[i][j];
      assert.ok(
        Math.abs(uv[0] - ((p[0] - r.a.x) * r.dx + (p[2] - r.a.z) * r.dz)) <
          1e-8,
      );
      assert.ok(
        Math.abs(uv[1] - ((p[0] - r.a.x) * r.nx + (p[2] - r.a.z) * r.nz)) <
          1e-8,
      );
    }
  }
});
test("default inlet gutters are concrete; brick paving remains an explicit choice", () => {
  const a = fixture().street,
    b = fixture({ paving: "brick" }).street;
  assert.ok(a.mesh.groups.includes("gutter"));
  assert.equal(a.mesh.groups.includes("brick"), false);
  assert.ok(b.mesh.groups.includes("brick"));
  assert.ok(b.mesh.groups.includes("gutter"));
  assert.equal(auditStreetscape(a).ok, true);
});
test("seed textured ribbons have finite noncollapsed UVs and preserve geometry checks", () => {
  const p = seedProject(),
    s = generateStreetscape(p, generateRoadMesh(p));
  assert.equal(s.mesh.faces.length, 132604);
  assert.equal(auditStreetscape(s).ok, true);
  const seamPhases = new Map();
  for (let i = 0; i < s.mesh.faces.length; i++)
    if (
      ["paving", "gutter", "curbstone", "granite", "ironcover"].includes(
        s.mesh.groups[i],
      )
    ) {
      const uv = normalizedFaceUV(s.mesh, i);
      assert.ok(uv.flat().every(Number.isFinite));
      if (s.mesh.groups[i] === "paving")
        s.mesh.faces[i].forEach((vertex, k) => {
          const previous = seamPhases.get(vertex);
          if (previous)
            for (let d = 0; d < 2; d++) {
              const difference = uv[k][d] - previous[d];
              assert.ok(
                Math.abs(difference - Math.round(difference)) < 1e-6,
                "slab phase discontinuity at a welded strip/corner seam",
              );
            }
          else seamPhases.set(vertex, uv[k]);
        });
      assert.ok(Math.abs(area(uv)) > 1e-10, `collapsed UV face ${i}`);
    }
});
test("renderer uses per-face UVs without changing welded source positions", () => {
  const { street } = fixture(),
    mesh = street.mesh,
    counts = [mesh.vertices.length, mesh.faces.length];
  const geo = mesh.geometry("paving");
  const first = mesh.groups.indexOf("paving"),
    expected = normalizedFaceUV(mesh, first);
  assert.equal(geo.attributes.uv.count, geo.attributes.position.count);
  for (const [i, k] of [0, 1, 2, 0, 2, 3].entries()) {
    assert.ok(Math.abs(geo.attributes.uv.getX(i) - expected[k][0]) < 1e-5);
    assert.ok(Math.abs(geo.attributes.uv.getY(i) - expected[k][1]) < 1e-5);
  }
  assert.deepEqual([mesh.vertices.length, mesh.faces.length], counts);
  geo.dispose();
});
test("road and streetscape OBJ retain four-corner faces with valid UV indices", () => {
  const { p, road, street } = fixture();
  for (const [obj, baseCount] of [
    [exportOBJ(road, p), road.mesh.faces.length],
    [exportStreetscapeOBJ(street), street.mesh.faces.length],
  ]) {
    const lines = obj.split("\n"),
      v = lines.filter((l) => l.startsWith("v ")).length,
      vt = lines.filter((l) => l.startsWith("vt ")).length,
      faces = lines.filter((l) => l.startsWith("f "));
    assert.equal(vt, baseCount * 4);
    for (const [i, line] of faces.entries()) {
      const corners = line.slice(2).split(" ");
      assert.equal(corners.length, 4);
      for (const token of corners) {
        const [position, uv] = token.split("/").map(Number);
        assert.ok(position >= 1 && position <= v);
        if (i < baseCount) assert.ok(uv >= 1 && uv <= vt);
      }
    }
  }
});

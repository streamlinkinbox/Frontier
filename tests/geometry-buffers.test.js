import test from "node:test";
import assert from "node:assert/strict";
import * as THREE from "three";
import { QuadMesh } from "../src/geometry.js";
import { normalizedFaceUV } from "../src/surface-uv.js";

test("compact material buffers preserve triangles, smooth normals and UVs", () => {
  const mesh = new QuadMesh();
  mesh.quad(
    [
      [0, 0, 0],
      [0, 0, 2],
      [2, 0, 2],
      [2, 0, 0],
    ],
    "road",
  );
  mesh.quad(
    [
      [2, 0, 0],
      [2, 0, 2],
      [4, 1, 2],
      [4, 1, 0],
    ],
    "road",
  );
  mesh.quad(
    [
      [50, 0, 50],
      [50, 0, 52],
      [52, 0, 52],
      [52, 0, 50],
    ],
    "pave",
  );
  mesh.surfaceKinds[2] = "concrete";
  for (const owner of [null, "road", "pave", "missing"]) {
    for (const surface of [null, "asphalt", "concrete", "missing"]) {
      // Previous full-vertex implementation is the reference output.
      const indices = [],
        uv = [];
      mesh.faces.forEach((face, i) => {
        if (owner !== null && mesh.groups[i] !== owner) return;
        if (surface !== null && mesh.surfaceKinds[i] !== surface) return;
        indices.push(face[0], face[1], face[2], face[0], face[2], face[3]);
        const coords = normalizedFaceUV(mesh, i);
        for (const k of [0, 1, 2, 0, 2, 3]) uv.push(...coords[k]);
      });
      const reference = new THREE.BufferGeometry();
      reference.setAttribute(
        "position",
        new THREE.Float32BufferAttribute(mesh.vertices.flat(), 3),
      );
      reference.setIndex(indices);
      reference.computeVertexNormals();
      const expected = reference.toNonIndexed();
      expected.setAttribute("uv", new THREE.Float32BufferAttribute(uv, 2));
      const actual = mesh.geometry(owner, surface);
      for (const attribute of ["position", "normal", "uv"])
        assert.deepEqual(
          actual.getAttribute(attribute).array,
          expected.getAttribute(attribute).array,
          `${owner}/${surface}/${attribute}`,
        );
      actual.dispose();
      expected.dispose();
      reference.dispose();
    }
  }
});

import { describe, it, expect } from "vitest";
import {
  terrainSdf,
  generateField,
  sampleField,
  sculptField,
  hash,
} from "../field";
import { floatToHalf, halfToFloat, rayBox } from "../math";
import { computeFlux, evolveField, redistanceField } from "../simulation";
import { DEFAULT_SETTINGS, type Settings } from "../types";
import {
  encodeProject,
  decodeProject,
  validateSettings,
  type Project,
} from "../project";
import { extractMesh, buildGLB } from "../mesh";
const small = { x: 32, y: 16, z: 32 };
const s = { ...DEFAULT_SETTINGS };

describe("volumetric terrain", () => {
  it("is deterministic and seeds produce different geometry", () => {
    expect(hash(-33, 21, 845)).toBe(hash(-33, 21, 845));
    const a = generateField(small, s),
      b = generateField(small, s),
      c = generateField(small, { ...s, seed: s.seed + 1 });
    expect(a).toEqual(b);
    expect(a.some((v, i) => Math.abs(v - c[i]) > 0.05)).toBe(true);
  });
  it("has multiple vertical surface crossings, including a true arch", () => {
    const arch = { ...s, preset: "arches" as const };
    expect(terrainSdf(0, 0, 0, arch)).toBeGreaterThan(0);
    expect(terrainSdf(0, 12, 0, arch)).toBeGreaterThan(1);
    expect(terrainSdf(0, 30, 0, arch)).toBeLessThan(-1);
    expect(terrainSdf(0, 37, 0, arch)).toBeGreaterThan(0);
    expect(terrainSdf(0, -1.1, 0, arch)).toBeLessThan(0);
  });
  it("keeps every preset finite and closes the outer boundary", () => {
    for (const preset of ["canyon", "arches", "badlands"] as const) {
      const data = generateField(small, { ...s, preset });
      expect(data.every(Number.isFinite)).toBe(true);
      for (const p of [
        [-48, 12, 0],
        [48, 12, 0],
        [0, -10, 0],
        [0, 38, 0],
        [0, 10, 48],
      ] as [number, number, number][])
        expect(terrainSdf(...p, { ...s, preset })).toBeGreaterThan(0);
    }
  });
  it("sculpts a sphere in 3D, leaves distant voxels untouched, and smooths safely", () => {
    const original = generateField(small, s),
      carved = original.slice(),
      added = original.slice();
    sculptField(carved, small, [15, 12, 20], "carve", { ...s, radius: 8 });
    sculptField(added, small, [15, 12, 20], "add", { ...s, radius: 8 });
    expect(carved.some((v, i) => v > original[i] + 0.01)).toBe(true);
    expect(added.some((v, i) => v < original[i] - 0.01)).toBe(true);
    expect(sampleField(carved, small, [-30, 22, -20])).toBe(
      sampleField(original, small, [-30, 22, -20]),
    );
    sculptField(carved, small, [15, 12, 20], "smooth", { ...s, radius: 8 });
    expect(carved.every(Number.isFinite)).toBe(true);
  });
});
describe("finite-volume transport and erosion", () => {
  it("redistances without flipping signs or changing the transported channels", () => {
    const data = new Float32Array(small.x * small.y * small.z * 4);
    for (let z = 0; z < small.z; z++)
      for (let y = 0; y < small.y; y++)
        for (let x = 0; x < small.x; x++) {
          const i = ((z * small.y + y) * small.x + x) * 4;
          data[i] = (-48 + (x + 0.5) * 3 + 0.3) * 1.8;
          data[i + 1] = 0.1;
          data[i + 2] = 0.02;
        }
    const out = new Float32Array(data.length);
    redistanceField(data, small, out);
    let improved = 0;
    for (let i = 0; i < data.length; i += 4) {
      expect(Math.sign(out[i])).toBe(Math.sign(data[i]));
      expect(out[i + 1]).toBe(data[i + 1]);
      expect(out[i + 2]).toBe(data[i + 2]);
      if (Math.abs(out[i]) < Math.abs(data[i])) improved++;
    }
    expect(improved).toBeGreaterThan(100);
    expect(out.every(Number.isFinite)).toBe(true);
  });

  const size = { x: 16, y: 8, z: 16 };
  const off: Settings = {
    ...s,
    rainfall: 0,
    evaporation: 0,
    erosion: 0,
    thermal: 0,
    sediment: 0,
  };
  it("conserves fluid and suspended sediment on internal faces without sources or sinks", () => {
    const data = new Float32Array(size.x * size.y * size.z * 4);
    for (let i = 0; i < data.length; i += 4) data[i] = 24;
    const center = ((8 * size.y + 4) * size.x + 8) * 4;
    data[center + 1] = 1;
    data[center + 2] = 0.2;
    const flux = new Float32Array((data.length / 4) * 3),
      out = new Float32Array(data.length);
    computeFlux(data, size, flux);
    evolveField(data, size, off, flux, out);
    let water = 0,
      sediment = 0;
    for (let i = 0; i < out.length; i += 4) {
      water += out[i + 1];
      sediment += out[i + 2];
      expect(out[i + 1]).toBeGreaterThanOrEqual(0);
    }
    expect(water).toBeCloseTo(1, 6);
    expect(sediment).toBeCloseTo(0.2, 6);
    expect(out[center - size.x * 4 + 1]).toBeGreaterThan(0);
  });
  it("does not alter a dry field when all processes are off", () => {
    const data = generateField(small, s),
      flux = new Float32Array((data.length / 4) * 3),
      out = new Float32Array(data.length);
    computeFlux(data, small, flux);
    evolveField(data, small, off, flux, out);
    expect(out).toEqual(data);
  });
  it("evolves geometry and water without NaNs or negative fluid", () => {
    let data = generateField(small, s),
      out = new Float32Array(data.length);
    const initial = data.slice(),
      flux = new Float32Array((data.length / 4) * 3);
    for (let k = 0; k < 12; k++) {
      computeFlux(data, small, flux);
      evolveField(data, small, s, flux, out);
      [data, out] = [out, data];
    }
    expect(data.every(Number.isFinite)).toBe(true);
    let wet = 0,
      changed = 0;
    for (let i = 0; i < data.length; i += 4) {
      wet += data[i + 1];
      changed += Math.abs(data[i] - initial[i]);
      expect(data[i + 1]).toBeGreaterThanOrEqual(0);
    }
    expect(wet).toBeGreaterThan(0);
    expect(changed).toBeGreaterThan(1);
  });
});
describe("portable project format", () => {
  it("round-trips half floats, including signed zero and subnormals", () => {
    for (const value of [0, -0, 1, -1, 0.01, 1e-5, 6.87, 32, -24])
      expect(halfToFloat(floatToHalf(value))).toBeCloseTo(
        value,
        Math.abs(value) > 1 ? 2 : 5,
      );
  });
  it("round-trips geometry and all editor settings", async () => {
    const p: Project = {
      version: 1,
      settings: s,
      size: small,
      steps: 42,
      savedAt: new Date(0).toISOString(),
      data: generateField(small, s),
    };
    const loaded = decodeProject(await encodeProject(p).arrayBuffer());
    expect(loaded.settings).toEqual(s);
    expect(loaded.size).toEqual(small);
    expect(loaded.steps).toBe(42);
    for (let i = 0; i < p.data.length; i += 101)
      expect(loaded.data[i]).toBeCloseTo(p.data[i], 1);
  });
  it("rejects invalid and truncated imports rather than allocating arbitrary volumes", async () => {
    expect(() => decodeProject(new ArrayBuffer(128))).toThrow();
    expect(() => validateSettings({ ...s, seed: Infinity })).toThrow();
    expect(() => validateSettings({ ...s, preset: "untrusted" })).toThrow();
    const p: Project = {
      version: 1,
      settings: s,
      size: small,
      steps: 0,
      savedAt: "",
      data: generateField(small, s),
    };
    const buffer = await encodeProject(p).arrayBuffer();
    expect(() => decodeProject(buffer.slice(0, -2))).toThrow();
  });
});
describe("mesh export", () => {
  it("extracts an outward-facing zero isosurface and a valid GLB container", () => {
    const data = new Float32Array(small.x * small.y * small.z * 4);
    for (let z = 0; z < small.z; z++)
      for (let y = 0; y < small.y; y++)
        for (let x = 0; x < small.x; x++)
          data[((z * small.y + y) * small.x + x) * 4] =
            Math.hypot(
              -48 + (x + 0.5) * 3,
              -10 + (y + 0.5) * 3 - 14,
              -48 + (z + 0.5) * 3,
            ) - 13;
    const mesh = extractMesh(data, small);
    expect(mesh.positions.length).toBeGreaterThan(0);
    expect(mesh.positions.every(Number.isFinite)).toBe(true);
    for (let i = 0; i < mesh.positions.length; i += 33) {
      if (i + 2 >= mesh.positions.length) break;
      const p = [
        mesh.positions[i],
        mesh.positions[i + 1] - 14,
        mesh.positions[i + 2],
      ];
      expect(
        p[0] * mesh.normals[i] +
          p[1] * mesh.normals[i + 1] +
          p[2] * mesh.normals[i + 2],
      ).toBeGreaterThan(0);
    }
    const glb = buildGLB(mesh),
      view = new DataView(glb);
    expect(view.getUint32(0, true)).toBe(0x46546c67);
    expect(view.getUint32(4, true)).toBe(2);
    expect(view.getUint32(8, true)).toBe(glb.byteLength);
    const json = JSON.parse(
      new TextDecoder().decode(
        new Uint8Array(glb, 20, view.getUint32(12, true)),
      ),
    );
    expect(json.accessors[0].count).toBe(mesh.positions.length / 3);
    expect(json.meshes[0].primitives[0].attributes.COLOR_0).toBe(2);
    expect(json.accessors[3].count).toBe(mesh.indices.length);
    expect(mesh.indices.every((i) => i < mesh.positions.length / 3)).toBe(true);
    expect(mesh.positions.length / 3).toBeLessThan(mesh.indices.length);
  });
  it("handles parallel camera rays at the volume boundary", () => {
    expect(
      rayBox([100, 10, 0], [0, 0, 1], [-48, -10, -48], [48, 38, 48]),
    ).toBeNull();
    expect(
      rayBox([0, 10, 100], [0, 0, -1], [-48, -10, -48], [48, 38, 48]),
    ).toEqual([52, 148]);
  });
});

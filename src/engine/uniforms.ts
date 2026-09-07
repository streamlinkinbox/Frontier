import type { FrameState, VolumeSize } from "./types";
export function packUniforms(f: FrameState, size: VolumeSize): Float32Array {
  const s = f.settings;
  const a = new Float32Array(56);
  a.set([...f.eye, f.time], 0);
  a.set([...f.forward, f.width / f.height], 4);
  a.set([...f.right, 0.41421356], 8);
  a.set([...f.up, (s.sunAngle * Math.PI) / 180], 12);
  a.set(
    [f.width, f.height, s.exposure, ["lit", "clay", "flow"].indexOf(s.view)],
    16,
  );
  a.set(
    [...(f.brush || [0, 0, 0]), f.brush && f.tool !== "orbit" ? s.radius : -1],
    20,
  );
  a.set([s.waterLevel, s.wind, s.water ? 1 : 0, s.grid ? 1 : 0], 24);
  a.set([0, s.terrainVisible ? 1 : 0, s.seed, s.detail], 28);
  a.set([size.x, size.y, size.z, 96 / size.x], 32);
  a.set([s.rainfall, s.erosion, s.sediment, s.evaporation], 36);
  a.set(
    [
      s.thermal,
      s.resistance,
      0.1,
      ["canyon", "arches", "badlands"].indexOf(s.preset),
    ],
    40,
  );
  a.set([0, 0, 0, s.strength], 44);
  a.set(
    [
      s.radius,
      s.falloff,
      ["orbit", "add", "carve", "smooth", "flatten"].indexOf(f.tool),
      0,
    ],
    48,
  );
  return a;
}

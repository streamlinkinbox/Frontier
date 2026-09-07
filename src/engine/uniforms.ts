import { MATERIAL_IDS, colorToLinear } from "./materials";
import { TOOL_IDS, type FrameState, type VolumeSize } from "./types";
export const UNIFORM_FLOATS = 100;
export const UNIFORM_BYTES = UNIFORM_FLOATS * 4;
export function packUniforms(f: FrameState, size: VolumeSize): Float32Array {
  const s = f.settings;
  const a = new Float32Array(UNIFORM_FLOATS);
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
  a.set([s.waterClarity, s.terrainVisible ? 1 : 0, s.seed, s.detail], 28);
  a.set([size.x, size.y, size.z, 96 / size.x], 32);
  a.set([s.rainfall, s.erosion, s.sediment, s.evaporation], 36);
  a.set(
    [
      s.thermal,
      s.resistance,
      s.cohesion,
      ["canyon", "arches", "badlands"].indexOf(s.preset),
    ],
    40,
  );
  a.set([0, 0, 0, s.strength], 44);
  a.set([s.radius, s.falloff, TOOL_IDS.indexOf(f.tool), s.settling], 48);
  a[54] = (s.talusAngle * Math.PI) / 180;
  const stamp = f.stamp;
  a.set([...(stamp?.origin ?? f.brush ?? [0, 0, 0]), s.brushDepth], 56);
  a.set([...(stamp?.normal ?? [0, 1, 0]), s.brushWidth], 60);
  a.set([...(stamp?.tangent ?? [1, 0, 0]), stamp?.seed ?? s.seed], 64);
  a.set([...(stamp?.previous ?? f.brush ?? [0, 0, 0]), 0], 68);
  const angle = (s.windDirection * Math.PI) / 180;
  a.set([s.channeling, s.windErosion, Math.cos(angle), Math.sin(angle)], 72);
  a.set(
    [
      MATERIAL_IDS.indexOf(s.material),
      s.materialRoughness,
      s.materialGrain * 0.001,
      s.materialRelief * 0.001,
    ],
    76,
  );
  a.set(
    [
      s.materialScale,
      s.materialBedding,
      s.materialPorosity,
      s.materialWeathering,
    ],
    80,
  );
  a.set([s.materialIOR, s.materialMoisture, 0, 0], 84);
  a.set([...colorToLinear(s.materialColor), 1], 88);
  a.set([s.waterAbsorption, s.waterFoam, 0, 0], 92);
  const flowAngle = (s.waterDirection * Math.PI) / 180;
  a.set(
    [
      s.waterCurrent,
      s.waterRippleScale,
      Math.cos(flowAngle),
      Math.sin(flowAngle),
    ],
    96,
  );
  return a;
}

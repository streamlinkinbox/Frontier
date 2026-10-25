// Frontier Fluid — public API surface for engine integration.
export { FluidSim, READBACK_RES } from './FluidSim';
export { WaterRenderer } from './WaterRenderer';
export { ParticleRenderer } from './ParticleRenderer';
export { RiverPath, type RiverPoint, type RiverOptions } from './RiverPath';
export { BuoyantBody, type BuoyancyBodyOptions } from './Buoyancy';
export {
  WATER_PRESET, MUD_PRESET, OCEAN_PRESET,
  type FluidSettings, type FluidInteractor, type FluidSource, type WaterSample,
} from './types';

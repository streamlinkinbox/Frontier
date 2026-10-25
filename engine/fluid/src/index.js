// Frontier Fluid – public API
export { FluidSim } from './FluidSim.js';
export { FluidParticles, PARTICLE_KIND } from './FluidParticles.js';
export { FluidRenderer } from './FluidRenderer.js';
export { RigidBody, BuoyancyProbe, FluidQueryBatch } from './Buoyancy.js';
export { RiverPath } from './RiverPath.js';
export { groundWGSL, SKY_FN, SHADOW_FN, VIEW_STRUCT } from './shaders/render.wgsl.js';
export * as fluidMath from './math.js';

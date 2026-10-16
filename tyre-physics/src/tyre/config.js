/**
 * Physical and numerical parameters (SI units). Values are chosen for a 205/55 R16-class
 * passenger tyre on a quarter-vehicle load. Everything that changes at run time is
 * pushed to the GPU through the uniform block; nothing here is read back by the CPU.
 */
export const PHYS = Object.freeze({
  dtSub: 1 / 480, // fixed substep [s]; the frame is an integer number of substeps
  maxSubstepsPerFrame: 16,
  gravity: 9.81,
  tyreMass: 10.0, // membrane mass [kg]
  hubMass: 30.0, // wheel + carrier mass without payload [kg]
  hubInertia: 0.6, // about the axle [kg m^2]
  effectiveRadius: 0.31, // loaded rolling radius used for the wheel-speed target [m]
  servoTau: 0.2, // speed / lateral servo time constant [s]
  motorTau: 0.01, // wheel-speed servo time constant [s]
  gamma: 1.4, // adiabatic index of air
  mu: 0.9, // tyre-road friction
  contactRadius: 0.004, // membrane half-thickness at the road [m]
  hubDamp: 2000, // hub vertical damper [N s/m]: structural damping stand-in for tyre hysteresis (not a spring)
  damping: 4.0, // velocity damping [1/s]; settles the hub bounce on the tyre (material damping proxy)
  // membrane compliances [m/N] before the stiffness multiplier; XPBD alpha = compliance / dt^2
  alphaStretch: 1e-6,
  alphaShear: 1e-5,
  alphaBend: 2e-4,
  smBeta: 0.5, // shape matching blend per iteration
  preTensionRadius: 0.07, // profile radius used for the membrane pre-tension p*r/2 [m]
  loadRampTime: 1.0, // payload weight ramps in over this simulated time [s]
  pressureRampTime: 0.5, // gas pressure ramps in over this simulated time [s]
});

export const PRESETS = Object.freeze({
  low: { label: 'Low  · 32×24 (768 particles)', NP: 32, NC: 24 },
  medium: { label: 'Medium · 48×32 (1.5k)', NP: 48, NC: 32 },
  high: { label: 'High · 96×64 (6.1k)', NP: 96, NC: 64 },
  ultra: { label: 'Ultra · 128×96 (12.3k)', NP: 128, NC: 96 },
});

export const SOLVERS = Object.freeze({
  xpbd: { label: 'XPBD · coloured Gauss-Seidel', defaultIterations: 4 },
  shape: { label: 'Shape matching · clusters', defaultIterations: 2 },
});

/** Layout of the uniform block. Order and types must match `struct Params` in the WGSL. */
export const PARAM_LAYOUT = Object.freeze([
  ['dt', 'f'], ['invDt', 'f'], ['invDt2', 'f'], ['frameDt', 'f'],
  ['gravityY', 'f'], ['totalMass', 'f'], ['hubInertia', 'f'], ['servoKv', 'f'],
  ['speedTarget', 'f'], ['latTarget', 'f'], ['servoKw', 'f'], ['omegaTarget', 'f'],
  ['rimRadius', 'f'], ['rimHalfWidth', 'f'], ['contactRadius', 'f'], ['mu', 'f'],
  ['volAlpha', 'f'], ['volTarget', 'f'], ['damping', 'f'], ['smBeta', 'f'],
  ['frameIndex', 'f'], ['simTime', 'f'], ['restVolume', 'f'], ['tyreMass', 'f'],
  ['numVerts', 'u'], ['numTris', 'u'], ['numAttach', 'u'], ['numEdges', 'u'],
  ['numVertWG', 'u'], ['numTriWG', 'u'], ['numAttachWG', 'u'], ['frameSamples', 'f'],
  ['loadRamp', 'f'], ['payloadFrac', 'f'], ['hubDamp', 'f'], ['pad0', 'f'],
]);

export const PARAM_BYTES = PARAM_LAYOUT.length * 4;

/** Telemetry block layout (see TELEM in tyre_sim.wgsl): 8 vec4 = 128 bytes. */
export const TELEM_BYTES = 128;

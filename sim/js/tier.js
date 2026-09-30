// Quality tiers. Default = GTX; ?tier=rtx for the RTX tier; ?lowres for weak/integrated GPUs.
const q = new URLSearchParams(globalThis.location?.search || '');
const LOW = q.has('lowres');
const name = LOW ? 'low' : (q.get('tier') || 'gtx').toLowerCase() === 'rtx' ? 'rtx' : 'gtx';
const T = {
  low: { smokeDims: [64, 32, 64],    smokeH: 0.5,   farDims: [64, 16, 64],   farH: 2.0, jacobi: 20, farJacobi: 12, steps: 96,  stepMul: 1.1, sand: 16384,  sandSpawn: 1.0, tornado: 8192 },
  gtx: { smokeDims: [128, 64, 128],  smokeH: 0.25,  farDims: [128, 32, 128], farH: 1.0, jacobi: 24, farJacobi: 16, steps: 96,  stepMul: 1.1, sand: 49152,  sandSpawn: 1.0, tornado: 24576 },
  // RTX retuned after first real test (39 fps on RDNA4): 1.95x GTX cells instead of 3.4x
  rtx: { smokeDims: [160, 80, 160],  smokeH: 0.2,   farDims: [160, 40, 160], farH: 1.0, jacobi: 30, farJacobi: 16, steps: 160, stepMul: 1.05, sand: 131072, sandSpawn: 2.2, tornado: 65536 },
};
export const TIER = { name, ...T[name] };

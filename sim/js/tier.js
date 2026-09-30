// Quality tiers. Default = RTX; ?tier=gtx for the GTX tier; ?lowres for weak/integrated GPUs.
const q = new URLSearchParams(globalThis.location?.search || '');
const LOW = q.has('lowres');
const name = LOW ? 'low' : (q.get('tier') || 'rtx').toLowerCase() === 'gtx' ? 'gtx' : 'rtx';
const T = {
  low: { smokeDims: [64, 32, 64],    smokeH: 0.5,   farDims: [64, 16, 64],   farH: 2.0, jacobi: 20, farJacobi: 12, steps: 96,  stepMul: 1.1, sand: 16384,  sandSpawn: 1.0, tornado: 8192 },
  gtx: { smokeDims: [128, 64, 128],  smokeH: 0.25,  farDims: [128, 32, 128], farH: 1.0, jacobi: 24, farJacobi: 16, steps: 96,  stepMul: 1.1, sand: 49152,  sandSpawn: 1.0, tornado: 24576 },
  rtx: { smokeDims: [192, 96, 192],  smokeH: 1 / 6, farDims: [192, 48, 192], farH: 1.0, jacobi: 40, farJacobi: 20, steps: 224, stepMul: 1.0, sand: 262144, sandSpawn: 3.5, tornado: 98304 },
};
export const TIER = { name, ...T[name] };

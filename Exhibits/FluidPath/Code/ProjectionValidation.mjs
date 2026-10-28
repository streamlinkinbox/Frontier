import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const Source = fs.readFileSync(new URL('../../../Experimental/FluidEditor/Fluid/FluidViewport.js', import.meta.url), 'utf8');
const Start = Source.indexOf('function AdvanceSimulation(Elapsed)');
const End = Source.indexOf('\nfunction RenderSurface()', Start);
assert.ok(Start >= 0 && End > Start);
const Sequence = [];
let Current = '';
const Compute = { setPipeline(Name) { Current = Name; }, setBindGroup() {}, dispatchWorkgroups() { Sequence.push(Current); }, end() {} };
const Encoder = { beginComputePass() { return Compute; }, finish() { return {}; } };
const Device = { createCommandEncoder() { return Encoder; }, queue: { submit() {} } };
const Names = ['ClearAccumulation', 'Scatter', 'Normalise', 'Divergence', 'PressureJacobi', 'Project', 'Gather', 'DensityClear', 'DensitySplat', 'DensityResolve'];
const Pipelines = Object.fromEntries(Names.map(Name => [Name, Name]));
const Held = { CellTally: 64, Bindings: {} };
const Viewport = { Device, Resources: Held, Pipelines, Specification: { Substeps: 1, PressureIterations: 2 }, ParticleTally: 4, ElapsedSeconds: 0 };
const Context = { Viewport, UploadColliders() {}, UploadSurfaceProfile() {}, UploadSolverProfile() {} };
vm.runInNewContext(Source.slice(Start, End) + '\nAdvanceSimulation(1 / 60);', Context);
assert.deepEqual(Sequence, ['ClearAccumulation', 'Scatter', 'Normalise', 'Divergence', 'PressureJacobi', 'PressureJacobi', 'Project', 'Divergence', 'Gather', 'DensityClear', 'DensitySplat', 'DensityResolve']);
Sequence.length = 0;
Viewport.Specification.Substeps = 2;
vm.runInNewContext(Source.slice(Start, End) + '\nAdvanceSimulation(1 / 60);', Context);
assert.equal(Sequence.filter(Name => Name === 'Divergence').length, 3);
assert.equal(Sequence.filter(Name => Name === 'Project').length, 2);

// Central divergence of a central pressure gradient spans two cells, but the Jacobi operator
// uses immediate neighbours. This counterexample is independent of boundary handling.
const Pressure = [0, 0, 1, 0, 0];
const NearestLaplacian = Pressure[1] - 2 * Pressure[2] + Pressure[3];
const CentralComposition = (Pressure[4] - 2 * Pressure[2] + Pressure[0]) / 4;
assert.notEqual(NearestLaplacian, CentralComposition);
console.log('PASS post-projection diagnostic dispatch order');
console.log(`CONFIRMED stencil mismatch: nearest Laplacian=${NearestLaplacian}, central divergence-gradient=${CentralComposition}`);
const SolverSource = fs.readFileSync(new URL('../../../Experimental/FluidEditor/Fluid/FluidKernels.js', import.meta.url), 'utf8');
const ScatterStart = SolverSource.indexOf('export const ScatterKernel');
const ScatterEnd = SolverSource.indexOf('export const NormaliseKernel', ScatterStart);
const Scatter = SolverSource.slice(ScatterStart, ScatterEnd);
assert.match(Scatter, /CellKind\s*:\s*array<atomic<u32>>/);
assert.match(Scatter, /atomicStore\(&CellKind\[CellIndex\(BaseCell, Profile.GridExtent\)\], CellFluid\)/);
console.log('PASS scatter classification uses atomic writes (source contract; execution not tested)');

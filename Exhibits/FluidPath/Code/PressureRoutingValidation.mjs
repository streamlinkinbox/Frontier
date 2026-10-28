import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const Source = fs.readFileSync(new URL('../../../Experimental/FluidEditor/Fluid/FluidViewport.js', import.meta.url), 'utf8');
function Extract(Start, End) {
    const First = Source.indexOf(Start);
    const Last = Source.indexOf(End, First);
    assert.ok(First >= 0 && Last > First, `Cannot find ${Start}`);
    return Source.slice(First, Last);
}
const Advance = Extract('function AdvanceSimulation(Elapsed)', '\nfunction RenderSurface()');
const Render = Extract('function RenderSurface()', '\n//------------------------------------------------------------------------------------------------------------------------');
const Allocate = Extract('function AllocateResources(GridExtent)', '\nfunction ReleaseResources()');
const Clear = Source.slice(Source.indexOf('function ClearFields()'));
const Events = [];
let Pipeline = '';
let Group = '';
const Compute = {
    setPipeline(Name) { Pipeline = Name; }, setBindGroup(_, Name) { Group = Name; },
    dispatchWorkgroups() { Events.push([Pipeline, Group]); }, end() {}
};
const Draw = {
    setPipeline(Name) { Pipeline = Name; }, setBindGroup(_, Name) { Group = Name; },
    draw() { Events.push([Pipeline, Group]); }, end() {}
};
const Device = {
    createCommandEncoder() { return { beginComputePass() { return Compute; }, beginRenderPass() { return Draw; }, finish() { return {}; } }; },
    createBuffer(Descriptor) { return { label: Descriptor.label, destroy() {} }; },
    queue: { submit() {}, writeBuffer(Buffer) { Events.push(['write', Buffer.label]); } }
};
const Names = ['ClearAccumulation', 'Scatter', 'Normalise', 'Divergence', 'PressureJacobi', 'Project', 'Gather', 'DensityClear', 'DensitySplat', 'DensityResolve', 'Surface'];
const Bindings = Object.fromEntries(['PressureForward', 'PressureReverse', 'ProjectPrimary', 'ProjectSecondary', 'SurfacePrimary', 'SurfaceSecondary'].map(Name => [Name, Name]));
const Held = { CellTally: 64, PressureIndex: 0, Bindings };
const Viewport = { Device, Resources: Held, Pipelines: Object.fromEntries(Names.map(Name => [Name, Name])), Specification: { Substeps: 2, PressureIterations: 1 }, ParticleTally: 4, ElapsedSeconds: 0, Context: { getCurrentTexture() { return { createView() { return {}; } }; } } };
const Context = vm.createContext({ Viewport, GPUBufferUsage: { STORAGE: 1, COPY_DST: 2, COPY_SRC: 4, UNIFORM: 8 },
    UploadColliders() {}, UploadSurfaceProfile() {}, UploadSolverProfile() {}, UploadRenderProfile() {},
    ResolveParticleCeiling() { return 16; }, ReleaseResources() { Viewport.Resources = null; },
    ComposeBindGroups() {}, SeedParticles() {} });
vm.runInContext(`${Advance}\n${Render}\n${Allocate}\n${Clear}`, Context);
function AdvanceAndCollect(Iterations, Substeps) {
    Events.length = 0;
    Viewport.Specification = { PressureIterations: Iterations, Substeps };
    Context.AdvanceSimulation(1 / 60);
    return Events.filter(([Name]) => Name === 'PressureJacobi' || Name === 'Project');
}
assert.deepEqual(AdvanceAndCollect(1, 2), [
    ['PressureJacobi', 'PressureForward'], ['Project', 'ProjectSecondary'],
    ['PressureJacobi', 'PressureReverse'], ['Project', 'ProjectPrimary']
], 'odd substeps must warm-start from the latest buffer');
assert.equal(Held.PressureIndex, 0);
assert.deepEqual(AdvanceAndCollect(1, 1), [['PressureJacobi', 'PressureForward'], ['Project', 'ProjectSecondary']]);
assert.equal(Held.PressureIndex, 1);
Events.length = 0;
Context.RenderSurface();
assert.deepEqual(Events.at(-1), ['Surface', 'SurfaceSecondary'], 'pressure visualization must use the projected buffer');
assert.deepEqual(AdvanceAndCollect(2, 1), [
    ['PressureJacobi', 'PressureReverse'], ['PressureJacobi', 'PressureForward'], ['Project', 'ProjectSecondary']
]);
assert.equal(Held.PressureIndex, 1);
for (const Name of ['Velocity', 'VelocityOriginal', 'PressurePrimary', 'PressureSecondary', 'Divergence', 'CellKind', 'DensityField']) Held[Name] = { label: Name };
Context.ClearFields();
assert.equal(Held.PressureIndex, 0, 'reset must restore the initial pressure buffer');
assert.deepEqual(Events.filter(([Name]) => Name === 'write').map(([, Name]) => Name),
    ['Velocity', 'VelocityOriginal', 'PressurePrimary', 'PressureSecondary', 'Divergence', 'CellKind', 'DensityField']);
Events.length = 0;
Context.RenderSurface();
assert.deepEqual(Events.at(-1), ['Surface', 'SurfacePrimary'], 'reset visualization must use the cleared primary');
Held.PressureIndex = 1;
Context.AllocateResources(16);
assert.equal(Viewport.Resources.PressureIndex, 0, 'reallocation must restore the initial pressure buffer');
console.log('PASS odd/even warm starts, projection, visualization, reset and reallocation routing');

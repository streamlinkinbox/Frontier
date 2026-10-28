import assert from 'node:assert/strict';
import { create, globals } from '../../../Experimental/VehicleHmiPrototype/Tools/node_modules/webgpu/index.js';
import { DivergenceKernel, PressureJacobiKernel, ProjectKernel, ScatterKernel } from '../../../Experimental/FluidEditor/Fluid/FluidKernels.js';

Object.assign(globalThis, globals);
const Gpu = create(['backend=vulkan', 'enable-dawn-features=allow_unsafe_apis']);
const Adapter = await Gpu.requestAdapter({ forceFallbackAdapter: true });
if (!Adapter) throw new Error('No Vulkan fallback adapter; see the exhibit setup instructions');
const Device = await Adapter.requestDevice();
Device.pushErrorScope('validation');
const U = GPUBufferUsage;
const Extent = 8;
const Count = Extent ** 3;
const Step = 0.125;
const CellWidth = 0.5;
const Profile = new ArrayBuffer(128);
const ProfileU = new Uint32Array(Profile);
const ProfileF = new Float32Array(Profile);
ProfileU.set([Extent, Extent, Extent, 0]);
ProfileF[4] = Extent * CellWidth;
ProfileF[5] = CellWidth;
ProfileF[6] = Step;
const Index = (X, Y, Z) => X + Extent * (Y + Extent * Z);
const Directions = [[1,0,0], [-1,0,0], [0,1,0], [0,-1,0], [0,0,1], [0,0,-1]];
const Kind = new Uint32Array(Count);
const Velocity = new Float32Array(Count * 4);
for (let Z = 0; Z < Extent; Z++) for (let Y = 0; Y < Extent; Y++) for (let X = 0; X < Extent; X++) {
    const I = Index(X, Y, Z);
    Kind[I] = X === 0 || Y === 0 || Z === 0 || X === 7 || Y === 7 || Z === 7 ? 2 : Y === 6 ? 0 : 1;
    if (Kind[I] === 1) {
        Velocity[I * 4] = Math.sin(X * 0.7 + Y * 0.2);
        Velocity[I * 4 + 1] = Math.cos(Y * 0.6 + Z * 0.3);
        Velocity[I * 4 + 2] = Math.sin(Z * 0.4 - X * 0.3);
    }
}
function Within(X, Y, Z) { return X >= 0 && Y >= 0 && Z >= 0 && X < Extent && Y < Extent && Z < Extent; }
function ReferenceDivergence(Field) {
    const Result = new Float32Array(Count);
    for (let Z = 0; Z < Extent; Z++) for (let Y = 0; Y < Extent; Y++) for (let X = 0; X < Extent; X++) {
        const I = Index(X, Y, Z);
        if (Kind[I] !== 1) continue;
        let Sum = 0;
        for (let Axis = 0; Axis < 3; Axis++) {
            const D = Directions[Axis * 2];
            const A = [X + D[0], Y + D[1], Z + D[2]];
            const B = [X - D[0], Y - D[1], Z - D[2]];
            const Read = P => Within(...P) && Kind[Index(...P)] !== 2 ? Field[Index(...P) * 4 + Axis] : 0;
            Sum += (Read(A) - Read(B)) * 0.5;
        }
        Result[I] = Sum / CellWidth;
    }
    return Result;
}
function ReferencePressure(Divergence, Sweeps) {
    let Current = new Float32Array(Count);
    for (let Sweep = 0; Sweep < Sweeps; Sweep++) {
        const Next = new Float32Array(Count);
        for (let Z = 0; Z < Extent; Z++) for (let Y = 0; Y < Extent; Y++) for (let X = 0; X < Extent; X++) {
            const I = Index(X, Y, Z);
            if (Kind[I] !== 1) continue;
            let Sum = 0, Faces = 0;
            for (const D of Directions) {
                const P = [X + D[0], Y + D[1], Z + D[2]];
                if (!Within(...P)) continue;
                const J = Index(...P);
                if (Kind[J] === 2) continue;
                Faces++;
                if (Kind[J] === 1) Sum += Current[J];
            }
            if (Faces) Next[I] = Current[I] / 3 + ((Sum - Divergence[I] * CellWidth * CellWidth / Step) / Faces) * 2 / 3;
        }
        Current = Next;
    }
    return Current;
}
function ReferenceProject(Pressure) {
    const Result = new Float32Array(Velocity);
    for (let Z = 0; Z < Extent; Z++) for (let Y = 0; Y < Extent; Y++) for (let X = 0; X < Extent; X++) {
        const I = Index(X, Y, Z);
        if (Kind[I] !== 1) continue;
        for (let Axis = 0; Axis < 3; Axis++) {
            const D = Directions[Axis * 2];
            const A = [X + D[0], Y + D[1], Z + D[2]];
            const B = [X - D[0], Y - D[1], Z - D[2]];
            const Read = P => Within(...P) && Kind[Index(...P)] !== 2 ? Pressure[Index(...P)] : Pressure[I];
            Result[I * 4 + Axis] -= (Read(A) - Read(B)) * 0.5 / CellWidth * Step;
        }
        Result[I * 4 + 3] = 0;
    }
    return Result;
}
function Buffer(Content, Usage = U.STORAGE | U.COPY_DST | U.COPY_SRC) {
    const Bytes = typeof Content === 'number' ? Content : Content.byteLength;
    const Result = Device.createBuffer({ size: Bytes, usage: Usage });
    if (typeof Content !== 'number') Device.queue.writeBuffer(Result, 0, Content);
    return Result;
}
function Pipeline(Source) {
    return Device.createComputePipeline({ layout: 'auto', compute: { module: Device.createShaderModule({ code: Source }), entryPoint: 'main' } });
}
function Group(Pipe, Buffers) {
    return Device.createBindGroup({ layout: Pipe.getBindGroupLayout(0), entries: Buffers.map((B, I) => ({ binding: I, resource: { buffer: B } })) });
}
async function Read(Buf, Length, Type = Float32Array) {
    const Copy = Device.createBuffer({ size: Length * 4, usage: U.COPY_DST | U.MAP_READ });
    const Encoder = Device.createCommandEncoder();
    Encoder.copyBufferToBuffer(Buf, 0, Copy, 0, Length * 4);
    Device.queue.submit([Encoder.finish()]);
    await Copy.mapAsync(GPUMapMode.READ);
    const Result = new Type(Copy.getMappedRange().slice(0));
    Copy.unmap(); Copy.destroy();
    return Result;
}
function Compare(Actual, Expected, Label, Limit = 2e-5) {
    let Max = 0;
    for (let I = 0; I < Expected.length; I++) {
        assert.ok(Number.isFinite(Actual[I]), `${Label} non-finite at ${I}`);
        Max = Math.max(Max, Math.abs(Actual[I] - Expected[I]));
    }
    assert.ok(Max <= Limit, `${Label} max error ${Max} > ${Limit}`);
    return Max;
}
function Norm(Field) { return Math.sqrt(Field.reduce((Sum, V, I) => Sum + (Kind[I] === 1 ? V * V : 0), 0)); }
const DivPipe = Pipeline(DivergenceKernel), PressurePipe = Pipeline(PressureJacobiKernel), ProjectPipe = Pipeline(ProjectKernel);
const ProfileBuffer = Buffer(Profile, U.UNIFORM | U.COPY_DST);
const KindBuffer = Buffer(Kind);
const InitialDivergence = ReferenceDivergence(Velocity);
console.log(`Adapter: ${Adapter.info.description || Adapter.info.device || 'Vulkan fallback'} (software fallback requested)`);
for (const Sweeps of [1, 2, 40, 100]) {
    const Vel = Buffer(Velocity), Div = Buffer(Count * 4), A = Buffer(new Float32Array(Count)), B = Buffer(new Float32Array(Count));
    const DivGroup = Group(DivPipe, [Vel, KindBuffer, Div, ProfileBuffer]);
    const Forward = Group(PressurePipe, [A, B, Div, KindBuffer, ProfileBuffer]);
    const Reverse = Group(PressurePipe, [B, A, Div, KindBuffer, ProfileBuffer]);
    const Last = Sweeps % 2 ? B : A;
    const Projection = Group(ProjectPipe, [Vel, Last, KindBuffer, ProfileBuffer]);
    const Encoder = Device.createCommandEncoder();
    const Pass = Encoder.beginComputePass();
    Pass.setPipeline(DivPipe); Pass.setBindGroup(0, DivGroup); Pass.dispatchWorkgroups(Math.ceil(Count / 64));
    Pass.setPipeline(PressurePipe);
    for (let I = 0; I < Sweeps; I++) { Pass.setBindGroup(0, I % 2 ? Reverse : Forward); Pass.dispatchWorkgroups(Math.ceil(Count / 64)); }
    Pass.setPipeline(ProjectPipe); Pass.setBindGroup(0, Projection); Pass.dispatchWorkgroups(Math.ceil(Count / 64));
    Pass.setPipeline(DivPipe); Pass.setBindGroup(0, DivGroup); Pass.dispatchWorkgroups(Math.ceil(Count / 64));
    Pass.end(); Device.queue.submit([Encoder.finish()]);
    const Pressure = await Read(Last, Count);
    const FinalVelocity = await Read(Vel, Count * 4);
    const FinalDivergence = await Read(Div, Count);
    const ExpectedPressure = ReferencePressure(InitialDivergence, Sweeps);
    const ExpectedVelocity = ReferenceProject(ExpectedPressure);
    const ExpectedDivergence = ReferenceDivergence(ExpectedVelocity);
    const Errors = [Compare(Pressure, ExpectedPressure, 'pressure'), Compare(FinalVelocity, ExpectedVelocity, 'velocity'), Compare(FinalDivergence, ExpectedDivergence, 'divergence')];
    console.log(`sweeps=${Sweeps} divergence_l2=${Norm(InitialDivergence).toFixed(6)} -> ${Norm(FinalDivergence).toFixed(6)} max_abs_error=${Math.max(...Errors).toExponential(3)}`);
    for (const Bf of [Vel, Div, A, B]) Bf.destroy();
}

// Many invocations mark one cell while atomically splatting fixed-point momentum into eight cells.
const ParticleTally = 1024;
ProfileU[3] = ParticleTally;
Device.queue.writeBuffer(ProfileBuffer, 0, Profile);
const Particles = new Float32Array(ParticleTally * 8);
for (let I = 0; I < ParticleTally; I++) Particles.set([1.125, 1.125, 1.125, 1, 2, -1, 0.5, 0], I * 8);
const ParticleBuffer = Buffer(Particles), Momentum = Buffer(Count * 3 * 4), Weight = Buffer(Count * 4), Mark = Buffer(Count * 4);
const Scatter = Pipeline(ScatterKernel);
const ScatterGroup = Group(Scatter, [ParticleBuffer, Momentum, Weight, Mark, ProfileBuffer]);
const Encoder = Device.createCommandEncoder();
const Pass = Encoder.beginComputePass();
Pass.setPipeline(Scatter); Pass.setBindGroup(0, ScatterGroup); Pass.dispatchWorkgroups(Math.ceil(ParticleTally / 64)); Pass.end();
Device.queue.submit([Encoder.finish()]);
const Weights = await Read(Weight, Count, Int32Array);
const Momenta = await Read(Momentum, Count * 3, Int32Array);
const Marks = await Read(Mark, Count, Uint32Array);
let MassSum = 0;
for (let Z = 0; Z < Extent; Z++) for (let Y = 0; Y < Extent; Y++) for (let X = 0; X < Extent; X++) {
    const I = Index(X, Y, Z);
    const W = (X === 2 || X === 3) && (Y === 2 || Y === 3) && (Z === 2 || Z === 3)
        ? ParticleTally * 65536 * (X === 2 ? 0.75 : 0.25) * (Y === 2 ? 0.75 : 0.25) * (Z === 2 ? 0.75 : 0.25) : 0;
    assert.equal(Weights[I], W);
    assert.equal(Momenta[I * 3], W * 2);
    assert.equal(Momenta[I * 3 + 1], W ? -W : 0);
    assert.equal(Momenta[I * 3 + 2], W / 2);
    assert.equal(Marks[I], X === 2 && Y === 2 && Z === 2 ? 1 : 0);
    MassSum += Weights[I];
}
console.log(`scatter particles=${ParticleTally} exact_fixed_point_mass=${MassSum} marker=1: PASS`);
for (const Bf of [ParticleBuffer, Momentum, Weight, Mark, ProfileBuffer, KindBuffer]) Bf.destroy();
await Device.queue.onSubmittedWorkDone();
const ValidationError = await Device.popErrorScope();
assert.equal(ValidationError, null, ValidationError?.message);
Device.destroy();
console.log('PASS software Vulkan pressure, projection, divergence and atomic scatter against CPU reference');

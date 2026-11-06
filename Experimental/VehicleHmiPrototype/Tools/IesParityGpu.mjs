//============================================================================================================================================
//                                                            IESPARITYGPU.MJS
//============================================================================================================================================
// 🧩 Executes the shader's EvaluateBeamProfile (LightClusterSpace.wgsl) on a real (software) GPU for random directions and prints it beside the CPU reference
//    (Photometry.js SampleAtlas through the same C-plane frame), as JSON. node RunOnSoftwareGpu.mjs node IesParityGpu.mjs

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { create, globals } from "webgpu";

Object.assign(globalThis, globals);
const Root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
Object.defineProperty(globalThis, "navigator", { value: { gpu: create([`backend=${process.env.BACKEND || "vulkan"}`]), userAgent: "node" }, configurable: true });
globalThis.fetch = async (Location) =>
{
    const File = path.join(Root, String(Location).split("?")[0]);
    if (!fs.existsSync(File)) return { ok: false, status: 404, text: async () => "" };
    const Bytes = fs.readFileSync(File);
    return { ok: true, status: 200, text: async () => Bytes.toString("utf8") };
};
const Load = (Name) => import(pathToFileURL(path.join(Root, "Source", Name)).href);
const { DeviceExchange } = await Load("DeviceExchange.js");
const { ShaderInterchange } = await Load("ShaderInterchange.js");
const { LightClusterSpace } = await Load("LightClusterSpace.js");
const { RetrievePhotometricAtlas, SampleAtlas, PhotometryProfile } = await Load("Photometry.js");

const Exchange = new DeviceExchange();
await Exchange.ConstructDevice();
const Device = Exchange.Device;
const Shaders = new ShaderInterchange(Exchange, "Shader");
await Shaders.RetrieveSources(["FrameDescriptor.wgsl", "SceneRecords.wgsl", "LightClusterSpace.wgsl"]);
Shaders.Sources.set("IesParityProbe.wgsl", `//! include "FrameDescriptor.wgsl"
//! include "LightClusterSpace.wgsl"
@group(3) @binding(3) var<uniform> SceneLights : LightClusterRecords;
@group(0) @binding(1) var<storage, read> Queries : array<vec4f>;          // xyz direction from the fixture, w light index
@group(0) @binding(2) var<storage, read_write> Results : array<f32>;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) Id: vec3u)
{
    if (Id.x >= arrayLength(&Queries)) { return; }
    let Light = SceneLights.Lights[u32(Queries[Id.x].w + 0.5)];
    let Direction = normalize(Queries[Id.x].xyz);
    Results[Id.x] = EvaluateBeamProfile(Light, Direction * 10.0, 10.0);
}`);

// the lights under test: one of each type, aimed in awkward directions (including straight down, where C0 needs its fallback reference)
const Lights = [
    { Type: 1, Axis: [0, 0, -1], Variant: 0 }, { Type: 2, Axis: [0.9, 0.1, -0.05], Variant: 0 }, { Type: 3, Axis: [-0.3, 0.9, 0.1], Variant: 0 },
    { Type: 4, Axis: [0.2, -0.8, -0.3], Variant: 0 }, { Type: 5, Axis: [0.7, 0.7, -0.2], Variant: 0 }, { Type: 5, Axis: [1, 0, 0], Variant: 1 }
];
const Norm = (V) => { const L = Math.hypot(...V); return V.map((X) => X / L); };
const Cross = (A, B) => [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];
const Dot = (A, B) => A[0] * B[0] + A[1] * B[1] + A[2] * B[2];

const Space = new LightClusterSpace(Exchange);
const Data = new Float32Array(32 * 16);
Lights.forEach((Light, Index) =>
{
    const O = Index * 16, Axis = Norm(Light.Axis);
    Data.set([1, 2, 3, Light.Type, ...Axis, 50.0, 1, 1, 1, 1], O);
    Data.set([Math.cos(30 * Math.PI / 180), Math.cos(45 * Math.PI / 180), Light.Variant, 1.0], O + 12);
});
Space.Update(Device, Data);
Space.AssignPhotometry(Device, await RetrievePhotometricAtlas());
const Frame = Exchange.ConstructRecords(528, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Frame");
const FrameBytes = new ArrayBuffer(528);
new Uint32Array(FrameBytes)[129] = 32 | 8;   // ExtendedIesProfiles | ExtendedLightGrid
Device.queue.writeBuffer(Frame, 0, FrameBytes);

// random directions (a fixed LCG so the CPU and GPU see the same ones), around each light's axis and over the sphere
let Seed = 12345;
const Random = () => { Seed = (Math.imul(Seed, 1664525) + 1013904223) >>> 0; return Seed / 4294967296; };
const Queries = [];
for (let Index = 0; Index < Lights.length; ++Index)
    for (let K = 0; K < 400; ++K)
    {
        const Z = Random() * 2 - 1, Phi = Random() * 2 * Math.PI, R = Math.sqrt(1 - Z * Z);
        Queries.push([R * Math.cos(Phi), R * Math.sin(Phi), Z, Index]);
    }
const QueryBuffer = Exchange.ConstructRecordsFrom(new Float32Array(Queries.flat()), GPUBufferUsage.STORAGE, "Queries");
const ResultBuffer = Exchange.ConstructRecords(Queries.length * 4, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC, "Results");
const Layout0 = Device.createBindGroupLayout({ entries: [
    { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } },
    { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: "read-only-storage" } },
    { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: "storage" } }] });
const Empty = Device.createBindGroupLayout({ entries: [] });
const Layout3 = Device.createBindGroupLayout({ entries: [{ binding: 3, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } }] });
const Program = await Shaders.ConstructComputeProgram("IesParityProbe.wgsl", "main", [Layout0, Empty, Empty, Layout3]);
const Encoder = Device.createCommandEncoder();
const Pass = Encoder.beginComputePass();
Pass.setPipeline(Program);
Pass.setBindGroup(0, Device.createBindGroup({ layout: Layout0, entries: [{ binding: 0, resource: { buffer: Frame } }, { binding: 1, resource: { buffer: QueryBuffer } }, { binding: 2, resource: { buffer: ResultBuffer } }] }));
Pass.setBindGroup(1, Device.createBindGroup({ layout: Empty, entries: [] }));
Pass.setBindGroup(2, Device.createBindGroup({ layout: Empty, entries: [] }));
Pass.setBindGroup(3, Device.createBindGroup({ layout: Layout3, entries: [{ binding: 3, resource: { buffer: Space.Records } }] }));
Pass.dispatchWorkgroups(Math.ceil(Queries.length / 64));
Pass.end();
const Read = Device.createBuffer({ size: Queries.length * 4, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
Encoder.copyBufferToBuffer(ResultBuffer, 0, Read, 0, Queries.length * 4);
Device.queue.submit([Encoder.finish()]);
await Read.mapAsync(GPUMapMode.READ);
const Gpu = new Float32Array(Read.getMappedRange().slice(0));

// the CPU reference: the same frame (C0 = up, +Z, or +X for a vertical axis), the atlas bilinear lookup, and the flood types' cone
const Atlas = await RetrievePhotometricAtlas();
const ProfileOf = (Light) => ({ 1: 1, 2: 3, 3: 4, 4: 5 }[Light.Type] || (Light.Variant > 0.5 ? 7 : 6));
const Smoothstep = (A, B, X) => { const T = Math.max(0, Math.min(1, (X - A) / (B - A))); return T * T * (3 - 2 * T); };
let Worst = 0, WorstCase = null, Sum = 0;
const Rows = Queries.map((Query, Index) =>
{
    const Light = Lights[Query[3]], Axis = Norm(Light.Axis), From = Norm(Query.slice(0, 3));
    const Reference = Math.abs(Axis[2]) > 0.99 ? [1, 0, 0] : [0, 0, 1];
    const Right = Norm(Cross(Axis, Reference)), Up = Cross(Right, Axis);
    const Polar = Math.acos(Math.max(-1, Math.min(1, Dot(From, Axis)))) * 180 / Math.PI;
    let Azimuth = Math.atan2(Dot(From, Right), Dot(From, Up)) * 180 / Math.PI; if (Azimuth < 0) Azimuth += 360;
    let Expected = SampleAtlas(Atlas, ProfileOf(Light), Azimuth, Polar);
    if (Light.Type === 5) Expected *= Smoothstep(Math.cos(45 * Math.PI / 180), Math.cos(30 * Math.PI / 180), Dot(From, Axis));
    const Error = Math.abs(Expected - Gpu[Index]); Sum += Error;
    if (Error > Worst) { Worst = Error; WorstCase = { Type: Light.Type, Polar, Azimuth, Expected, Gpu: Gpu[Index] }; }
    return Expected;
});
const NonZero = Gpu.filter((Value) => Value > 0.05).length;
console.log("RESULT " + JSON.stringify({ Count: Queries.length, Worst, WorstCase, Mean: Sum / Queries.length, NonZero, Max: Math.max(...Gpu), Finite: Gpu.every(Number.isFinite) }));
process.exit(0);
